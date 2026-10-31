# 原神 iOS B服登录桥接研究

在原神 iOS 客户端接入 B站真实登录，通过原游戏 combo 认证，再适配区服调度的版本与 seed 配套参数。当前 v0.19 针对 **原神7.1.0 / build1241**。

用户已实机反馈进入世界并可操作角色；独立设备日志确认 combo 认证与配套改写生效。长期运行、其他账号/设备及未来版本尚未验证。这不是任意版本的一键切服工具。

## 目录

| 路径 | 内容 |
| --- | --- |
| `Genshin-Bilibili-iOS/src/` | C / Objective-C runtime 桥接、调度与诊断实现 |
| `Genshin-Bilibili-iOS/build_catalog_pairing.py` | v0.19正式构建入口，保留证据与版本检查 |
| `Genshin-Bilibili-iOS/one_click.py` | Windows续签准备与同版重建 |
| `Genshin-Bilibili-iOS/test*`、`tests/fixtures/` | 回归测试和虚构输入 |
| `research/` | 静态分析、受限诊断、只读采集及早期探索脚本 |
| `templates/` | 本机私有配置模板，无真实seed或设备身份 |
| `docs/` | 原理、复现和研究过程 |
| `tools/` | 测试入口及公开提交检查 |
| `private/`（本机、被忽略） | 实际seed、采集证明、本机路径、日志和解码材料 |

## 快速开始

代码可独立阅读，纯逻辑测试使用虚构seed，不需要账号、手机或联网。安装Python3.11及Zig0.13后：

```powershell
python -m pip install --target .tools/zig13 ziglang==0.13.0
python tools/run_tests.py
```

构建真实IPA需要自行提供匹配的原始IPA、安卓B服APK、SDK donor和经过验证的配套参数。它们不随源码发布。参见 [复现说明](docs/reproduction.md)。

```powershell
python Genshin-Bilibili-iOS/one_click.py --check-only
python Genshin-Bilibili-iOS/one_click.py --mode rebuild --no-open
```

日常续签通常不必卸载游戏：使用同一Apple账号、同一Team与相同最终Bundle ID覆盖更新；不要先删除应用。脚本仅准备文件和打开签名工具，Apple登录、验证码及安装在iLoader界面完成。保留约30GB资源的现场续签尚未验证，不能保证系统或游戏永远不清理可重下载资源。

## 原理与边界

1. B站SDK正常取得真实登录凭证，桥接交给原神官方combo认证。
2. B服渠道为14，读取真实QD目录；当前版本同时改写调度version与配套dispatchSeed。
3. 保留iOS platform=1、真实账号参数及原游戏服务器响应处理，随后由游戏继续登录。

当前seed来自原版安卓B服两次独立启动的只读采集，并通过匿名元数据及所检查资源字段对照。同版已保存后不必每次开模拟器；未发现通用生成算法，更新需重新验证。

不包含充值功能、第三方SDK二进制、完整游戏包、账号密码、Apple签名材料或个人设备日志。生产构建不会启用测试seed，也不会启用v0.18原生URL写入观察补丁。

详见 [架构](docs/architecture.md)、[研究过程](docs/research-history.md)、[隐私说明](PRIVACY.md)、[第三方来源](THIRD_PARTY.md)。
