# Dashboard UI v2

This replaces the visual part of DESIGN.md §11 (tokens, type, layout,
components, motion). Everything else in §11 stays: plain HTML/CSS/JS, no CDN,
no framework, no build step at runtime, Thai copy, the three transports, the
URL flags, CSV export, and "the dashboard never re-implements the algorithm".
The protocol (docs/PROTOCOL.md) does not change.

Why v2: the owner wants the look of modern health apps (big score ring,
large numerals with small light units, soft rounded cards, icon-in-circle
labels, colored metric tiles, pill buttons, floating nav) and fewer words on
screen. v1 also squeezes and stretches badly at several sizes (see §10).

---

## 1. Visual language

- Calm near-black (dark) or soft gray (light) background. Cards have **no
  borders**, large radius, and separate from the page by tone (dark) or by a
  soft diffuse shadow (light).
- One hero per screen: the depth readout on the live screen, the score ring
  on the summary.
- Big numerals in a light weight (300–400) with a small, muted unit beside
  them. Labels are tiny and short.
- Every label gets an icon in a circle. Status is always icon + color + word.
- Pills everywhere: buttons, chips, segmented controls, nav.
- Few words. A card label is 1–3 words. Anything longer lives behind a round
  (i) button that opens the info sheet (§6.8).
- The disclaimer ("สื่อการเรียนรู้ ไม่ใช่เครื่องมือแพทย์") appears once on the
  summary as a one-line footnote with a shield icon, and in the About part of
  settings. Nowhere else.

## 2. Tokens

All on `:root` (dark, default) and `[data-theme="light"]`. Names are fixed;
components use only tokens.

| Token | Dark | Light | Use |
|---|---|---|---|
| --bg | #0E0F13 | #F2F3F6 | page |
| --surface | #191B21 | #FFFFFF | cards |
| --surface-2 | #22252D | #F4F5F8 | inset areas, tracks, chips |
| --surface-3 | #2C3039 | #E9EBF0 | pressed, dividers |
| --text | #F4F5F7 | #14161B | primary |
| --text-2 | #A3A8B4 | #5B616E | secondary |
| --text-3 | #6E7380 | #8E94A0 | captions, units |
| --accent | #8B7CFF | #6B5BF2 | brand, primary CTA, chart line, active nav |
| --accent-2 | #C77DFF | #A855F7 | gradient end for rings and CTA |
| --on-accent | #0E0F13 | #FFFFFF | text on accent |
| --ok | #3DDC84 | #16A34A | good depth, good rate, recoil ok, hand ok |
| --warn | #FFB547 | #D97706 | too shallow, too slow, recoil incomplete, pause |
| --bad | #FF5C5C | #DC2626 | too deep, too fast, wrong hand, overload, long pause |
| --idle | #6E7380 | #9AA0AB | no data |
| --ok-tint / --warn-tint / --bad-tint / --accent-tint | color at 14% alpha | color at 10% alpha | tile backgrounds, chips, icon circles |
| --shadow-1 | none | 0 1px 2px rgba(20,22,27,.04), 0 8px 24px rgba(20,22,27,.06) | cards |
| --shadow-2 | 0 16px 48px rgba(0,0,0,.5) | 0 24px 64px rgba(20,22,27,.16) | sheets, floating nav |
| --backdrop | rgba(5,6,8,.62) | rgba(20,22,27,.32) | sheet backdrop |

Status colors never change meaning between themes. Accent is violet on
purpose: it can't be mistaken for any status color.

Gradients: `--grad-accent: linear-gradient(135deg, var(--accent), var(--accent-2))`
for the score ring, the primary CTA and the active nav item.

Radius: `--r-sm 12px` (chips inside cards, inputs), `--r-md 20px` (tiles),
`--r-lg 28px` (hero cards, sheets), `--r-pill 999px`.

Spacing: `--s-1 4px, --s-2 8px, --s-3 12px, --s-4 16px, --s-5 20px,
--s-6 24px, --s-7 32px, --s-8 48px`. Card padding: `clamp(16px, 4vw, 24px)`.
Grid gap: `clamp(12px, 2.5vw, 20px)`.

## 3. Type

Same font files (Anuphan subset, system fallback). `font-variant-numeric:
tabular-nums` on every number.

