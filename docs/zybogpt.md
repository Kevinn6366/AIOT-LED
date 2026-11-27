# ZyboGPT 英文续写

在本机灯控台的「英文台词续写」输入 ASCII 提示词和采样种子。上下文上限 128 个字符，网页单次最多续写 64 个。此模型按字符预测英文台词，可能产生拼造词或重复换行。

浏览器通过 `POST /api/generate` 提交 `prompt`、`length`、`seed`。本机服务将请求转发到板端；Bottle 将提示词写入独立 FIFO，SDK 编译的 `gpt.elf` 经 UIO 驱动 PL。接口以 NDJSON 逐字符返回输出和累计硬件周期数。网页分别显示 PL 每秒处理字符数（含提示词）及浏览器观测到的首字符延迟。PL 时钟为 50 MHz，AXI-Lite 地址为 `0x43C00000`，独立于数字灯和 RGB 的地址空间。

## 构建

1. 上游源码为 [mpai17/ZyboGPT](https://github.com/mpai17/ZyboGPT/tree/3410ce3d17360a794b4fe0dc24a58e94d6246149)。从项目根目录运行 `python model/prepare.py --train`。脚本在 `build/zybogpt` 获取固定版本、训练 Tiny Shakespeare 权重、导出 BRAM 初值和整数参考向量。若已有本地权重，可用 `python model/prepare.py --checkpoint checkpoints/quant/best.pt`。权重路径相对于上游构建目录。该版使用 3000 步浮点预训练和 1500 步量化微调。RMSNorm 平方寄存器使用 15 位容纳 16384。
2. 在 `build/zybogpt/hw` 使用 JDK 11 和 sbt 运行 `sbt "runMain zybogpt.ZyboGPTVerilog"`。保持 `gen/` 中的 Verilog 与所有 BRAM 初始化文件来自同一次导出。
3. 将 `build/zybogpt/export/golden.hex` 复制至 `hw/gen/`（相对于上游）。该目录下用 Vivado 2019.1 的 `xvlog -sv ZyboGPTWrapper.v ZyboGPTTop.v <项目>/tests/gpt_tb.sv`、`xelab gpt_tb -timescale 1ns/1ps -s gpt_test` 和 `xsim gpt_test -runall`，检查日志中出现 `GOLDEN PASS 16`。
4. 在 Vivado 2019.1 的 Tcl Console 中将环境变量 `AIOT_GPT_RTL` 设为 `R:/build/zybogpt/hw/gen`，执行 `source R:/hardware/panel.tcl`。新工程位于 `build/gpt-panel`；布局布线通过后，合并位流和 HDF 位于 `build/gpt-artifacts`。在 Xilinx SDK 环境执行 `xsct R:/device/build-sdk.tcl` 构建 ARM 程序。
5. 在合并位流目录执行 `bootgen -arch zynq -image ../../hardware/network.bif -process_bitstream bin -w`。用 PetaLinux 的 dtc 执行 `dtc -@ -I dts -O dtb -o build/artifacts/gpt-overlay.dtbo hardware/gpt-overlay.dts`。按 [SD 启动说明](sd-boot.md) 生成运行包，在执行 `device/build-sd.sh` 时设置 `AIOT_BIT_BIN=build/gpt-artifacts/panel_io_wrapper.bit.bin`，再签名并写入备用槽。
6. 核对新启动 ID、固件版本及 `/api/system` 中的 `gpt`，运行 `python tests/verify_gpt.py` 对比两个提示词各 16 个字符和 PL 周期。再运行 `python tests/verify_console.py` 检查 LD4 GPIO、四路拨杆跟随和网页接管、两路 RGB 以及同步渐变。最后在浏览器输入提示词并查看逐字符结果。

模型源码和生成的合并位流保存在本机项目的构建目录；仓库中的灯控基础位流用于原有配置。

## Z7-20 实板验收（2026-11-27）

最终固件 `e6cfba3eaf0c` 写入 SD 卡 B 槽，FIT 的 SD 回读 SHA-256 为 `7e17b18dabc453b0855589ae62788b88033b661e271309dfc6bc8ac9cac2fbc6`。重启后的 ID 为 `442206dd-c0cb-4ed3-b075-bec32a9c807b`。合并设计使用 16809 个 LUT、30 个 BRAM Tile 和 47 个 DSP。50 MHz 布局布线 WNS 为 2.808 ns，WHS 为 0.015 ns。

- `ROMEO:` 和 `JULIET:` 两个提示词各生成 16 字符，Vivado XSim、板端输出均与独立 INT8 参考逐字吻合。实板计数分别为 596967 和 629178 个周期，均含提示词。
- Edge 输入 `To be, or`，网页收到 64 个续写字符。该次 PL 吞吐为 1344 字符/秒（含提示词），浏览器观测到首字符耗时 59.3 ms。
- 四路数字灯通过接管开关及跟随拨杆回读，LD4 通过 PS GPIO 输出寄存器 bit 7 开关回读。LD5、LD6 通过独立颜色、使能及同步渐变检查，12 次同步采样两路色值一致。另在 120 字符输出流打开期间完成 LD3 开关回读并恢复原模式。
