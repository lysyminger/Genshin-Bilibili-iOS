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
