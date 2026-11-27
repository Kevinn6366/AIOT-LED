# SD 启动与网线升级

## 上电与观察

SD 卡留在 ZYBO Z7-20，选择 SD 启动，网线接实验电脑。板端每次启动配置 `169.254.155.221/16`，通过 Linux FPGA Manager 加载六路 PWM 位流，绑定两个 UIO 外设并启动 PS、PL 控制程序和 Bottle 服务。COM3 用于观察引导，服务自启无需电脑发送串口指令。

在电脑上运行 `host/open-monitor.ps1` 打开 [启动监视页](http://127.0.0.1:8089/)。监视程序只读 COM3，波特率 115200。需要同时观察 U-Boot 和 Linux 启动消息时，在上电前打开该页。电脑首次运行需执行 `python -m pip install --target build/host-python pyserial==3.5`。板端上线后，可以在[灯控页面](http://169.254.155.221:8080/)的「系统与固件」查看运行时间、内核、SD 卡、UIO 和 PS/PL 进程状态。

## 使用网页更新

在「系统与固件」选择本机 `build/sd/AIOT-LED.aiot`，点击「写入 SD 卡」。该包包含 Linux 内核、设备树、根文件系统、PL 位流、板端 C 程序和网页。开发板验证 RSA-2048 / SHA-256 签名，将 FIT 写入另一槽，回读 SHA-256 后切换 `uEnv.txt`。等网页提示完成，再重启或断电上电。

卡根目录中的 `aiot-a.ub` 与 `aiot-b.ub` 是项目启动槽。原始 `BOOT.BIN`、`image.ub` 保留，另有 `aiot-original/` 备份。板端不响应时，可在读卡器中将卡根的 `uEnv.txt` 改名为 `uEnv.disabled`，下次上电恢复原实验系统。已通过 FIT 校验但无法启动应用的版本需要人工回退。

## 构建

1. 在 Vivado 和 Xilinx SDK 中生成 [PL 位流和 ARM 程序](panel.md)。从 `build/artifacts/` 执行 `bootgen -arch zynq -image ../../hardware/network.bif -process_bitstream bin -w`。
2. 在项目根目录按[运行包说明](ld4.md)生成 `build/board.tgz`。Windows Python 需安装 `cryptography`。执行 `python device/sign-sd.py prepare` 生成或复用本机配对私钥。
3. 在对应的 PetaLinux 2019.1 环境运行 `sh device/build-sd.sh <images/linux目录> <内核recipe-sysroot-native/usr/bin目录>`，再执行 `python device/sign-sd.py sign`。
4. `build/sd/aiot.ub` 用于首次配置新卡；`build/sd/AIOT-LED.aiot` 用于已配对开发板的网络升级。私钥 `build/sd/signing-key.pem` 只保存在开发电脑，需自行备份。生成的系统镜像和签名密钥均由 Git 忽略。
