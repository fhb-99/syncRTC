#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RELEASE_SCRIPT="$(cd "${SCRIPT_DIR}/.." && pwd)/remote-release.sh"
passed=0
failed=0

pass() { echo "PASS: $1"; passed=$((passed + 1)); }
fail() { echo "FAIL: $1" >&2; failed=$((failed + 1)); }

new_fixture() {
    FIXTURE="$(mktemp -d)"
    ROOT="${FIXTURE}/backend"
    MOCK_BIN="${FIXTURE}/mock-bin"
    STATE="${FIXTURE}/state"
    PROC="${FIXTURE}/proc"
    RELEASE_ID="20260903-120000-deadbeef"
    STAGE="${ROOT}/updates/${RELEASE_ID}"
    mkdir -p "${MOCK_BIN}" "${STATE}" "${PROC}" "${ROOT}/deploy" \
        "${ROOT}/runtime/lib" "${ROOT}/backups" \
        "${ROOT}/syncRTC-server/GateServer/bin/conf" \
        "${ROOT}/syncRTC-server/RealtimeServer/bin/conf" \
        "${ROOT}/syncRTC-server/MediaServer/bin/conf" \
        "${ROOT}/syncRTC-server/VarifyServer" \
        "${STAGE}/artifacts/gate" "${STAGE}/artifacts/realtime" "${STAGE}/artifacts/media"

    printf 'old-gate\n' > "${ROOT}/syncRTC-server/GateServer/bin/GateServer"
    printf 'old-realtime\n' > "${ROOT}/syncRTC-server/RealtimeServer/bin/RealtimeServer"
    printf 'old-media\n' > "${ROOT}/syncRTC-server/MediaServer/bin/MediaServer"
    printf '[safe]\nvalue=ok\n' > "${ROOT}/syncRTC-server/GateServer/bin/conf/config.ini"
    printf '[safe]\nvalue=ok\n' > "${ROOT}/syncRTC-server/RealtimeServer/bin/conf/config.ini"
    printf '[safe]\nvalue=ok\n' > "${ROOT}/syncRTC-server/MediaServer/bin/conf/config.ini"
    printf '{"preserved":true}\n' > "${ROOT}/syncRTC-server/VarifyServer/config.json"
    printf 'new-gate\n' > "${STAGE}/artifacts/gate/GateServer"
    printf 'new-realtime\n' > "${STAGE}/artifacts/realtime/RealtimeServer"
    printf 'new-media\n' > "${STAGE}/artifacts/media/MediaServer"
    printf '{}\n' > "${STAGE}/metadata.json"
    cp "${RELEASE_SCRIPT}" "${STAGE}/remote-release.sh"
    chmod +x "${STAGE}/remote-release.sh"
    touch "${STATE}/gate.active" "${STATE}/realtime.active" "${STATE}/media.active"

    mkdir -p "${PROC}/101" "${PROC}/102" "${PROC}/103"
    ln -s "${ROOT}/syncRTC-server/GateServer/bin/GateServer" "${PROC}/101/exe"
    ln -s "${ROOT}/syncRTC-server/RealtimeServer/bin/RealtimeServer" "${PROC}/102/exe"
    ln -s "${ROOT}/syncRTC-server/MediaServer/bin/MediaServer" "${PROC}/103/exe"

    cat > "${ROOT}/deploy/verify-config.py" <<'PY'
print("ALL CONFIG AND DATABASE CHECKS PASSED")
PY

    cat > "${MOCK_BIN}/systemctl" <<'SH'
#!/usr/bin/env bash
set -e
cmd="$1"; shift
unit_to_name() {
    case "$1" in
        syncrtc-gate.service) echo gate ;;
        syncrtc-realtime.service) echo realtime ;;
        syncrtc-media.service) echo media ;;
        *) exit 1 ;;
    esac
}
case "$cmd" in
    is-active)
        [[ "${1:-}" == "--quiet" ]] && shift
        name="$(unit_to_name "$1")"
        test -f "${FAKE_STATE_DIR}/${name}.active"
        ;;
    show)
        unit="${@: -1}"
        case "$(unit_to_name "$unit")" in gate) echo 101;; realtime) echo 102;; media) echo 103;; esac
        ;;
    start|stop)
        for unit in "$@"; do
            name="$(unit_to_name "$unit")"
            echo "$cmd $name" >> "${FAKE_STATE_DIR}/operations.log"
            if [[ "$cmd" == start ]]; then touch "${FAKE_STATE_DIR}/${name}.active"; else rm -f "${FAKE_STATE_DIR}/${name}.active"; fi
        done
        ;;
    *) exit 1 ;;
