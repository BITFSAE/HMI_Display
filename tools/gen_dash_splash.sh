#!/usr/bin/env bash
# 把 tools/assets/BITFSAE.svg(字标+方格旗)和 silver-shark-sky.svg(鲨鱼)栅格化为开屏动画用的 A8 位图,
# 输出 stm32_port/Core/Src/dash_splash_data.c。每个精灵再切成若干水平条带, 条带只存各自的有效列范围:
# 斜体字母的包围盒约一半是空的, 整块存 A8 要多占约 40% Flash; 条带仍是 A8, 由 LVGL 直接从 Flash 做遮罩绘制, 不占 RAM。
# 依赖 Node.js; @resvg/resvg-js 与 gen_dash_logo.sh 共用缓存目录。
# 用法: tools/gen_dash_splash.sh [字标宽度px, 默认420] [鲨鱼高度px, 默认120]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WM_WIDTH="${1:-420}"
SHARK_HEIGHT="${2:-120}"
CACHE="${XDG_CACHE_HOME:-$HOME/.cache}/hmi-dash-logo"
mkdir -p "$CACHE"
[ -d "$CACHE/node_modules/@resvg/resvg-js" ] || npm install --prefix "$CACHE" --no-save --silent @resvg/resvg-js@2
NODE_PATH="$CACHE/node_modules" node - "$ROOT/tools/assets/BITFSAE.svg" "$ROOT/tools/assets/silver-shark-sky.svg" \
    "$WM_WIDTH" "$SHARK_HEIGHT" "$ROOT/stm32_port/Core/Src/dash_splash_data.c" <<'JS'
const {Resvg} = require('@resvg/resvg-js');
const fs = require('fs');
const [wmSvg, sharkSvg, wmWidth, sharkHeight, out] = process.argv.slice(2);

const SCREEN_W = 800, SCREEN_H = 480;
const ALPHA_MIN = 4;     // 低于此覆盖度的像素视为透明: 肉眼不可见, 却会撑大条带
const STRIP_COST = 48;   // 每个条带的固定代价(字节): 28 B 描述符 + 一次绘制调用的开销; 在 24..64 内总体积几乎不变, 取大值少画几次
const GAP = 32;          // 鲨鱼与字标之间的间距
// 字标 13 个子路径 → 精灵。顺序即 dash_splash_data.h 的 SPLASH_SPR_*。
const GROUPS = [['B',[1]], ['I',[4]], ['T',[5,9]], ['F',[10,6]], ['S',[7]], ['A',[3]], ['E',[11,8]], ['FLAG',[0,2,12]]];

function alphaOf(svg, width){
  const img = new Resvg(svg, {fitTo: {mode: 'width', value: width}}).render();
  const px = img.pixels;                                   // getter 每次取值都会整块复制, 只取一次
  const a = new Uint8Array(img.width * img.height);
  for(let i = 0; i < a.length; i++) a[i] = px[i * 4 + 3] < ALPHA_MIN ? 0 : px[i * 4 + 3];
  return {w: img.width, h: img.height, a};
}
// 裁到非零像素的包围盒; x0/y0 是它在原画布里的位置
function crop(s){
  let x0 = s.w, y0 = s.h, x1 = -1, y1 = -1;
  for(let y = 0; y < s.h; y++) for(let x = 0; x < s.w; x++) if(s.a[y * s.w + x]){
    if(x < x0) x0 = x; if(x > x1) x1 = x; if(y < y0) y0 = y; if(y > y1) y1 = y;
  }
  const w = x1 - x0 + 1, h = y1 - y0 + 1, a = new Uint8Array(w * h);
  for(let y = 0; y < h; y++) a.set(s.a.subarray((y0 + y) * s.w + x0, (y0 + y) * s.w + x0 + w), y * w);
  return {x0, y0, w, h, a};
}
// 动态规划切条带: 最小化 Σ(条带有效宽 × 条带高 + STRIP_COST); 全空的行不占条带
function stripsOf(s){
  const lo = [], hi = [];
  for(let y = 0; y < s.h; y++){
    let a = -1, b = -1;
    for(let x = 0; x < s.w; x++) if(s.a[y * s.w + x]){ if(a < 0) a = x; b = x; }
    lo.push(a); hi.push(b);
  }
  const dp = new Array(s.h + 1).fill(Infinity), from = new Array(s.h + 1).fill(0);
  dp[0] = 0;
  for(let j = 1; j <= s.h; j++){
    if(lo[j - 1] < 0){ dp[j] = dp[j - 1]; from[j] = -1; }
    let mn = Infinity, mx = -1;
    for(let i = j - 1; i >= 0 && j - i <= 255; i--){
      if(lo[i] >= 0){ mn = Math.min(mn, lo[i]); mx = Math.max(mx, hi[i]); }
      if(mx < 0) continue;
      if(mx - mn + 1 > 255) break;
      const c = dp[i] + (mx - mn + 1) * (j - i) + STRIP_COST;
      if(c < dp[j]){ dp[j] = c; from[j] = i + 1; }          // 存 i+1, 0 留给"跳过空行"
    }
  }
  const bands = [];
  for(let j = s.h; j > 0;){
    if(from[j] === -1){ j--; continue; }
    const i = from[j] - 1;
    let top = i, bottom = j;                                // 去掉带内首尾的空行
    while(lo[top] < 0) top++;
    while(lo[bottom - 1] < 0) bottom--;
    let mn = Infinity, mx = -1;
    for(let y = top; y < bottom; y++) if(lo[y] >= 0){ mn = Math.min(mn, lo[y]); mx = Math.max(mx, hi[y]); }
    const w = mx - mn + 1, h = bottom - top, data = new Uint8Array(w * h);
    for(let y = 0; y < h; y++) data.set(s.a.subarray((top + y) * s.w + mn, (top + y) * s.w + mn + w), y * w);
    bands.unshift({x: mn, y: top, w, h, data});
    j = i;
  }
  return bands;
}