| Token | Size | Weight | Line height | Use |
|---|---|---|---|---|
| --t-hero | clamp(72px, 24vw, 132px) | 300 | 1 | depth number |
| --t-score | clamp(48px, 14vw, 72px) | 300 | 1 | score in ring |
| --t-big | clamp(32px, 8vw, 44px) | 400 | 1.1 | tile values |
| --t-title | 20px | 600 | 1.4 | screen and sheet titles |
| --t-label | 15px | 600 | 1.45 | card and tile labels |
| --t-body | 15px | 400 | 1.6 | sheet text |
| --t-small | 13px | 500 | 1.5 | captions, chips, units |
| --t-unit | 0.32em of the number it follows | 400 | 1 | units next to numbers, color --text-3 |

Rules: never below 12px. Thai labels never break inside a word; set
`word-break: keep-all; overflow-wrap: normal` on labels and make the copy short
enough to fit instead. No fixed heights on text containers (Thai upper and lower
marks get clipped).

## 4. Icons

Lucide (ISC, vendor/lucide/, licence in vendor/lucide/LICENSE). A build-time
script `tools/build_icons.py` (stdlib only) reads the SVGs listed in it and
writes the `<svg id="icon-sprite" hidden>` block of `<symbol id="i-NAME"
viewBox="0 0 24 24">` elements between the markers
`<!-- icons:begin -->` and `<!-- icons:end -->` in web/index.html. Strip the
licence comment and the width/height/class attributes; keep stroke settings
on the symbol. Add `web/LICENSES.txt` with the Lucide ISC text and the OFL
note for Anuphan. Usage: `<svg class="ic" aria-hidden="true"><use
href="#i-NAME"/></svg>`, size via CSS (`.ic {width:20px;height:20px}`,
`.ic-lg 28px`, `.ic-xl 40px`), stroke 2 (1.75 at `.ic-xl`).

Icon map (use exactly these):

| Thing | Icon |
|---|---|
| brand mark | heart-pulse |
| depth | ruler |
| rate | activity |
| recoil | arrow-up-from-line |
| hand | hand |
| count | repeat |
| timer / test | timer |
| breaths | wind |
| score / summary nav | trophy |
| live nav | heart-pulse |
| settings | settings |
| Wi-Fi / USB / demo | wifi / usb / monitor-play |
| start / stop | play / square |
| metronome | music |
| info | info |
| help in card corner | circle-help |
| close | x |
| ok / done | circle-check, check |
| warning | triangle-alert |
| overload | zap |
| download CSV | download |
| again | rotate-ccw |
| disclaimer | shield-check |
| theme | sun, moon |
| profile | baby (child), user (adult) |
| calibration | sliders-horizontal, scale, target |
| chevrons | chevron-right, chevron-left, chevron-down |
| continuity (CCF) | clock |
| graph | bar-chart-3 |

## 5. App shell and layout

### 5.1 Structure

```
<body>
  <div class="app">                  grid: rail | main
    <nav class="nav">                 bottom floating pill (phone) or side rail (wide)
    <header class="appbar">           brand · connection pill · round buttons
    <main class="screen" id="screenLive">   the live screen (only screen)
  </div>
  <div class="backdrop">  + sheets: #sheetConnect #sheetSummary #sheetCalib #sheetSettings #sheetInfo
```
Keep these ids: `sheetConnect`, `sheetSummary`, `sheetCalib`, `sheetSettings`
(rename from whatever v1 used if needed and keep `?sheet=settings` working),
plus the new `sheetInfo`. A sheet is hidden with the `hidden` attribute when
closed (the screenshot script waits for `#sheetX:not([hidden])`).

Nav items (3): **ฝึก** (heart-pulse, the live screen), **ผล** (trophy, opens
the summary sheet with the last `sum`; when there is none it opens with an
empty state "ยังไม่มีผล · กดเริ่มฝึกก่อน"), **ตั้งค่า** (settings sheet). The
app bar keeps only: brand mark + "CPR Smart Chest", the connection pill, and the
metronome round button.

### 5.2 Breakpoints

Mobile-first. Width breakpoints on the viewport; inside cards use container
queries (`container-type: inline-size`) so a card lays itself out by its own
width, not the screen's.

