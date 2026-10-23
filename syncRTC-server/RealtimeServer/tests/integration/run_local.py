#!/usr/bin/env python3
"""真实本地服务联调。仅启动自己的实例，临时改写独立工作树 bin/conf 并在 finally 恢复。"""
import argparse
import concurrent.futures
import json
import os
from pathlib import Path
import signal
import socket
import struct
import subprocess
import time
import traceback

import pymysql
import redis


def wait_for(predicate, timeout=10):
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        try:
            value = predicate()
            if value:
                return value
        except (OSError, pymysql.Error, redis.RedisError) as error:
            last = error
        time.sleep(.05)
    raise AssertionError(f"condition timed out ({timeout}s): {last}")


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


class Client:
    def __init__(self, port):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=5)
        self.pending = []

    def send(self, ident, body):
        data = json.dumps(body, ensure_ascii=False).encode()
        self.sock.sendall(struct.pack("!HH", ident, len(data)) + data)

    def read_exact(self, count):
        result = b""
        while len(result) < count:
            part = self.sock.recv(count - len(result))
            assert part, "unexpected TCP EOF"
            result += part
        return result

    def receive(self, ident, predicate=lambda body: True, timeout=10):
        deadline = time.monotonic() + timeout
        while True:
            for i, (kind, body) in enumerate(self.pending):
                if kind == ident and predicate(body):
                    self.pending.pop(i)
                    return body
            self.sock.settimeout(max(.01, deadline - time.monotonic()))
            kind, length = struct.unpack("!HH", self.read_exact(4))
            self.pending.append((kind, json.loads(self.read_exact(length))))

    def request(self, ident, body, expected=0):
        self.send(ident, body)
        response = self.receive(ident + 1)
        assert response.get("error") == expected, (ident, response)
        return response

    def close(self):
        self.sock.close()


