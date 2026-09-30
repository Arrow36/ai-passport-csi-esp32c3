[English](README.md)

# CSI Scope 中文字库

设备中文界面使用 `csi_han_12.c` 和 `csi_han_14.c` 应用子集：共 236 个
可打印字符，12/14 像素字号、4 bpp、不压缩。包含所有固定中文、标点和
可打印 ASCII。手机输入的路由器名称不显示在设备上；自动生成的热点名称、
随机密码和技术错误码使用 ASCII。这个子集不支持任意中文文本。

源字体为 Adobe 思源黑体简体中文 Regular 2.005R，许可是 SIL OFL 1.1。
再分发资源时保留 [OFL.txt](OFL.txt)。字体来自
[Adobe 官方固定版本](https://github.com/adobe-fonts/source-han-sans/blob/2.005R/OTF/SimplifiedChinese/SourceHanSansSC-Regular.otf)。
OTF 的 SHA-256：`f1d8611151880c6c336aabeac4640ef434fa13cbfbf1ffe82d0a71b2a5637256`。

使用官方 `lv_font_conv` 1.5.3 生成。在仓库根目录执行：

```text
node tools/generate_csi_fonts.mjs /path/to/lv_font_conv/lv_font_conv.js
python tools/check_csi_fonts.py
```

生成脚本把全部字符码点写入 `csi-glyphs.txt`，并明确指定字号、范围、格式和
导出符号。main 的 CMake 编译两份字库；每个中文标签都显式选择相应字库，
大号数字继续使用 Montserrat 40。启用 UTF-8 和缺字占位符。位图作为常量
位于 Flash，不把整个字体加载到内存。

主机 LVGL 9.5.0 已通过 `lv_font_get_glyph_dsc` 检查两种字库的全部字符，
要求返回真实字形而非占位符，并检查不存在的 U+9F98 作为反例。已使用模拟数据
渲染实时、子载波、无数据及配网页面。24 KiB 主机 LVGL 池剩余 5768 字节，
最大空闲块 4992 字节。实际设备上的中文字形、错误状态及 Wi-Fi 工作时的堆
内存仍需实机验收。
