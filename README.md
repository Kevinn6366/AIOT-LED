# AIOT-LED

ZYBO Z7-20 网页物联网电灯实验。目标是在浏览器控制板载 LED 和 RGB LED 亮度，并查看按键与拨码开关状态。

电脑端运行 [本机灯控台](http://127.0.0.1:8089/)，通过网线访问 ZYBO。开发板支持 SD 上电自启和网络写入签名固件。启动观察、升级与回退见 [SD 启动说明](docs/sd-boot.md)。

SW0–SW3 默认直接控制 LD0–LD3。网页每半秒读取拨杆与输出，可逐路接管或恢复拨杆控制。LD4 使用 PS GPIO。LD5、LD6 各有独立的颜色选择器和开关，关闭时保留选色。详见 [PL 控制与接线](docs/panel.md)和 [PS 点灯说明](docs/ld4.md)。

[模型工作台](http://127.0.0.1:8089/llm) 提供 Qwen 中文问答与 ZyboGPT 英文续写。Qwen 可选择纯 PS 或 PL 全层矩阵后端。硬件覆盖、数据通路与同题对照见 [Qwen 全层矩阵后端](docs/qwen-matrices.md)。ARM 基线见[板端中文问答](docs/qwen.md)。

ZyboGPT 扩展在 PL 上运行量化字符模型，经本机网页提交英文提示词。构建方法和实板验收见 [ZyboGPT 英文续写](docs/zybogpt.md)。

| 目录 | 用途 |
| --- | --- |
| `web/` | 浏览器页面和交互脚本 |
| `server/` | Python Bottle 服务 |
| `device/` | 板端 Linux C 程序 |
| `hardware/` | Vivado 工程源文件、RTL 和约束 |
| `tests/` | 软件及跨模块验证 |
| `docs/` | 架构与实验记录 |

参与开发前请阅读[开发规范](AGENTS.md)和[架构说明](docs/architecture.md)。
