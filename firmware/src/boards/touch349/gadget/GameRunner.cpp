#ifdef BOARD_TOUCH349

#include "GameRunner.h"
#include <Preferences.h>
#include <esp_random.h>
#include "../../../core/state.h"
#include "Radio.h"
#include "RunnerSim.h"
#include "Settings.h"
#include "Theme.h"
#include "Vfd.h"

using namespace vfd;
using namespace runner;

// Endless runner. A small courier runs right over a scrolling ground. Touch the TOP half of the screen to jump (the press
// length sets the height: a tap is a hop, a hold goes high), the BOTTOM half to duck and slide for as long as the finger
// stays (at least 0.25 s). Both act on touch-down. Score = metres (5 cells = 1 m). The rules live in RunnerSim.h (the same
// file firmware/test/runner_fair_test.cpp runs against a bot); this file draws and reads the touch.
//
// World units: everything is in sprite cells, drawn sc pixels per cell (4, the C64 theme 5), so it plays the same in
// every theme.
//   jump: v0 = 82 cells/s, gravity 220 -> 15.3 cells high, 0.75 s in the air. Lifting the finger cuts the climb to
//         46 cells/s, but not before 60 ms: shortest hop 9.3 cells high (air time 0.56 s). Touching the lower half in the
//         air drops fast (120 cells/s) and ducks on landing.
//   duck: the hit box is 5.5 cells high instead of 11 (the sprite 6 instead of 12 rows); held >= 250 ms.
//   speed: 62 cells/s, +0.6 per second of running, at most 104.
//   enemies (unlock distance in m of score, the action they ask for):
//     0    puddle 1.2 high, crate 5.5, jump          150  low bird 2..6.5 (jump), ground bug 3.5 crawls at 18 (jump)
//     200  tall crate 12.5, high jump               300  archer: bow drawn 0.6 s, arrow 80 cells/s on top of the scroll,
//     450  overhead beam from 7.5 up (duck), bug swarm 7..12.5 (duck)      at head height 7.7..9.3 (duck) or foot height
//     600  pit 14..32 wide (jump), hopping bug (high jump or duck under it at the top of a hop)  1.7..3.3 (jump)
//     650  ledge, ground 4.5 higher for 26 cells (jump onto or over it)
//     800  diving wasp down a dotted path to head height (duck), and combos like crate then beam
//   spacing rule: the edge-to-edge gap between two obstacles is at least baseGap seconds of running (0.50 s jump to
//         jump, 0.36 jump to duck, 0.40 duck to jump, 0.32 duck to duck; +0.10..0.22 after a tall crate, hopping bug,
//         ledge, pit), x1.05 for the speed gained while it scrolls in, plus slack that shrinks from 0.50 s to 0.10 s and a
//         random 0.90 s shrinking to 0.30 s over the first 1500 m. Moving enemies wake 104 cells ahead and are placed so
//         they meet the runner where their slot is; the archer's arrow is placed the same way, and it gives the bow 0.6 s
//         plus 0.23..0.47 s of flight. A bot that sees things 250 ms late and ducks or hops as asked survives 5000 m on
//         200 of 200 seeds (also +-70 ms sloppy); one that never acts is dead by 17 m.
//   scenery: every 400 m the sky, ground and backdrop props change (meadow, desert, city at dusk, snow).

