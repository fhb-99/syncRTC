#!/usr/bin/env python3
"""心跳与恢复统一路径联调；复用隔离实例，包含业务恢复一期回归。"""
import argparse
import json
from pathlib import Path
import socket
import time
import traceback

from run_local import Client, wait_for
from run_recovery import RecoveryHarness


class HeartbeatHarness(RecoveryHarness):
    def setup(self):
        super().setup()
        for uid in range(201, 204):
            self.sql("INSERT INTO users (id,username,email,password_hash,display_name) VALUES (%s,%s,%s,'fixture',%s)",
                     (uid, f"user{uid}", f"user{uid}@integration.invalid", f"心跳测试{uid}"))
            self.cache.set(f"auth::session:local-fixture-{uid}", json.dumps({"uid": uid}), ex=3600)

    @staticmethod
    def expect_closed(client):
        client.sock.settimeout(3)
        while client.sock.recv(4096):
            pass

    def run(self):
        super().run()
        host = self.login(201)
        observer = self.login(202)
        created = host.request(1012, dict(title="heartbeat-room"))
        code = next(x["meeting_code"] for x in created["meetings"] if x["title"] == "heartbeat-room")
        snapshot = host.request(1016, dict(meeting_code=code))
        mid = snapshot["meeting_id"]
        observer.request(1016, dict(meeting_code=code))
        host.request(1018, dict(meeting_id=mid))

        unauthenticated = Client(self.tcp_port)
        self.clients.append(unauthenticated)
        # 只有半个帧头不能刷新登录期限；这条连接在其他场景运行时持续等待。
        unauthenticated.sock.sendall(b"\x03")
        unauthenticated_started = time.monotonic()

        self.sql("LOCK TABLES meeting_messages WRITE")
        latencies = []
        try:
            host.send(1026, self.chat(mid, "heartbeat-slow-21s"))
            self.wait_insert()
            blocked = time.monotonic()
            for sequence in range(1, 6):
                if sequence > 1:
                    time.sleep(5)
                started = time.monotonic()
                last_ping = started
                host.send(1048, {"seq": str(sequence)})
                assert host.receive(1049, timeout=2) == {"seq": str(sequence)}
                latencies.append(time.monotonic() - started)
            time.sleep(1)
            blocked_seconds = time.monotonic() - blocked
            assert blocked_seconds >= 20
            assert self.cache.hget(f"room:{mid}:member:201", "room_state") == "active"
        finally:
            self.sql("UNLOCK TABLES")
        assert host.receive(1027)["error"] == 0
        self.record("heartbeat_stays_alive_during_20_second_sql_wait",
                    blocked_seconds=round(blocked_seconds, 2), max_pong_ms=round(max(latencies) * 1000, 2))

        # 最后一次有效 PING 后完全静默；超时必须走普通断线清理且只广播一次。
        wait_for(lambda: self.cache.hget(f"room:{mid}:member:201", "room_state") == "reconnecting", 24)
        timeout_seconds = time.monotonic() - last_ping
        assert timeout_seconds >= 19, "服务端提前判定心跳超时"
        self.expect_closed(host)
        notice = observer.receive(1031)
        assert str(notice["user_id"]) == "201"
        try:
            observer.receive(1031, timeout=.3)
            raise AssertionError("心跳超时产生了重复断线通知")
        except socket.timeout:
            pass
        assert self.cache.scard(f"room:{mid}:members") == 2
        assert self.cache.zscore("room:reconnect_deadlines", f"{mid}:201") is not None
        self.record("heartbeat_timeout_uses_one_disconnect_and_keeps_recovery_grace",
                    timeout_seconds=round(timeout_seconds, 2))

        resumed = self.login(201)
        restored = resumed.request(1046, self.resume_request(snapshot))
        assert restored["membership_generation"] == "2"
        assert self.cache.hget(f"room:{mid}:member:201", "room_state") == "active"
        assert self.cache.scard(f"room:{mid}:members") == 2
        self.record("heartbeat_timeout_then_auth_and_resume_restores_same_membership")

        self.expect_closed(unauthenticated)
        self.record("partial_frame_does_not_extend_30_second_login_deadline",
                    observed_after_seconds=round(time.monotonic() - unauthenticated_started, 2))

        flood = self.login(203)
        for sequence in range(1, 6):
            flood.send(1048, {"seq": str(sequence)})
        self.expect_closed(flood)
        # 另一已登录连接仍可请求业务，违规连接关闭没有影响其他 Session。
        resumed.request(1014, {})
        self.record("heartbeat_rate_limit_closes_only_offending_connection")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    harness = HeartbeatHarness(parser.parse_args())
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