| Name | Condition | Layout |
|---|---|---|
| phone | width < 600 | one column; app bar 56px; mode switch under app bar; hero card; CTA; tiles 2×2; graph; floating bottom nav pill (height 64, 12px from bottom + safe area, max-width 360, centered). Main gets `padding-bottom: calc(64px + 24px + env(safe-area-inset-bottom))` so nothing hides behind the nav. |
| phone-landscape | height < 500 and width > height | side rail (64px) instead of the bottom nav; app bar 48px; main is two columns: left = hero (depth number + ruler, CTA under it), right = mode switch on top then tiles 2×2. Graph below both, reachable by scrolling. At 844×390 hero + tiles + CTA must be fully visible without scrolling. |
| tablet | 600 ≤ width < 1024 (and not phone-landscape) | one column, max content width 760px centered; hero card has number left and ruler right in one row; tiles 4 across when the grid is ≥ 720px wide, else 2×2; graph full width; floating bottom nav. |
| desktop | width ≥ 1024 and height ≥ 600 | side rail 80px with icon + small label per item; main max-width 1280px centered with `margin-inline:auto`; 12-column grid, three rows `auto auto minmax(160px, 1fr)`: hero spans columns 1–5 and rows 1–2 only (beside the mode/CTA row and the tiles, so it is as tall as its neighbours, never taller); columns 6–12: mode switch + CTA in row 1, tiles 2×2 in row 2; graph spans all 12 columns in row 3 and takes what's left (min 160px plot). Fits in 100dvh at 1280×800 without page scroll. |
| rail (phone-landscape + desktop) | — | The rail is a plain grid column: no offsets inherited from the floating pill (`inset:auto`, no translate). Items stack from the top with a fixed height (desktop 64px, landscape 48px) and an 8px gap; the active indicator has exactly one item's height and slides by `index × (item + gap)`. |
| wide | width ≥ 1600 | same as desktop with max-width 1360px; type tokens stop growing (clamp max). No card wider than its max. |

General: no horizontal scroll at 320px; touch targets ≥ 44×44px; tile
`min-width: 0` everywhere in grids so long content can't push columns; grid
tracks use `minmax(0, 1fr)`.

## 6. Components

### 6.1 Mode switch
Segmented pill on `--surface-2`, three items **อิสระ · 30:2 · 2 นาที**, an
active pill (`--surface` in light, `--surface-3` in dark, with `--shadow-1`)
that slides under the active item with `transform: translateX()`. Height 44.
Max width 360px; it never stretches across a wide screen.

### 6.2 Hero depth card
`--r-lg`, `--surface`, `position: relative; overflow: hidden`.
- Glow background (ref: green aurora): three absolutely positioned layers,
  each a blurred radial gradient in --ok, --warn and --bad (and a fourth in
  --accent for idle) at 18% (dark) / 12% (light) alpha, behind the content.
  Only the layer of the current state has `opacity: 1`; the others 0. The
  state is always the level of the last completed compression.
- **Number rule (v2):** the big number shows the PEAK depth (or peak kg) of
  the last completed compression, not the live value. It changes once per
  compression (on the `c` event), so the number, the status pill, the hint
  and the glow always describe the same compression. Before the first
  compression of a session it shows the last peak from before (idle rule of
  v1), or 0.0 with status **พร้อม** if there is none. The live value is shown
  only by the ruler's live marker (damped, real time); the peak caret sits at
  the last peak, i.e. at the same value as the number.
- Top row: icon circle (ruler) + label **ความลึก**, and the round (i) button
  on the right (opens info "depth").
- Middle: the number at `--t-hero` with the unit **ซม.** (or **กก.** in kg
  mode) at `--t-unit`, then the status pill under it (outlined pill like ref 3,
  1.5px border in the state color, icon + word): **ตื้นไป / พอดี / ลึกไป /
  พร้อม**. Then a one-line hint in --text-2: **กดลึกอีกนิด / ดีมาก /
  เบาลงหน่อย / เริ่มกดได้เลย**.
- Bottom: the tick ruler (ref 3): a horizontal scale 0–8 cm (kg mode: 0 to
  ovkg) with a tick every 0.5 cm (taller ticks with labels at whole cm), the
  target band (gmin–dmin, or gkg–dkg) drawn as a soft `--ok-tint` capsule with
  an --ok top edge, a live marker (a 4px wide, 28px tall rounded bar in the
  state color) that moves with the live value, and a small caret ▲ in --text-2
  at the last peak. The ruler is one inline SVG; the marker moves with
  `transform` only.
- Pause banner and breaths banner sit under the hero (6.6).

