# SD 启动与网线升级

## 上电与观察

SD 卡留在 ZYBO Z7-20，选择 SD 启动，网线接实验电脑。板端每次启动配置 `169.254.155.221/16`，通过 Linux FPGA Manager 加载六路 PWM 位流，绑定两个 UIO 外设并启动 PS、PL 控制程序和 Bottle 服务。COM3 用于观察引导，服务自启无需电脑发送串口指令。

电脑运行 `host/open-monitor.ps1` 后，Edge 打开 [本机灯控台](http://127.0.0.1:8089/)。页面、脚本和样式均由电脑从项目目录提供。电脑服务通过网线向板端接口发送数字灯、RGB 开关和颜色指令，并转发签名固件。板端断电时本机页面仍可访问，硬件状态显示离线。

[启动监视页](http://127.0.0.1:8089/boot) 只读 COM3，波特率 115200。需要观察完整启动过程时，在开发板上电前启动电脑服务。电脑首次运行需执行 `python -m pip install --target build/host-python pyserial==3.5`。板端上线后，本机灯控台的「系统与固件」显示运行时间、内核、SD 卡、UIO 和 PS/PL 进程状态。

## 使用网页更新

在「系统与固件」选择本机 `build/sd/AIOT-LED.aiot`，点击「写入 SD 卡」。该包包含 Linux 内核、设备树、根文件系统、PL 位流、板端 C 程序和网页。开发板验证 RSA-2048 / SHA-256 签名，将 FIT 写入另一槽，回读 SHA-256 后切换 `uEnv.txt`。等网页提示完成，再重启或断电上电。

本机页面的「重启开发板」通过网线发起 PetaLinux 正常重启。电脑使用项目配对私钥签署本次 Linux 启动 ID，开发板验签、同步 SD 卡后重启。页面持续轮询，取得新的启动 ID 并确认 SD、UIO、PS 和 PL 全部就绪后显示恢复完成。物理断电后仍需接通开发板电源。启动后 LD0–LD3 跟随拨杆，LD4 和两路 RGB 默认关闭。

卡根目录中的 `aiot-a.ub` 与 `aiot-b.ub` 是项目启动槽。原始 `BOOT.BIN`、`image.ub` 保留，另有 `aiot-original/` 备份。板端不响应时，可在读卡器中将卡根的 `uEnv.txt` 改名为 `uEnv.disabled`，下次上电恢复原实验系统。已通过 FIT 校验但无法启动应用的版本需要人工回退。

## 断电验收

2026-11-27，A 槽首次重启后，签名包通过以太网写入 B 槽并通过 SHA-256 回读。用户关闭再打开电源，电脑监视器观察到新的 U-Boot 与灯控就绪消息。网页报告新的 Linux 启动 ID，SD 已挂载、UIO 已绑定，PS 和 PL 进程均就绪。测试期间没有在串口执行 IP 配置、UIO 绑定或启动服务命令。

本机控制台另经现场验收：通过 `127.0.0.1:8089` 设置 LD5 红、LD6 蓝并开启，用户确认两灯颜色正确。开发板再次上电后回到默认关灯状态。修改签名的固件经本机入口上传后收到 HTTP 400，SD 启动槽保持 B。

本次网页重启验收使用 A 槽固件 `2d1b1969a8b6`，SD 回读 SHA-256 为 `898a479ed40912bf7ad4f27a56ab2711502b0496ce3841d2a25c56f27be0ce9b`。网页发起重启后，同一标签页显示 PL 离线，随后自动显示灯控恢复。Linux 启动 ID 从 `b61f4644-3aab-485e-bfb7-9b472202e492` 变为 `781b145d-2049-4140-8c4d-ddd0ee9be9b9`。SW2 为 0 时，在网页接管 LD2 后回读为亮，恢复跟随拨杆后回读为灭。两次启动均通过数字灯及 RGB 接口回读测试，旧启动 ID 的重启请求返回 HTTP 403。

运行 `python tests/verify_console.py` 可从本机网页接口逐路测试 LD0–LD3 的开、关、跟随拨杆，以及 LD5、LD6 的颜色和使能回读。测试结束恢复原来的通道设置。RGB 的网页回读表示控制设定，现场灯色由观察或板上 PWM 采样核验。

## 构建

1. 在 Vivado 和 Xilinx SDK 中生成 [PL 位流和 ARM 程序](panel.md)。从 `build/artifacts/` 执行 `bootgen -arch zynq -image ../../hardware/network.bif -process_bitstream bin -w`。
2. 在项目根目录按[运行包说明](ld4.md)生成 `build/board.tgz`。Windows Python 需安装 `cryptography`。执行 `python device/sign-sd.py prepare` 生成或复用本机配对私钥。
3. 在对应的 PetaLinux 2019.1 环境运行 `sh device/build-sd.sh <images/linux目录> <内核recipe-sysroot-native/usr/bin目录>`，再执行 `python device/sign-sd.py sign`。
4. `build/sd/aiot.ub` 用于首次配置新卡；`build/sd/AIOT-LED.aiot` 用于已配对开发板的网络升级。私钥 `build/sd/signing-key.pem` 只保存在开发电脑，需自行备份。生成的系统镜像和签名密钥均由 Git 忽略。
