# 系统架构

下表按课程架构图记录数据流，供后续实现对照。

| 环节 | 数据流 |
| --- | --- |
| 浏览器与服务 | Python Bottle 提供 `index.html`。页面加载 `main.js`，脚本通过 AJAX 发送 HTTP 请求和读取状态。 |
| 灯光控制 | Python 服务将 LED 控制数据写入命名管道，板端 C 程序读取并操作硬件。 |
| 输入回读 | C 程序读取按键与拨码开关状态，经另一条命名管道送回 Python 服务。 |
| Linux 与 PL | C 程序通过 Linux UIO 访问 PL 外设。图中列出 `/dev/uio0`、`/dev/uio1`。`myip.v` 对接普通 LED、按键和拨码开关，`mypwm.v` 驱动 RGB LED。 |
| Linux 与 PS | 图中 PS 侧 LED 通过 `/sys/class/gpio` 控制。 |

四路普通灯和拨杆映射为 `/dev/uio0`（0x41200000），两盏 RGB 彩灯的六路 PWM 映射为 `/dev/uio1`（0x41210000）。两部分均使用 Vivado Block Design 接线。按键尚待实现。接口与数据方向见 [PL 灯光控制](panel.md)。

ZyboGPT 扩展在同一 Block Design 中增加独立 AXI-Lite 从设备（0x43C00000）。本机网页发送提示词，板端 Bottle 通过 FIFO 交给 SDK C 程序；程序经 UIO 启动推理核并读取字符与周期计数。详见 [ZyboGPT 英文续写](zybogpt.md)。

中文问答经本机网页、板端 Bottle 和回环 HTTP 接口进入 PS 上的 Qwen2.5-0.5B-Instruct。ARM 程序从 SD 卡映射量化权重，逐段向网页返回文字。详见[板端中文问答](qwen.md)。
