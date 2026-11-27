# AIOT-LED

ZYBO Z7-20 网页物联网电灯实验。目标是在浏览器控制板载 LED 和 RGB LED 亮度，并查看按键与拨码开关状态。

当前进度为仓库初始化。网页、板端程序和硬件工程均待实现，上板效果待验证。

| 目录 | 用途 |
| --- | --- |
| `web/` | 浏览器页面和交互脚本 |
| `server/` | Python Bottle 服务 |
| `device/` | 板端 Linux C 程序 |
| `hardware/` | Vivado 工程源文件、RTL 和约束 |
| `tests/` | 软件及跨模块验证 |
| `docs/` | 架构与实验记录 |

参与开发前请阅读[开发规范](AGENTS.md)和[架构说明](docs/architecture.md)。