namespace {

constexpr int GROUND_Y = 146;  // feet, in pixels
constexpr int RUNNER_SX = 44;  // the runner's left edge, in pixels

enum class Rs : uint8_t { Ready, Run, Dead };

// Sprites: '#' main color, 'o' detail color, '.' empty.
const char *const RUN_A[] = { "...####..", "..######.", "..#####o.", "..######.", "...####..", "oo.####..", ".o#####.#", "..#####.#", "..#####..", "..##.##..", ".##...##.", "##.....##" };
const char *const RUN_B[] = { "...####..", "..######.", "..#####o.", "..######.", "...####..", "oo.####..", ".o#####.#", "..#####.#", "..#####..", "...###...", "...#.#...", "..##.##.." };
const char *const RUN_J[] = { "...####..", "..######.", "..#####o.", "..######.", "...####..", "oo.####..", ".o#####.#", "..#####.#", "..#####..", ".#######.", ".##...##.", "........." };
const char *const RUN_D[] = { "...####..", "..#####o.", "oo.####..", "o########", ".#######.", ".##...##." };
const char *const PUDDLE_S[] = { "...########...", ".############." };
const char *const CRATE_S[] = { "########", "#oo..oo#", "#.oooo.#", "#.oooo.#", "#oo..oo#", "########" };
const char *const TALL_S[] = { "########", "#oo..oo#", "#.oooo.#", "#.oooo.#", "#oo..oo#", "########", "########", "#oo..oo#", "#.oooo.#", "#.oooo.#", "#.oooo.#", "#oo..oo#", "########" };
const char *const BIRD_A[] = { "o.........o", "oo.......oo", ".ooo###ooo.", "oo.#o####..", "...#####...", "....#.#...." };
const char *const BIRD_B[] = { "...........", "...#####...", "ooo#o####oo", ".ooo####oo.", "...#####...", "....#.#...." };
const char *const BUG_A[] = { "o......o", ".o####o.", "########", "#.#..#.#" };
const char *const BUG_B[] = { "o......o", ".o####o.", "########", ".#.##.#." };
const char *const HOP_G[] = { "o.o.....", ".#####..", "#o####.#", "########", "##....##" };
const char *const HOP_A[] = { "o.o.....", ".#####..", "#o####.#", "########", "#......#" };
const char *const SWARM_S[] = { "o#o...o#o.o#o.", ".#.....#...#..", "....o#o.......", ".....#........", "o#o.......o#o.", ".#.........#.." };
const char *const WASP_A[] = { "...o.o...", "..ooooo..", "#o#o#o##.", ".#o#o#o#.", "...#.#..." };
const char *const WASP_B[] = { ".........", "...ooo...", "#o#o#o##.", ".#o#o#o#.", "..o#.#o.." };
const char *const ARROW_S[] = { ".#......o#", "##########", ".#......o#" };
const char *const BEAM_S[] = { "##oo##oo##oo##oo##oo##oo##oo##", "##############################", "##############################", "#.#.#.#.#.#.#.#.#.#.#.#.#.#.#." };
const char *const ARCHER_HI_I[] = { "......###....", "..#o.#####...", ".#.o.#o####..", "#..o######...", ".#.o..###....", "..#o.#####...", ".....#####...", ".....#####...", "......##.##..", "......##.##..", ".....###.###.", "....####.####" };
const char *const ARCHER_HI_D[] = { "......###....", "..#o.#####...", ".#..o#o####..", "#oooo#####...", ".#..o.###....", "..#o.#####...", ".....#####...", ".....#####...", "......##.##..", "......##.##..", ".....###.###.", "....####.####" };
const char *const ARCHER_LO_I[] = { "......###....", ".....#####...", ".....#o####..", ".....#####...", "......###....", ".....#####...", ".....#####...", "..#o.#####...", ".#.o..##.##..", "#..o####.##..", ".#.o.###.###.", "..#o####.####" };
const char *const ARCHER_LO_D[] = { "......###....", ".....#####...", ".....#o####..", ".....#####...", "......###....", ".....#####...", ".....#####...", "..#o.#####...", ".#..o.##.##..", "#oooo###.##..", ".#..o###.###.", "..#o####.####" };

struct Spr {
  const char *const *rows;
  int n;
};
#define SPR(a) { a, (int)(sizeof(a) / sizeof(a[0])) }

Preferences prefs;
bool prefs_open = false;
Sim sim;
Rs rs = Rs::Ready;
#ifdef PHOSPHOR_FRAME_DEBUG  // developer build flag (-DPHOSPHOR_FRAME_DEBUG): draws the frame times; off for everyone else
bool show_fps = true;
#else
constexpr bool show_fps = false;
#endif

uint32_t last_ms = 0, dead_ms = 0, score_hi = 0, zone_until = 0;
bool new_hi = false;
float ema_gap = 33, ema_draw = 10;
uint32_t last_draw_ms = 0;

int sc() { return theme::current() == theme::C64 ? 5 : 4; }
float viewCells() { return (float)W / sc(); }

void openPrefs()
{
  if (prefs_open) return;
  prefs.begin("games", false);
  prefs_open = true;
}

void startRun()
{
  sim.reset(esp_random(), viewCells());
  new_hi = false;
  rs = Rs::Run;
  last_ms = millis();
  zone_until = 0;
  state.dirty = true;
}

void die()
{
  rs = Rs::Dead;
  dead_ms = millis();
  openPrefs();
  uint32_t score = (uint32_t)sim.score();
  if (score > score_hi) {
    score_hi = score;
    new_hi = true;
    prefs.putUInt("run_hi2", score_hi);  // new key: the old, easier game's scores (rn_hi) do not stand
  }
  Radio_Beep(150, 220);
  state.dirty = true;
}



// ---------------------------------------------------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------------------------------------------------

struct Look {
  uint16_t body, accent, band;  // band: color of every second row (2600), 0 = none
  bool outline;                 // VFD: only the rim is drawn, glowing
  uint16_t halo;
};

// Draws a sprite at pixel (x, y) with its feet/bottom at the given row.
void drawSprite(const char *const *rows, int n, int x, int ybottom, int s, const Look &lk, bool flip = false)
{
  int top = ybottom - n * s;
  for (int pass = lk.outline && style().glow ? 0 : 1; pass < 2; pass++) {
    for (int j = 0; j < n; j++) {
      int len = strlen(rows[j]);
      for (int i = 0; i < len; i++) {
        char c = rows[j][i];
        if (c == '.') continue;
        int ii = flip ? len - 1 - i : i;
        if (lk.outline && c == '#') {  // rim only: a body pixel with an empty neighbour
          bool edge = i == 0 || i == len - 1 || j == 0 || j == n - 1 || rows[j][i - 1] == '.' || rows[j][i + 1] == '.' ||
                      (int)strlen(rows[j - 1]) <= i || rows[j - 1][i] == '.' || (int)strlen(rows[j + 1]) <= i || rows[j + 1][i] == '.';
          if (!edge) continue;
        }
        if (pass == 0) {
          rect(x + ii * s - 1, top + j * s - 1, s + 2, s + 2, lk.halo);
        } else {
          uint16_t col = c == 'o' ? lk.accent : (lk.band && (j & 1)) ? lk.band : lk.body;
          rect(x + ii * s, top + j * s, s, s, col);
        }
      }
    }
  }
}

void drawSprite(const Spr &sp, int x, int ybottom, int s, const Look &lk, bool flip = false) { drawSprite(sp.rows, sp.n, x, ybottom, s, lk, flip); }

uint16_t rgb12(int r, int g, int b)  // 4 bits per channel, like the Amiga chipset
{
  return px::rgb((r >> 4) * 17, (g >> 4) * 17, (b >> 4) * 17);
}

uint16_t mix(uint16_t a, uint16_t b, int t)  // t 0..255 of b
{
  int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31, br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
  int r = ar + (br - ar) * t / 255, g = ag + (bg - ag) * t / 255, bl = ab + (bb - ab) * t / 255;
  return (r << 11) | (g << 5) | bl;
}

int pmod(int a, int m) { return ((a % m) + m) % m; }

// One scenery zone of one theme: sky, backdrop colors, ground. The zone changes every 400 m.
struct Scene {
  uint16_t sky, sky2;                // flat: sky; gradient and bands: the two ends / the two band colors
  uint16_t sun;                      // 0 = none
  uint16_t far_c, near_c, win;       // far props, near props, windows / snow / flowers
  uint16_t g_top, g_body, g_dot;     // ground: top stripe, body, specks
  uint16_t hole;                     // what shows through a pit
};
Scene scene_now;

const char *zoneName(int z)
{
  switch (z) {
    case 1: return T("DESERT");
    case 2: return T("CITY");
    case 3: return T("SNOW");
    default: return T("MEADOW");
  }
}

Scene sceneFor(int z)
{
  using px::rgb;
  Scene s = {};
  switch (theme::current()) {
    case theme::C64:  // flat sky, the 16 colors of the machine
      switch (z) {
        case 0: s = { BG, BG, 0, rgb(108, 94, 181), 0xFFFF, 0xFFFF, rgb(88, 141, 67), rgb(111, 79, 37), rgb(67, 57, 0), 0 }; break;
        case 1: s = { rgb(112, 164, 178), 0, rgb(184, 199, 111), rgb(184, 105, 98), rgb(88, 141, 67), 0xFFFF, rgb(184, 199, 111), rgb(111, 79, 37), rgb(67, 57, 0), 0 }; break;
        case 2: s = { 0, 0, rgb(149, 149, 149), rgb(68, 68, 68), rgb(108, 94, 181), rgb(184, 199, 111), rgb(149, 149, 149), rgb(68, 68, 68), 0, 0 }; break;
        default: s = { rgb(108, 94, 181), 0, 0, 0xFFFF, rgb(88, 141, 67), 0xFFFF, 0xFFFF, rgb(112, 164, 178), rgb(149, 149, 149), 0 }; break;
      }
      s.hole = 0;
      break;
    case theme::AMIGA:  // copper gradient sky, 12-bit colors
      switch (z) {
        case 0: s = { rgb12(0, 20, 90), rgb12(255, 110, 40), rgb12(255, 221, 0), rgb12(255, 85, 34), rgb12(255, 255, 255), 0, rgb12(255, 136, 0), rgb12(51, 17, 0), rgb12(136, 51, 0), 0 }; break;
        case 1: s = { rgb12(0, 119, 204), rgb12(255, 238, 170), rgb12(255, 255, 204), rgb12(204, 102, 51), rgb12(0, 136, 0), 0, rgb12(221, 170, 85), rgb12(153, 102, 34), rgb12(119, 68, 17), 0 }; break;
        case 2: s = { rgb12(0, 0, 34), rgb12(102, 0, 119), rgb12(221, 221, 255), rgb12(34, 34, 85), rgb12(68, 51, 136), rgb12(255, 221, 0), rgb12(102, 102, 170), rgb12(34, 34, 68), rgb12(17, 17, 51), 0 }; break;
        default: s = { rgb12(0, 34, 102), rgb12(170, 221, 255), 0, rgb12(238, 238, 255), rgb12(0, 102, 51), rgb12(255, 255, 255), rgb12(238, 238, 255), rgb12(136, 153, 204), rgb12(187, 204, 238), 0 }; break;
      }
      s.hole = rgb12(0, 0, 34);
      break;
    case theme::ATARI:  // black with flat color bands
      switch (z) {
        case 0: s = { rgb(24, 26, 80), rgb(80, 26, 24), rgb(252, 188, 116), rgb(60, 60, 130), rgb(40, 90, 40), 0, rgb(92, 186, 92), rgb(60, 130, 60), rgb(40, 90, 40), 0 }; break;
        case 1: s = { rgb(96, 64, 0), rgb(136, 92, 16), rgb(236, 236, 236), rgb(160, 52, 36), rgb(60, 130, 60), 0, rgb(216, 172, 92), rgb(184, 140, 60), rgb(140, 100, 30), 0 }; break;
        case 2: s = { rgb(60, 20, 80), rgb(24, 26, 80), rgb(236, 236, 236), rgb(40, 40, 96), rgb(70, 70, 140), rgb(252, 188, 116), rgb(110, 110, 110), rgb(70, 70, 70), rgb(40, 40, 40), 0 }; break;
        default: s = { rgb(0, 60, 100), rgb(40, 100, 160), 0, rgb(236, 236, 236), rgb(60, 130, 60), rgb(236, 236, 236), rgb(236, 236, 236), rgb(180, 190, 230), rgb(140, 150, 200), 0 }; break;
      }
      s.hole = 0;
      break;
    default:  // VFD: dark glass, everything is unlit ghost except the horizon
      switch (z) {
        case 1: s = { BG, BG, DIM, GHOST, GHOST, DIM, LIT, BG, GHOST, 0 }; break;
        case 2: s = { BG, BG, DIM, GHOST, FAINT, DIM, LIT, BG, GHOST, 0 }; break;
        case 3: s = { BG, BG, 0, GHOST, GHOST, DIM, LIT, BG, GHOST, 0 }; break;
        default: s = { BG, BG, 0, GHOST, GHOST, DIM, LIT, BG, GHOST, 0 }; break;
      }
      s.hole = BG;
      break;
  }
  return s;
}

void tri(int cx, int base, int w, int h, uint16_t c)  // stepped triangle
{
  for (int yy = 0; yy < h; yy += 4) {
    int ww = w * (yy + 4) / h;
    rect(cx - ww / 2, base - h + yy, ww, 4, c);
  }
}

void sunAt(int cx, int cy, int r, uint16_t c)  // the 2600 has no round shapes
{
  if (theme::current() == theme::ATARI) rect(cx - r, cy - r, 2 * r, 2 * r, c);
  else Apps_Disc(cx, cy, r, c);
}

void cloud(int cx, int cy, uint16_t c)
{
  if (theme::current() == theme::ATARI) {  // wide playfield blocks
    rect(cx, cy, 64, 12, c);
    rect(cx + 16, cy - 8, 32, 8, c);
    return;
  }
  // x and width of each row, top down: two puffs on a flat base (C64 double-wide pixels, Amiga finer rows)
  static const uint8_t PUFF[][2] = { { 28, 12 }, { 22, 24 }, { 18, 32 }, { 10, 44 }, { 6, 56 }, { 2, 64 }, { 0, 68 }, { 2, 64 }, { 8, 52 } };
  int rh = theme::current() == theme::C64 ? 4 : 3, y = cy - 12;
  for (auto &r : PUFF) {
    rect(cx + r[0], y, r[1], rh, c);
    y += rh;
  }
}

void building(int x, int base, int w, int h, uint16_t c, uint16_t win)
{
  rect(x, base - h, w, h, c);
  if (!win) return;
  for (int yy = base - h + 8; yy < base - 8; yy += 12) {
    for (int xx = x + 6; xx < x + w - 8; xx += 10) {
      if (((xx * 7 + yy * 3) >> 3) & 1) rect(xx, yy, 4, 5, win);
    }
  }
}

void pine(int cx, int base, int h, uint16_t c, uint16_t snow)
{
  tri(cx, base - 6, h * 2 / 3, h - 6, c);
  rect(cx - 2, base - 6, 4, 6, c);
  if (snow) rect(cx - 2, base - h, 4, 4, snow);
}

void cactus(int x, int base, uint16_t c)
{
  rect(x, base - 30, 6, 30, c);
  rect(x - 8, base - 22, 8, 4, c);
  rect(x - 8, base - 28, 4, 10, c);
  rect(x + 6, base - 16, 8, 4, c);
  rect(x + 10, base - 24, 4, 12, c);
}

// Backdrop props of a zone, two layers sliding at different speeds.
void drawProps(int z, int cell, const Scene &sn, bool vfd)
{
  switch (z) {
    case 0:  // hills and clouds (VFD: far stars)
      for (int k = 0; k < 4; k++) tri(pmod(k * 260 + 40 - cell / 8, 1040) - 200, GROUND_Y, 200, 34 + (k * 17) % 22, sn.far_c);
      if (vfd) {
        for (int k = 0; k < 6; k++) rect(pmod(k * 131 - cell / 8, 760) - 60, 24 + (k * 37) % 70, 3, 3, sn.near_c);
      } else {
        for (int k = 0; k < 3; k++) cloud(pmod(k * 230 + 60 - cell / 6, 760) - 100, 30 + k * 16, theme::current() == theme::C64 ? px::rgb(108, 94, 181) : sn.near_c);  // white would hide the score
      }
      break;
    case 1:  // desert: mesas, a few cacti, the sun
      if (sn.sun && !vfd) sunAt(470, 70, 26, sn.sun);
      if (vfd) {
        Apps_Disc(480, 66, 20, GHOST);
        Apps_Disc(480, 66, 16, BG);
      }
      for (int k = 0; k < 3; k++) {
        int x = pmod(k * 330 - cell / 8, 990) - 150, h = 40 + (k * 13) % 20;
        rect(x, GROUND_Y - h, 120, h, sn.far_c);
        rect(x + 14, GROUND_Y - h - 8, 92, 8, sn.far_c);
      }
      for (int k = 0; k < 3; k++) cactus(pmod(k * 250 + 90 - cell / 3, 750) - 60, GROUND_Y, sn.near_c);
      break;
    case 2:  // a city: two rows of buildings, lit windows; a moon (top right: the score fills the top left)
      if (sn.sun && !vfd) sunAt(560, 40, 16, sn.sun);
      for (int k = 0; k < 8; k++) {
        int w = 30 + (k * 11) % 26;
        building(pmod(k * 118 - cell / 10, 944) - 80, GROUND_Y, w, 30 + (k * 29) % 46, sn.far_c, 0);
      }
      for (int k = 0; k < 5; k++) {
        int w = 36 + (k * 17) % 24;
        building(pmod(k * 190 + 40 - cell / 4, 950) - 80, GROUND_Y, w, 26 + (k * 23) % 38, sn.near_c, sn.win);
      }
      break;
    default: {  // snow: mountains, pines, flakes falling
      for (int k = 0; k < 3; k++) {
        int x = pmod(k * 330 + 60 - cell / 8, 990) - 150;
        tri(x, GROUND_Y, 240, 64 + (k * 19) % 20, sn.far_c);
      }
      for (int k = 0; k < 6; k++) pine(pmod(k * 150 + 30 - cell / 3, 900) - 40, GROUND_Y, 36 + (k * 7) % 12, sn.near_c, sn.win);
      uint32_t t = millis();
      for (int k = 0; k < 18; k++) rect(pmod(k * 97 - cell / 3 + (int)(t / 30), 700) - 30, 6 + pmod(k * 41 + (int)(t / 22), 136), 2, 2, sn.win ? sn.win : 0xFFFF);
      break;
    }
  }
}

void drawBackground(int z, int cell)
{
  Scene sn = sceneFor(z);
  scene_now = sn;
  bool vfd = theme::current() == theme::VFD;
  switch (theme::current()) {
    case theme::C64: {
      clear(sn.sky);
      drawProps(z, cell, sn, false);
      rect(0, GROUND_Y, W, 8, sn.g_top);
      rect(0, GROUND_Y + 8, W, H - GROUND_Y - 8, sn.g_body);
      for (int k = 0; k < 14; k++) rect(pmod(k * 61 - cell, 700) - 30, GROUND_Y + 14 + (k % 3) * 6, 8, 4, sn.g_dot);
      break;
    }
    case theme::AMIGA: {  // copper bars: the sky is a gradient written scanline by scanline, 12-bit colors
      for (int yy = 0; yy < GROUND_Y; yy += 4) {
        int t = yy * 255 / GROUND_Y;
        uint16_t c;
        if (z == 0) {
          c = t < 128 ? rgb12(0, 20 + t / 3, 90 + t) : rgb12((t - 128) * 2, 50 + (t - 128), 200 - (t - 128) * 2);
          if (t > 190) c = rgb12(255, 110 + (t - 190), 40);
        } else {
          c = mix(sn.sky, sn.sky2, t);
          c = rgb12(((c >> 11) & 31) << 3, ((c >> 5) & 63) << 2, (c & 31) << 3);
        }
        rect(0, yy, W, 4, c);
      }
      if (z == 0) {
        Apps_Disc(470, GROUND_Y - 6, 30, sn.sun);
        for (int k = 0; k < 4; k++) rect(430, GROUND_Y - 22 + k * 6, 80, 2 + k, sn.far_c);  // the sun is striped
        for (int k = 0; k < 3; k++) tri(pmod(k * 300 + 40 - cell / 8, 900) - 150, GROUND_Y, 160, 26, rgb12(153, 51, 0));
      } else {
        drawProps(z, cell, sn, false);
      }
      rect(0, GROUND_Y, W, H - GROUND_Y, sn.g_body);
      rect(0, GROUND_Y, W, 3, sn.g_top);
      for (int k = 0; k < 10; k++) rect(pmod(k * 77 - cell, 700) - 20, GROUND_Y + 10 + (k % 3) * 5, 14, 2, sn.g_dot);
      break;
    }
    case theme::ATARI: {  // flat sky bands and a striped ground, nothing finer than a 2600 could do
      clear(px::rgb(0, 0, 0));
      for (int k = 0; k < 6; k++) rect(0, 24 + k * 14, W, 5, k < 3 ? sn.sky : sn.sky2);
      if (z == 0) {
        rect(300, 30, 54, 54, sn.sun);
        for (int k = 0; k < 5; k++) rect(300, 42 + k * 10, 54, 3, px::rgb(0, 0, 0));
      } else {
        drawProps(z, cell, sn, false);
      }
      for (int yy = GROUND_Y; yy < H; yy += 6) rect(0, yy, W, 6, ((yy - GROUND_Y) / 6) & 1 ? sn.g_body : sn.g_top);
      for (int k = 0; k < 9; k++) rect(pmod(k * 83 - cell, 700) - 20, GROUND_Y + 6, 24, 6, sn.g_dot);
      break;
    }
    default: {  // VFD: dark glass, a glowing horizon, dim ticks that slide by
      clear();
      drawProps(z, cell, sn, true);
      rect(0, GROUND_Y - 1, W, 4, HALO);
      rect(0, GROUND_Y, W, 1, LIT);
      int tw = z == 1 ? 6 : z == 2 ? 26 : z == 3 ? 3 : 18;  // the ticks differ by zone
      for (int k = 0; k < 12; k++) rect(pmod(k * 58 - cell, 700) - 30, GROUND_Y + 7 + (k % 3) * 6, tw, z == 3 ? 3 : 2, GHOST);
      break;
    }
  }
  (void)vfd;
}

Look runnerLook()
{
  using px::rgb;
  switch (theme::current()) {
    case theme::C64: return { 0xFFFF, rgb(184, 105, 98), 0, false, 0 };
    case theme::AMIGA: return { 0xFFFF, rgb12(255, 136, 0), 0, false, 0 };
    case theme::ATARI: return { rgb(252, 188, 116), rgb(236, 236, 236), rgb(208, 128, 92), false, 0 };
    default: return { LIT, HOT, 0, true, HALO };
  }
}

// Colors of every enemy in every theme. Hazards are bright and warm against the cool sky of every zone; the VFD draws
// them all as glowing magenta rims with a cyan detail, the colored themes tell the families apart: crawlers green, flyers
// yellow, the archer and its arrow red/white, overhead beams striped.
Look obstacleLook(int type)
{
  using px::rgb;
  switch (theme::current()) {
    case theme::C64:
      switch (type) {
        case PUDDLE: return { rgb(112, 164, 178), 0xFFFF, 0, false, 0 };
        case BIRD: return { rgb(184, 105, 98), 0xFFFF, 0, false, 0 };
        case BUG: return { rgb(154, 210, 132), rgb(53, 40, 121), 0, false, 0 };
        case HOPPER: return { rgb(154, 210, 132), 0xFFFF, 0, false, 0 };
        case SWARM: return { rgb(184, 199, 111), rgb(111, 79, 37), 0, false, 0 };
        case WASP: return { rgb(184, 199, 111), 0, 0, false, 0 };
        case ARCHER: return { rgb(184, 105, 98), 0xFFFF, 0, false, 0 };
        case ARROW: return { 0xFFFF, rgb(184, 199, 111), 0, false, 0 };
        case BEAM: return { 0xFFFF, rgb(184, 105, 98), 0, false, 0 };
        default: return { rgb(184, 199, 111), rgb(111, 79, 37), 0, false, 0 };
      }
    case theme::AMIGA:
      switch (type) {
        case PUDDLE: return { rgb12(0, 170, 255), rgb12(255, 255, 255), 0, false, 0 };
        case BIRD: return { rgb12(170, 68, 255), rgb12(255, 255, 255), 0, false, 0 };
        case BUG: return { rgb12(51, 255, 102), rgb12(0, 51, 0), 0, false, 0 };
        case HOPPER: return { rgb12(153, 255, 51), rgb12(0, 68, 0), 0, false, 0 };
        case SWARM: return { rgb12(255, 221, 0), rgb12(51, 17, 0), 0, false, 0 };
        case WASP: return { rgb12(255, 221, 0), rgb12(0, 0, 0), 0, false, 0 };
        case ARCHER: return { rgb12(255, 51, 85), rgb12(255, 255, 255), 0, false, 0 };
        case ARROW: return { rgb12(255, 255, 255), rgb12(255, 221, 0), 0, false, 0 };
        case BEAM: return { rgb12(221, 136, 51), rgb12(255, 255, 255), 0, false, 0 };
        default: return { rgb12(255, 136, 0), rgb12(51, 17, 0), 0, false, 0 };
      }
    case theme::ATARI:
      switch (type) {
        case PUDDLE: return { rgb(68, 120, 200), rgb(160, 200, 252), 0, false, 0 };
        case BIRD: return { rgb(160, 160, 200), rgb(236, 236, 236), rgb(120, 120, 170), false, 0 };
        case BUG: return { rgb(252, 188, 116), rgb(160, 52, 36), rgb(208, 128, 92), false, 0 };
        case HOPPER: return { rgb(92, 186, 92), rgb(236, 236, 236), rgb(60, 130, 60), false, 0 };
        case SWARM: return { rgb(252, 188, 116), rgb(200, 72, 48), 0, false, 0 };
        case WASP: return { rgb(252, 188, 116), rgb(0, 0, 0), rgb(208, 128, 92), false, 0 };
        case ARCHER: return { rgb(200, 72, 48), rgb(236, 236, 236), rgb(160, 52, 36), false, 0 };
        case ARROW: return { rgb(236, 236, 236), rgb(252, 188, 116), 0, false, 0 };
        case BEAM: return { rgb(160, 160, 200), rgb(236, 236, 236), rgb(120, 120, 170), false, 0 };
        default: return { rgb(200, 72, 48), rgb(252, 188, 116), rgb(160, 52, 36), false, 0 };
      }
    default:
      if (type == PUDDLE) return { LIT, DIM, 0, true, HALO };
      return { HOT, LIT, 0, true, HOTH };
  }
}

uint16_t guide() { return obstacleLook(ARROW).body; }  // color of the dotted telegraphs

void drawObstacles(float d, int s)
{
  uint32_t now = millis();
  bool wing = (now / 150) & 1;
  int gy = GROUND_Y;
  const Scene &sn = scene_now;
  // pits and ledges first: they are part of the ground
  for (const Ob &o : sim.obs) {
    if (!o.alive || (o.type != PIT && o.type != LEDGE)) continue;
    int x = RUNNER_SX + (int)((o.x - d) * s), w = (int)(o.w * s);
    if (x > W || x + w < 0) continue;
    if (o.type == PIT) {
      rect(x, gy, w, H - gy, sn.hole);
      rect(x - 2, gy, 2, 10, sn.g_top);  // the rims
      rect(x + w, gy, 2, 10, sn.g_top);
      if (theme::current() == theme::VFD) {
        rect(x, gy - 1, w, 4, BG);
        rect(x - 1, gy - 1, 2, 8, LIT);
        rect(x + w - 1, gy - 1, 2, 8, LIT);
      } else {
        for (int k = 0; k < w / 16; k++) rect(x + 6 + k * 16, H - 8, 8, 4, obstacleLook(BEAM).accent);  // spikes' tips at the bottom
      }
    } else {
      int h = (int)(DIMS[LEDGE].ht * s);
      rect(x, gy - h, w, h, sn.g_body);
      rect(x, gy - h, w, 4, sn.g_top);
      if (theme::current() == theme::VFD) {
        rect(x, gy - h - 1, w, 4, HALO);
        rect(x, gy - h, w, 1, LIT);
        rect(x, gy - h, 1, h, LIT);
        rect(x + w - 1, gy - h, 1, h, LIT);
      }
    }
  }
  for (const Ob &o : sim.obs) {
    if (!o.alive || o.type == PIT || o.type == LEDGE) continue;
    int x = RUNNER_SX + (int)((o.x - d) * s);
    int w = (int)(o.w * s);
    if (x > W + 20 || x + w < -20) continue;
    Look lk = obstacleLook(o.type);
    switch (o.type) {
      case PUDDLE: drawSprite(Spr SPR(PUDDLE_S), x, gy, s, lk); break;
      case CRATE: drawSprite(Spr SPR(CRATE_S), x, gy, s, lk); break;
      case TALL: drawSprite(Spr SPR(TALL_S), x, gy, s, lk); break;
      case BIRD: drawSprite(wing ? Spr SPR(BIRD_B) : Spr SPR(BIRD_A), x, gy - (int)(1.5f * s), s, lk); break;
      case BUG: drawSprite(((now / 110) & 1) ? Spr SPR(BUG_B) : Spr SPR(BUG_A), x, gy, s, lk); break;
      case HOPPER: drawSprite(o.y > 0.6f ? Spr SPR(HOP_A) : Spr SPR(HOP_G), x, gy - (int)(o.y * s), s, lk); break;
      case SWARM: drawSprite(Spr SPR(SWARM_S), x, gy - (int)(7.0f * s) - (((now / 90) & 1) ? 0 : s), s, lk); break;
      case WASP: {
        // the path it will dive along, dotted: the wasp follows these dots
        float dxr = o.x - (sim.dist + RUN_X0);
        for (float p = dxr - 8; p > 4; p -= 9) {
          int px_ = RUNNER_SX + (int)((p + RUN_X0 + 4) * s), py_ = gy - (int)((Sim::waspY(p) + 2.2f) * s);
          if (px_ < W) rect(px_, py_, 3, 3, guide());
        }
        drawSprite(wing ? Spr SPR(WASP_B) : Spr SPR(WASP_A), x, gy - (int)(o.y * s), s, lk);
        break;
      }
      case BEAM: {
        int top = gy - (int)(DIMS[BEAM].hb * s) - 4 * s;  // the beam hangs from two chains
        for (int cx : { x + 3 * s, x + (int)o.w * s - 4 * s }) {
          for (int yy = 0; yy < top; yy += 3 * s) rect(cx, yy, s, 2 * s, lk.accent);
        }
        drawSprite(Spr SPR(BEAM_S), x, gy - (int)(DIMS[BEAM].hb * s), s, lk);
        break;
      }
      case ARROW: drawSprite(Spr SPR(ARROW_S), x, gy - (int)(o.y * s), s, lk); break;
      case ARCHER: {
        bool low = o.var == 1, drawing = o.active && !o.fired && o.t > 0.12f;
        if (o.active && !o.fired) {  // the lane the arrow will fly along, dotted, blinking once the bow is nearly drawn
          bool show = o.t < 0.35f || ((now / 70) & 1);
          int ly = gy - (int)((low ? 2.5f : 8.5f) * s);
          if (show) {
            for (int px_ = x - 4 * s; px_ > RUNNER_SX + 10 * s; px_ -= 7 * s) rect(px_, ly, 2 * s, 2, guide());
          }
        }
        const Spr &sp = low ? (drawing ? Spr SPR(ARCHER_LO_D) : Spr SPR(ARCHER_LO_I)) : (drawing ? Spr SPR(ARCHER_HI_D) : Spr SPR(ARCHER_HI_I));
        drawSprite(sp, x, gy, s, lk);
        break;
      }
      default: break;
    }
  }
}

void drawRunner(int s)
{
  bool duck = sim.ducking;
  const char *const *rows = !sim.on_ground ? RUN_J : (((int)(sim.dist / 6.0f)) & 1) ? RUN_B : RUN_A;
  int n = 12;
  if (duck) rows = RUN_D, n = 6;
  if (rs == Rs::Ready) rows = RUN_B, n = 12;
  int yb = GROUND_Y - (int)(sim.y * s);
  Look lk = runnerLook();
  if (rs == Rs::Dead) {  // the runner flashes the accent color
    lk.body = lk.accent;
    lk.band = 0;
  }
  drawSprite(rows, n, RUNNER_SX, yb, s, lk);
}

void drawHud()
{
  char b[24];
  Ink score_ink = LIT_INK, dim = DIM_INK;
  if (theme::current() != theme::VFD) {
    score_ink = Ink{ 0xFFFF };
    dim = theme::current() == theme::ATARI ? Ink{ px::rgb(236, 236, 236) } : Ink{ 0xFFFF };
  }
  // top left: nothing that comes at the runner ever flies there
  snprintf(b, sizeof(b), "%05lu", (unsigned long)sim.score());
  text(b, 20, 14, 4, score_ink);
  snprintf(b, sizeof(b), "%s %05lu", T("HI"), (unsigned long)score_hi);
  text(b, 20, 46, 2, dim);
  if (rs == Rs::Run && (int32_t)(zone_until - millis()) > 0) {
    snprintf(b, sizeof(b), "%s", zoneName(Sim::zoneOf(sim.score())));
    text(b, 20, 66, 2, dim);
  }
  if (show_fps) {
    snprintf(b, sizeof(b), "%dMS %dMS", (int)ema_gap, (int)ema_draw);
    text(b, W - 140, 14, 2, dim);
  }
}

// A dark box behind the messages, so they read over the copper sky and the clouds.
void panel(int y0, int y1)
{
  uint16_t fill = BG, edge = DIM;
  switch (theme::current()) {
    case theme::C64: edge = HALO; break;
    case theme::AMIGA: fill = px::rgb(0, 0, 34), edge = 0xFFFF; break;
    case theme::ATARI: fill = 0, edge = px::rgb(160, 160, 200); break;
    default: break;
  }
  // x 156..484: clears the score and "REKOR 00000" at the top left (they end at x 152), still fits "TAP TO RUN" at size 5
  rect(156, y0, 328, y1 - y0, fill);
  frame(156, y0, 328, y1 - y0, edge);
  frame(157, y0 + 1, 326, y1 - y0 - 2, edge);
}

void centered(const char *s, int p, int yy, const Ink &ink) { text(s, (W - textWidth(s, p)) / 2, yy, p, ink); }

void runnerDraw(const DateTime &)
{
  uint32_t t0 = millis();
  int s = sc();
  drawBackground(Sim::zoneOf(sim.score()), (int)(sim.dist * s));
  drawObstacles(sim.dist, s);
  drawRunner(s);
  drawHud();
  Ink hot = HOT_INK, lit = LIT_INK;
  if (theme::current() != theme::VFD) {
    hot = Ink{ (uint16_t)(theme::current() == theme::ATARI ? px::rgb(252, 188, 116) : theme::current() == theme::AMIGA ? rgb12(255, 136, 0) : px::rgb(184, 199, 111)) };
    lit = Ink{ 0xFFFF };
  }
  if (rs == Rs::Ready) {
    panel(28, 138);
    centered(T("TAP TO RUN"), 5, 40, lit);
    centered(T("TOP: JUMP  HOLD: HIGHER"), 2, 86, hot);
    centered(T("BOTTOM: DUCK  HOLD: LOW"), 2, 108, hot);
  } else if (rs == Rs::Dead) {
    panel(24, 136);
    centered(T("GAME OVER"), 5, 36, hot);
    char b[32];
    snprintf(b, sizeof(b), "%s %lu", new_hi ? T("NEW BEST") : T("SCORE"), (unsigned long)sim.score());
    centered(b, 3, 82, lit);
    if (millis() - dead_ms > 600) centered(T("TAP TO RETRY"), 2, 112, hot);
  }
  if (theme::current() == theme::C64) {
    const theme::Def &d = theme::def();
    rect(0, 0, W, d.border_w, d.border);
    rect(0, H - d.border_w, W, d.border_w, d.border);
    rect(0, 0, d.border_w, H, d.border);
    rect(W - d.border_w, 0, d.border_w, H, d.border);
  }
  uint32_t now = millis();
  ema_draw += ((float)(now - t0) - ema_draw) * 0.1f;
  if (last_draw_ms) ema_gap += ((float)(now - last_draw_ms) - ema_gap) * 0.1f;
  last_draw_ms = now;
}

// ---------------------------------------------------------------------------------------------------------------------
// Input and loop
// ---------------------------------------------------------------------------------------------------------------------

void runnerEnter()
{
  openPrefs();
  score_hi = prefs.getUInt("run_hi2", 0);
  sim.reset(esp_random(), viewCells());
  rs = Rs::Ready;
  last_draw_ms = 0;
}

// The finger edges, from the touch driver the moment it lands / lifts. In play the upper half jumps and the lower half
// ducks right now; on the title and game-over screens a tap (the gesture below) starts a run.
void runnerTouch(bool down, int, int y)
{
  if (rs != Rs::Run) return;
  if (!down) sim.release();
  else if (y < H / 2) sim.touchTop();
  else sim.touchBottom();
}

// In play the touch is used up at touch-down, so it makes no tap or swipe on release.
bool runnerTouchDown(int, int) { return rs == Rs::Run; }

bool runnerSwipesLocked() { return rs == Rs::Run; }

bool runnerGesture(Gesture g, int, int)
{
  uint32_t now = millis();
  if (g == Gesture::Tap && (rs == Rs::Ready || (rs == Rs::Dead && now - dead_ms > 600))) {
    startRun();
    return true;
  }
  if (g == Gesture::LongPress) return true;
  // taps and up/down swipes (a sloppy tap) were already used as the touch edge; they must not reach the volume control
  return g == Gesture::Tap || g == Gesture::SwipeUp || g == Gesture::SwipeDown;
}

void runnerTick()
{
  uint32_t now = millis();
  float dt = (now - last_ms) / 1000.0f;
  last_ms = now;
  if (dt > 0.1f) dt = 0.1f;
  if (rs == Rs::Run) {
    sim.view = viewCells();
    float left = dt;
    while (left > 0 && !sim.dead) {  // small steps keep the jump arc and the collision test honest
      float h = rmin(left, 0.008f);
      sim.step(h);
      left -= h;
    }
    uint32_t ev = sim.ev;
    sim.ev = 0;
    if (ev & EV_ZONE) zone_until = now + 2200;
    if (sim.dead) {
      die();
    } else if (ev & EV_ARROW) {
      Radio_Beep(1500, 10);
    } else if (ev & EV_MILE) {
      Radio_Beep(1318, 22);
    } else if (ev & EV_JUMP) {
      Radio_Beep(740, 18);
    } else if (ev & EV_DUCK) {
      Radio_Beep(420, 14);
    }
  } else if (rs == Rs::Dead && sim.fell && sim.y > -20) {
    sim.y -= 70.0f * dt;  // it falls into the pit
  }
}

uint32_t runnerFrameMs() { return rs == Rs::Run ? 1 : 80; }

void iconRunner(int x, int yy, uint16_t c)
{
  for (int j = 0; j < 12; j++) {
    for (int i = 0; i < 9; i++) {
      if (RUN_A[j][i] != '.') rect(x + 6 + i * 4, yy + j * 4, 4, 4, c);
    }
  }
}

}  // namespace

const App APP_RUNNER = { "runner", N_("RUNNER"), iconRunner, true, true, runnerEnter, nullptr, runnerDraw, runnerGesture, runnerTick, runnerFrameMs, runnerTouchDown, runnerSwipesLocked, runnerTouch };

#endif
