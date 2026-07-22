# 客户端 device_id 持久化设计

## 目标

为每一份 SyncRTC 客户端安装生成并持久化一个随机 `device_id`。该标识供后续登录接口使用，本次不修改任何网络请求、登录页面、TCP 鉴权或服务端代码。

## 范围

- 新增一个小型 `DeviceIdStore`，提供 `loadOrCreate()`。
- 首次读取不到值时生成不带花括号的 UUID，并立即写入用户级 INI 设置。
- 后续调用返回同一个值。
- 在客户端启动阶段调用一次，确保安装标识已创建。
- 新增单元测试覆盖生成、写入和再次读取。

## 非目标

- 不采集 MAC 地址、硬件序列号或其他机器指纹。
- 不把 `device_id` 作为身份凭证，也不放入 Windows 凭据库。
- 不发送 `device_id` 给 GateServer，不实现 session、自动登录或 token 持久化。
- 不修改现有运行时 `config/config.ini`；该文件仅保存部署配置。

## 方案

`DeviceIdStore` 使用显式的 `QSettings::IniFormat` 和 `QSettings::UserScope`，组织名为 `SyncRTC`、应用名为 `rtc_client`。因此值保存在当前 Windows 用户的应用数据目录，不会随部署配置复制、覆盖或因安装目录不可写而失败。

读取键为 `identity/device_id`：

1. 若键存在且非空，直接返回该值。
2. 否则使用 `QUuid::createUuid().toString(QUuid::WithoutBraces)` 生成 UUID。
3. 写入设置并调用 `sync()`，将写入错误记录到日志；仍返回本次生成值。

`device_id` 是安装实例标识而非秘密。卸载或清除应用数据后重新生成是预期行为；正常退出登录或账号切换不删除它。

## 文件与测试

- 新增 `core/src/models/deviceidstore.h/.cpp`。
- 在 `core/src/main.cpp` 的启动阶段调用 `DeviceIdStore::loadOrCreate()`。
- 将新源文件列入 `CMakeLists.txt` 的应用目标。
- 新增 `core/tests/deviceidstore_test.cpp` 和独立 `deviceidstore_test` CTest 目标。

测试使用临时 INI 文件而不触碰真实用户设置，验证：

- 空设置首次调用生成格式正确的 UUID 并写入。
- 重新创建 `QSettings` 后返回同一 UUID。

## 验证

运行新增的 `deviceidstore_test`，再运行现有 CTest 集合并构建 `apprtc_client`。检查 Git diff，确认改动仅限 device_id 源码、测试、CMake 和本设计文档。
