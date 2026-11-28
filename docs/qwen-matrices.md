# Qwen 全层矩阵后端

模型为板端 Qwen2.5-0.5B-Instruct Q4_0，24 层，隐藏维度 896，FFN 维度 4864，上下文 512。网页 `/llm` 可以选择 PL 或纯 PS，使用同一个 GGUF、分词器、贪心采样器和 Xilinx SDK 2019.1 编译的 ELF。

| PL 执行的 ggml 矩阵乘 | 数量 | 输入 |
| --- | ---: | --- |
| Q、K、V、注意力输出投影 | 每层 4 个 | Q4_0 权重，Q8_0 激活 |
| FFN gate、up、down | 每层 3 个 | Q4_0 权重，Q8_0 激活 |
| 注意力分数与注意力加权求和 | 每层 2 个 | KV 缓存和量化至 FP16 的右操作数，展开为 FP32 后相乘 |
| 词表输出头 | 1 个 | Q8_0 权重，151936 行 |

PS 负责分词、嵌入查表、归一化、RoPE、Softmax、逐元素运算、激活量化、调度与采样。PL 选项把该模型图中的每个 MUL_MAT 交给 HP0 核，输出直接回填对应张量。遇到未知矩阵或超时会报错。首次遇到每类运算时，主机还对采样行做 CPU 数值比对。

## 存储与计数

设备树在 DDR 的 `0x20000000` 起保留 432 MiB。168 个层权重和 1 个输出头首次使用时重排一次，驻留约 418.2 MiB。分块格式每 32 个权重占 24 或 40 字节，含 FP32 缩放值和对齐填充。动态注意力矩阵、输入与输出使用不重叠的暂存区。HLS 核通过 64 位 HP0 读取权重，一次处理最多两列，按 64 行分块写回。

网页给出本次请求的首个可见字符延迟、板端生成速度、PL 调用数、调用窗口周期和执行层数。NDJSON 完成帧另含逐层计数及十种矩阵调用计数，顺序是 Q、K、V、注意力输出、gate、up、down、注意力分数、加权求和、输出头。纯 PS 对照的 PL 增量必须为零。

## 构建与复测

1. 在映射到项目的 R 盘根目录用 Vivado HLS 2019.1 执行 `vivado_hls -f hardware/qwen/sim.tcl`。仿真覆盖 Q4、Q8、浮点点积、单双列、行尾和 4864 维输入。运行 `hardware/qwen/build-ip.ps1` 导出 IP。
2. 在 Vivado Tcl Console 设置环境变量 `AIOT_GPT_RTL=R:/build/zybogpt/hw/gen` 和 `AIOT_QWEN_IP=R:/build/qwen_all_hls/sol/impl/ip`，执行 `source hardware/panel.tcl`。工程位于 `build/qwen-all-panel`，位流及 HDF 位于 `build/qwen-all-artifacts`。
3. 执行 `device/build-qwen.ps1` 编译 ARM ELF。在位流目录运行 `bootgen -arch zynq -image ../../hardware/network.bif -process_bitstream bin -w`。按项目 SD 流程打包，构建 FIT 时设置 `AIOT_QPROJ=1` 和 `AIOT_BIT_BIN=build/qwen-all-artifacts/panel_io_wrapper.bit.bin`。
4. 签名写入备用槽并重启。在装有 Playwright 和 Edge 的电脑上运行 `python tests/compare_qwen.py`。脚本先预热权重，再交替测试两个后端。切换后端会清除旧 KV，核对各轮提示 token 数和输出一致。详细记录写入 `build/qwen/web-comparison.json`。

## 2026-11-28 开发板网页实测

在同一块 ZYBO Z7-20 上使用固件 `12a39286aa2c`，从本机 Edge 的 `/llm` 提交“一盏红灯加两盏绿灯，一共有几盏灯？”。权重已预热，每次切换后重新计算 42 个提示 token，生成 17 个 token。四次回答均为“一盏红灯加两盏绿灯，一共有三盏灯。”首字按浏览器收到可见输出计时，生成速度取板端 llama.cpp 的解码统计。[四次请求及板端计数](qwen-web-comparison.json)

| 执行顺序 | 矩阵后端 | 首字（秒） | 生成（token/s） | PL 调用 | 有 PL 调用的层数 |
| ---: | --- | ---: | ---: | ---: | ---: |
| 1 | PL | 161.23 | 0.09691 | 31,037 | 24 |
| 2 | PS | 24.73 | 1.04479 | 0 | 0 |
| 3 | PL | 161.26 | 0.09688 | 31,037 | 24 |
| 4 | PS | 24.88 | 1.03504 | 0 | 0 |

按两次测量的算术平均值，PL 首字耗时为 PS 的 6.50 倍，生成速度为 PS 的 9.3%。每次 PL 推理中，注意力分数和加权求和合计调用 24,864 次，输出头调用 17 次。50 MHz 下各调用窗口合计约 330 秒，含核执行、总线通信与软件等待，这组数据无法单独量出三者耗时。
