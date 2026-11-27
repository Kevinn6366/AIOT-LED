# 系统架构

下表按课程架构图记录数据流，供后续实现对照。

| 环节 | 数据流 |
| --- | --- |
| 浏览器与服务 | Python Bottle 提供 `index.html`。页面加载 `main.js`，脚本通过 AJAX 发送 HTTP 请求和读取状态。 |
| 灯光控制 | Python 服务将 LED 控制数据写入命名管道，板端 C 程序读取并操作硬件。 |
| 输入回读 | C 程序读取按键与拨码开关状态，经另一条命名管道送回 Python 服务。 |
| Linux 与 PL | C 程序通过 Linux UIO 访问 PL 外设。图中列出 `/dev/uio0`、`/dev/uio1`。`myip.v` 对接普通 LED、按键和拨码开关，`mypwm.v` 驱动 RGB LED。 |
| Linux 与 PS | 图中 PS 侧 LED 通过 `/sys/class/gpio` 控制。 |

设备节点与外设的对应关系、寄存器地址、HTTP 接口和管道消息格式待实验资料及板端验证确认。
