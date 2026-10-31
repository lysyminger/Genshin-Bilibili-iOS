# 当前实现

流程：游戏login_login → 渠道菜单 → B站SDK → 官方combo认证 → indexed JSON回调 → QD区服调度 → 原游戏后续登录。

| 模块 | 职责 |
| --- | --- |
| GBLoginBridgeCore/Runtime | 状态、取消、超时、原调用索引 |
| GBBiliComponent | SDK生命周期及登录交接 |
| GBComboLogin | 使用原游戏签名/网络，真实服务器认证与回调序列化 |
| GBDispatchTrace | HTTPS初始请求守护、认证状态守护、GF到QD路由 |
| GBDispatchProfileCore / GBCatalogProfileCore | 整体核对请求，再原子替换配套version/seed；保留其余编码query |
| GBRegionDirectory | 区服选择及历史目录实验；当前读真实Android B目录 |
| GBDiagnostic | 允许字段诊断，不展开登录凭证 |

当前配置：channel14、subchannel0、app4、account_type14；B站配置game2270/merchant777/server2489由原APK读取。iOS platform和client_type保持1。客户端版本CNRELiOS7.1.0在调度请求适配为CNRELAndroid7.1.0。

动态信息包括真实UID、access_key、open_id、combo_token、设备ID、时间戳、签名、RegionInfo及网关响应，均不应写死为另一账号的值。

version与seed必须配套。两次安卓独立进程取得相同seed，匿名元数据请求保留iOS平台也成功，所检查资源字段与原生iOS参考相同；这不是对所有账号/设备和未来版本的证明。

工程要求：16KB段对齐、dylib不带LC_LOAD_DYLINKER、保留签名命令空间、使用常规绑定。程序UUID/SDK ABI是版本约束，更新不能只改版本文本。

自编元数据解码器对当前真实响应的参考ServerPub验签未通过；它只作有限诊断，不宣称独立完成官方响应认证。游戏自身TLS及响应验证路径保持原状。

最新实机状态：iPhone仍在下载资源；iPad的认证与实际二级调度缓存已通过，但进入世界加载时出现314302。此前进入世界的表述已撤回。目前尚不能确定是资源/客户端一致性问题还是其他校验失败。

## v0.24 双入口

同一IPA提供官服/B服登录选择。一级请求保留原生iOS参数；原天空岛记录及全部配置字节保留，只追加世界树记录。不再启动时强制改写已保存区服。

官服选项转交原登录调用，保持channel/account_type1，二级请求不改写；B服通过真实combo后才启用channel14与配套二级调度。退出B服恢复官服状态，避免旧B会话参数影响下一次登录。游戏中的区服仍需与账号对应：官服选天空岛，B服选世界树。双入口尚未实机验证，B服314302仍按未解决处理。
