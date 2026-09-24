#!/usr/bin/env bash
set -Eeuo pipefail

readonly DEPLOY_ROOT=/opt/syncrtc/backend
readonly SERVICE_ROOT="${DEPLOY_ROOT}/syncRTC-server/AIServer"
readonly SERVICE_UNIT=syncrtc-ai.service
readonly PORT=50052
RELEASE_ID=""; PACKAGE=""; DRY_RUN=0
usage(){ echo "用法: deploy-syncrtc-ai --release-id ID --package FILE [--dry-run]" >&2; }
die(){ echo "AIServer 发布失败: $*" >&2; exit 65; }
while [[ $# -gt 0 ]]; do
  case "$1" in
    --release-id) RELEASE_ID="${2:?}"; shift 2;;
    --package) PACKAGE="${2:?}"; shift 2;;
    --dry-run) DRY_RUN=1; shift;;
    *) usage; exit 64;;
  esac
done
[[ "$RELEASE_ID" =~ ^[0-9]{8}-[0-9]{6}-[0-9a-f]{8}$ ]] || die "release-id 格式非法"
[[ -f "$PACKAGE" ]] || die "发布包不存在"
[[ "$(realpath -m "$PACKAGE")" == /var/lib/github-deploy/syncrtc-ai/incoming/* ]] || die "发布包路径不在受控暂存目录"
sha256sum --check --strict "${PACKAGE}.sha256" 2>/dev/null || die "发布包 SHA-256 校验失败"

tmp="${SERVICE_ROOT}.publisher-${RELEASE_ID}"
backup="${DEPLOY_ROOT}/backups/syncrtc-ai/${RELEASE_ID}"
cleanup(){ rm -rf -- "$tmp"; }
trap cleanup EXIT
install -d -m 0750 -- "$tmp" "$backup"
tar -xzf "$PACKAGE" -C "$tmp" || die "发布包解压失败"
[[ -d "$tmp/app" && -f "$tmp/requirements.txt" ]] || die "发布包内容不完整"
[[ -f "$SERVICE_ROOT/.env" ]] || die "线上 .env 不存在，拒绝覆盖"

systemctl is-active --quiet "$SERVICE_UNIT" || die "AIServer 发布前不是 active"
ss -lntp | grep -Eq ":${PORT}([^0-9]|$)" || die "AIServer 未监听 ${PORT}"
if [[ "$DRY_RUN" -eq 1 ]]; then
  echo "DRY-RUN 通过：包校验、目录、.env、服务状态和 ${PORT} 监听检查通过"
  exit 0
fi

cp -a -- "$SERVICE_ROOT/app" "$backup/"
cp -a -- "$SERVICE_ROOT/proto" "$backup/" 2>/dev/null || true
cp -a -- "$SERVICE_ROOT/requirements.txt" "$backup/"
rm -rf -- "$SERVICE_ROOT/app" "$SERVICE_ROOT/proto"
cp -a -- "$tmp/app" "$SERVICE_ROOT/app"
cp -a -- "$tmp/proto" "$SERVICE_ROOT/proto"
install -m 0644 -- "$tmp/requirements.txt" "$SERVICE_ROOT/requirements.txt"
chown -R syncrtc-ai:syncrtc-ai "$SERVICE_ROOT/app" "$SERVICE_ROOT/proto" "$SERVICE_ROOT/requirements.txt"
systemctl restart "$SERVICE_UNIT"
sleep 2
systemctl is-active --quiet "$SERVICE_UNIT" || die "AIServer 重启后未处于 active"
ss -lntp | grep -Eq ":${PORT}([^0-9]|$)" || die "AIServer 重启后未监听 ${PORT}"
echo "AIServer 发布成功: release=${RELEASE_ID} port=${PORT}"