class Harness:
    def __init__(self, args):
        self.args = args
        self.repo = args.repo.resolve()
        self.root = args.output.resolve()
        self.root.mkdir(parents=True, exist_ok=False)
        self.root.chmod(0o700)
        self.processes = []
        self.clients = []
        self.configs = {}
        self.results = []
        self.mysql_port, self.redis_port, self.tcp_port = [free_port() for _ in range(3)]
        self.uds = str(self.root / "media.sock")
        assert len(self.uds.encode()) < 104, "UDS path too long"
        self.rt_dir = self.repo / "syncRTC-server/RealtimeServer/bin"
        self.media_dir = self.repo / "syncRTC-server/MediaServer/bin"
        self.env = dict(os.environ)
        self.env["LD_LIBRARY_PATH"] = str(args.runtime.resolve() / "usr/lib/x86_64-linux-gnu")
        self.db = None
        self.cache = None
        self.rt = None
        self.media = None
        self.restart_count = 0

    def record(self, name, passed=True, **evidence):
        result = dict(name=name, passed=bool(passed), **evidence)
        self.results.append(result)
        (self.root / "results.json").write_text(json.dumps(self.results, indent=2, ensure_ascii=False))
        print(json.dumps(result, ensure_ascii=False), flush=True)

    def launch(self, name, command):
        log = open(self.root / f"{name}.log", "wb")
        process = subprocess.Popen([str(x) for x in command], env=self.env, stdout=log, stderr=subprocess.STDOUT)
        log.close()
        self.processes.append(process)
        return process

    def sql(self, statement, values=None):
        with self.db.cursor() as cursor:
            cursor.execute(statement, values)
            return cursor.fetchall()

    def configure(self, directory, text):
        path = directory / "conf/config.ini"
        self.configs[path] = path.read_bytes() if path.exists() else None
        path.parent.mkdir(exist_ok=True)
        # 备份留在受限测试目录，避免异常中断后无法恢复，不输出配置内容。
        backup = self.root / f"{directory.parent.name}-config.backup"
        if self.configs[path] is not None:
            backup.write_bytes(self.configs[path])
            backup.chmod(0o600)
        path.write_text(text)

    def setup(self):
        runtime = self.args.runtime.resolve()
        data = self.root / "mysql-data"
        data.mkdir()
        mysql = runtime / "usr/sbin/mysqld"
        init = self.launch("mysql-initialize", [mysql, "--no-defaults", "--initialize-insecure",
                         f"--basedir={runtime / 'usr'}", f"--datadir={data}", "--innodb-buffer-pool-size=64M"])
        assert init.wait(timeout=120) == 0, "MySQL initialization failed; inspect local log"
        self.mysql = self.launch("mysql", [mysql, "--no-defaults", f"--basedir={runtime / 'usr'}",
            f"--datadir={data}", f"--port={self.mysql_port}", "--bind-address=127.0.0.1", "--mysqlx=0",
            f"--socket={self.root / 'mysql.sock'}", f"--pid-file={self.root / 'mysql.pid'}",
            "--skip-log-bin", "--secure-file-priv=NULL", "--innodb-buffer-pool-size=64M"])
        self.db = wait_for(lambda: pymysql.connect(unix_socket=str(self.root / "mysql.sock"),
                              user="root", autocommit=True), 30)
        schema = (self.repo / "syncRTC-server/db/init/01-schema.sql").read_text()
        for statement in schema.split(";"):
            if statement.strip():
                self.sql(statement)
        self.sql("CREATE USER 'integration'@'127.0.0.1' IDENTIFIED WITH mysql_native_password BY ''")
        self.sql("GRANT ALL ON syncrtc.* TO 'integration'@'127.0.0.1'")
        for uid in range(101, 109):
            self.sql("INSERT INTO users (id,username,email,password_hash,display_name) VALUES (%s,%s,%s,'fixture',%s)",
                     (uid, f"user{uid}", f"user{uid}@integration.invalid", f"本地用户{uid}"))
        self.redis_process = self.launch("redis", [runtime / "usr/bin/redis-server", "--bind", "127.0.0.1",
            "--port", self.redis_port, "--save", "", "--appendonly", "no", "--dir", self.root])
        self.cache = redis.Redis(host="127.0.0.1", port=self.redis_port, decode_responses=True)
        wait_for(self.cache.ping)
        for uid in range(101, 109):
            self.cache.set(f"auth::session:local-fixture-{uid}", json.dumps({"uid": uid}), ex=3600)
        self.configure(self.rt_dir, f"[RealtimeServer]\nPort={self.tcp_port}\n[MediaServer]\nInternalSocketPath={self.uds}\n"
            f"[Mysql]\nHost=127.0.0.1\nPort={self.mysql_port}\nUser=integration\nPassword=\nDatabase=syncrtc\n"
            f"[Redis]\nHost=127.0.0.1\nPort={self.redis_port}\nPassword=\n[AIServer]\nHost=127.0.0.1\nPort=1\n")
        self.configure(self.media_dir, f"[MediaServer]\nInternalSocketPath={self.uds}\n[WebRTC]\nStunHost=\n")
        self.start_media()
        self.start_realtime()
        self.offer = self.args.offer.read_text()
        assert self.offer.startswith("v=0") and "m=video" in self.offer and "m=audio" in self.offer
        self.record("real_dependencies_started", mysql=self.sql("SELECT VERSION()")[0][0],
                    redis=self.cache.info("server")["redis_version"], ports=[self.mysql_port,self.redis_port,self.tcp_port])

    def start_media(self):
        self.media = self.launch(f"media-{self.restart_count}", [self.media_dir / "MediaServer"])
        wait_for(lambda: Path(self.uds).exists() and self.media.poll() is None)

    def start_realtime(self):
        self.rt = self.launch(f"realtime-{self.restart_count}", [self.rt_dir / "RealtimeServer"])
        probe = wait_for(lambda: socket.create_connection(("127.0.0.1", self.tcp_port), .2))
        probe.close()
        time.sleep(.3)

    def login(self, uid):
        client = Client(self.tcp_port)
        self.clients.append(client)
        client.request(1010, dict(uid=uid, email=f"user{uid}@integration.invalid", token=f"local-fixture-{uid}"))
        return client

    def meeting(self, host, title):
        result = host.request(1012, dict(title=title))
        code = next(x["meeting_code"] for x in result["meetings"] if x["title"] == title)
        joined = host.request(1016, dict(meeting_code=code))
        mid = joined["meeting_id"]
        host.request(1018, dict(meeting_id=mid))
        return mid, code

    def chat(self, mid, tag):
        return dict(meeting_id=mid, chat_type="group", target_user_id=None, content=f"中文联调 {tag}", client_msg_id=tag)

    def media_offer(self, client, mid):
        client.send(1038, dict(meeting_id=mid, type="offer", sdp=self.offer))
        answer = client.receive(1040)
        assert answer.get("error") == 0 and "m=video" in answer.get("sdp", ""), answer
        candidate = client.receive(1041)
        assert candidate.get("candidate"), candidate
        wait_for(lambda: self.udp_sockets() > 0)

    def udp_sockets(self):
        if self.media.poll() is not None:
            return 0
        inodes = set()
        for path in Path(f"/proc/{self.media.pid}/fd").iterdir():
            try:
                link = path.readlink().as_posix()
                if link.startswith("socket:["):
                    inodes.add(link[8:-1])
            except FileNotFoundError:
                pass
        return sum(line.split()[9] in inodes for kind in ("udp", "udp6")
                   for line in Path(f"/proc/{self.media.pid}/net/{kind}").read_text().splitlines()[1:])

    def check_media_released(self, name):
        try:
            wait_for(lambda: self.udp_sockets() == 0, 3)
            self.record(name, True, media_udp_sockets=0)
        except AssertionError:
            self.record(name, False, media_udp_sockets=self.udp_sockets())

    def wait_insert(self):
        return wait_for(lambda: any("INSERT INTO meeting_messages" in (row[7] or "")
                                   for row in self.sql("SHOW FULL PROCESSLIST")))

    def run(self):
        host, member, other = self.login(101), self.login(102), self.login(103)
        mid, code = self.meeting(host, "integration-room-a")
        mid_b, code_b = self.meeting(other, "integration-room-b")
        # 连续入会和聊天不能被异步会议号查询打乱。
        member.send(1016, dict(meeting_code=code))
        member.send(1026, self.chat(mid, "join-then-chat"))
        joined, chat = member.receive(1017), member.receive(1027)
        assert joined["error"] == chat["error"] == 0
        assert self.cache.smembers(f"room:{mid}:members") == {"101", "102"}
        assert self.sql("SELECT participation_status FROM meeting_participants WHERE meeting_id=%s AND user_id=102", (mid,)) == ((1,),)
        self.record("join_then_chat_fifo", meeting=mid, sql_participant_status=1, redis_members=2)
        host.receive(1028, lambda m: m.get("client_msg_id") == "join-then-chat")
        duplicate = member.request(1026, self.chat(mid, "join-then-chat"))
        assert duplicate["message_id"] == chat["message_id"]
        assert self.sql("SELECT COUNT(*) FROM meeting_messages WHERE client_msg_id='join-then-chat'")[0][0] == 1
        self.record("chat_persistence_and_retry_idempotency")
        history = member.request(1034, dict(meeting_id=mid, limit=100))
        assert any(m["client_msg_id"] == "join-then-chat" and m["content"] == "中文联调 join-then-chat"
                   for m in history["messages"])
        self.record("chat_history_readback")
        private = self.chat(mid, "private-message")
        private.update(chat_type="private", target_user_id=101)
        member.request(1026, private)
        host.receive(1028, lambda m: m.get("client_msg_id") == "private-message")
        private_history = host.request(1036, dict(meeting_id=mid, peer_user_id=102))
        assert [m["client_msg_id"] for m in private_history["messages"]] == ["private-message"]
        group_history = host.request(1034, dict(meeting_id=mid))
        assert all(m["client_msg_id"] != "private-message" for m in group_history["messages"])
        other.request(1036, dict(meeting_id=mid, peer_user_id=102), expected=107)
        self.record("private_chat_history_and_cross_room_access")

        def send_batch(client, prefix):
            for number in range(20):
                client.send(1026, self.chat(mid, f"burst-{prefix}-{number:02}"))
            replies = [client.receive(1027) for _ in range(20)]
            assert all(r["error"] == 0 for r in replies), replies
            assert [r["client_msg_id"] for r in replies] == [f"burst-{prefix}-{n:02}" for n in range(20)]

        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as workers:
            jobs = [workers.submit(send_batch, host, "a"), workers.submit(send_batch, member, "b")]
            for job in jobs:
                job.result(timeout=15)
        pushed = [host.receive(1028, lambda m: m.get("client_msg_id", "").startswith("burst-")) for _ in range(40)]
        stored = self.sql("SELECT id, client_msg_id FROM meeting_messages WHERE client_msg_id LIKE 'burst-%%' ORDER BY id")
        assert [int(m["message_id"]) for m in pushed] == [r[0] for r in stored]
        self.record("concurrent_chat_broadcast_matches_sql_order", messages=len(stored), producers=2)

        # 真实 MySQL 表锁使一个存储任务停在 INSERT，同房间后续请求必须等待。
        self.sql("LOCK TABLES meeting_messages WRITE")
        try:
            host.send(1026, self.chat(mid, "locked-first"))
            self.wait_insert()
            member.send(1026, self.chat(mid, "locked-second"))
            other.send(1014, {})
            started = time.monotonic()
            assert other.receive(1015)["error"] == 0
            elapsed = time.monotonic() - started
            time.sleep(.3)
            waiting = [r for r in self.sql("SHOW FULL PROCESSLIST") if "INSERT INTO meeting_messages" in (r[7] or "")]
            assert len(waiting) == 1, waiting
            assert self.sql("SELECT COUNT(*) FROM meeting_messages WHERE client_msg_id LIKE 'locked-%'")[0][0] == 0
        finally:
            self.sql("UNLOCK TABLES")
        first, second = host.receive(1027), member.receive(1027)
        assert first["error"] == second["error"] == 0
        order = [r[0] for r in self.sql("SELECT client_msg_id FROM meeting_messages WHERE client_msg_id LIKE 'locked-%' ORDER BY id")]
        assert order == ["locked-first", "locked-second"], order
        self.record("slow_mysql_room_serialization_and_other_room_progress", insert_order=order,
                    other_room_response_ms=round(elapsed*1000, 1), simultaneous_waiting_inserts=1)

        duplicate_login = self.login(102)
        duplicate_login.request(1016, dict(meeting_code=code_b), expected=107)
        duplicate_login.close()
        self.record("same_uid_cross_room_rejected")

        # 真实 MediaServer 接收 Offer 并返回 Answer / ICE；检查进程自有 UDP fd 的回收。
        self.media_offer(member, mid)
        self.record("real_media_offer_answer_candidate", media_udp_sockets=self.udp_sockets())
        member.send(1026, self.chat(mid, "chat-before-leave"))
        member.send(1021, dict(meeting_id=mid))
        assert member.receive(1027)["error"] == member.receive(1022)["error"] == 0
        host.receive(1028, lambda m: m.get("client_msg_id") == "chat-before-leave")
        host.receive(1030, lambda m: m.get("user_id") == 102)
        assert not self.cache.sismember(f"room:{mid}:members", "102")
        assert self.cache.hget("presence:102", "status") == "online"
        assert self.cache.hget("presence:102", "meeting_id") == ""
        assert self.cache.zscore("room:reconnect_deadlines", f"{mid}:102") is None
        self.record("explicit_leave_redis_cleanup", not self.cache.exists(f"room:{mid}:member:102"),
                    member_hash_exists=bool(self.cache.exists(f"room:{mid}:member:102")))
        self.check_media_released("explicit_leave_media_cleanup")

        member.request(1016, dict(meeting_code=code))
        self.media_offer(member, mid)
        member.close()
        host.receive(1031, lambda m: m.get("user_id") == 102)
        assert self.cache.hget(f"room:{mid}:member:102", "room_state") == "reconnecting"
        assert self.cache.sismember(f"room:{mid}:members", "102")
        self.check_media_released("tcp_disconnect_media_cleanup")
        member = self.login(102)
        member.request(1016, dict(meeting_code=code))
        host.receive(1032, lambda m: m.get("user_id") == 102)
        assert self.cache.zscore("room:reconnect_deadlines", f"{mid}:102") is None
        assert self.cache.hget(f"room:{mid}:member:102", "room_state") == "active"
        self.record("tcp_reconnect_within_grace", member_count=self.cache.scard(f"room:{mid}:members"))
        self.media_offer(member, mid)
        host.request(1023, dict(meeting_id=mid))
        member.receive(1025)
        assert self.sql("SELECT status FROM meetings WHERE id=%s", (mid,))[0][0] == 2
        assert not self.cache.exists(f"room:{mid}:members", f"room:{mid}:member:101", f"room:{mid}:member:102")
        assert self.cache.zscore("room:reconnect_deadlines", f"{mid}:102") is None
        assert self.cache.hget("presence:101", "meeting_id") == self.cache.hget("presence:102", "meeting_id") == ""
        self.check_media_released("end_meeting_media_cleanup")
        self.record("end_meeting_sql_and_redis_cleanup")

        # 只终止本脚本启动的 Media，保留 Realtime；验证单 I/O 自动重连与失效关联拒绝。
        self.media.terminate()
        self.media.wait(timeout=10)
        time.sleep(.3)
        mid, code = self.meeting(host, "integration-recovery")
        member.request(1016, dict(meeting_code=code))
        member.send(1038, dict(meeting_id=mid, type="offer", sdp=self.offer))
        assert member.receive(1040)["error"] == 101
        assert other.request(1014, {})["error"] == 0
        self.restart_count += 1
        self.start_media()
        time.sleep(2)
        member.send(1039, dict(meeting_id=mid, candidate="candidate:1 1 UDP 1 127.0.0.1 9 typ host", mid="video"))
        assert member.receive(1041)["error"] == 101
        self.record("old_media_binding_rejected_after_uds_reconnect")
        self.media_offer(member, mid)
        self.record("media_restart_uds_reconnect_new_offer", realtime_pid_unchanged=self.rt.poll() is None)
        member.close()
        host.receive(1031, lambda m: m.get("user_id") == 102)
        started = time.monotonic()
        host.receive(1033, lambda m: m.get("user_id") == 102, timeout=50)
        elapsed = time.monotonic() - started
        assert 40 < elapsed < 50, elapsed
        assert not self.cache.sismember(f"room:{mid}:members", "102")
        assert not self.cache.exists(f"room:{mid}:member:102")
        assert self.cache.zscore("room:reconnect_deadlines", f"{mid}:102") is None
        self.record("real_45_second_reconnect_expiry", elapsed_seconds=round(elapsed, 2))
        member = self.login(102)
        member.request(1016, dict(meeting_code=code))
        self.record("join_again_after_expiry")

        # SIGTERM 到达时正在等待真实 MySQL，进程应排空在途操作和断线收尾。
        self.media_offer(member, mid)
        self.sql("LOCK TABLES meeting_messages WRITE")
        try:
            member.send(1026, self.chat(mid, "shutdown-inflight"))
            self.wait_insert()
            self.rt.send_signal(signal.SIGTERM)
            time.sleep(.4)
            assert self.rt.poll() is None, "shutdown abandoned in-flight work"
        finally:
            self.sql("UNLOCK TABLES")
        started = time.monotonic()
        assert self.rt.wait(timeout=15) == 0
        assert self.sql("SELECT COUNT(*) FROM meeting_messages WHERE client_msg_id='shutdown-inflight'")[0][0] == 1
        assert self.cache.hget(f"room:{mid}:member:102", "room_state") == "reconnecting"
        self.check_media_released("realtime_shutdown_uds_peer_cleanup")
        self.record("sigterm_drains_real_slow_task", exit_code=0, drain_after_unlock_ms=round((time.monotonic()-started)*1000,1))
        self.restart_count += 1
        self.start_realtime()
        host, member = self.login(101), self.login(102)
        host.request(1016, dict(meeting_code=code))
        member.request(1016, dict(meeting_code=code))
        host.request(1023, dict(meeting_id=mid))
        self.rt.send_signal(signal.SIGINT)
        assert self.rt.wait(timeout=15) == 0
        self.record("realtime_restart_rejoin_and_sigint_exit", exit_code=0)

    def cleanup(self):
        for client in self.clients:
            client.close()
        for process in reversed(self.processes):
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
                    self.record("forced_cleanup_required", False, pid=process.pid)
        if self.db:
            self.db.close()
        for path, original in self.configs.items():
            if original is None:
                path.unlink(missing_ok=True)
            else:
                path.write_bytes(original)
        self.record("owned_processes_stopped_and_runtime_configs_restored",
                    all(p.poll() is not None for p in self.processes) and
                    all((path.read_bytes() == original) if original is not None else not path.exists()
                        for path, original in self.configs.items()))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--offer", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    harness = Harness(args)
    try:
        harness.setup()
        harness.run()
    except Exception:
        harness.record("unexpected_failure", False, details=traceback.format_exc())
    finally:
        harness.cleanup()
    return 0 if all(result["passed"] for result in harness.results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
