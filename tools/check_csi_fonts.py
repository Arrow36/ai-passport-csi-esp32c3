"""Check that fixed Chinese UI text and generated font inventories stay aligned.

This static check complements, but does not replace, LVGL glyph/render checks.
"""
from pathlib import Path
import re

root = Path(__file__).resolve().parent.parent
source = "".join((root / name).read_text(encoding="utf-8") for name in ("main/csi_ui.c", "main/csi_scope.c"))
required = set(range(32, 127)) | {ord(c) for c in source if ord(c) > 127}
inventory = {int(line[2:], 16) for line in (root / "assets/fonts/csi-glyphs.txt").read_text().splitlines()}
if required != inventory:
    raise SystemExit("CSI glyph inventory changed; regenerate the fonts")
for size in (12, 14):
    font = (root / f"assets/fonts/csi_han_{size}.c").read_text(encoding="utf-8")
    generated = {int(cp, 16) for cp in re.findall(r'/\* U\+([0-9A-Fa-f]+) ', font)}
    if generated != required:
        raise SystemExit(f"Font {size}: unexpected/missing glyphs {generated ^ required}")
print(f"CSI font inventory: PASS ({len(required)} glyphs at 12/14 px)")
