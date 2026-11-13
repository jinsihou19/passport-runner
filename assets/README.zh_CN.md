<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 资源目录（Assets）

本目录集中存放可复用的资源（字库、图片、音乐、电池参数等），按资源类型分子目录管理。每个资源放在其类型对应的子目录，并记录放置路径、命名方式、集成方式与来源/许可。二进制资源（字体、图片、音频）不属于纯 markdown 文档，请勿与文档混放。涉及版权/授权的资源需注明来源与许可。

## 字库（fonts）

可复用的字库文件与生成的字库源码放在 `fonts/`。

- 命名要能反映字族、字重、字级与格式。
- 记录来源、许可、字符范围、转换命令与目标放置路径。
- 添加字库前评估 Flash 与内部 RAM 影响；ESP32-C3 无 PSRAM。
- 不提交许可不允许分发的字库。

应用管理器、ESP-Claw 和番茄钟屏幕使用 [passport_cjk_16.c](fonts/passport_cjk_16.c)，从 [Sans2.004](https://github.com/notofonts/noto-cjk/blob/523d033d6cb47f4a80c58a35753646f5c3608a78/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf) 的 Noto Sans CJK SC Regular 生成 16 px、4 bpp 子集，按 [SIL 开源字体许可证](fonts/passport_cjk_16.OFL.txt) 分发。字库覆盖可打印 ASCII 和 LCD 界面用到的中文字。LVGL 直接从 Flash 读取 24,057 字节的点阵数据。

使用 Pillow 12.1.1 和 fonttools 4.60.2，运行 `python3 tools/generate_passport_font.py --font /path/to/NotoSansCJKsc-Regular.otf` 重新生成。脚本检查源字体的字符映射，并逐像素校验打包后的字形。静态验证会将生成的子集与界面源码比对。

## 网页资源

管理器使用的 [HARICA 根证书](../main/runner/assets/harica_roots.pem) 取自 ESP-IDF 5.5.3 的完整证书包。
管理器将 2021 年 RSA 根证书和用于交叉签名的 2015 年根证书加入精简包，用于验证
MET Norway 天气接口。

[passport_import.mjs](../main/runner/assets/passport_import.mjs) 在浏览器中提取并检查社区应用。
[passport_hashes.mjs](../main/runner/assets/passport_hashes.mjs) 打包了
[@noble/hashes 2.0.1](https://github.com/paulmillr/noble-hashes/tree/v2.0.1)
的 MD5 和 SHA-256，脚本内附 MIT 许可。Passport 直接提供这两个脚本，局域网
HTTP 页面可以离线导入文件。

重新打包时，用 esbuild 0.25.12 处理一个模块，分别从 `@noble/hashes/legacy.js`
和 `@noble/hashes/sha2.js` 导出 `md5` 和 `sha256`。使用
`--bundle --minify --legal-comments=none --platform=browser --format=esm --target=es2020`
并在产物开头附上完整的 MIT 许可。固件构建直接嵌入仓库中的打包产物。

## 图片（images）

可复用的源图与生成的显示资产放在 `images/`。

| 文件 | 尺寸与格式 | 用途与来源 |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160，JPEG | 嵌入中英文项目 README 的产品主图，突出 AI Passport 产品形象与开放、人人可创作的理念。 |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724，PNG RGBA | 保留为可选技术参考图，不再用于首页主视觉。于 2026-09-17 使用内置图像生成工具为本仓库生成；已根据文档中的硬件能力契约核对图中的六项标签与参数。 |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336，PNG RGBA | 从仓库原始 `images/logo.png` 中精确裁切并去除背景的黑色字标；用于中英文项目 README 的浅色主题。 |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336，PNG RGBA | 提取字标的白色版本；README 使用 `<picture>` 在 GitHub 深色主题下显示。 |
| [`main/runner/assets/passport_wordmark.a8`](../main/runner/assets/passport_wordmark.a8) | 176 × 36，原始 A8 | LCD 字标。用 Pillow 12.1.1 的 `Image.Resampling.LANCZOS` 缩小 `logo-wordmark.png` 的 Alpha 通道，将 `tobytes()` 输出写入文件。固件将 6,336 字节的蒙版存入 Flash，由 LVGL 着色。 |

- 使用描述性命名，并记录尺寸、像素格式、转换步骤与目标路径。
- 优先采用适合 240 × 320 RGB565 显示的格式，并纳入 Flash 与内部 RAM 考量。
- 许可允许时保留可编辑源文件，并记录来源与许可。
- 图片中不得包含设备二维码秘密、凭证或个人数据。

## 音乐与音效（music）

可复用的音乐与音效源码放在 `music/`。

- 记录来源、许可、采样率、位深、声道、转换命令与目标路径。
- 与当前 BSP 音频路径匹配时优先采用 16 kHz、16 位单声道 PCM。
- 嵌入音频前评估 Flash 与内部 RAM 成本；长录音应流式或分块。
- 无再分发许可不提交媒体文件。

## 电池参数（battery）

[`battery/wanli-500mah-profile2-xil40x-20261020.txt`](battery/wanli-500mah-profile2-xil40x-20261020.txt)
保存万锂新能源 500 mAh 电芯的 CW2017 参数（profile2、XIL40X、2026-10-20）。
该文件是项目提供的 `喜梨_智能工卡_万锂新能源500mAh_profile2_XIL40X_20261020.txt`
的原样副本，按维护者要求保留用于本次固件更新；原文件没有附带单独的许可说明。

格式为 80 行两位十六进制数字，每行一个字节，按寄存器顺序排列。
BSP 在 `components/bsp/src/bsp_battery.c` 中嵌入这 80 字节，主机检查会验证
源文件及数组内容完全一致。文本文件 SHA-256：
`7ea96eba4634f4495a37c25ffacd54c1deaa0693b162c892d671a2ba68460919`。
解码后 80 字节 SHA-256：
`fa7365671093ef449c5bdcfb4adb279e3d275f888109f50b6b2f21e9b96220d5`。
这是电量计参数，不是充电配置，也不能通用于所有 500 mAh 电芯；详见
[硬件指南](../docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.zh_CN.md#9-cw2017-电池计)。
