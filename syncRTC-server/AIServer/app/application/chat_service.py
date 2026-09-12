"""会议内普通 AI 问答业务编排。"""

from __future__ import annotations

import logging
from dataclasses import dataclass
from typing import Protocol, Sequence


logger = logging.getLogger(__name__)


class ChatClient(Protocol):
    """ChatService 依赖的最小 LLM 客户端契约。"""

    def chat(self, messages: Sequence[dict[str, str]]) -> str:
        """根据消息列表返回模型答案。"""


@dataclass(frozen=True)
class ChatResult:
    """一次问答的稳定业务结果。"""

    success: bool
    answer: str = ""
    error_code: str = ""
    error_message: str = ""


class ChatService:
    """处理会议助手的普通文本问答，不读取实时音频。"""

    # 限制输入大小，避免单个请求占用过多 Token 和费用。
    MAX_QUESTION_LENGTH = 2_000
    SYSTEM_PROMPT = (
        "你是会议系统中的 AI 助手。请使用清晰、准确的中文回答问题。"
        "如果问题中的信息不足，请明确说明你不知道，不要编造事实。"
    )

    def __init__(self, llm_client: ChatClient) -> None:
        """注入实现了 ``chat(messages)`` 的 LLM 客户端。"""

        self._llm_client = llm_client

    def ask(self, question: str | None) -> ChatResult:
        """校验问题、调用 LLM，并始终返回 ChatResult。"""

        if question is None or not question.strip():
            return ChatResult(
                success=False,
                error_code="EMPTY_QUESTION",
                error_message="问题不能为空。",
            )

        clean_question = question.strip()
        if len(clean_question) > self.MAX_QUESTION_LENGTH:
            return ChatResult(
                success=False,
                error_code="QUESTION_TOO_LONG",
                error_message=(
                    f"问题最多允许 {self.MAX_QUESTION_LENGTH} 个字符，"
                    f"当前为 {len(clean_question)} 个字符。"
                ),
            )

        messages = [
            {"role": "system", "content": self.SYSTEM_PROMPT},
            {"role": "user", "content": clean_question},
        ]

        try:
            answer = self._llm_client.chat(messages)
        except TimeoutError:
            # LlmTimeoutError 继承 TimeoutError，因此可以统一映射。
            logger.warning("LLM request timed out")
            return ChatResult(
                success=False,
                error_code="LLM_TIMEOUT",
                error_message="AI 服务响应超时，请稍后再试。",
            )
        except Exception:
            # 详细堆栈只写服务端日志，客户端得到稳定的错误信息。
            logger.exception("LLM request failed")
            return ChatResult(
                success=False,
                error_code="LLM_REQUEST_FAILED",
                error_message="AI 服务暂时不可用，请稍后再试。",
            )

        if not answer or not answer.strip():
            return ChatResult(
                success=False,
                error_code="EMPTY_LLM_ANSWER",
                error_message="AI 服务没有返回有效答案，请稍后重试。",
            )

        return ChatResult(success=True, answer=answer.strip())
