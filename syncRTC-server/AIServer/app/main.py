"""AIService 进程入口。

这里只负责启动顺序和进程生命周期，不编写普通问答、ASR、LLM 等业务逻辑。
RealtimeServer 是唯一调用方，后续通过 TCP gRPC 调用本服务，不使用本地套接字。
"""

from __future__ import annotations

import logging
import os
import signal
import grpc
from concurrent import futures
from dataclasses import dataclass
from typing import Any
from dotenv import load_dotenv
from app.application.chat_service import ChatService
from app.infrastructure.llm_client import LlmClient
from app.api.grpc_server import register_grpc_services


LOGGER = logging.getLogger("syncrtc.ai")
DEFAULT_GRPC_ENDPOINT = "0.0.0.0:50052"


@dataclass(frozen=True)
class AppSettings:
    """AIService 的进程级配置；真实密钥不从代码或仓库读取。"""

    grpc_endpoint: str
    grpc_max_workers: int
    job_timeout_seconds: int


def load_settings() -> AppSettings:
    """从环境变量读取启动配置，并在启动阶段尽早校验。"""

    # 50051 已由 VerifyServer 使用，AIService 使用独立的 TCP gRPC 端口。
    endpoint = os.getenv("AI_GRPC_ENDPOINT", DEFAULT_GRPC_ENDPOINT).strip()
    if not endpoint:
        raise ValueError("AI_GRPC_ENDPOINT 不能为空")
    if endpoint.startswith(("unix://", "unix-abstract:")):
        raise ValueError("AI_GRPC_ENDPOINT 必须是 TCP 地址，例如 0.0.0.0:50052")

    host, separator, port_text = endpoint.rpartition(":")
    if not separator or not host or not port_text.isdigit():
        raise ValueError("AI_GRPC_ENDPOINT 必须是 host:port 格式，例如 0.0.0.0:50052")
    if not 1 <= int(port_text) <= 65535:
        raise ValueError("AI_GRPC_ENDPOINT 的端口必须在 1 到 65535 之间")

    try:
        max_workers = int(os.getenv("AI_GRPC_MAX_WORKERS", "4"))
        job_timeout = int(os.getenv("AI_JOB_TIMEOUT_SECONDS", "900"))
    except ValueError as exc:
        raise ValueError("AI_GRPC_MAX_WORKERS 和 AI_JOB_TIMEOUT_SECONDS 必须是整数") from exc

    if max_workers <= 0 or job_timeout <= 0:
        raise ValueError("AI_GRPC_MAX_WORKERS 和 AI_JOB_TIMEOUT_SECONDS 必须大于 0")

    return AppSettings(
        grpc_endpoint=endpoint,
        grpc_max_workers=max_workers,
        job_timeout_seconds=job_timeout,
    )


def configure_logging() -> None:
    """初始化基础日志；禁止在日志中输出 API Key、完整音频和完整转写。"""

    logging.basicConfig(
        level=os.getenv("AI_LOG_LEVEL", "INFO").upper(),
        format="%(asctime)s %(levelname)s %(name)s %(message)s",
    )


def main() -> None:
    """AIService 进程入口。"""

    # 本地开发时加载 AIServer/.env；生产环境由部署系统直接注入环境变量。
    load_dotenv()
    configure_logging()
    settings = load_settings()
    LOGGER.info("AIService 启动，配置：%s", settings)

    llm_client = LlmClient()
    chat_service =ChatService(llm_client)

    # 创建grpc server，LLM的调用是同步阻塞的，创建一个线程池
    server = grpc.server(
        futures.ThreadPoolExecutor(max_workers=settings.grpc_max_workers)
    )

    # 注册grpc服务
    register_grpc_services(server, chat_service)

    bound_port = server.add_insecure_port(settings.grpc_endpoint)
    if bound_port == 0:
        raise RuntimeError(f"gRPC 端点绑定失败：{settings.grpc_endpoint}")

    def handle_signal(signum, _frame) -> None:
        LOGGER.info("收到信号 %s，准备优雅退出", signum)
        server.stop(grace=5)

    signal.signal(signal.SIGINT, handle_signal)
    signal.signal(signal.SIGTERM, handle_signal)

    server.start()
    LOGGER.info("gRPC 服务已启动，监听 %s", settings.grpc_endpoint)

    try:
        # 同步 gRPC Server 自己等待请求和终止信号。
        server.wait_for_termination()
    except KeyboardInterrupt:
        LOGGER.info("收到键盘中断，准备优雅退出")
    finally:
        server.stop(grace=5).wait()
        LOGGER.info("gRPC 服务已停止")


if __name__ == "__main__":
    main()
