#!/usr/bin/env bash
set -Eeuo pipefail

readonly DEFAULT_DEPLOY_ROOT="/opt/syncrtc/backend"
readonly TEST_MODE="${SYNCRTC_TEST_MODE:-0}"

if [[ "${TEST_MODE}" == "1" ]]; then
    DEPLOY_ROOT="${SYNCRTC_TEST_ROOT:?测试模式必须设置 SYNCRTC_TEST_ROOT}"
    PROC_ROOT="${SYNCRTC_TEST_PROC_ROOT:-/proc}"
else
    # 正式发布时禁止通过环境变量改写部署根目录，避免 sudo 环境或误操作把
    # 文件写到未审核的位置。测试注入只在显式测试模式下开放。
    if [[ -n "${SYNCRTC_TEST_ROOT:-}" || -n "${SYNCRTC_TEST_PROC_ROOT:-}" ]]; then
        echo "正式模式不允许覆盖部署路径" >&2
        exit 64
    fi
    DEPLOY_ROOT="${DEFAULT_DEPLOY_ROOT}"
    PROC_ROOT="/proc"
    if [[ "$(id -u)" -ne 0 ]]; then
        echo "远端发布脚本必须由 root 执行" >&2
        exit 77
    fi
fi

readonly DEPLOY_ROOT PROC_ROOT
readonly UPDATES_ROOT="${DEPLOY_ROOT}/updates"
readonly BACKUPS_ROOT="${DEPLOY_ROOT}/backups"
readonly SERVER_ROOT="${DEPLOY_ROOT}/syncRTC-server"
readonly RUNTIME_LIB="${DEPLOY_ROOT}/runtime/lib"

SERVICE=""
RELEASE_ID=""
DRY_RUN=0
KEEP_STAGING=0

usage() {
    echo "用法: remote-release.sh --service gate|realtime|media|all --release-id YYYYMMDD-HHMMSS-xxxxxxxx [--dry-run] [--keep-staging]" >&2
}

die() {
    echo "发布失败: $*" >&2
    # 返回非零，让 set -E 和 ERR trap 在事务开始后统一执行回滚；直接 exit
    # 会绕过 ERR trap，导致“检查失败但未恢复旧版本”。
    return 65
}

redact() {
    # 服务当前可能把配置写入日志。所有可能展示给发布终端的文本都先按键名
    # 脱敏；脚本从不主动读取或打印 .env、config.json 的内容。
    sed -E \
        -e 's/((Password|password|passwd|token|secret|authorization)[[:space:]]*[=:][[:space:]]*)[^[:space:],;]+/\1[REDACTED]/Ig' \
        -e 's/((smtp|turn)[_-]?(password|passwd|secret|token)[[:space:]]*[=:][[:space:]]*)[^[:space:],;]+/\1[REDACTED]/Ig'
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --service)
            [[ $# -ge 2 ]] || { usage; exit 64; }
            SERVICE="$2"
            shift 2
            ;;
        --release-id)
            [[ $# -ge 2 ]] || { usage; exit 64; }
            RELEASE_ID="$2"
            shift 2
            ;;
        --dry-run)
            DRY_RUN=1
            shift
            ;;
        --keep-staging)
            KEEP_STAGING=1
            shift
            ;;
        *)
            usage
            exit 64
            ;;
    esac
done

case "${SERVICE}" in
    gate|realtime|media|all) ;;
    *) usage; exit 64 ;;
esac
[[ "${RELEASE_ID}" =~ ^[0-9]{8}-[0-9]{6}-[0-9a-f]{8}$ ]] || die "release-id 格式非法"

readonly STAGE_DIR="${UPDATES_ROOT}/${RELEASE_ID}"
readonly MANIFEST="${STAGE_DIR}/manifest.sha256"
readonly BACKUP_DIR="${BACKUPS_ROOT}/${RELEASE_ID}"

declare -a SELECTED=()
case "${SERVICE}" in
    gate) SELECTED=(gate) ;;
    realtime) SELECTED=(realtime) ;;
    media) SELECTED=(media) ;;
    all) SELECTED=(media realtime gate) ;;