### 6.3 CTA card
Full-width pill-ish card (`--r-lg`, min-height 64px) in `--grad-accent` with
--on-accent text: left the title **เริ่มฝึก** (during a session **จบรอบ**)
and a one-line subtitle by mode (**ไม่จับเวลา** / **กด 30 ให้ลม 2** /
**จับเวลา 2 นาที**); right a 48px round dark (--bg at 85%) button with the
play or square icon. The whole card is the button. It sends `start`/`stop`
exactly as v1 did.

### 6.4 Metric tiles (2×2 or 4×1)
`--r-md`, background = the state tint (`--ok-tint` etc., `--surface` when idle),
padding 16. Layout inside: top row icon circle (36px, state color icon on a
slightly stronger tint) + label, (i) button 32px round at top right; then the
value; then the status pill. All four tiles have the same height in a row.

| Tile | Label | Value | Pill / extra |
|---|---|---|---|
| rate | จังหวะ | `110` + unit `/นาที` | ช้าไป / พอดี / เร็วไป; under the value a capsule track 60–160 with the 100–120 band in --ok-tint and a 10px dot marker (transform) |
| recoil | คืนตัว | `9/10` + unit `ครั้ง` (last 10 presses that recoiled) | คืนสุด / ยังค้าง; a row of 10 small capsules (6×18px) colored ok/warn, newest on the right |
| hand | มือ | word **ถูกจุด** or **ผิดจุด** at --t-big | a 44px chest-target icon: rounded torso outline with a circle on the lower sternum, filled ok or bad |
| count | free: จำนวน; 30:2: รอบ 30:2; test: เวลา | free: `27` + `ครั้ง`; 30:2: ring 56px with `20` + `/30` and caption `รอบที่ 2`; test: ring countdown `1:23` | free caption: `ถูก 25` ; test caption `เหลือ` |

The (i) buttons open the info sheet with the keys `rate`, `recoil`, `hand`,
`count`.

### 6.5 Graph card
`--surface`, `--r-lg`. Header: icon circle (bar-chart-3) + **กราฟความลึก**, a
small pill **10 วิ** on the right. Canvas as in v1 (depth axis down, target
band, peak dots by level), line in --accent 2.5px, area under the line filled
with --accent at 8%. Axis labels --text-3 at 12px. Keep the canvas DPR-aware.

### 6.6 Banners
Full-width rounded (`--r-md`) banners, icon in a 32px circle + one short line:
- pause: --warn-tint bg, triangle-alert, **หยุดกด 4 วิ**; after maxPause →
  --bad-tint, **หยุดนานเกิน 10 วิ**.
- breaths (30:2 awaiting): --accent-tint, wind, **ให้ลม 2 ครั้ง**.
- low SPS: --warn-tint, triangle-alert, **เซนเซอร์อ่านช้า** + (i) (key `sps`).
- overload: --bad-tint, zap, **กดแรงเกิน**.
Banners never shift the hero or the tiles. They are toasts: a
`position: fixed` stack, horizontally centered over the main area (not over
the rail), `width: min(420px, 100% - 32px)`, stacked with 8px gaps, and
animate per §7. Phone/tablet: top = app bar height + 8px, so they stay visible
while the page is scrolled. Desktop and phone-landscape: top 12px, centered in
the app bar strip, so they cover only the empty middle of the app bar, never
the mode switch or the CTA. Each toast is opaque: its tint is layered over
--surface-2 (`background: linear-gradient(tint, tint), var(--surface-2)`) with
--shadow-2, so nothing behind it shows through.

### 6.7 Sheets
One sheet system. Phone/tablet: bottom sheet, `--r-lg` top corners, grab
handle (36×4 pill), max-height 88dvh, content scrolls inside, sticky footer
for action buttons. A bottom sheet is never wider than 600px: from 600px
viewport width it is centered (`margin-inline:auto`) with all its content
inside that width. Phone-landscape (height < 500): centered sheet
`width:min(600px, 100% - 32px)`, max-height `calc(100dvh - 16px)`, the
summary lays out in two columns (score ring 140px left, metrics + facts
right) and the calibration step card is horizontal (48px icon left, text
right) so the primary action is visible without scrolling at 844×390.
Desktop (≥ 1024): centered dialog, max-width 560px (summary 640px), `--r-lg`. Backdrop `--backdrop`. Close with the round x
button, the backdrop, or Escape. Focus trap and return focus as v1.

### 6.8 Info sheet (#sheetInfo)
Title with the topic icon in a circle, then short body text. Content is fixed
copy from §8, chosen by key: `depth, rate, recoil, hand, count, ccf, score,
sps, about`.

