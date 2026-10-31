# 复现和续签

## 当前版本本机重建

1. Python3.11及Zig0.13.0；`python -m pip install --target .tools/zig13 -r requirements-build.txt`。
2. 自备匹配的原始iOS IPA、安卓B服APK、指定donor。公开仓库不含游戏和SDK二进制。
3. 将`templates/repro_baseline.example.json`复制为`private/repro_baseline.json`，填写自己的路径、Team和最终Bundle ID，核对输入SHA256。相对路径以仓库根目录为基准。
4. 在`private/GBReleaseCatalogProfile.h`保存经过验证的配套参数，在`private/AndroidSeed-pairing.json`保存真实两次独立启动和四组对照证据。模板占位符和测试fixture不能用于真实构建。
5. `python Genshin-Bilibili-iOS/one_click.py --check-only`，材料齐全后使用`--mode rebuild --no-open`。构建不覆盖成功IPA，并核对ZIP CRC、保留资源、原包哈希和库结构。
6. 使用iLoader自行签名安装。不要先卸载已有资源的游戏，沿用相同Apple账号/Team/最终Bundle ID更新。不要把已经追加Team后缀的签名包反复作为输入。

本机发布副本的private基线可以引用原工作目录中的大文件，不会复制整包和编译器。纯源码clone没有这些本机材料，不能直接生成可登录IPA。

## 以后版本适配

1. 取得同版本iOS与安卓原件，记录版本、build、哈希；核对架构、UUID、加密状态、SDK类和方法类型。
2. 检查APK渠道和SDK参数，验证真实B站登录及官方combo。
3. 取该版本未做seed替换的原生iOS参考；不要把成功桥接包中已替换的seed当原生值。
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
