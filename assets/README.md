<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Assets

This directory stores reusable fonts, images, music, sound effects, and battery profiles, organized by asset type.

Keep each asset in the matching subdirectory and document its destination, naming, integration method, and source/license. Do not mix binary assets with Markdown documentation.

## Fonts

Store reusable font files and generated font sources in `fonts/`.

- Use descriptive names that include the family, weight, size, and format when relevant.
- Document the source, license, character range, conversion command, and expected destination.
- Check Flash and internal-RAM impact before adding a font; the ESP32-C3 has no PSRAM.
- Do not commit fonts whose license does not permit redistribution.

The app manager, ESP-Claw, and Pomodoro screens use [passport_cjk_16.c](fonts/passport_cjk_16.c), a 16 px, 4 bpp subset of Noto Sans CJK SC Regular from [Sans2.004](https://github.com/notofonts/noto-cjk/blob/523d033d6cb47f4a80c58a35753646f5c3608a78/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf) under the [SIL Open Font License](fonts/passport_cjk_16.OFL.txt). It covers printable ASCII, the Chinese characters in the LCD UI, and the Gold Miner community title. LVGL reads the 25,273 bytes of bitmap data from Flash.

Regenerate with Pillow 12.1.1 and fonttools 4.60.2 using `python3 tools/generate_passport_font.py --font /path/to/NotoSansCJKsc-Regular.otf`. The generator checks the source cmap and verifies the packed glyphs pixel by pixel. The static gate checks the generated subset against the UI source.

## Web assets

The manager's [HARICA root certificates](../main/runner/assets/harica_roots.pem) come from
ESP-IDF 5.5.3's full certificate bundle. The manager adds the 2021 RSA root
and its 2015 cross-signing root to its smaller bundle for the MET Norway
weather API.

[passport_import.mjs](../main/runner/assets/passport_import.mjs) extracts and checks community
apps in the browser. [passport_hashes.mjs](../main/runner/assets/passport_hashes.mjs) bundles
the MD5 and SHA-256 exports of [@noble/hashes 2.0.1](https://github.com/paulmillr/noble-hashes/tree/v2.0.1)
under its embedded MIT license. The script is served by the Passport so
the importer works on the local HTTP page without an external service.

To regenerate the bundle, use esbuild 0.25.12 with a module that exports
`md5` from `@noble/hashes/legacy.js` and `sha256` from `@noble/hashes/sha2.js`.
Use `--bundle --minify --legal-comments=none --platform=browser --format=esm --target=es2020`
and prepend the package's complete MIT notice. Firmware builds embed the
checked-in bundle directly.

## Images

Store reusable source images and generated display assets in `images/`.

| File | Dimensions and format | Use and source |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160, JPEG | Product hero image embedded in both project README files to foreground AI Passport and its open, maker-oriented identity. |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724, PNG RGBA | Optional technical infographic retained as a reference asset; it is no longer used as the homepage hero. Generated for this repository with the built-in image generation tool on 2026-09-17; the six labels and values were checked against the documented hardware contract. |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336, PNG RGBA | Transparent black wordmark extracted from the repository's original `images/logo.png`; embedded in both project README files for light backgrounds. |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336, PNG RGBA | White version of the extracted wordmark, used by the README `<picture>` element when GitHub is in dark mode. |
| [`main/runner/assets/passport_wordmark.a8`](../main/runner/assets/passport_wordmark.a8) | 176 × 36, raw A8 | LCD wordmark. Resize `logo-wordmark.png`’s alpha channel with Pillow 12.1.1 (`Image.Resampling.LANCZOS`) and write the channel’s `tobytes()` output. The firmware stores the 6,336-byte mask in Flash and tints it in LVGL. |

- Use descriptive names and document dimensions, pixel format, conversion steps, and destination.
- Prefer formats suitable for the 240 × 320 RGB565 display and account for Flash and internal RAM.
- Preserve editable sources where licensing permits, and record the source and license.
- Never commit device QR secrets, credentials, or personal data in images.

## Music and sound effects

Store reusable music and sound-effect sources in `music/`.

- Document the source, license, sample rate, bit depth, channels, conversion command, and destination.
- Prefer 16 kHz, 16-bit mono PCM when it matches the current BSP audio path.
- Check Flash and internal-RAM cost before embedding audio; stream or chunk long recordings.
- Do not commit media without redistribution permission.

## Battery profiles

[`battery/wanli-500mah-profile2-xil40x-20261020.txt`](battery/wanli-500mah-profile2-xil40x-20261020.txt)
contains the supplied CW2017 profile for the Wanli New Energy 500 mAh cell
(profile2, XIL40X, 2026-10-20). It is an unchanged copy of the project-supplied
file, retained at the maintainer's request for this firmware update. Its
original filename is recorded in the paired Chinese document. No separate
license was included in the supplied file.

The format is 80 lines of two hexadecimal digits, one byte per line in register
order. The BSP embeds those bytes in `components/bsp/src/bsp_battery.c`; the
host gate checks the source file and initializer for an exact match. Text-file
SHA-256: `7ea96eba4634f4495a37c25ffacd54c1deaa0693b162c892d671a2ba68460919`.
Decoded 80-byte SHA-256:
`fa7365671093ef449c5bdcfb4adb279e3d275f888109f50b6b2f21e9b96220d5`.
This is a fuel-gauge profile, not a charging configuration or a generic profile
for every 500 mAh cell; see the [hardware guide](../docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md#9-cw2017-fuel-gauge).
