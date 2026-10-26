#!/usr/bin/env python3
"""业务恢复真实依赖回归；不启动媒体，不访问共享数据库。"""
import argparse
import json
from pathlib import Path
import struct
import time
import traceback
import uuid

import pymysql
import redis
from run_local import Client, Harness, wait_for


class RecoveryHarness(Harness):
    def setup(self):
        runtime = self.args.runtime.resolve()
        data = self.root / "mysql-data"
        data.mkdir()
        mysql = runtime / "usr/sbin/mysqld"
        init = self.launch("mysql-initialize", [mysql, "--no-defaults", "--initialize-insecure",
            f"--basedir={runtime / 'usr'}", f"--datadir={data}", "--innodb-buffer-pool-size=64M"])
        assert init.wait(timeout=120) == 0
        self.launch("mysql", [mysql, "--no-defaults", f"--basedir={runtime / 'usr'}",
            f"--datadir={data}", f"--port={self.mysql_port}", "--bind-address=127.0.0.1", "--mysqlx=0",
            f"--socket={self.root / 'mysql.sock'}", f"--pid-file={self.root / 'mysql.pid'}",
            "--skip-log-bin", "--secure-file-priv=NULL", "--innodb-buffer-pool-size=64M"])
        self.db = wait_for(lambda: pymysql.connect(unix_socket=str(self.root / "mysql.sock"),
            user="root", autocommit=True), 30)
        for statement in (self.repo / "syncRTC-server/db/init/01-schema.sql").read_text().split(";"):
            if statement.strip():
                self.sql(statement)
        self.sql("CREATE USER 'integration'@'127.0.0.1' IDENTIFIED WITH mysql_native_password BY ''")
        self.sql("GRANT ALL ON syncrtc.* TO 'integration'@'127.0.0.1'")
        for uid in range(101, 105):
            self.sql("INSERT INTO users (id,username,email,password_hash,display_name) VALUES (%s,%s,%s,'fixture',%s)",
                (uid, f"user{uid}", f"user{uid}@integration.invalid", f"恢复测试{uid}"))
        self.launch("redis", [runtime / "usr/bin/redis-server", "--bind", "127.0.0.1",
            "--port", self.redis_port, "--save", "", "--appendonly", "no", "--dir", self.root])
        self.cache = redis.Redis(host="127.0.0.1", port=self.redis_port, decode_responses=True)
        wait_for(self.cache.ping)
        for uid in range(101, 105):
            self.cache.set(f"auth::session:local-fixture-{uid}", json.dumps({"uid": uid, "device_id": "test"}), ex=3600)
        self.configure(self.rt_dir, f"[RealtimeServer]\nPort={self.tcp_port}\n[MediaServer]\nInternalSocketPath={self.uds}\n"
            f"[Mysql]\nHost=127.0.0.1\nPort={self.mysql_port}\nUser=integration\nPassword=\nDatabase=syncrtc\n"
            f"[Redis]\nHost=127.0.0.1\nPort={self.redis_port}\nPassword=\n")
        self.start_realtime()
        self.record("isolated_mysql_redis_realtime_started")

    def resume_request(self, snapshot, request_id=None):
        return dict(meeting_id=snapshot["meeting_id"], resume_ticket=snapshot["resume_ticket"],
                    membership_generation=snapshot["membership_generation"],
                    request_id=request_id or str(uuid.uuid4()))

    def run(self):
        host = self.login(101)
        member = self.login(102)
        created = host.request(1012, dict(title="password-recovery", password="meeting-secret"))
        code = next(item["meeting_code"] for item in created["meetings"] if item["title"] == "password-recovery")
        host_joined = host.request(1016, dict(meeting_code=code, password="meeting-secret"))
        joined = member.request(1016, dict(meeting_code=code, password="meeting-secret"))
        mid = joined["meeting_id"]
        assert len(joined["resume_ticket"]) == 64
        assert joined["membership_generation"] == "1"
        self.record("join_issues_ticket_and_generation")

        takeover = self.login(102)
        request = self.resume_request(joined)
        resumed = takeover.request(1046, request)
        assert resumed["membership_generation"] == "2"
        assert resumed["resume_ticket"] == joined["resume_ticket"]
        assert self.cache.scard(f"room:{mid}:members") == 2
        # 旧连接从未主动断开；接管主动失效它，其迟到断线不能把新成员改为 reconnecting。
        time.sleep(.2)
        assert self.cache.hget(f"room:{mid}:member:102", "room_state") == "active"
        takeover.request(1026, self.chat(mid, "after-active-takeover"))
        duplicate = takeover.request(1046, request)
        assert duplicate["membership_generation"] == "2"
        assert self.sql("SELECT COUNT(*) FROM meeting_participants WHERE meeting_id=%s AND user_id=102", (mid,))[0][0] == 1
        self.record("active_takeover_password_bypass_with_ticket_and_idempotent_retry", members=2)

        # 用本隔离实例中的 SQL / Redis 错误验证分类，恢复资格不能被误判失效。
        retry_request = self.resume_request(resumed)
        self.sql("RENAME TABLE meetings TO recovery_meetings_backup")
        try:
            takeover.request(1046, retry_request, expected=3)
        finally:
            self.sql("RENAME TABLE recovery_meetings_backup TO meetings")
        members_key = f"room:{mid}:members"
        saved_members = self.cache.smembers(members_key)
        self.cache.delete(members_key)
        self.cache.set(members_key, "wrong-type-fixture")
        try:
            takeover.request(1046, retry_request, expected=2)
        finally:
            self.cache.delete(members_key)
            self.cache.sadd(members_key, *saved_members)
        resumed = takeover.request(1046, retry_request)
        self.record("storage_errors_are_retryable_and_keep_ticket_valid")

        self.cache.set("auth::session:another-device", json.dumps({"uid": 102, "device_id": "different"}))
        wrong_device = Client(self.tcp_port)
        self.clients.append(wrong_device)
        wrong_device.request(1010, dict(uid=102, email="user102@integration.invalid", token="another-device"))
        wrong_device.request(1046, self.resume_request(resumed), expected=107)
        bad_ticket = self.login(102)
        invalid = self.resume_request(resumed)
        invalid["resume_ticket"] = "invalid"
        bad_ticket.request(1046, invalid, expected=107)
        self.record("wrong_ticket_and_other_device_rejected")

        # 在服务端发出成功响应后主动丢弃连接，客户端仍只有旧代次和 request_id。
        lost_request = self.resume_request(resumed)
        lost = self.login(102)
        lost.send(1046, lost_request)
        wait_for(lambda: self.cache.hget(f"room:{mid}:member:102", "room_state") == "active")
        time.sleep(.2)
        lost.close()
        wait_for(lambda: self.cache.hget(f"room:{mid}:member:102", "room_state") == "reconnecting")
        recovered = self.login(102)
        latest = recovered.request(1046, lost_request)
        assert int(latest["membership_generation"]) >= 4
        assert self.cache.zscore("room:reconnect_deadlines", f"{mid}:102") is None
        assert self.cache.scard(f"room:{mid}:members") == 2
        self.record("lost_response_retries_same_request_without_duplicate_membership")

        # 快照构造等待真实数据库，后续开始会议不能越过快照响应。
        ordered = self.login(102)
        self.sql("LOCK TABLES users WRITE")
        try:
            ordered.send(1046, self.resume_request(latest))
            wait_for(lambda: any("users" in (row[7] or "") and row[0] != self.db.thread_id()
                                 for row in self.sql("SHOW FULL PROCESSLIST")))
            host.send(1018, dict(meeting_id=mid))
            time.sleep(.1)
        finally:
            self.sql("UNLOCK TABLES")
        kind, length = struct.unpack("!HH", ordered.read_exact(4))
        ordered_snapshot = json.loads(ordered.read_exact(length))
        assert kind == 1047 and ordered_snapshot["status"] == "scheduled"
        assert ordered.receive(1020)["status"] == "in_progress"
        assert host.receive(1019)["error"] == 0
        self.record("snapshot_precedes_later_meeting_state_after_slow_query")

        # 传输心跳绕过被会议慢任务占据的队列。
        self.sql("LOCK TABLES meeting_messages WRITE")
        try:
            ordered.send(1026, self.chat(mid, "blocked-chat"))
            self.wait_insert()
            ordered.send(1048, {})
            assert ordered.receive(1049, timeout=2) == {}
        finally:
            self.sql("UNLOCK TABLES")
        assert ordered.receive(1027)["error"] == 0
        self.record("heartbeat_does_not_wait_for_meeting_storage")

        ordered.request(1021, dict(meeting_id=mid))
        invalidated = self.login(102)
        invalidated.request(1046, self.resume_request(ordered_snapshot), expected=107)
        self.record("explicit_leave_invalidates_ticket")

        # 断网期间会议结束，重连不能重建已经结束的会议。
        joined_again = invalidated.request(1016, dict(meeting_code=code, password="meeting-secret"))
        invalidated.close()
        wait_for(lambda: self.cache.hget(f"room:{mid}:member:102", "room_state") == "reconnecting")
        host.request(1023, dict(meeting_id=mid))
        ended = self.login(102)
        ended.request(1046, self.resume_request(joined_again), expected=105)
        self.record("ended_meeting_cannot_resume")

        timeout_host = self.login(103)
        timeout_mid, timeout_code = self.meeting(timeout_host, "timeout-room")
        expiring = self.login(104)
        expiring_snapshot = expiring.request(1016, dict(meeting_code=timeout_code))
        started = time.monotonic()
        expiring.close()
        wait_for(lambda: self.cache.hget(f"room:{timeout_mid}:member:104", "room_state") == "reconnecting")
        # 模拟墙钟前跳产生的提前到期候选，仍须完整等待本地单调时钟宽限期。
        self.cache.hset(f"room:{timeout_mid}:member:104", "reconnect_deadline_at", "1")
        self.cache.zadd("room:reconnect_deadlines", {f"{timeout_mid}:104": 1})
        wait_for(lambda: not self.cache.sismember(f"room:{timeout_mid}:members", "104"), 50)
        expired = self.login(104)
        expired.request(1046, self.resume_request(expiring_snapshot), expected=107)
        elapsed = time.monotonic() - started
        assert elapsed >= 44, "宽限期提前结束"
        self.record("real_45_second_grace_expiry_rejects_ticket", elapsed_seconds=round(elapsed, 2))

        self.cache.delete("auth::session:local-fixture-104")
        revoked = Client(self.tcp_port)
        self.clients.append(revoked)
        revoked.request(1010, dict(uid=104, email="user104@integration.invalid", token="local-fixture-104"), expected=112)
        self.record("revoked_auth_token_is_terminal_not_redis_outage")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    harness = RecoveryHarness(parser.parse_args())
    try:
        harness.setup()
        harness.run()
    except Exception:
        harness.record("unexpected_failure", False, details=traceback.format_exc())
    finally:
        harness.cleanup()
    return 0 if all(item["passed"] for item in harness.results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
