#!/usr/bin/env bash
set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVICE_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
OUTPUT_DIR="${1:-${SERVICE_ROOT}/dist}"
PYTHON_BIN="${PYTHON_BIN:-python3}"

cd "${SERVICE_ROOT}"
install -d -m 0755 -- "${OUTPUT_DIR}"

echo "[1/4] 检查 Python 语法"
PYTHONDONTWRITEBYTECODE=1 "${PYTHON_BIN}" - <<'PY'
import ast
from pathlib import Path

for path in Path("app").rglob("*.py"):
    ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
PY

echo "[2/4] 检查模块导入和最小 gRPC 适配链路"
PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=. "${PYTHON_BIN}" - <<'PY'
from app.api.grpc_server import MeetingAiGrpcService
from app.application.chat_service import ChatService
from app.generated import meeting_ai_pb2


class FakeLlmClient:
    def chat(self, messages):
        assert messages[-1]["role"] == "user"
        return "测试答案"


chat_service = ChatService(FakeLlmClient())
result = chat_service.ask("测试问题")
assert result.success is True
assert result.answer == "测试答案"

response = MeetingAiGrpcService(chat_service).AskAssistant(
    meeting_ai_pb2.AskAssistantRequest(question="测试问题"),
    None,
)
assert response.error == 0
assert response.answer == "测试答案"
PY

echo "[3/4] 生成 AIService 发布包"
PACKAGE="${OUTPUT_DIR}/syncrtc-ai-service.tar.gz"
rm -f -- "${PACKAGE}" "${PACKAGE}.sha256"
tar \
    --exclude='app/**/__pycache__' \
    --exclude='*.pyc' \
    --exclude='.env' \
    --exclude='.venv' \
    -czf "${PACKAGE}" \
    -C "${SERVICE_ROOT}" \
    app proto requirements.txt .env.example README.md
sha256sum "${PACKAGE}" > "${PACKAGE}.sha256"

echo "[4/4] AIService CI 检查完成"
