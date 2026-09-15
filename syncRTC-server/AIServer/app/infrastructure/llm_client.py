"""千问 LLM 客户端。

该模块只负责供应商 API 通信和响应转换，不承载会议业务规则。
"""

from __future__ import annotations

import os
from typing import Mapping, Sequence

import requests


class LlmError(RuntimeError):
    """LLM 请求或响应处理失败。"""


class LlmTimeoutError(TimeoutError):
    """LLM 连接或读取响应超时。"""


class LlmResponseError(LlmError):
    """LLM 返回的 JSON 不符合预期。"""


def get_env(name: str) -> str:
    """读取非空环境变量；真实 API Key 不从代码或仓库读取。"""

    value = os.getenv(name, "").strip()
    if not value:
        raise ValueError(f"缺少环境变量：{name}")
    return value


class LlmClient:
    """面向业务层的 LLM 客户端，正式调用接口为 ``chat(messages)``。"""

    def __init__(
        self,
        api_key: str | None = None,
        base_url: str | None = None,
        model: str | None = None,
        timeout: tuple[float, float] = (5.0, 60.0),
    ) -> None:
        self._api_key = api_key or get_env("DASHSCOPE_API_KEY")
        self._base_url = (base_url or get_env("AI_LLM_BASE_URL")).rstrip("/")
        self._model = model or get_env("AI_LLM_MODEL")
        self._timeout = timeout

    def chat(self, messages: Sequence[Mapping[str, str]]) -> str:
        """发送 OpenAI/Qwen 格式的消息并返回答案文本。"""

        if not messages:
            raise ValueError("messages 不能为空")

        payload = {
            "model": self._model,
            "messages": [dict(message) for message in messages],
            "temperature": 0.7,
            "max_tokens": 512,
        }
        headers = {
            "Authorization": f"Bearer {self._api_key}",
            "Content-Type": "application/json",
        }

        try:
            response = requests.post(
                f"{self._base_url}/chat/completions",
                headers=headers,
                json=payload,
                timeout=self._timeout,
            )
            response.raise_for_status()
        except requests.exceptions.Timeout as exc:
            raise LlmTimeoutError("请求超时，请稍后重试。") from exc
        except requests.exceptions.HTTPError as exc:
            # 不把供应商原始响应全文返回给上层，避免把内部细节带到客户端。
            raise LlmError(f"接口调用失败，状态码：{response.status_code}") from exc
        except requests.exceptions.RequestException as exc:
            raise LlmError("请求异常，请稍后重试。") from exc

        try:
            data = response.json()
            answer = data["choices"][0]["message"]["content"]
        except (ValueError, KeyError, IndexError, TypeError) as exc:
            raise LlmResponseError("LLM 返回格式不符合预期。") from exc

        if not isinstance(answer, str) or not answer.strip():
            raise LlmResponseError("LLM 没有返回有效答案。")
        return answer.strip()


def ask_llm(question: str) -> str:
    """命令行兼容入口；业务层应注入并调用 ``LlmClient.chat``。"""

    messages = [{"role": "user", "content": question}]
    return LlmClient().chat(messages)


if __name__ == "__main__":
    question = input("请输入你的问题：")
    answer = ask_llm(question)
    print("\n--- 模型回答 ---")
    print(answer)
