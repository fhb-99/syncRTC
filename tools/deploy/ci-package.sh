#!/usr/bin/env bash
set -Eeuo pipefail

SERVICE="all"
COMMIT=""
RELEASE_ID=""
OUTPUT=""

usage() {
    echo "用法: ci-package.sh --service gate|realtime|media|all --commit <40位小写Git SHA> --release-id YYYYMMDD-HHMMSS-xxxxxxxx --output <目录>" >&2
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --service) SERVICE="${2:-}"; shift 2 ;;
        --commit) COMMIT="${2:-}"; shift 2 ;;
        --release-id) RELEASE_ID="${2:-}"; shift 2 ;;
        --output) OUTPUT="${2:-}"; shift 2 ;;
        *) usage; exit 64 ;;
    esac
done

case "${SERVICE}" in gate|realtime|media|all) ;; *) usage; exit 64 ;; esac
[[ "${COMMIT}" =~ ^[0-9a-f]{40}$ ]] || { echo "commit 格式非法" >&2; exit 64; }
[[ "${RELEASE_ID}" =~ ^[0-9]{8}-[0-9]{6}-[0-9a-f]{8}$ ]] || { echo "release-id 格式非法" >&2; exit 64; }
[[ -n "${OUTPUT}" ]] || { usage; exit 64; }

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
OUTPUT="$(realpath -m -- "${OUTPUT}")"

if [[ -e "${OUTPUT}" ]] && find "${OUTPUT}" -mindepth 1 -print -quit | grep -q .; then
    echo "输出目录必须不存在或为空: ${OUTPUT}" >&2
    exit 65
fi
install -d -m 0755 -- "${OUTPUT}"