### 6.9 Summary sheet (#sheetSummary)
1. Score ring: 200px (phone) / 180px (desktop; 140px phone-landscape) ring, 14px stroke on a
   --surface-2 track, progress in --grad-accent (SVG linearGradient), round
   caps. In the center a 64px round badge (--accent-tint) with the trophy icon,
   above the score number (`--t-score`) and the caption **คะแนน**.
   (Center order top→bottom: badge, number, caption.)
2. Grade word (**ยอดเยี่ยม / ดี / พอใช้ / ฝึกอีกนิด**, color ok/ok/warn/bad by
   band ≥85, ≥70, ≥50, <50) and a caption line: mode · profile · date, e.g.
   **2 นาที · เด็ก · 24 พ.ย.**
3. Five small metric rings in one row (wraps to 3+2 under 360px): 56px rings
   with the icon in the center, % under each, a 1-word label under that:
   **ลึก · จังหวะ · คืนตัว · มือ · ต่อเนื่อง** (ต่อเนื่อง = CCF). Ring color:
   ok ≥ 80%, warn 50–79, bad < 50 — except **ต่อเนื่อง**, which is colored
   by its 60% target like the score is: ok ≥ 60%, warn 40–59, bad < 40 (the
   ring still fills to and shows the real %). A small round (i) next to the row title
   **คะแนนมาจาก** opens info `score`.
4. Facts as list cards (ref 1 list items), 1 column on phone, 2 columns ≥ 600:
   icon circle + label left, value right: **จำนวน** `220 ครั้ง`, **เวลา**
   `2:00`, **จังหวะเฉลี่ย** `110 /นาที`, **ลึกเฉลี่ย** `5.3 ซม.`, **หยุดนานสุด**
   `5.1 วิ` (or `ไม่มี`), and only in 30:2 mode **รอบ 30:2** `4 รอบ`.
5. Footer (sticky): **ฝึกอีกครั้ง** primary pill (rotate-ccw), **CSV**
   secondary pill (download).
6. Footnote: shield-check icon + **สื่อการเรียนรู้ ไม่ใช่เครื่องมือแพทย์**
   in --text-3, --t-small, one line.
Empty state (no sum yet): trophy badge in --surface-2, **ยังไม่มีผล**, caption
**กดเริ่มฝึกก่อน**, and a primary pill **ไปฝึก** that closes the sheet.

### 6.10 Connect sheet (#sheetConnect)
Title **เชื่อมต่อ**. Three list cards (icon circle 44px, title, one-line
caption, chevron-right):
- **Wi-Fi** · `หุ่นที่ 192.168.4.1`
- **สาย USB** · `Chrome / Edge บนคอม` (disabled look + caption
  `เบราว์เซอร์นี้ไม่รองรับ` when `navigator.serial` is missing)
- **เดโม** · `ดูตัวอย่างโดยไม่ต่อหุ่น`, with a small pill dropdown
  (chevron-down) on the card to choose the recording (ref 1 "Week ▾").

### 6.11 Calibration sheet (#sheetCalib)
Stepper: 4 small pills (24px tall) in a row, active = --grad-accent with its
number, done = --ok with check, pending = --surface-2; labels under each in
--t-small: **ศูนย์ · น้ำหนัก · ช่วงกด · บันทึก**. Step body: a 72px round
badge with the step icon (target, scale, ruler, circle-check), the step title
in --t-title, one line of instruction, the control (input or live counter),
and the primary pill button. Same commands and flow as v1.

Copy: 1 **ยกมือออกจากหุ่น** / "แล้วกดตั้งศูนย์" / button **ตั้งศูนย์**.
2 **วางของที่รู้น้ำหนัก** / "ใส่น้ำหนักเป็นกิโลกรัม" / input + **บันทึก**.
3 with ToF **กดช้าๆ ให้ลึกราว 6 ซม.** / "กดหลายๆ ครั้งจนตัวเลขขึ้น" / live
count + **เสร็จ**; without ToF **กดให้ลึก 5 ซม.** / "วัดด้วยไม้บรรทัด แล้วกดเก็บค่า"
/ **เก็บค่า** (twice: 5 cm then 6 cm). 4 **บันทึกลงหุ่น** / button **บันทึก**,
success state badge circle-check in --ok + **เรียบร้อย**.