esac
SH

    cat > "${MOCK_BIN}/docker" <<'SH'
#!/usr/bin/env bash
if [[ "$*" == *"{{.State.Running}}"* ]]; then echo true; else echo healthy; fi
SH
    cat > "${MOCK_BIN}/file" <<'SH'
#!/usr/bin/env bash
echo "$*: ELF 64-bit LSB pie executable, x86-64"
SH
    cat > "${MOCK_BIN}/readelf" <<'SH'
#!/usr/bin/env bash
echo '  Machine:                           Advanced Micro Devices X86-64'
SH
    cat > "${MOCK_BIN}/ldd" <<'SH'
#!/usr/bin/env bash
if [[ -f "${FAKE_STATE_DIR}/ldd-missing" ]]; then echo 'libmissing.so => not found'; else echo 'libc.so.6 => /lib/libc.so.6'; fi
SH
    cat > "${MOCK_BIN}/ss" <<'SH'
#!/usr/bin/env bash
if [[ "$*" == *-xlpn* ]]; then
    echo 'u_str LISTEN 0 128 /tmp/syncrtc-mediaserver.sock users:(("MediaServer",pid=103,fd=3))'
else
    echo 'LISTEN 0 128 127.0.0.1:50051 users:(("node",pid=200,fd=1))'
    echo 'LISTEN 0 128 0.0.0.0:8081 users:(("GateServer",pid=101,fd=1))'
    echo 'LISTEN 0 128 0.0.0.0:8090 users:(("RealtimeServer",pid=102,fd=1))'
fi
SH
    cat > "${MOCK_BIN}/curl" <<'SH'
#!/usr/bin/env bash
echo 'receive get_test requestparam1 key is source, value is publisher'
SH
    cat > "${MOCK_BIN}/journalctl" <<'SH'
#!/usr/bin/env bash
if [[ -f "${FAKE_STATE_DIR}/uds-log-fail" ]]; then echo 'connect MediaServer UDS failed'; fi
SH
    cat > "${MOCK_BIN}/sleep" <<'SH'
