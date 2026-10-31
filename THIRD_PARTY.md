# 第三方材料

- 原神及其IPA/APK、游戏资源、内置SDK属于相应权利人，本仓库不提供这些文件。
- B站登录组件来自[Arknights-Bilibili-for-IOS](https://github.com/lazhire/Arknights-Bilibili-for-IOS)的本机参考包。准备脚本锁定donor指纹并移除方舟启动入口及类注册；它不是从源码恢复的纯净独立SDK，仍有不参与启动的遗留代码。只发布自编准备脚本，不重新分发donor。
- iLoader：<https://github.com/nab138/iloader>。签名工具另行下载；本仓库不包含其程序。
- Zig：<https://ziglang.org/>，本项目使用0.13.0。
- pymobiledevice3：<https://github.com/doronz88/pymobiledevice3>，仅用于本机诊断。
- 其他Python依赖列于requirements文件，遵循各自许可证。

研究引用：

- Android切服脚本参考：<https://github.com/Anyexyz/xyz.anye.yuanshen>，使用不同原版APK及共享资源目录，不能据此认定其实现了本项目登录桥接。
- PC切服参考：<https://github.com/Luviichann/Genshin-Impact-server-switch>，主要修改渠道配置，未提供本项目所需seed生成算法。
- 官方Apple应用更新/签名排错：<https://developer.apple.com/library/archive/technotes/tn2319/>。

公开源码不改变第三方材料的权利归属。未在本仓库另行授予第三方SDK或游戏的再分发许可。
