# 网页控制 LD4

本次选用 PS 侧 LD4（MIO7，高电平点亮）。引脚对应关系见 [Digilent ZYBO Z7 参考手册第 13 节](https://digilent.com/reference/_media/reference/programmable-logic/zybo-z7/zybo-z7_rm.pdf)。该通路使用已有 Linux 启动映像，不需要新增 PL 逻辑。

浏览器点击开灯或关灯后，`main.js` 向 `POST /api/led` 发送 JSON，例如 `{"on":true}`。Bottle 向命名管道写入一行 `1` 或 `0`。C 程序读取命令并写入 LD4 的 GPIO value 节点。HTTP 202 表示管道接受命令，503 表示管道不可用。

## 构建与启动

1. 在项目根目录创建 `build/`。用 Xilinx SDK 2019.1 的 ARM Linux 编译器执行：

   ```sh
   arm-linux-gnueabihf-gcc -std=gnu11 -O2 -Wall -Wextra device/led_control.c -o build/led-control
   ```

2. 将 `build/led-control`、`server/`、`web/` 放在板端同一项目目录下并保留相对路径。Python 环境须能导入 `server/requirements.txt` 指定的 Bottle。
3. 在板端查看 `/sys/class/gpio/gpiochip*/label` 和对应的 `base`，确认 Zynq PS GPIO 控制器后，将其 base 加 7 得到 LD4 的 Linux GPIO 编号。核实 MIO7 可作 GPIO 输出后，用该编号设置环境变量 `LD4_GPIO`。在项目目录运行：

   ```sh
   : "${LD4_GPIO:?请先确认 PS GPIO 编号}"
   pin=/sys/class/gpio/gpio${LD4_GPIO}
   [ -d "$pin" ] || echo "$LD4_GPIO" > /sys/class/gpio/export
   echo low > "$pin/direction"
   mkdir -p -m 700 /tmp/aiot-led
   ./build/led-control /tmp/aiot-led/commands "$pin/value" &
   python3 server/app.py --fifo /tmp/aiot-led/commands
   ```

4. 在同一网络的浏览器打开 `http://开发板IP:8080/`，依次点击开灯、关灯并观察 LD4。

## 主机测试

在 Linux 系统的项目根目录安装 Bottle 后运行：

```sh
mkdir -p build/test
gcc -std=gnu11 -Wall -Wextra -Werror device/led_control.c -o build/test/led-control
PYTHONPATH=. python3 tests/test_led.py
```

测试使用真实 HTTP 请求、命名管道和 C 进程，用临时文件代替 GPIO value，分别验证开、关和错误响应。

## 实板验证（2026-11-27）

ZYBO Z7-20 使用现有 Linux 4.19.0-xilinx-v2019.1 映像。串口核对到 `zynq_gpio` 基址为 902，LD4 使用 GPIO 909。浏览器经有线网口访问板端 8080 端口，依次点击开、关、开。串口读取 GPIO Bank 0 的 DATA 输出寄存器依次得到 `0x0000FE81`、`0x0000FE01`、`0x0000FE81`，对应位 7 为 1、0、1。此次读到的 sysfs value 在开和关时都为 0，故以输出寄存器记录命令执行结果。

该映像未预装 Python。本次使用同版本 PetaLinux 构建中的 ARM Python 3.5.6 和 Bottle 0.13.4，在板端临时目录运行。复现时，在仓库根目录将 Bottle 安装到 `build/python`，再执行 `sh device/package-runtime.sh /路径/到/build/tmp`。可通过环境变量 `RPM2CPIO` 指定构建环境的 rpm2cpio。将生成的 `build/board.tgz` 解压到板端 `/tmp/aiot-led`，切换到该目录执行：

```sh
chmod +x build/python-arm build/led-control
export LD_LIBRARY_PATH="$PWD/lib" PYTHONHOME="$PWD" PYTHONPATH="$PWD/build/python"
# 按上文核实并配置 LD4 的 GPIO、启动 C 程序后运行：
build/python-arm server/app.py --fifo /tmp/aiot-led/commands --host "$BOARD_IP"
```

`BOARD_IP` 为板端有线网口已配置的 IPv4 地址。本次临时地址及运行目录均在板端重启后失效。
