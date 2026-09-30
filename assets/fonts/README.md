[简体中文](README.zh_CN.md)

# CSI Scope Chinese fonts

The Chinese device UI uses the application subsets `csi_han_12.c` and
`csi_han_14.c`: 236 printable glyphs at 12/14 px, 4 bpp, uncompressed.
All fixed Chinese text, punctuation and printable ASCII are included.
Router SSIDs entered on the phone are not rendered on the device; its generated
hotspot name, random password, and technical error codes use printable ASCII.
This subset does not support arbitrary Chinese text.

The input is Adobe Source Han Sans SC Regular, release 2.005R, licensed under
SIL OFL 1.1. Keep [OFL.txt](OFL.txt) with redistributed assets. Original source:
[Adobe's pinned font](https://github.com/adobe-fonts/source-han-sans/blob/2.005R/OTF/SimplifiedChinese/SourceHanSansSC-Regular.otf).
The OTF SHA-256 is `f1d8611151880c6c336aabeac4640ef434fa13cbfbf1ffe82d0a71b2a5637256`.

Generation uses official `lv_font_conv` 1.5.3. From the repository root:

```text
node tools/generate_csi_fonts.mjs /path/to/lv_font_conv/lv_font_conv.js
python tools/check_csi_fonts.py
```

The generator records every required code point in `csi-glyphs.txt` and passes
the exact sizes, range, format, and exported symbols to the converter. Main CMake
compiles both assets. Every Chinese label selects one of these fonts explicitly;
the large numeric score retains Montserrat 40. UTF-8 and font placeholders stay
enabled. Bitmap data is constant Flash data, not a full font loaded into RAM.

Host LVGL 9.5.0 checked every glyph in both fonts with `lv_font_get_glyph_dsc`,
requiring non-placeholder results. An absent U+9F98 negative case was also checked.
Live, subcarrier, no-data, and setup pages were rendered with simulated data.
The 24 KiB host LVGL pool retained 5768 free bytes (4992 largest block).
Physical Chinese rendering, error states and runtime heap with Wi-Fi are still
unverified until device acceptance.
