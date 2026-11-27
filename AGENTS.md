# AIOT-LED 开发规范

## 开发范围

- 所有开发、仿真、构建和实验记录均在本仓库目录内进行。本机路径为 `D:\嵌入式lab\AIOT-LED`。
- Vivado 工程和 Block Design 放在 `build/vivado/`，日志放在 `build/`，可交付的 BIT、HDF 和 ARM ELF 放在 `build/artifacts/`，与本次迭代源码一起提交。为适配旧版工具，本机 `R:\` 映射到仓库根目录。
- 开发板为 ZYBO Z7-20（zybo-z720）。PL 逻辑在 Vivado 图形界面中画图搭建，由 Vivado 生成 HDL 并烧录。PS 端 C 程序使用 Xilinx SDK 的 Linux 应用目标编译。架构以 [architecture.md](docs/architecture.md) 为准。
- 实验按用户提供的报告顺序执行。缺少报告或关键要求不清时，先确认再开展对应实验。
- 保留报告原有结构和格式，在对应步骤填写文字和真实截图。如实标注未完成、未验证的内容。沟通和实验记录使用简洁中文。

## 代码与审查

- Python、JavaScript、HTML/CSS、Verilog 和 Shell 遵循 [Google Style Guides](https://google.github.io/styleguide/) 对应指南。C 源文件沿用模块内一致的命名与缩进风格。
- 每次变更解决一个明确问题。接口或行为发生变化时，同步更新相关文档和验证用例。参见 [Google 小变更指南](https://google.github.io/eng-practices/review/developer/small-cls.html)。
- 审查时核对功能、可读性、错误处理及验证结果。涉及软硬件接口时同时核对两端的数据方向和格式。

## 每次迭代必须提交并推送

1. 检查当前分支和工作区差异，运行本次改动对应的检查。涉及硬件时记录实际验证环境和结果。
2. 用 `git add` 暂存本次迭代文件，检查 `git diff --cached` 和 `git diff --cached --check`。仅纳入项目源码、配置与可公开的文档。
3. 用 `git commit` 提交，标题简述改动，必要时在正文记录原因与验证结果。
4. 每次迭代结束必须执行 `git push` 上传到 GitHub 当前对应分支。新分支首次使用 `git push -u origin HEAD`。
5. 确认远端分支指向本次提交，再报告提交号。推送失败时保留本地提交，记录原因并重试。