TEMP_ROOT="$(mktemp -d)"
cleanup() {
    local resolved
    resolved="$(realpath -m -- "${TEMP_ROOT}")"
    case "${resolved}" in /tmp/*) rm -rf -- "${resolved}" ;; *) echo "拒绝清理异常临时目录: ${resolved}" >&2 ;; esac
}
trap cleanup EXIT

SOURCE_ROOT="${TEMP_ROOT}/source"
INFRA_ROOT="${TEMP_ROOT}/infra"
install -d -m 0755 -- "${SOURCE_ROOT}" "${INFRA_ROOT}"

declare -A DIRECTORY=(
    [gate]="GateServer"
    [realtime]="RealtimeServer"
    [media]="MediaServer"
)
declare -A BINARY=(
    [gate]="GateServer"
    [realtime]="RealtimeServer"
    [media]="MediaServer"
)
declare -A TESTS=(
    [gate]='^(mysql_mgr_registration_test|logic_system_token_test)$'
    [realtime]='^(session_test|logic_system_test|meeting_info_test)$'
    [media]='^media_room_test$'
)

if [[ "${SERVICE}" == "all" ]]; then
    SELECTED=(media realtime gate)
else
    SELECTED=("${SERVICE}")
fi

echo "[1/5] 生成提交 ${COMMIT} 的干净源码快照"
git -C "${PROJECT_ROOT}" archive "${COMMIT}" | tar -xf - -C "${SOURCE_ROOT}"
# 构建上下文不携带任何运行配置或密钥，测试配置只在临时目录中生成。
find "${SOURCE_ROOT}" -type f \( -name 'config.ini' -o -name '.env' -o -name '.env.*' -o -name 'config.json' \) -delete
install -D -m 0644 "${SCRIPT_DIR}/cmake/gRPCConfig.cmake" "${SOURCE_ROOT}/.publisher/cmake/gRPCConfig.cmake"

SUFFIX="${RELEASE_ID//-/}"
NETWORK="syncrtc-ci-${SUFFIX}"
MYSQL="syncrtc-ci-mysql-${SUFFIX}"
REDIS="syncrtc-ci-redis-${SUFFIX}"
INFRA_STARTED=0

stop_infra() {
    [[ "${INFRA_STARTED}" -eq 1 ]] || return 0
    docker rm -f "${MYSQL}" "${REDIS}" >/dev/null 2>&1 || true
    docker network rm "${NETWORK}" >/dev/null 2>&1 || true
    INFRA_STARTED=0
}
trap 'stop_infra; cleanup' EXIT

start_infra() {
    local secret schema
    secret="$(openssl rand -hex 32)"
    schema="${SOURCE_ROOT}/syncRTC-server/db/init/01-schema.sql"
    [[ -f "${schema}" ]] || { echo "数据库初始化脚本缺失" >&2; exit 65; }

    printf 'MYSQL_ROOT_PASSWORD=%s\nMYSQL_DATABASE=syncrtc\nMYSQL_USER=syncrtc\nMYSQL_PASSWORD=%s\n' \
        "${secret}" "${secret}" > "${INFRA_ROOT}/mysql.env"
    printf 'bind 0.0.0.0\nprotected-mode no\nport 6379\nrequirepass %s\n' \
        "${secret}" > "${INFRA_ROOT}/redis.conf"
    cat > "${INFRA_ROOT}/config.ini" <<EOF
[GateServer]
Port=8081
[VarifyServer]
Host=127.0.0.1
Port=50051
[RealtimeServer]
Host=127.0.0.1
Port=8090
[Redis]
Host=${REDIS}
Port=6379
Password=${secret}
[Mysql]
Host=${MYSQL}
Port=3306
User=syncrtc
Password=${secret}
Database=syncrtc
[MediaServer]
InternalSocketPath=/tmp/syncrtc-mediaserver.sock
EOF
    chmod 0600 "${INFRA_ROOT}"/*

    docker network create "${NETWORK}" >/dev/null
    INFRA_STARTED=1
    docker run -d --name "${MYSQL}" --network "${NETWORK}" \
        --env-file "${INFRA_ROOT}/mysql.env" \
        -v "${schema}:/docker-entrypoint-initdb.d/01-schema.sql:ro" \
        mysql:8.4 >/dev/null
    docker run -d --name "${REDIS}" --network "${NETWORK}" \
        -v "${INFRA_ROOT}/redis.conf:/usr/local/etc/redis/redis.conf:ro" \
        redis:7-alpine redis-server /usr/local/etc/redis/redis.conf >/dev/null

    local ready=0
    for _ in $(seq 1 90); do
        if docker exec "${MYSQL}" mysqladmin ping --host=127.0.0.1 --user=syncrtc \
            --password="${secret}" --silent >/dev/null 2>&1; then
            ready=1
            break
        fi
        sleep 2
    done
    [[ "${ready}" -eq 1 ]] || { echo "临时 MySQL 未在 180 秒内就绪" >&2; docker logs "${MYSQL}"; exit 1; }

    # docker run 返回时 Redis 进程可能仍在初始化，限时重试避免启动竞态。
    ready=0
    for _ in $(seq 1 30); do
        if docker exec "${REDIS}" redis-cli -a "${secret}" ping 2>/dev/null | grep -Fxq PONG; then
            ready=1
            break
        fi
        sleep 1
    done
    [[ "${ready}" -eq 1 ]] || {
        echo "临时 Redis 未在 30 秒内完成认证就绪检查" >&2
        docker logs "${REDIS}"
        exit 1
    }
}

build_service() {
    local name="$1" tag container_id config_target
    tag="syncrtc-ci:${RELEASE_ID}-${name}"
    echo "[2/5] 构建 ${name}"
    docker build --progress plain --platform linux/amd64 --target build \
        --tag "${tag}" --build-arg "SERVICE=${name}" \
        --file "${SCRIPT_DIR}/Dockerfile.build" "${SOURCE_ROOT}"

    echo "[3/5] 测试 ${name}"
    if [[ "${name}" == "gate" || "${name}" == "realtime" ]]; then
        if [[ "${INFRA_STARTED}" -eq 0 ]]; then start_infra; fi
        config_target="/opt/syncrtc/backend/syncRTC-server/${DIRECTORY[${name}]}/bin/conf/config.ini"
        docker run --rm --network "${NETWORK}" \
            -v "${INFRA_ROOT}/config.ini:${config_target}:ro" \
            "${tag}" ctest --test-dir "/build/${name}" --output-on-failure \
            --timeout 90 --tests-regex "${TESTS[${name}]}"
    else
        docker run --rm "${tag}" ctest --test-dir "/build/${name}" --output-on-failure \
            --timeout 90 --tests-regex "${TESTS[${name}]}"
    fi

    install -d -m 0755 -- "${OUTPUT}/artifacts/${name}"
    container_id="$(docker create "${tag}")"
    docker cp "${container_id}:/out/${BINARY[${name}]}" "${OUTPUT}/artifacts/${name}/"
    docker rm -f "${container_id}" >/dev/null
    file "${OUTPUT}/artifacts/${name}/${BINARY[${name}]}" | grep -Eq 'ELF 64-bit.*x86-64'
}

for name in "${SELECTED[@]}"; do
    build_service "${name}"
done
stop_infra

echo "[4/5] 生成严格白名单发布包"
cat > "${OUTPUT}/metadata.json" <<EOF
{
  "release_id": "${RELEASE_ID}",
  "service": "${SERVICE}",
  "commit": "${COMMIT}",
  "generated_at": "$(date -u '+%Y-%m-%dT%H:%M:%SZ')",
  "tests": "passed",
  "acceptance_boundary": "process-port-uds-basic-endpoint-only; not end-to-end media"
}
EOF
install -m 0644 "${SCRIPT_DIR}/remote-release.sh" "${OUTPUT}/remote-release.sh"

files=(metadata.json remote-release.sh)
for name in "${SELECTED[@]}"; do
    files+=("artifacts/${name}/${BINARY[${name}]}")
done
IFS=$'\n' read -r -d '' -a sorted_files < <(printf '%s\n' "${files[@]}" | sort && printf '\0')
(cd "${OUTPUT}" && sha256sum "${sorted_files[@]}") > "${OUTPUT}/manifest.sha256"

mapfile -t actual_files < <(find "${OUTPUT}" -type f -printf '%P\n' | sort)
expected_files=("${sorted_files[@]}" manifest.sha256)
IFS=$'\n' read -r -d '' -a expected_files < <(printf '%s\n' "${expected_files[@]}" | sort && printf '\0')
[[ "${actual_files[*]}" == "${expected_files[*]}" ]] || { echo "发布包包含白名单之外的文件" >&2; exit 65; }
(cd "${OUTPUT}" && sha256sum --check --strict manifest.sha256)

echo "[5/5] 构建、测试和打包完成: service=${SERVICE} release=${RELEASE_ID}"