### 6.12 Settings sheet (#sheetSettings)
Grouped list cards, each row: icon circle + label + control right, no
subtitles:
- **ธีม** (sun/moon) segmented **มืด · สว่าง**
- **โปรไฟล์** (baby/user) segmented **เด็ก · ผู้ใหญ่**
- **เสียงจังหวะ** (music) switch
- **เสียงบนหุ่น** switch (if v1 had it)
- **เชื่อมต่อ** (wifi/usb/monitor-play by transport) value text + chevron → opens connect sheet
- **ปรับเทียบ** (sliders-horizontal) chevron → calibration sheet
- **เกี่ยวกับ** (info) chevron → info `about`
Switches: 52×32 pill, knob slides with transform.

## 7. Motion

Plain CSS transitions/keyframes plus a little JS. Only `transform` and
`opacity` are animated (exception: SVG `stroke-dashoffset` on rings, which
doesn't trigger layout). No library.

Tokens:
```
--d-fast 150ms   press feedback, chips
--d-base 240ms   most transitions, live marker
--d-sheet 360ms  sheet in; out is 240ms
--d-slow 900ms   summary ring + count-up only
--e-out  cubic-bezier(.2,.8,.2,1)
--e-in   cubic-bezier(.4,0,1,1)
```

| What | How |
|---|---|
| first load | cards fade in + rise 12px, 320ms --e-out, stagger 40ms by order, at most 8 cards; done once per page load and when the mode changes (only the tiles) |
| mode switch | the active pill slides (transform) in --d-base; tiles whose content changes cross-fade (opacity 0→1, 160ms) |
| live depth marker, rate dot | JS rAF loop with critical damping: each frame `x += (target - x) * (1 - exp(-dt/τ))`, τ = 60 ms (95% in ~180 ms). Never queue animations; the newest value always wins. |
| hero number (new peak) | on each `c` event the text is set at once to the new peak and the number does a quick flip: opacity .4→1 + translateY(6px→0) over 180ms --e-out (keyframes, restarted per event). It never tweens through intermediate values. |
| hero state | glow layers cross-fade opacity 300ms; status pill color/text cross-fade 150ms |
| compression event | count value: scale 1 → 1.08 → 1 over 200ms (keyframes on transform); recoil capsule for the new press: scaleY 0 → 1 from bottom 180ms |
| banners | enter: opacity 0→1 + translateY(-8px→0) 200ms; leave: opacity 1→0 160ms then `hidden` |
| sheets | backdrop opacity 0→1 --d-base; phone sheet translateY(100%→0) --d-sheet --e-out; desktop dialog opacity 0→1 + scale(.96→1) --d-base; closing reverses with --e-in and 240ms, `hidden` set on transitionend (with a 400ms fallback timer) |
| summary | on open: ring dashoffset from empty to value over --d-slow --e-out, number counts up in sync (rAF, ease-out), metric rings follow with 80ms stagger each over 600ms, fact cards stagger in 40ms |
| press feedback | buttons, tiles, list cards: `:active { transform: scale(.97) }` with --d-fast; release springs back with --e-out |
| live status | connection pill dot has a ::after ring that scales 1→2.4 and fades .5→0 over 1.6s, infinite, only while connected or in demo |
| nav | active indicator (filled circle/pill) slides with transform --d-base |

Performance: `will-change: transform` only on the depth marker, rate dot and
an opening sheet (removed after). No animated box-shadow, filter or
width/height. Glow layers are pre-blurred gradients (no animated `filter`).
Animations must never delay real-time readouts: frames update the DOM/canvas
on the next rAF regardless of running transitions.

**Screenshot flag `?motion=freeze`:** adds `data-motion="freeze"` on `<html>`; every transition/animation duration is 0, no stagger, no count-up, no pulses, rings and numbers show final values at once, the connection dot ring is not drawn. Real-time behaviour (dampers) is unaffected except τ = 0. tools/screenshots.py and the audit use it.

`prefers-reduced-motion: reduce`: all durations become 0.01ms, no stagger, no
pulses, no count-up (final values shown), dampers snap (τ = 0).

## 8. Copy (exact Thai text)

Info sheet bodies (keys from §6.8). Keep them like this, short.

- **depth** — title **ความลึก**: "เด็กให้กดลึกราว 5 ซม. หรือประมาณหนึ่งในสามของความหนาหน้าอก และไม่เกิน 6 ซม. ผู้ใหญ่ 5–6 ซม." Second paragraph in --text-3: "อ้างอิงแนวทาง AHA ปี 2025"
- **rate** — **จังหวะ**: "กด 100–120 ครั้งต่อนาที ถ้าจับจังหวะยาก เปิดเสียงจังหวะ 110 ที่ปุ่มโน้ตด้านบน"
- **recoil** — **คืนตัว**: "ยกมือให้หน้าอกกลับขึ้นสุดทุกครั้ง อย่าทิ้งน้ำหนักค้างไว้ เลือดจะได้ไหลกลับเข้าหัวใจ"
- **hand** — **ตำแหน่งมือ**: "วางส้นมือกลางหน้าอก บนครึ่งล่างของกระดูกหน้าอก ไฟตำแหน่งบนหุ่นจะติดเมื่อวางถูก"
- **count** — **การนับ**: "อิสระ นับไปเรื่อยๆ · 30:2 กด 30 ครั้งแล้วให้ลม 2 ครั้ง · 2 นาที จับเวลาแล้วสรุปผลให้"
- **ccf** (used inside **score**) — "ต่อเนื่อง คือสัดส่วนเวลาที่ยังกดอยู่ ควรได้อย่างน้อย 60% และหยุดแต่ละครั้งไม่เกิน 10 วินาที"
- **score** — **คะแนนมาจากไหน**: a stacked horizontal bar (height 12px, `--r-pill`, 5 segments with 2px gaps) with the weights **ลึก 30 · จังหวะ 25 · คืนตัว 20 · มือ 10 · ต่อเนื่อง 15**, each segment in its own color (use: --accent, --ok, #4FC3F7 (light #0284C7), --warn, --accent-2), then a legend of colored dots with the label and %. Under it the ccf line above, then in --text-3: "น้ำหนักคะแนนทีมตั้งเอง ไม่ใช่เกณฑ์ทางการ"
- **sps** — **เซนเซอร์อ่านช้า**: "เซนเซอร์แรงอ่านได้แค่ 10 ครั้งต่อวินาที ค่าที่เห็นจะคลาดเคลื่อน แก้ได้โดยย้ายขา RATE ของบอร์ด HX711 ไปต่อไฟเลี้ยง ดูวิธีทำในคู่มือหัวข้อแก้ปัญหา"
- **about** — **เกี่ยวกับ**: "CPR Smart Chest" / shield-check + "สื่อการเรียนรู้ ไม่ใช่เครื่องมือแพทย์" / then board, firmware version and transport from `cfg` in --text-3.

Connection pill text: **เดโม · เชื่อมต่อแล้ว · กำลังต่อ · ไม่ได้ต่อ**.
Toasts/acks: keep v1 texts but no longer than ~20 characters.

Everything not listed keeps v1 copy, shortened to the 1–3 word rule.

## 9. Size budget

html + css + js gzipped ≤ 48 KB (v1: 32.7 KB). Icon sprite is inline in
index.html. ESP32 flash must stay ≤ 88% (v1: 84.3%).

## 10. Problems found in v1 (audit 24 Nov 2026, before shots in /workspace/cpr-tools/before)

1. 320 wide: the hero fills the whole first screen; "ช่วงเป้าหมาย" wraps and "5–6 ซม." is truncated with "…"; the recoil dot row overflows the card (`.dot-item` past the right edge in every scene).
2. 844×390 landscape: only the hero is visible, its gauge is cut off, the mode switch stretches across the full width; there is no landscape layout.
3. 1920×1080: everything stretches; the hero card is mostly empty space with a giant bar; side cards look tiny; no max content width.
4. 768×1024: rate labels "60" and "160" touch the card edges; the recoil card has a big empty middle; the mode switch stretches full width; the graph is below the fold.
5. Every card has two labels (an overline and a title saying nearly the same thing), e.g. "ความลึกการกด / ระยะกดหน้าอก".
6. Summary at 320: metric labels break inside words ("ความ / ลึกถูก"), the date line wraps, and fact rows are cut under the sticky footer.
7. Test mode at 390: the countdown is below the fold.
8. Light theme: the "too shallow" bar renders an olive color (amber mixed with the fill), not the warn color.
9. Summary footnote pastes the score-weight sentence and the disclaimer inline.
10. Settings rows carry a subtitle each; the about block repeats the disclaimer.
11. 1440/1920: the bottom action bar puts three small buttons far left and the profile switch far right, with a huge gap.
