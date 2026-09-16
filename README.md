# SyncRTC

SyncRTC 是一个使用 C++、Qt/QML、Boost.Asio、FFmpeg、libdatachannel、MySQL 和 Redis 开发的视频会议项目。当前仓库包含桌面客户端和多个服务端进程，功能以账号、会议管理、文字聊天以及 WebRTC 媒体信令链路为主。

## 当前已实现的功能

- **账号与联系人**：GateServer 提供邮箱验证码、用户注册、登录、密码重置、联系人查询、添加和删除接口；用户和联系人数据持久化到 MySQL，会话信息使用 Redis。
- **会议管理**：RealtimeServer 支持创建会议、设置会议密码和可见性、查询最近会议与历史会议、按会议号加入会议、开始会议、离开会议和结束会议，并在 MySQL 中保存会议及参与者状态。
- **会议文字聊天**：支持会议群聊和会议内私聊，消息写入 MySQL，并提供历史消息查询。
- **WebRTC 信令与媒体转发**：RealtimeServer 负责校验并转发 offer、answer、ICE candidate；MediaServer 通过 Unix Domain Socket 接收信令，使用 libdatachannel 建立 PeerConnection，并在会议房间内转发 RTP 音视频数据。
- **Qt/QML 桌面客户端**：包含登录、注册、找回密码页面，以及会议首页、历史会议、通讯录、AI 助手和设置等工作台页面；认证页面已接入 GateServer HTTP 接口，会议工作台中的部分模块仍是界面展示。
- **服务端测试与部署工具**：各服务提供 CMake 构建配置和部分逻辑测试，仓库还包含 Docker 构建文件、服务发布脚本和远程发布检查脚本。

## 服务端目录

| 目录 | 作用 |
| --- | --- |
| `syncRTC-server/GateServer` | HTTP 认证、验证码和联系人接口 |
| `syncRTC-server/RealtimeServer` | TCP 长连接、会议状态和聊天业务 |
| `syncRTC-server/MediaServer` | WebRTC PeerConnection、房间媒体会话和 RTP 转发 |
| `syncRTC-server/VarifyServer` | 验证码相关 Node.js 服务 |
| `syncRTC-server/db` | MySQL 初始化脚本和迁移脚本 |

## 客户端目录

`syncRTC-client/rtc_client` 是 Qt 6/QML 桌面客户端，包含认证页面、会议工作台、会议房间页面以及对应的 C++ Controller 和网络模块。客户端配置位于 `syncRTC-client/rtc_client/config/config.ini`。

## 当前范围

仓库暂未形成完整可用的 AI 会议闭环；实时字幕、会议录音后的 ASR、会议纪要、待办事项提取和会后问答不作为当前已实现功能写入本说明。客户端中的 AI 助手页面目前主要用于界面展示，不能据此视为已经接入 AI 服务。