esac

declare -A UNIT=(
    [gate]="syncrtc-gate.service"
    [realtime]="syncrtc-realtime.service"
    [media]="syncrtc-media.service"
)
declare -A BINARY=(
    [gate]="GateServer"
    [realtime]="RealtimeServer"
    [media]="MediaServer"
)
declare -A TARGET=(
    [gate]="${SERVER_ROOT}/GateServer/bin/GateServer"
    [realtime]="${SERVER_ROOT}/RealtimeServer/bin/RealtimeServer"
    [media]="${SERVER_ROOT}/MediaServer/bin/MediaServer"
)
declare -A CONFIG=(
    [gate]="${SERVER_ROOT}/GateServer/bin/conf/config.ini"
    [realtime]="${SERVER_ROOT}/RealtimeServer/bin/conf/config.ini"
    [media]="${SERVER_ROOT}/MediaServer/bin/conf/config.ini"
)
declare -A ORIGINAL_STATE=()

assert_under() {
    local path="$1"
    local parent="$2"
    local resolved parent_resolved
    resolved="$(realpath -m -- "${path}")"
    parent_resolved="$(realpath -m -- "${parent}")"
    case "${resolved}" in
        "${parent_resolved}"/*) ;;
        *) die "路径越界: ${path}" ;;
    esac
}

artifact_path() {
    local name="$1"
    printf '%s/artifacts/%s/%s\n' "${STAGE_DIR}" "${name}" "${BINARY[${name}]}"
}

validate_paths() {
    [[ -d "${DEPLOY_ROOT}" ]] || die "部署根目录不存在"
    assert_under "${STAGE_DIR}" "${UPDATES_ROOT}"
    [[ "$(basename -- "${STAGE_DIR}")" == "${RELEASE_ID}" ]] || die "暂存目录名称不匹配"
    [[ -d "${STAGE_DIR}" && -f "${MANIFEST}" ]] || die "发布暂存包不完整"

    local name target
    for name in "${SELECTED[@]}"; do
        target="${TARGET[${name}]}"
        assert_under "${target}" "${SERVER_ROOT}"
        [[ "$(realpath -m -- "${target}")" == "${target}" ]] || die "目标路径解析结果异常: ${name}"
        [[ -f "${target}" ]] || die "线上目标文件不存在: ${name}"
    done
}

validate_package() {
    local -a expected_files=(metadata.json remote-release.sh)
    local name
    for name in "${SELECTED[@]}"; do
        expected_files+=("artifacts/${name}/${BINARY[${name}]}")
    done

    mapfile -t expected_files < <(printf '%s\n' "${expected_files[@]}" | sort)
    mapfile -t actual_files < <(find "${STAGE_DIR}" -type f ! -name manifest.sha256 -printf '%P\n' | sort)
    [[ "${actual_files[*]}" == "${expected_files[*]}" ]] || die "发布包包含非白名单文件或缺少文件"

    local expected_line_count="${#expected_files[@]}"
    local actual_line_count
    actual_line_count="$(grep -Ec '^[0-9a-f]{64}  (metadata.json|remote-release.sh|artifacts/(gate/GateServer|realtime/RealtimeServer|media/MediaServer))$' "${MANIFEST}" || true)"
    [[ "${actual_line_count}" -eq "${expected_line_count}" ]] || die "哈希清单内容不符合白名单"
    [[ "$(wc -l < "${MANIFEST}")" -eq "${expected_line_count}" ]] || die "哈希清单存在额外条目"
    (cd "${STAGE_DIR}" && sha256sum --check --strict manifest.sha256) | redact
}

validate_elf() {
    local name="$1"
    local artifact output
    artifact="$(artifact_path "${name}")"
    chmod 0755 -- "${artifact}"

    output="$(file -- "${artifact}")"
    [[ "${output}" == *"ELF 64-bit"* && "${output}" == *"x86-64"* ]] || die "${name} 不是 Linux x86-64 ELF"
    readelf -h -- "${artifact}" | grep -Eq 'Machine:[[:space:]]+Advanced Micro Devices X86-64' || die "${name} ELF 架构检查失败"

    if ! output="$(LD_LIBRARY_PATH="${RUNTIME_LIB}" ldd -- "${artifact}" 2>&1)"; then
        printf '%s\n' "${output}" | redact >&2
        die "${name} ldd 执行失败"
    fi
    if grep -Eqi 'not found|GLIBC_[0-9.]+.*not found|not a dynamic executable' <<<"${output}"; then
        printf '%s\n' "${output}" | redact >&2
        die "${name} 存在缺失或不兼容的运行库"
    fi
}

container_state() {
    docker inspect --format '{{if .State.Health}}{{.State.Health.Status}}{{else}}{{.State.Status}}{{end}}' "$1" 2>/dev/null
}

check_dependencies() {
    [[ "$(container_state syncrtc-mysql)" == "healthy" ]] || die "syncrtc-mysql 不是 healthy"
    [[ "$(container_state syncrtc-redis)" == "healthy" ]] || die "syncrtc-redis 不是 healthy"
    # verify-config.py 是只读检查；日常代码发布绝不调用 sync-config.py sync。
    python3 "${DEPLOY_ROOT}/deploy/verify-config.py" 2>&1 | redact
}

check_varify() {
    [[ "$(docker inspect --format '{{.State.Running}}' syncrtc-varify 2>/dev/null)" == "true" ]] || die "syncrtc-varify 未运行"
    ss -lntp | grep -Eq ':50051([^0-9]|$)' || die "VarifyServer 未监听 50051"
}

service_pid() {
    systemctl show -p MainPID --value "${UNIT[$1]}"
}

check_process_binary() {
    local name="$1"
    local pid
    pid="$(service_pid "${name}")"
    [[ "${pid}" =~ ^[1-9][0-9]*$ ]] || die "${name} 没有有效 PID"
    [[ "$(readlink -f -- "${PROC_ROOT}/${pid}/exe")" == "${TARGET[${name}]}" ]] || die "${name} 运行的不是预期二进制"
    printf '%s\n' "${pid}"
}

health_gate() {
    systemctl is-active --quiet "${UNIT[gate]}" || die "GateServer 未处于 active"
    local pid body
    pid="$(check_process_binary gate)"
    ss -lntp | grep -E ':8081([^0-9]|$)' | grep -Fq "pid=${pid}," || die "8081 不是由当前 GateServer 监听"
    body="$(curl --noproxy '*' --max-time 10 -fsS 'http://127.0.0.1:8081/get_test?source=publisher')"
    [[ "${body}" == *"receive get_test request"* && "${body}" == *"publisher"* ]] || die "GateServer /get_test 正文不符合预期"
}

health_realtime() {
    local since="${1:-}"
    systemctl is-active --quiet "${UNIT[realtime]}" || die "RealtimeServer 未处于 active"
    local pid logs
    pid="$(check_process_binary realtime)"
    ss -lntp | grep -E ':8090([^0-9]|$)' | grep -Fq "pid=${pid}," || die "8090 不是由当前 RealtimeServer 监听"
    if [[ -n "${since}" ]]; then
        logs="$(journalctl -u "${UNIT[realtime]}" --since "${since}" --no-pager 2>&1 | redact)"
        if grep -Eqi '(media.*(socket|uds).*(fail|error|refused|not found)|connect.*media.*(fail|error|refused))' <<<"${logs}"; then
            printf '%s\n' "${logs}" >&2
            die "RealtimeServer 日志出现 MediaServer UDS 连接失败"
        fi
    fi
}

health_media() {
    systemctl is-active --quiet "${UNIT[media]}" || die "MediaServer 未处于 active"
    local pid
    pid="$(check_process_binary media)"
    [[ -S "/tmp/syncrtc-mediaserver.sock" || "${TEST_MODE}" == "1" ]] || die "MediaServer UDS 不存在"
    ss -xlpn | grep -F '/tmp/syncrtc-mediaserver.sock' | grep -Fq "pid=${pid}," || die "MediaServer UDS 不是由当前新进程持有"
}

record_states() {
    local name
    for name in gate realtime media; do
        if systemctl is-active --quiet "${UNIT[${name}]}"; then
            ORIGINAL_STATE[${name}]="active"
        else
            ORIGINAL_STATE[${name}]="inactive"
        fi
    done
}

preflight() {
    validate_paths
    validate_package
    check_dependencies

    local name
    for name in "${SELECTED[@]}"; do
        [[ -f "${CONFIG[${name}]}" ]] || die "${name} 线上运行配置不存在"
        if grep -q 'replace-before-deploy' "${CONFIG[${name}]}"; then
            die "${name} 线上配置仍有占位值"
        fi
        systemctl is-active --quiet "${UNIT[${name}]}" || die "${name} 发布前不是 active，拒绝无健康基线发布"
        validate_elf "${name}"
    done

    case "${SERVICE}" in
        gate) check_varify; health_gate ;;
        realtime) health_media; health_realtime ;;
        media)
            health_media
            if [[ "${ORIGINAL_STATE[realtime]}" == "active" ]]; then health_realtime; fi
            ;;
        all) check_varify; health_media; health_realtime; health_gate ;;
    esac
}

backup_targets() {
    assert_under "${BACKUP_DIR}" "${BACKUPS_ROOT}"
    install -d -m 0750 -- "${BACKUP_DIR}"
    local name
    for name in "${SELECTED[@]}"; do
        install -m 0755 -- "${TARGET[${name}]}" "${BACKUP_DIR}/${BINARY[${name}]}"
    done
}

replace_binary() {
    local name="$1"
    local target temp
    target="${TARGET[${name}]}"
    temp="${target}.publisher-${RELEASE_ID}"
    [[ "$(dirname -- "${temp}")" == "$(dirname -- "${target}")" ]] || die "临时替换路径异常"
    install -m 0755 -- "$(artifact_path "${name}")" "${temp}"
    mv -f -- "${temp}" "${target}"
}

restore_original_states() {
    local failed=0
    # 回滚只触碰本次发布涉及的服务。MediaServer 发布期间需要暂停依赖它的
    # RealtimeServer；单独发布 GateServer/RealtimeServer 时不得干扰其他服务。
    case "${SERVICE}" in
        gate) systemctl stop "${UNIT[gate]}" >/dev/null 2>&1 || true ;;
        realtime) systemctl stop "${UNIT[realtime]}" >/dev/null 2>&1 || true ;;
        media) systemctl stop "${UNIT[realtime]}" "${UNIT[media]}" >/dev/null 2>&1 || true ;;
        all) systemctl stop "${UNIT[gate]}" "${UNIT[realtime]}" "${UNIT[media]}" >/dev/null 2>&1 || true ;;
    esac

    local name target temp
    for name in "${SELECTED[@]}"; do
        target="${TARGET[${name}]}"
        temp="${target}.rollback-${RELEASE_ID}"
        if [[ -f "${BACKUP_DIR}/${BINARY[${name}]}" ]]; then
            install -m 0755 -- "${BACKUP_DIR}/${BINARY[${name}]}" "${temp}" && mv -f -- "${temp}" "${target}" || failed=1
        else
            failed=1
        fi
    done

    case "${SERVICE}" in
        gate)
            if [[ "${ORIGINAL_STATE[gate]}" == "active" ]]; then systemctl start "${UNIT[gate]}" || failed=1; health_gate || failed=1; fi
            ;;
        realtime)
            if [[ "${ORIGINAL_STATE[realtime]}" == "active" ]]; then systemctl start "${UNIT[realtime]}" || failed=1; health_realtime || failed=1; fi
            ;;
        media)
            if [[ "${ORIGINAL_STATE[media]}" == "active" ]]; then systemctl start "${UNIT[media]}" || failed=1; health_media || failed=1; fi
            if [[ "${ORIGINAL_STATE[realtime]}" == "active" ]]; then systemctl start "${UNIT[realtime]}" || failed=1; health_realtime || failed=1; fi
            ;;
        all)
            if [[ "${ORIGINAL_STATE[media]}" == "active" ]]; then systemctl start "${UNIT[media]}" || failed=1; health_media || failed=1; fi
            if [[ "${ORIGINAL_STATE[realtime]}" == "active" ]]; then systemctl start "${UNIT[realtime]}" || failed=1; health_realtime || failed=1; fi
            if [[ "${ORIGINAL_STATE[gate]}" == "active" ]]; then systemctl start "${UNIT[gate]}" || failed=1; health_gate || failed=1; fi
            ;;
    esac
    return "${failed}"
}

handle_failure() {
    local original_code="$1"
    local line="$2"
    trap - ERR INT TERM
    set +e
    echo "新版本发布在第 ${line} 行失败，开始自动回滚" >&2
    if restore_original_states; then
        echo "旧版本及发布前运行状态已恢复" >&2
        exit "${original_code}"
    fi
    echo "严重错误：新版本失败且自动回滚未完全成功，请立即人工检查本次涉及的业务服务" >&2
    exit 70
}

deploy_selected() {
    local started_at
    case "${SERVICE}" in
        gate)
            systemctl stop "${UNIT[gate]}"
            replace_binary gate
            systemctl start "${UNIT[gate]}"
            sleep 2
            health_gate
            check_varify
            ;;
        realtime)
            systemctl stop "${UNIT[realtime]}"
            replace_binary realtime
            # journalctl --since 不接受带时区偏移的 ISO 8601，使用本地时间格式。
            started_at="$(date '+%Y-%m-%d %H:%M:%S')"
            systemctl start "${UNIT[realtime]}"
            sleep 2
            health_realtime "${started_at}"
            ;;
        media)
            if [[ "${ORIGINAL_STATE[realtime]}" == "active" ]]; then systemctl stop "${UNIT[realtime]}"; fi
            systemctl stop "${UNIT[media]}"
            replace_binary media
            systemctl start "${UNIT[media]}"
            sleep 2
            health_media
            if [[ "${ORIGINAL_STATE[realtime]}" == "active" ]]; then
                started_at="$(date '+%Y-%m-%d %H:%M:%S')"
                systemctl start "${UNIT[realtime]}"
                sleep 2
                health_realtime "${started_at}"
            fi
            ;;
        all)
            # 停止顺序与依赖相反，启动顺序按 MediaServer -> RealtimeServer。
            systemctl stop "${UNIT[gate]}"
            systemctl stop "${UNIT[realtime]}"
            systemctl stop "${UNIT[media]}"
            replace_binary media
            replace_binary realtime
            replace_binary gate
            systemctl start "${UNIT[media]}"
            sleep 2
            health_media
            started_at="$(date '+%Y-%m-%d %H:%M:%S')"
            systemctl start "${UNIT[realtime]}"
            sleep 2
            health_realtime "${started_at}"
            systemctl start "${UNIT[gate]}"
            sleep 2
            health_gate
            check_varify
            ;;
    esac
}

cleanup_stage() {
    [[ "${KEEP_STAGING}" -eq 0 ]] || return 0
    assert_under "${STAGE_DIR}" "${UPDATES_ROOT}"
    # 已校验目录必须精确以 release-id 命名后，才逐项删除并移除空目录；不使用
    # 未展开变量、通配符或指向部署根目录的递归删除。
    find "${STAGE_DIR}" -depth -mindepth 1 -delete
    rmdir -- "${STAGE_DIR}"
}

record_states
preflight

if [[ "${DRY_RUN}" -eq 1 ]]; then
    echo "DRY-RUN 通过：构建包、路径、哈希、ELF、ldd、配置、依赖和当前服务健康检查均通过"
    echo "DRY-RUN 未停止服务、未替换文件、未修改配置/数据库/防火墙"
    cleanup_stage
    exit 0
fi

backup_targets
trap 'handle_failure "$?" "$LINENO"' ERR
trap 'handle_failure 130 "$LINENO"' INT TERM
deploy_selected
trap - ERR INT TERM

cleanup_stage
echo "发布成功: service=${SERVICE} release=${RELEASE_ID}"
echo "验收边界：仅证明文件、进程、端口/UDS、基础接口、配置与依赖健康；不代表端到端音视频已通过"
