"""MeetingAiService 的 gRPC 协议适配层。

职责只有两个：

1. 把 protobuf 请求转换成 ChatService 能理解的参数。
2. 把 ChatResult 转换成 protobuf 响应。

这里不直接访问千问，也不编写会议问答的业务规则。
"""

from __future__ import annotations

import logging
from enum import IntEnum

from app.generated import meeting_ai_pb2
from app.generated import meeting_ai_pb2_grpc

from app.application.chat_service import ChatService


logger = logging.getLogger(__name__)


class GrpcErrorCode(IntEnum):
    """返回给 RealtimeServer 的数字错误码。"""

    SUCCESS = 0

    # 请求参数错误
    EMPTY_QUESTION = 1001
    QUESTION_TOO_LONG = 1002

    # 大模型服务错误
    LLM_TIMEOUT = 2001
    LLM_REQUEST_FAILED = 2002
    EMPTY_LLM_ANSWER = 2003

    # 未预料到的服务端错误
    INTERNAL_ERROR = 9000


# ChatService 使用可读的字符串错误码；proto 使用 int32。
# 适配层在这里完成二者之间的转换。
ERROR_CODE_MAP = {
    "EMPTY_QUESTION": GrpcErrorCode.EMPTY_QUESTION,
    "QUESTION_TOO_LONG": GrpcErrorCode.QUESTION_TOO_LONG,
    "LLM_TIMEOUT": GrpcErrorCode.LLM_TIMEOUT,
    "LLM_REQUEST_FAILED": GrpcErrorCode.LLM_REQUEST_FAILED,
    "EMPTY_LLM_ANSWER": GrpcErrorCode.EMPTY_LLM_ANSWER,
}


class MeetingAiGrpcService(meeting_ai_pb2_grpc.MeetingAiServiceServicer):
    """由生成的 gRPC 基类派生出的服务实现。"""

    def __init__(self, chat_service: ChatService) -> None:
        # ChatService 由 main.py 创建并传进来，这就是依赖注入。
        self._chat_service = chat_service

    def AskAssistant(self, request, context):
        """处理 proto 中定义的 AskAssistant RPC。"""

        try:
            # request.question 来自 AskAssistantRequest 的 question 字段。
            result = self._chat_service.ask(request.question)
        except Exception:
            # 正常业务异常应由 ChatService 转换成 ChatResult。
            # 这里兜底捕获意外错误，避免异常直接扩散到调用方。
            logger.exception("Unexpected error while handling AskAssistant")
            return meeting_ai_pb2.AskAssistantResponse(
                error=int(GrpcErrorCode.INTERNAL_ERROR),
                answer="",
                error_message="AI 服务内部错误，请稍后再试。",
            )

        if result.success:
            return meeting_ai_pb2.AskAssistantResponse(
                error=int(GrpcErrorCode.SUCCESS),
                answer=result.answer,
                error_message="",
            )

        # 如果以后 ChatService 新增错误码、但这里忘记添加映射，
        # 就统一按 INTERNAL_ERROR 返回，避免给客户端发送未知的 0。
        grpc_error = ERROR_CODE_MAP.get(
            result.error_code,
            GrpcErrorCode.INTERNAL_ERROR,
        )

        return meeting_ai_pb2.AskAssistantResponse(
            error=int(grpc_error),
            answer="",
            error_message=result.error_message,
        )


def register_grpc_services(
    server,
    chat_service: ChatService,
) -> None:
    """把 MeetingAiGrpcService 注册到一个 gRPC Server 中。

    server 的创建、端口绑定和启动仍由 main.py 负责。
    """

    service = MeetingAiGrpcService(chat_service)
    meeting_ai_pb2_grpc.add_MeetingAiServiceServicer_to_server(service, server)
