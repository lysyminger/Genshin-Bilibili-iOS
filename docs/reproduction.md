# 复现和续签

当前只完成登录认证与区服调度阶段验证，尚未验证进入世界。iPad加载时报314302；下面的重建操作复现的是当前实验包，不保证完整可玩。

## 当前版本本机重建

1. Python3.11及Zig0.13.0；`python -m pip install --target .tools/zig13 -r requirements-build.txt`。
2. 自备匹配的原始iOS IPA、安卓B服APK、指定donor。公开仓库不含游戏和SDK二进制。
3. 将`templates/repro_baseline.example.json`复制为`private/repro_baseline.json`，填写自己的路径、Team和最终Bundle ID，核对输入SHA256。相对路径以仓库根目录为基准。
4. 在`private/GBReleaseCatalogProfile.h`保存经过验证的配套参数，在`private/AndroidSeed-pairing.json`保存真实两次独立启动和四组对照证据。模板占位符和测试fixture不能用于真实构建。
5. `python Genshin-Bilibili-iOS/one_click.py --check-only`，材料齐全后使用`--mode rebuild --no-open`。构建不覆盖基线IPA，并核对ZIP CRC、保留资源、原包哈希和库结构。
6. 使用iLoader自行签名安装。不要先卸载已有资源的游戏，沿用相同Apple账号/Team/最终Bundle ID更新。不要把已经追加Team后缀的签名包反复作为输入。

本机发布副本的private基线可以引用原工作目录中的大文件，不会复制整包和编译器。纯源码clone没有这些本机材料，不能直接生成可登录IPA。

## 以后版本适配

1. 取得同版本iOS与安卓原件，记录版本、build、哈希；核对架构、UUID、加密状态、SDK类和方法类型。
2. 检查APK渠道和SDK参数，验证真实B站登录及官方combo。
3. 取该版本未做seed替换的原生iOS参考；不要把桥接包中已替换的seed当原生值。
4. 在原版安卓B服触发区服请求，采集两个独立启动的上下文。在资源下载界面通常已产生请求，不必下载完整游戏资源。
5. 对照原安卓query、aid=0且无Cookie/Authorization、再只改iOS platform，以及原生iOS官服参考。要求HTTP200、retcode0、RegionInfo和实际所检查资源字段一致；仅HTTP200不够。
6. 生成新的本机配套配置和证据，再适配UUID、ABI及严格版本守护，构建后验证调度、资源、网关、进入正确角色世界和复登。

没有通用seed算法；当前只支持7.1.0/1241。旧采集协调脚本保留为研究资料，不是一键迁移任意新版的工具。

## 可选诊断环境

研究依赖见requirements-research.txt。不要运行诊断脚本来完成日常续签。

- `GB_ADB`：自己的ADB可执行文件，默认使用PATH中的adb。
- `GB_ANDROID_SERIAL`：自己的模拟器/安卓设备serial。
- `GB_ANDROID_UID`：从包管理器核对的目标游戏UID，相关脚本缺失时拒绝执行。
- `GB_IOS_BUNDLE_ID`：自己签名后实际安装的Bundle ID。
- `GB_NATIVE_IOS_SEED_SHA256`：当前版本已核实原生iOS参考的指纹。

只读扫描C源码构建时必须显式设置`-DGB_ANDROID_UID=<实际UID>`，并核对只读helper版本与宿主调用参数。默认UID=0会拒绝扫描，避免碰到其他应用。

元数据解码材料自行放入`private/dispatch-decode/`，不随源码公开。Frida脚本是早期临时注入诊断，构建和测试不会自动运行它。

## 314302 的官服对照

未加桥接的原包覆盖后，用户反馈可进入官服世界且无需重下资源，当前同版资源索引与覆盖前一致。下一步对照使用只记录SDK调用的诊断库：

```powershell
python Genshin-Bilibili-iOS/build_official_control.py inputs/original.ipa --output Genshin-Bilibili-iOS/output/official_control_v0.20.ipa
```

该包没有B服登录选项，不替换渠道、调度参数或校验结果。仍需同一Apple账号/Team/最终Bundle ID覆盖安装，不能先卸载。若官服对照也报314302，只能说明问题范围缩小到了加载修改后的客户端等差异，尚不能单凭该现象认定具体触发机制。

用户反馈v0.20可进入官服，日志确认版本及原始渠道。下一阶段的SDK对照为：

```powershell
python Genshin-Bilibili-iOS/build_sdk_control.py inputs/original.ipa --output Genshin-Bilibili-iOS/output/sdk_control_v0.21.ipa
```

同身份覆盖后，第一次直接选“官服登录”并测试进入世界。若成功，再正常重开游戏，选择“B站登录测试（实验）”，等SDK页面显示后取消或关闭，点结果提示中的“返回官服登录”，再测试官服。这一步不用提交B站账号凭证，且不能登录世界树；不要把SDK测试结束提示当作游戏认证失败。退出并重开一次可回到SDK尚未加载的对照条件。

v0.21的两种官服测试已有用户成功反馈和SDK初始化日志。下一步v0.22恢复B服流程，但一级目录继续使用原生iOS配置，只在二级请求应用配套参数：

```powershell
python Genshin-Bilibili-iOS/build_native_directory.py inputs/original.ipa --proof private/AndroidSeed-pairing.json --output Genshin-Bilibili-iOS/output/bili_native_directory_v0.22.ipa
```

它仍是B服实验包，无法凭本机测试保证解决314302。同身份覆盖后正常完成B站登录，观察是否实际进入地图并可移动角色；停在下载界面、登录成功提示或加载画面都不算世界进入。保留设备连接以核对版本、实际一级请求和二级响应。

该实验随后仍有开门数据异常反馈。下一阶段只做真实认证结构对照：

```powershell
python Genshin-Bilibili-iOS/build_auth_shape_control.py inputs/original.ipa --output Genshin-Bilibili-iOS/output/auth_shape_control_v0.23.ipa
```

v0.23同身份覆盖后，选择“B服认证取样后回官服”，正常完成B站登录。取样结束会自动恢复原官服路径；若出现官服登录页，使用官服账号继续。此包不会进入世界树，目的是在同一进程采集真实B服认证字段结构和原生官服回调结构，避免把两种账号的凭证混交给游戏。日志只含固定字段的类型/非空状态和受限的渠道编号；真实字符串内容不记录。结果待实机验证。
