# 板端中文问答

本机控制台提交单轮中文问题，Bottle 把请求转给开发板回环地址上的 llama.cpp。Qwen2.5-0.5B-Instruct 在 PS 双核 Cortex-A9 上计算，使用 512 token 上下文、最多 80 token 回答和两条计算线程。英文续写继续使用 PL 中的 ZyboGPT。

## 从源码构建

Windows 安装 CMake、Ninja 和 Xilinx SDK 2019.1，在仓库根目录运行：

```powershell
./device/build-qwen.ps1 -Ninja 'ninja.exe'
```

脚本固定 llama.cpp b3948，替换 ggml 针对其他 ARM 板的浮点指令选项，生成 `build/artifacts/llama-server.elf`。Ninja 不在 PATH 时可传可执行文件的完整路径。[llama.cpp 源码](https://github.com/ggml-org/llama.cpp/tree/55e47786e373c90fc7803e718e3e1dd6d53c3db6)

## SD 卡与启动

将官方 Q4_0 权重保存为 SD 卡第一分区的 `qwen/qwen.gguf`。本次通过板端有线网口写入临时文件，核对 SHA-256 后改名。文件大小 428730208 字节，SHA-256 为 `7671c0c304e6ce5a7fc577bcb12aba01e2c155cc2efd29b2213c95b18edaf6ed`。[官方权重](https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF/blob/main/qwen2.5-0.5b-instruct-q4_0.gguf)

按项目 SD 流程打包含 ZyboGPT 的镜像。S98aiot 在灯控启动后加载卡上权重，日志位于板端 `/tmp/aiot-led/qwen.log`。访问本机控制台的「中文问答」提问。运行 `python tests/verify_chat.py` 可从相同网页接口检查中文回答、首字延迟和板端生成速度。

## ZYBO Z7-20 实测

| 网页输入 | 板端原始回答 | 首字 | 生成速度 |
| --- | --- | ---: | ---: |
| 中国的首都是哪里？请只回答城市名。 | 北京 | 37.4 秒 | 1.24 token/秒 |
| 一盏红灯加两盏绿灯，一共有几盏灯？ | 一盏红灯加两盏绿灯，一共有三盏灯。 | 22.6 秒 | 0.66 token/秒 |

第一题加载完成后首次提问，第二题复用了相同的系统提示前缀。首字计时包含提示词计算。第一题仅生成两个 token，长回答的速度参考第二题。
