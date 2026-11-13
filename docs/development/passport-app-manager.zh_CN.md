[English](passport-app-manager.md) · **简体中文**

# Passport Runner 应用管理器

顶部状态栏显示 UTC+8 时间、Wi-Fi 状态和电量。首页三块依次显示标志或大时钟和天气、
应用数量与剩余空间、三个入口。上、下键移动选中项，短按确定键进入。长按上键可从
管理器子页面返回。

- **开发**显示网页地址和 8 位连接码，可从网页安装应用。需要时长按确定键开启配网热点。
- **应用**列出已安装的应用，包括上传后的 ESP-Claw。短按确定键打开，长按确定键管理。
- **设置**可切换首页标志或时钟、切换 Passport 绿和橘色、调节管理器亮度并打开 Wi-Fi
  配网页。默认用 Passport 绿。首页样式、主题色和亮度在重启后保留。

已安装的应用共用 4,160 KiB 的 Flash 存储区，单个应用还需要放进 2,368 KiB 的共用
启动区。网页显示总空间、已用空间、剩余空间、单个应用上限和各应用大小。

## 首页天气

在管理网页打开**首页天气**，搜索并保存城市。浏览器用 [Open-Meteo](https://open-meteo.com/en/docs/geocoding-api)
搜索城市，Passport 在首页用所选城市的坐标向 [MET Norway](https://api.met.no/weatherapi/locationforecast/2.0/documentation)
查询天气。气温以摄氏度取整，天气描述经翻译，数据使用 [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/)。
点击**关闭天气**会清除保存的城市。没有可用预报时，首页显示相应状态。

## 安装和运行番茄钟

1. 启动 Passport，选择**开发**。如果设备处于热点模式，加入屏幕显示的
   Wi-Fi 网络，热点密码使用同一页显示的 8 位连接码。
2. 在浏览器打开屏幕上的地址，输入同一个连接码。在**安装应用**上传
   `Passport-Pomodoro.bin`。也可以在 **Wi-Fi 设置**加入现有的 2.4 GHz 网络，
   配网后按设备屏幕上的新地址重新打开网页。
3. 长按上键回到首页，进入**应用**，选中**番茄钟**并按确定键。启动前可以用上、下键选择 15、25 或
   45 分钟。确定键用于开始、暂停和继续。专注结束后可以开始 5 分钟休息。
4. 在番茄钟或 ESP-Claw 里长按上键可返回应用管理。番茄钟会保存剩余时间，下次打开
   时保持暂停。重启后应用列表仍保留已安装的镜像。在网页列表点**移除**可释放该应用
   占用的空间，下一次上传可以使用这些空间。

打开网页安装的应用时，管理器先将它复制到共用启动区，再重启进入应用。工厂分区里的
管理器启用了 ESP-IDF 的 OTA 回退机制，选中的应用保持待确认状态。再次重启就会进入
管理器，应用失去响应时可以断电再开机。
切换时 Wi-Fi、ESP-Claw 设置和已保存的计时状态使用同一个 NVS 分区。每次启动都会
生成新的 8 位连接码。

## 在 Passport 上管理应用

用上、下键选中已安装的应用，短按确定键打开。长按确定键进入管理页，可查看应用大小
和剩余空间。选**删除应用**后，确认页默认选中**取消**。用上、下键选中**删除**并按
确定键，设备会删除应用并返回列表。刷新网页也能看到更新后的列表和剩余空间，以后可
重新上传原来的应用文件。

## 记住此浏览器

登录时输入连接码并勾选**记住此浏览器（90 天）**。之后用同一个浏览器打开同一个
地址即可自动登录，设备重启或切换到新版 ESP-Claw 后也能继续使用。配网热点和
局域网的地址不同，建议在常用的局域网地址配对一次。换浏览器或换地址时需要
重新配对。点击**退出**可取消当前配对。

设备最多保存八条配对记录，加入第九个浏览器时会替换最早的一条。90 天后或清除
站点数据后，浏览器会删除保存的登录信息。新配对和加入配网热点仍使用屏幕上的
8 位连接码。请在自己的浏览器和可信的局域网使用记住登录。

## 导入社区固件

从[玩法社区](https://ai-passport.folotoy.cn/plays/)下载原始合并 `.bin`。在管理网页的
**安装应用**卡片里展开**导入社区固件**，选择下载的文件。浏览器在本地检查并提取应用。
核对大小、版本和名称后，点**安装此应用**。

也可以在**从官网玩法链接导入**中粘贴[玩法详情页](https://ai-passport.folotoy.cn/plays/1471/)，
再点**下载并安装**。Passport 需连接可上网的 Wi-Fi。设备把官网的固件分块转发给浏览器；
浏览器按官网发布的 SHA-256 校验固件、提取应用，检查通过后自动安装。应用库只占用提取后
应用的空间。安装后可在设备的应用列表打开。

网页导入支持最多 8 MiB 的原始整包，要求分区表有效，并包含一个带 ESP-IDF SHA-256
尾部、未签名的 ESP32-C3 factory 应用。网页会拦下带额外分区或应用外数据的整包。
提取的应用需要放得进 2,368 KiB 的共用启动区，也需要设备有足够剩余空间。安装通过
表示镜像格式通过校验。社区应用保留原有按键操作，没有返回手势的应用可重启设备回到管理器。

也可以在项目目录用 Python 3 运行[本地提取工具](../../tools/extract_passport_app.py)：

```bash
python3 tools/extract_passport_app.py community-full.bin
python3 tools/extract_passport_app.py community-full.bin -o extracted.bin
python3 tools/extract_passport_app.py community-full.bin -o Passport-Pinball.bin --name Passport-Pinball
```

第一条列出应用分区和镜像大小。第二条原样提取应用。如果整包含有多个应用，用
`--partition 分区名` 指定。第三条设置镜像里的应用名，并更新校验和与 SHA-256，
生成可尝试上传的文件。工具保留原下载文件，也不会覆盖同名输出。

在管理网页的 **Passport 应用 .bin** 一栏上传用 `--name` 生成的文件。本地工具也可
查看和提取部分网页无法直接导入的整包。画面、按键和功能需要在 Passport 上单独测试。

## 代码目录

| 目录 | 内容 |
| --- | --- |
| `main/runner/` | Passport Runner 固件、应用安装与启动、管理网页、天气、专用配置和分区表 |
| `main/apps/esp-claw/` | ESP-Claw 应用、模型与 Lua 逻辑、专用网页 |
| `main/apps/pomodoro/` | 番茄钟应用和计时状态机 |
| `main/common/` | 共用持久化配置和中文字库的构建入口 |
| `main/common/network/` | Runner 与 ESP-Claw 共用的 Wi-Fi、浏览器配对和 HTTP 处理 |
| `components/bsp/` | 板级驱动和接口 |

根目录 CMake 通过 `PASSPORT_APP` 选择目标：`manager`（默认）、`claw` 或 `pomodoro`。
每个目标在自己的 `CMakeLists.txt` 中列出源码和资源。

## 构建和打包

激活 ESP-IDF 5.5.3 并运行 `./tools/validate.sh`。验证流程会构建三个应用并检查
[8 MiB 分区表](../../main/runner/partitions.apps.csv)。合并固件包含 `factory` 管理器、空白的
LittleFS 存储区 `app_store` 和留空的 `ota_0` 共用启动区 `app_run`。从 `0x0` 刷写
`build/FoloToy-AI-Passport-full.bin`。管理器对应的 ELF 在 `build/firmware/`。
验证流程会输出 `build/apps/` 下可单独上传的 `Passport-ESP-Claw.bin` 和
`Passport-Pomodoro.bin` 路径，同目录也保存应用的调试产物。

刷写合并固件会覆盖原来的 NVS 和应用存储区。已有设备使用相同的分区表时，只更新
`0x20000` 的管理器可保留 Wi-Fi、浏览器配对和已装应用。
[刷写和存储数据](engineering/firmware-layout.zh_CN.md)

## 开发应用

在电脑上用 ESP-IDF 5.5.3，按 Passport 的布局和 BSP 构建独立的 ESP32-C3 应用镜像。项目名以
`Passport-` 开头，后缀可用 ASCII 字母、数字、连字符或下划线，总长度最多 31 个字符。
上传带 ESP-IDF 镜像 SHA-256 尾部的应用 `.bin`，管理器会在加入列表前校验目标、大小和
完整镜像。应用应保持 OTA 待确认状态，返回管理器的按键调用 `esp_restart()`。原生应用
具备设备固件的权限，请安装自己信任的镜像。
