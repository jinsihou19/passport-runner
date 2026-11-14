[English](esp-claw-passport.md) · **简体中文**

# AI Passport 上的 ESP-Claw

Passport 在 ESP32-C3 上运行 ESP-Claw 的 Agent 循环，通过 OpenAI 兼容的 Chat Completions 接口请求模型。移植保留上游工具注册和事件路由，显示、按键和电量沿用 Passport BSP。ESP-Claw 作为应用独立运行，使用片内内存。

## 连接和对话

1. 在[应用管理器](passport-app-manager.zh_CN.md)网页上传 `Passport-ESP-Claw.bin`，然后在设备上选择 ESP-Claw。如果连接页显示热点，用屏幕上 8 位连接码作为 Wi-Fi 密码加入。打开设备显示的 HTTP 地址，在浏览器输入同一个连接码。长按上键返回应用管理。
2. 在**设备设置**里填写模型基地址（通常以 `/v1` 结尾）、模型名以及所需的 API 密钥。请选支持 Chat Completions 工具调用的模型和接口。设备会在基地址后追加 `/chat/completions`。HTTPS 使用 ESP-IDF 的证书包验证服务器证书。网络未能校时时，已配对的浏览器会在首次 HTTPS 请求时提供时间。可信局域网里的自托管服务可以指定私有 IPv4 地址使用 HTTP。
3. 点**扫描附近的 Wi-Fi**选择 2.4 GHz 网络，也可手动填写名称。输入密码后点**连接网络**。ESP-Claw 与 Runner 共用两组已保存的网络和自动回退功能。切换后，将手机或电脑接入设备所在的网络，打开屏幕显示的地址。设备重启后会生成新的连接码。[配置 Wi-Fi](passport-app-manager.zh_CN.md#配置-wi-fi)
4. 在**对话**里发送指令。模型可以读取设备状态、调背光、执行 Lua，或把脚本绑定到确定键。设备在内存中保留最近一轮成功对话作为上下文，点**新会话**可以清除。

登录时勾选**记住此浏览器（90 天）**，再打开同一地址即可自动登录。新版 ESP-Claw
和应用管理器共用配对记录。[记住此浏览器](passport-app-manager.zh_CN.md#记住此浏览器)

设备网页通过本地 HTTP 访问。请在设备的加密配网热点或可信局域网里配置模型密钥。Wi-Fi 凭据、模型配置和确定键脚本保存在设备的 NVS。网页接口只返回模型密钥是否已保存。

## 屏幕和按键

连接页显示网络状态、浏览器地址和连接码。助手页显示模型是否就绪和已保存脚本的状态。短按**上键**或**下键**切页。设备空闲时，在连接页长按**确定键**重新开启热点。在助手页短按**确定键**运行已保存脚本。网页的**运行一次**走同一条事件路由。可在**设备设置**里删除脚本或清除已保存的 Wi-Fi。

可以试试「把确定键绑定为将屏幕亮度设为 35%」。脚本支持 Lua 语法和 `passport.battery()`（返回电量百分比或 nil）、`passport.backlight(percent)`（10–100）。脚本上限为 768 字节 UTF-8 文本、16 KiB Lua 分配和 10,000 条指令。设备一次处理一个请求，中文对话正文显示在浏览器里。LCD 字库覆盖固定中文界面和连接信息用到的 ASCII 字符。

## 构建和验证

激活 ESP-IDF 5.5.3 后运行 `./tools/validate.sh`。验证流程会构建[管理器和应用](passport-app-manager.zh_CN.md#构建和打包)、检查输入边界和字库覆盖，生成可从 `0x0` 刷写的 `build/FoloToy-AI-Passport-full.bin`。ESP-Claw 的 ELF 和 MAP 与应用镜像一同归档在 `build/apps/`。[固件布局和存储数据](engineering/firmware-layout.zh_CN.md)

采用的 ESP-Claw 版本和 Apache 许可见[上游声明](../../third_party/esp-claw/NOTICE)。中文子集从 Noto Sans CJK SC Regular 生成。 [字体资源和生成方式](../../assets/README.zh_CN.md)

C3 移植从 Flash 加载内置事件规则，脚本保存在 NVS。模型响应大小、工具调用数量和输出缓冲区都按片内内存设了上限。模型请求拒绝重定向，凭据只发送给已配置的端点。串口日志省略对话正文和工具参数及结果。
