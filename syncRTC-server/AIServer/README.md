# SyncRTC AIService

AIService 是独立部署的会议 AI 服务，只通过 gRPC 接收 RealtimeServer 的请求。

## 通信边界

- RealtimeServer 是唯一调用方，负责用户身份、会议成员权限、录音授权、任务状态和客户端推送。
- MediaServer 不与 AIService 通信，只负责将音频归档到受限磁盘目录并在会议结束后完成封存。
- AIService 通过音频归档清单读取已封存文件，调用外部 ASR/LLM API，并返回结构化结果。
- 客户端不直接访问 AIService，也不接触模型 API Key、完整音频或供应商 Token。

## 当前目录说明

目录只完成服务骨架，不代表 AI 能力已经实现。后续按 `proto/meeting_ai.proto` 固化 RealtimeServer ↔ AIService 的 gRPC 契约，再逐步实现普通问答、会后转写和纪要任务。
