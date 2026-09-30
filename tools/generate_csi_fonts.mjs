// Regenerate the fixed UI subset with lv_font_conv 1.5.3.
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const converter = process.argv[2];
if (!converter) throw new Error('Pass the path to lv_font_conv.js (version 1.5.3).');
const sources = ['main/csi_ui.c', 'main/csi_scope.c'].map(p => fs.readFileSync(path.join(root, p), 'utf8')).join('');
const codepoints = [...new Set([...sources].filter(c => c.codePointAt(0) > 127).map(c => c.codePointAt(0)))].sort((a, b) => a - b);
const symbols = codepoints.map(c => String.fromCodePoint(c)).join('');
const inventory = [...Array.from({length:95}, (_,i) => i+32), ...codepoints];
fs.writeFileSync(path.join(root, 'assets/fonts/csi-glyphs.txt'), inventory.map(c => `U+${c.toString(16).toUpperCase().padStart(4,'0')}`).join('\n')+'\n');
for (const size of [12, 14]) {
    const result = spawnSync(process.execPath, [path.resolve(converter), '--font', 'assets/fonts/SourceHanSansSC-Regular.otf',
        '--range', '0x20-0x7E', '--symbols', symbols, '--size', String(size), '--bpp', '4', '--format', 'lvgl', '--no-compress',
        '--lv-font-name', `csi_han_${size}`, '--lv-include', 'lvgl.h', '--output', `assets/fonts/csi_han_${size}.c`], {cwd:root, stdio:'inherit'});
    if (result.status !== 0) process.exit(result.status || 1);
}
console.log(`Generated ${inventory.length} glyphs at 12 and 14 pixels.`);
