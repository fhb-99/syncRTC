# Realtime 本地真实依赖联调

## 心跳与业务恢复专项

`run_heartbeat.py` 使用与 `run_recovery.py` 相同的 `--repo`、`--runtime`、`--output` 参数。
先执行业务恢复一期回归，再验证 20 秒以上真实 SQL 表锁期间序号 PING/PONG 持续响应、
心跳超时只产生一次断线通知、保留 45 秒恢复资格、重新鉴权与 RESUME、未鉴权半包超时和心跳限流。
心跳每 5 秒发送，服务端静默期限仍为生产参数 20 秒；未缩短生产时间以加快联调。

## 客户端业务恢复专项

业务恢复专项使用 `run_recovery.py --repo <独立工作树> --runtime <依赖目录> --output <新输出目录>`。
它只启动 MySQL、Redis、RealtimeServer，不需要 Media 或 Offer。覆盖密码会议票据恢复、活跃连接接管、
重复请求、回包丢失、错误票据/设备会话、SQL/Redis 错误分类、快照顺序、心跳绕过慢查询、主动离会、
会议结束、完整 45 秒过期和令牌撤销。结果写入 `results.json`，退出恢复配置并关闭自己启动的进程。

输出和数据目录应放在该工作树的 `out/` 内。WSL 的 Windows 挂载盘可能不支持 Unix socket；
可在工作树的 `out/wsl/` 临时挂载 tmpfs 后运行，结束后将结果与日志保存回 `out/` 普通目录并卸载。
不要在构建服务端的同时运行，构建会覆盖测试所用的 `bin/conf/config.ini`。

## 既有业务与媒体信令联调

这组测试启动真实 MySQL、Redis、RealtimeServer、MediaServer 子进程，通过 TCP 协议请求和 SQL / Redis 查询交叉验证。只在专用独立工作树运行；不要对正在服务的 checkout 运行，也不要同时构建该目录（构建会复制配置）。

## 环境与运行

Linux / WSL，Python 3 + `PyMySQL==1.1.1`、`redis==5.0.8`；构建好当前 checkout 的两个服务。MySQL 8 和 Redis 的发行包可解包到专用 `runtime/`，不需要安装或启动系统服务。`--runtime` 下需有 `usr/sbin/mysqld`、`usr/bin/redis-server` 及依赖共享库。本次验证使用 Ubuntu 24.04 的 MySQL 8.0.46、Redis 7.0.15。

```bash
# 路径按实际环境设置。MEDIA_BUILD 对应已完成的 MediaServer 静态依赖构建。
cmake -S syncRTC-server/RealtimeServer/tests/integration -B /path/to/fixture-build \
  -DMEDIA_BUILD=/path/to/media-build \
  -DLIBDATACHANNEL_SOURCE=/path/to/libdatachannel
cmake --build /path/to/fixture-build
/path/to/fixture-build/offer_fixture > /path/to/offer.sdp

python syncRTC-server/RealtimeServer/tests/integration/run_local.py \
  --repo /absolute/path/to/isolated/SyncRTC \
  --runtime /path/to/runtime \
  --offer /path/to/offer.sdp \
  --output /path/to/new-short-run-directory
```

`--output` 必须是不存在的新目录，路径不能过长（Unix socket 路径上限）。脚本创建全新 MySQL 数据目录，随机选择本地端口，Redis 无持久化；只操作自己创建的子进程和测试数据。MySQL 初始化和真实 45 秒宽限期需要等待。使用的空密码数据库账号和固定认证 token 只属于本次隔离实例，不得用于共享/线上环境。

当前服务配置入口固定为源码根目录的 `bin/conf/config.ini`。脚本先备份两个文件，临时填写本地实例配置，并在 `finally` 恢复；不修改 `conf/config.ini`。备份、运行日志和数据目录留在权限为 0700 的输出目录。进程被强杀或 WSL 重启时 `finally` 无法保证执行，应先确认测试进程已退出，再从 `*-config.backup` 恢复对应的 `bin/conf/config.ini`，不要把配置备份提交到 Git。

## 验证内容与边界

- 登录走真实 Realtime TCP + Redis token + MySQL 用户资料；测试预置认证 token，不覆盖 Gate 注册/登录。
- 连续入会→聊天、聊天持久化/历史/重复消息、两个客户端并发聊天的广播与落库顺序。
- 用真实 MySQL 表锁阻塞 INSERT，确认同房间只有一个在途 INSERT、另一会议的查询能继续响应；释放锁后检查提交顺序。
- 主动离会、断线宽限期内重连、完整 45 秒超时、超时后重新入会。
- 真实 libdatachannel Offer → TCP → Realtime → UDS → Media → Answer/ICE；通过 `/proc` 核对 Media 进程拥有的 UDP socket 回收。
- Media 进程停止/重启，Realtime 继续工作并自动恢复 UDS；旧绑定 Candidate 拒绝，新 Offer 成功。
- SIGTERM 到达时存在等待真实 MySQL 的任务，释放表锁后进程正常退出、消息落库、断线状态落 Redis、UDS 断开使 Media 释放资源；重启 Realtime 后重新登录入会；SIGINT 正常退出。

结果实时写入 `results.json`。失败场景返回非零；清理断言失败会保留结果并继续后续场景，异常则停止场景并执行收尾。默认不加入 CTest，避免普通单元测试意外启动数据库或覆盖运行配置。

本测试没有采集/编码/发送音视频，也不完成 ICE/DTLS 连接或真实双端播放。Offer 来自真实库，但生成后客户端 PeerConnection 即关闭。UDP fd 回收到基线能验证传输资源释放，不能单独证明所有堆对象都无泄漏。客户端自动重新生成 Offer 仍需另行实现/验收。这里的 SQL/Redis 正常运行证据也不等于跨存储事务原子性或数据库故障恢复验证。