#!/usr/bin/env bash
exit 0
SH
    chmod +x "${MOCK_BIN}"/*
}

write_manifest() {
    local service="$1"
    local -a files=(metadata.json remote-release.sh)
    case "$service" in
        gate) files+=(artifacts/gate/GateServer) ;;
        realtime) files+=(artifacts/realtime/RealtimeServer) ;;
        media) files+=(artifacts/media/MediaServer) ;;
        all) files+=(artifacts/gate/GateServer artifacts/realtime/RealtimeServer artifacts/media/MediaServer) ;;
    esac
    (cd "${STAGE}" && printf '%s\n' "${files[@]}" | sort | xargs sha256sum) > "${STAGE}/manifest.sha256"
    find "${STAGE}/artifacts" -type f | while read -r path; do
        rel="${path#${STAGE}/}"
        if ! printf '%s\n' "${files[@]}" | grep -Fxq "${rel}"; then rm -f "${path}"; fi
    done
}

run_release() {
    env PATH="${MOCK_BIN}:${PATH}" SYNCRTC_TEST_MODE=1 \
        SYNCRTC_TEST_ROOT="${ROOT}" SYNCRTC_TEST_PROC_ROOT="${PROC}" \
        FAKE_STATE_DIR="${STATE}" bash "${RELEASE_SCRIPT}" "$@"
}

cleanup_fixture() {
    local resolved
    resolved="$(realpath "${FIXTURE}")"
    case "$resolved" in /tmp/*) rm -rf -- "$resolved";; *) echo "拒绝清理异常测试目录: $resolved" >&2;; esac
}

test_invalid_service() {
    new_fixture
    if run_release --service '../../bad' --release-id "${RELEASE_ID}" >/dev/null 2>&1; then fail '参数白名单'; else pass '参数白名单'; fi
    cleanup_fixture
}

test_path_escape() {
    new_fixture
    if run_release --service gate --release-id '../escape' >/dev/null 2>&1; then fail '路径越界'; else pass '路径越界'; fi
    cleanup_fixture
}

test_hash_mismatch_before_stop() {
    new_fixture; write_manifest gate
    printf 'tampered\n' >> "${STAGE}/artifacts/gate/GateServer"
    if run_release --service gate --release-id "${RELEASE_ID}" >/dev/null 2>&1; then
        fail 'SHA 不一致拦截'
    elif [[ ! -s "${STATE}/operations.log" ]]; then pass 'SHA 不一致拦截'; else fail 'SHA 失败前不应停服务'; fi
    cleanup_fixture
}

test_ldd_missing_before_stop() {
    new_fixture; write_manifest realtime; touch "${STATE}/ldd-missing"
    if run_release --service realtime --release-id "${RELEASE_ID}" >/dev/null 2>&1; then
        fail 'ldd 缺库拦截'
    elif [[ ! -s "${STATE}/operations.log" ]]; then pass 'ldd 缺库拦截'; else fail 'ldd 失败前不应停服务'; fi
    cleanup_fixture
}

test_health_failure_rolls_back() {
    new_fixture; write_manifest gate
    cat > "${MOCK_BIN}/curl" <<'SH'
#!/usr/bin/env bash
count_file="${FAKE_STATE_DIR}/curl-count"
count=0; [[ -f "$count_file" ]] && count="$(cat "$count_file")"
count=$((count + 1)); echo "$count" > "$count_file"
if [[ "$count" -eq 2 ]]; then echo 'unexpected body'; exit 0; fi
echo 'receive get_test requestparam1 key is source, value is publisher'
SH
    chmod +x "${MOCK_BIN}/curl"
    if run_release --service gate --release-id "${RELEASE_ID}" >/dev/null 2>&1; then
        fail '健康失败触发回滚'
    elif ! grep -Fxq 'old-gate' "${ROOT}/syncRTC-server/GateServer/bin/GateServer"; then
        fail '健康失败未恢复旧文件'
    elif grep -Eq ' (realtime|media)$' "${STATE}/operations.log"; then
        fail 'GateServer 回滚不应触碰其他服务'
    else
        pass '健康响应异常触发隔离回滚'
    fi
    cleanup_fixture
}

test_media_realtime_order() {
    new_fixture; write_manifest media
    if run_release --service media --release-id "${RELEASE_ID}" --keep-staging >/dev/null 2>&1; then
        expected=$'stop realtime\nstop media\nstart media\nstart realtime'
        actual="$(cat "${STATE}/operations.log")"
        if [[ "$actual" == "$expected" ]]; then pass 'MediaServer/RealtimeServer 顺序'; else fail "服务顺序: $actual"; fi
    else fail 'MediaServer 发布测试执行失败'; fi
    cleanup_fixture
}

test_dry_run_no_mutation() {
    new_fixture; write_manifest all
    before="$(sha256sum "${ROOT}/syncRTC-server/VarifyServer/config.json" | cut -d' ' -f1)"
    if run_release --service all --release-id "${RELEASE_ID}" --dry-run >/dev/null 2>&1; then
        after="$(sha256sum "${ROOT}/syncRTC-server/VarifyServer/config.json" | cut -d' ' -f1)"
        if [[ "$before" == "$after" && ! -e "${ROOT}/backups/${RELEASE_ID}" && ! -s "${STATE}/operations.log" ]]; then
            pass 'dry-run 无破坏动作且 VarifyServer 配置不变'
        else fail 'dry-run 产生了写入或改动 VarifyServer 配置'; fi
    else fail 'dry-run 执行失败'; fi
    cleanup_fixture
}

test_invalid_service
test_path_escape
test_hash_mismatch_before_stop
test_ldd_missing_before_stop
test_health_failure_rolls_back
test_media_realtime_order
test_dry_run_no_mutation

echo "测试结果: passed=${passed} failed=${failed}"
[[ "${failed}" -eq 0 ]]
