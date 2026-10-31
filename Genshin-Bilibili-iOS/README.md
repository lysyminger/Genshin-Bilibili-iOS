# 桥接实现

项目总览、依赖和当前状态见 [根目录README](../README.md)。

正式构建入口为 `build_catalog_pairing.py`；旧版本构建入口保留作研究复现，不代表同样可进入世界。
真实配置位于根目录被忽略的 `private/`，模板在 `templates/`。`src/GBReleaseCatalogProfile.h`仅负责选择私有配置或明确启用的虚构单元测试配置。

运行测试：`python tools/run_tests.py`（从仓库根目录）。带本机原包/SDK的完整检查另用 `--integration`。
