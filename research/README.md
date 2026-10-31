# 研究工具

保留本项目自编的分析和诊断源码，不包含下载的第三方实现、反汇编转储或运行日志。

- sdk_research.py、scan_ios_symbols.py：静态Mach-O分析。
- inspect_dispatch_cache.py：读取目标应用调度缓存，只在内存恢复SQLite快照。
- validate_android_dispatch.py、scan_android_dispatch.c：受目标包/UID限制的只读上下文采集。
- verify_android_seed_pairing.py：首次7.1.0适配的两次启动及对照协调，需先设置本机变量，非通用新版适配器。
- explore_seed_*.py：未得到通用算法的早期本地探索，私有输入已移至private/seed_research.json。
- catalog_profile_console.py、inspect_v15_profile_controls.py：历史诊断，不是正式构建入口。

真实采集不是纯离线单元测试；请先阅读docs/reproduction.md，确认环境和作用范围。日常使用不需要运行这些工具。