// 字标: 每组在整幅字标坐标系里单独栅格化, 保留相对位置
const wm = fs.readFileSync(wmSvg, 'utf8');
const paths = [...wm.matchAll(/<path d="([^"]*)"/g)].map(m => m[1]);
if(paths.length !== 13) throw new Error(`BITFSAE.svg 应有 13 个子路径(字母/方格旗分组按序号写死), 实际 ${paths.length}`);
const head = wm.slice(0, wm.indexOf('<path'));
const sprites = GROUPS.map(([name, idx]) => {
  const c = crop(alphaOf(head + idx.map(i => `<path d="${paths[i]}"/>`).join('') + '</g></svg>', +wmWidth));
  return {name, ...c};
});
// 鲨鱼: 先按 1:1 量图形高度, 再按目标高度栅格化
const shark = fs.readFileSync(sharkSvg, 'utf8').replace(/ width="\d+" height="\d+"/, '');
const viewW = +shark.match(/viewBox="[-\d.]+ [-\d.]+ ([\d.]+)/)[1];
const probe = crop(alphaOf(shark, Math.round(viewW)));
const sharkSprite = {name: 'SHARK', ...crop(alphaOf(shark, Math.round(viewW * +sharkHeight / probe.h)))};

// 版面: 鲨鱼在上、字标在下, 水平居中, 整体略偏上
const minX = Math.min(...sprites.map(s => s.x0)), minY = Math.min(...sprites.map(s => s.y0));
const wmW = Math.max(...sprites.map(s => s.x0 + s.w)) - minX, wmH = Math.max(...sprites.map(s => s.y0 + s.h)) - minY;
const top = Math.round((SCREEN_H - (sharkSprite.h + GAP + wmH)) / 2) - 6;
sharkSprite.x = Math.round((SCREEN_W - sharkSprite.w) / 2); sharkSprite.y = top;
const left = Math.round((SCREEN_W - wmW) / 2), wmTop = top + sharkSprite.h + GAP;
for(const s of sprites){ s.x = left + s.x0 - minX; s.y = wmTop + s.y0 - minY; }
const all = [sharkSprite, ...sprites];                       // 与 SPLASH_SPR_* 同序: 鲨鱼, B I T F S A E, 方格旗

let dataBytes = 0, stripCount = 0, naive = 0;
const body = [], tables = [], rows = [];
all.forEach((s, i) => {
  const bands = stripsOf(s);
  naive += s.w * s.h; stripCount += bands.length;
  bands.forEach((b, k) => {
    dataBytes += b.data.length;
    const lines = [];
    for(let p = 0; p < b.data.length; p += 16) lines.push('    ' + [...b.data.subarray(p, p + 16)].map(v => '0x' + v.toString(16).padStart(2, '0')).join(', ') + ',');
    body.push(`static const uint8_t strip_${i}_${k}[] = {\n${lines.join('\n')}\n};`);
  });
  tables.push(`static const splash_strip_t strips_${i}[] = {\n` +
    bands.map((b, k) => `    {${b.x}, ${b.y}, {.header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_A8, .w = ${b.w}, .h = ${b.h}, .stride = ${b.w}}, .data_size = ${b.data.length}, .data = strip_${i}_${k}}},`).join('\n') + '\n};');
  rows.push(`    {strips_${i}, ${bands.length}, ${s.x}, ${s.y}, ${s.w}, ${s.h}},  /* ${s.name} */`);
  console.log(`${s.name.padEnd(5)} ${s.w}x${s.h} at (${s.x},${s.y})  ${bands.length} strips, ${bands.reduce((a, b) => a + b.data.length, 0)} B (bbox ${s.w * s.h} B)`);
});
const descBytes = stripCount * 28;
fs.writeFileSync(out, `/* Generated by tools/gen_dash_splash.sh from tools/assets/BITFSAE.svg and silver-shark-sky.svg, do not edit.
 * 字标宽 ${wmWidth} px, 鲨鱼高 ${sharkHeight} px; A8 条带 ${dataBytes} B + 描述符 ${descBytes} B (整块包围盒需 ${naive} B)。 */
#include "dash_splash_data.h"

${body.join('\n\n')}

${tables.join('\n\n')}

const splash_sprite_t dash_splash_sprites[SPLASH_SPRITE_COUNT] = {
${rows.join('\n')}
};
`);
console.log(`dash_splash_data: ${stripCount} strips, ${dataBytes} B data + ${descBytes} B descriptors = ${dataBytes + descBytes} B (bbox-only ${naive} B)`);
JS
