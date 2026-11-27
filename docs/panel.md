# PL 拨杆与灯光控制

SW0–SW3 分别对应 LD0–LD3。复位后四路跟随拨杆，1 亮、0 灭。每行都有独立的控制方式；网页接管一路后仍显示其物理拨杆位置，选择「跟随拨杆」会立即采用当前位置。PS 侧 LD4 保留原有控制接口。

页面参考 [Siemens Industrial Experience 的应用框架、数据表和状态反馈](https://ix.siemens.io/docs/components/overview)，用同一行显示通道、输入、灯输出与操作。输入和输出都有文字表示，连接中断时显示未知状态。

## 工程与产物

| 项目内路径 | 内容 |
| --- | --- |
| `hardware/panel.tcl`、`hardware/panel.xdc` | Vivado IP Integrator 接线与板卡约束 |
| `build/rgb-vivado/panel.xpr` | 本机 Vivado 2019.1 图形工程 |
| `build/rgb-vivado/panel.srcs/sources_1/bd/panel_io/panel_io.bd` | 包含六路 PWM 的 Block Design 接线图 |
| `build/artifacts/panel_io_wrapper.bit`、`build/artifacts/panel.hdf` | 下载位流和导出的硬件定义 |
| `build/artifacts/panel-io.elf`、`build/artifacts/led-control.elf` | Xilinx SDK 编译的 PL I/O 与 PS 指示灯 Linux 程序 |
| `build/artifacts/rgb-overlay.dtbo` | 第二个 UIO 外设的设备树覆盖文件 |
| `build/rgb.log`、`build/rgb-program.log`、`build/sdk-build.log` | 本机实现、下载及编译日志 |

在干净的项目检出目录中，用 Vivado 2019.1 图形界面的 Tcl Console 执行 `source hardware/panel.tcl` 生成 Block Design 及位流；已有工程直接打开 `panel.xpr`。在 Xilinx SDK 2019.1 命令环境运行 `xsct device/build-sdk.tcl`。连接开发板后，在 Vivado 中执行 `source hardware/program.tcl` 经 JTAG 下载到 PL。

## 板端接口

- 双通道 AXI GPIO 的 DATA（偏移 0x00）低四位保存网页输出，第 4–7 位逐路选择网页接管。组合逻辑在 PL 内选择拨杆或网页输出。
- DATA2（偏移 0x08）低四位读取拨杆，第 4–7 位回读 LD0–LD3 输出，第 8–13 位依次采样 LD5 R/G/B、LD6 R/G/B 的 PWM 输出，高 18 位为设计标识 21063。管脚参照 [Digilent ZYBO Z7 约束](https://github.com/Digilent/digilent-xdc/blob/master/Zybo-Z7-Master.xdc)。
- RGB 外设基址为 `0x41210000`，偏移 0 和 8 分别写入 LD5、LD6 的 `0xRRGGBB`。每种颜色有 256 档，PL 用同一个相位计数器驱动六个比较器，单色占空比为分量值 / 512。设备树可用 `dtc -@ -I dts -O dtb -o build/artifacts/rgb-overlay.dtbo hardware/rgb-overlay.dts` 重新生成。
- 网页对 `/api/rgb/5` 或 `/api/rgb/6` 发送 POST。传 `{"color":"#ff8000"}` 选择颜色，传 `{"on":true}` 或 `{"on":false}` 控制开关。开灯时选色立即生效。关灯后仍可预选下次的颜色；状态接口的 `rgb` 数组返回两灯的设定值。
- 网页通过 `GET /api/panel` 读取三个四位掩码 `switches`、`leds`、`remote`。通过 `POST /api/panel/0` 至 `/3` 发送 `{"mode":"switch"}`、`{"mode":"on"}` 或 `{"mode":"off"}`。Bottle 走 `pl-commands` 向 C 进程发命令，再通过 `pl-status` 取得同一请求的实板回读。
- 使用现有 Linux 映像，按 [LD4 说明](ld4.md) 配置网口、准备运行环境并启动原 PS 程序。新运行包解压后，执行 `sh device/start-panel.sh /tmp/aiot-led` 启动 PL 程序，再启动同目录的新 Bottle 服务。脚本将当前映像的 `41200000.gpio` 从 `xilinx-gpio` 交给 UIO；启动 C 程序时校验设计标识。
- JTAG 位流及板端临时目录在断电后需要重新加载。此次未改写 SD 启动映像。

## RGB 实板验证

在 ZYBO Z7-20 上运行 `tests/verify_rgb.py http://板端IP:8080 /dev/uio0`（使用板端 Python）。该测试通过 HTTP 独立控制 LD5、LD6，再从 PL 反馈寄存器采样实际输出的六路 PWM。全关及逐灯红、绿、蓝依次检测到活动掩码 00、01、08、02、10、04、20；两灯混色同时开启时为 33（均为十六进制）。测试结束后恢复原来的开关和颜色。

## 四路普通灯实测（2026-11-27）

Vivado 实现报告 WNS 为 12.982 ns，WHS 为 0.053 ns，满足设计中的时序约束。Hardware Manager 报告 XC7Z020 下载成功。Xilinx SDK 的 ARM Linux GCC 8.2.0 已编译两个 ELF。

逐路执行网页接口的开、关、跟随拨杆，共 12 次。仅对应灯输出位被置位，四路分别读到 1、2、4、8；释放后恢复物理输入。现场将 SW0 拨到 1，用户确认 LD0 亮，网页接口同期读到 `switches=1, leds=1, remote=0`。SW1–SW3 的拨动与灯珠亮灭尚未逐路目视确认。
