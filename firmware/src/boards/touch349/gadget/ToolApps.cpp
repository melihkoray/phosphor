#ifdef BOARD_TOUCH349

#include "ToolApps.h"
#include <Preferences.h>
#include <esp_random.h>
#include "../../../core/Board.h"
#include "../../../core/state.h"
#include "Motion.h"
#include "Radio.h"
#include "Settings.h"
#include "ClockFaces.h"
#include "MachineDigits.h"
#include "RetroScene.h"
#include "Theme.h"
#include "ThemeFonts.h"
#include "Vfd.h"

using namespace vfd;

// The five apps of the Tools page. Each draws the whole panel in the current theme (palette and font of the settings
// page: the VFD look with glow in the VFD theme, flat colors in the machine themes), keeps its state in a few statics
// and lets go of everything in exit().

// ---------------------------------------------------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------------------------------------------------

static int textX(const char *s, int p, int x0, int x1) { return (x0 + x1 - textWidth(s, p)) / 2; }

// Filled ellipse (rx, ry) from horizontal spans.
static void ellipse(int cx, int cy, int rx, int ry, uint16_t color)
{
  if (ry < 1) {
    rect(cx - rx, cy, 2 * rx + 1, 1, color);
    return;
  }
  for (int dy = -ry; dy <= ry; dy++) {
    float f = 1.0f - (float)(dy * dy) / (float)(ry * ry);
    int dx = (int)(rx * sqrtf(f < 0 ? 0 : f) + 0.5f);
    rect(cx - dx, cy + dy, 2 * dx + 1, 1, color);
  }
}

static uint16_t rgb(int r, int g, int b) { return px::rgb(r, g, b); }

// ---------------------------------------------------------------------------------------------------------------------
// Machine themes: each tool as a little program of that machine (its font, color bands and pixel size)
// ---------------------------------------------------------------------------------------------------------------------

static bool machine() { return theme::current() != theme::VFD; }

struct MachineInk {
  uint16_t bright, dim, hot, good;
};
static MachineInk minks()
{
  switch (theme::current()) {
    case theme::C64: return { rgb(255, 255, 255), rgb(149, 149, 149), rgb(184, 199, 111), rgb(154, 210, 132) };
    case theme::AMIGA: return { rgb(255, 255, 255), rgb(187, 187, 204), rgb(255, 153, 0), rgb(136, 238, 102) };
    default: return { rgb(236, 236, 236), rgb(160, 160, 200), rgb(252, 188, 116), rgb(92, 186, 92) };
  }
}

// Switches to the machine font while in scope.
struct MachineFont {
  Style saved = style();
  MachineFont()
  {
    auto t = theme::current();
    setStyle({ t == theme::C64 ? &C64_FONT : t == theme::AMIGA ? &AMIGA_FONT : &VFD_FONT, false, false, false });
  }
  ~MachineFont() { setStyle(saved); }
};

// Outlined text, at most w wide: the pitch shrinks until it fits.
static void mtext(const char *s, int x, int y, int p, uint16_t c, int w = W)
{
  while (p > 2 && textWidth(s, p) > w) p--;
  for (int dx = -2; dx <= 2; dx += 2)
    for (int dy = -2; dy <= 2; dy += 2)
      if (dx || dy) text(s, x + dx, y + dy, p, Ink{ rgb(0, 0, 0) });
  text(s, x, y, p, Ink{ c });
}

// Horizontal color bands (raster bars).
static void mbands(int x, int y, int w, int h, const uint16_t *c, int n)
{
  for (int i = 0; i < n; i++) {
    int y0 = y + h * i / n, y1 = y + h * (i + 1) / n;
    rect(x, y0, w, y1 - y0, c[i]);
  }
}


// The weather pages' sky as the backdrop ("stars", "clear"...); plain when there is no asset pack.
static void msky(const char *sky)
{
  if (theme::current() == theme::ATARI) {  // its stars are text-sized blocks: bands only
    static const uint16_t NIGHT[] = { px::rgb(0, 0, 60), px::rgb(0, 8, 88), px::rgb(8, 20, 116), px::rgb(20, 36, 140) };
    mbands(0, 0, W, H, NIGHT, 4);
    return;
  }
  char name[24];
  snprintf(name, sizeof(name), "sky_%s_%s", sky, theme::def().key);
  if (!RetroPicture_Draw(name)) clear();
}

static int blockW() { return theme::current() == theme::C64 ? 4 : theme::current() == theme::ATARI ? 8 : 2; }
static int blockH() { return theme::current() == theme::AMIGA ? 2 : 4; }

// Ellipse on the machine's pixel grid, one color per band from top to bottom.
static void mellipse(int cx, int cy, int rx, int ry, const uint16_t *bands, int n)
{
  int bw = blockW(), bh = blockH();
  for (int dy = -ry; dy < ry; dy += bh) {
    float f = (dy + bh * 0.5f) / (float)ry;
    f = 1.0f - f * f;
    int dx = ((int)(rx * sqrtf(f < 0 ? 0 : f)) + bw / 2) / bw * bw;
    if (!dx) dx = bw;
    rect(cx - dx, cy + dy, 2 * dx, bh, bands[constrain((dy + ry) * n / (2 * ry), 0, n - 1)]);
  }
}
static void mdisc(int cx, int cy, int r, uint16_t c) { mellipse(cx, cy, r, r, &c, 1); }

// ---------------------------------------------------------------------------------------------------------------------
// Spirit level
// ---------------------------------------------------------------------------------------------------------------------

static const char *const ICON_LEVEL[] = { "............", "............", "############", "#..........#", "#...####...#", "#..######..#",
                                          "#...####...#", "#..........#", "############", "............", "....#..#....", "............" };
static const char *const ICON_COIN[] = { "...######...", "..#......#..", ".#...##...#.", "#...####...#", "#..##..##..#", "#..#....#..#",
                                         "#..#....#..#", "#..##..##..#", "#...####...#", ".#...##...#.", "..#......#..", "...######..." };
static const char *const ICON_BREATHE[] = { "############", "#..........#", "#.########.#", "#.#......#.#", "#.#.####.#.#", "#.#.#..#.#.#",
                                            "#.#.#..#.#.#", "#.#.####.#.#", "#.#......#.#", "#.########.#", "#..........#", "############" };
static const char *const ICON_SCORE[] = { "############", "#..........#", "#.###...#..#", "#...#..##..#", "#.###...#..#",
                                          "#.#.....#..#", "#.###..###.#", "#..........#", "############" };
static const char *const ICON_LAMP[] = { "...######...", "..########..", ".##########.", ".##########.", ".##########.", ".##########.",
                                         "..########..", "...######...", "....####....", "....#..#....", "....####....", ".....##....." };

static void iconLevel(int x, int y, uint16_t c) { sprite(ICON_LEVEL, 12, x, y, 4, c); }
static void iconCoin(int x, int y, uint16_t c) { sprite(ICON_COIN, 12, x, y, 4, c); }
static void iconBreathe(int x, int y, uint16_t c) { sprite(ICON_BREATHE, 12, x, y, 4, c); }
static void iconScore(int x, int y, uint16_t c) { sprite(ICON_SCORE, 9, x, y + 6, 4, c); }
static void iconLamp(int x, int y, uint16_t c) { sprite(ICON_LAMP, 12, x, y, 4, c); }

static Preferences tools_prefs;
static bool tools_prefs_open = false;
static void openPrefs()
{
  if (tools_prefs_open) return;
  tools_prefs.begin("tools", false);
  tools_prefs_open = true;
}

static float zero_x = 0, zero_y = 0;  // degrees that count as level (NVS lvx / lvy)
static uint32_t level_note_until = 0;
static bool level_note_clear = false;

static void levelEnter()
{
  openPrefs();
  zero_x = tools_prefs.getFloat("lvx", 0);
  zero_y = tools_prefs.getFloat("lvy", 0);
  level_note_until = 0;
}

// Tilt of the long axis and of the other one, in degrees. Lying flat the second axis is the short edge; standing on
// the long edge it is the lean towards or away from you.
static void levelAngles(float &ax, float &ay, bool &flat)
{
  float gx, gy, gz;
  Motion_Gravity(gx, gy, gz);
  float m = sqrtf(gx * gx + gy * gy + gz * gz);
  if (m < 0.3f) m = 1;
  gx /= m, gy /= m, gz /= m;
  flat = fabsf(gz) > 0.7f;
  ax = asinf(constrain(gx, -1.0f, 1.0f)) * 57.2958f;
  ay = asinf(constrain(flat ? gy : gz, -1.0f, 1.0f)) * 57.2958f;
}

static bool levelGesture(Gesture g, int, int)
{
  if (g != Gesture::Tap && g != Gesture::LongPress) return false;
  openPrefs();
  if (g == Gesture::Tap) {
    float ax, ay;
    bool flat;
    // the zero is kept as the raw angle: undo the current offset first
    levelAngles(ax, ay, flat);
    zero_x = ax;
    zero_y = ay;
    level_note_clear = false;
  } else {
    zero_x = zero_y = 0;
    level_note_clear = true;
  }
  tools_prefs.putFloat("lvx", zero_x);
  tools_prefs.putFloat("lvy", zero_y);
  level_note_until = millis() + 1800;
  Radio_Beep(g == Gesture::Tap ? 1046 : 523, 50);
  return true;
}

// A builder's level: a wooden (2600: brown) beam with a yellow-green vial, and a bull's-eye.
static void levelMachine(float ax, float ay, bool flat, bool level)
{
  MachineFont font;
  MachineInk k = minks();
  auto t = theme::current();
  static const uint16_t W64[] = { px::rgb(67, 57, 0), px::rgb(111, 79, 37), px::rgb(111, 79, 37), px::rgb(154, 103, 89), px::rgb(111, 79, 37), px::rgb(111, 79, 37), px::rgb(67, 57, 0) };
  static const uint16_t WAM[] = { px::rgb(238, 187, 119), px::rgb(204, 136, 68), px::rgb(187, 119, 51), px::rgb(170, 102, 51), px::rgb(153, 85, 34), px::rgb(136, 68, 17), px::rgb(85, 34, 0) };
  static const uint16_t WAT[] = { px::rgb(180, 122, 48), px::rgb(160, 104, 36), px::rgb(136, 92, 16), px::rgb(120, 76, 12), px::rgb(104, 64, 8), px::rgb(88, 52, 0), px::rgb(68, 40, 0) };
  static const uint16_t V64[] = { px::rgb(88, 141, 67), px::rgb(154, 210, 132), px::rgb(184, 199, 111), px::rgb(184, 199, 111), px::rgb(154, 210, 132), px::rgb(88, 141, 67) };
  static const uint16_t VAM[] = { px::rgb(102, 136, 0), px::rgb(170, 204, 17), px::rgb(221, 255, 85), px::rgb(204, 238, 51), px::rgb(170, 204, 17), px::rgb(102, 136, 0) };
  static const uint16_t VAT[] = { px::rgb(92, 120, 24), px::rgb(132, 160, 40), px::rgb(176, 200, 64), px::rgb(176, 200, 64), px::rgb(132, 160, 40), px::rgb(92, 120, 24) };
  const uint16_t *wood = t == theme::C64 ? W64 : t == theme::AMIGA ? WAM : WAT, *vial = t == theme::C64 ? V64 : t == theme::AMIGA ? VAM : VAT;
  clear();
  mbands(0, 38, 430, 64, wood, 7);
  if (t != theme::ATARI)
    for (int i = 0; i < 26; i++) rect((i * 53) % 400, 42 + (i * 23) % 54, 18 + (i * 7) % 22, 2, wood[i % 3 ? 5 : 1]);  // grain
  const int TX = 40, TY = 48, TW = 348, TH = 44, TC = TX + TW / 2;
  rect(TX - 4, TY - 4, TW + 8, TH + 8, 0);
  mbands(TX, TY, TW, TH, vial, 6);
  int bx = constrain(TC - (int)(ax * 14.0f), TX + 24, TX + TW - 25);
  uint16_t air = level ? k.bright : rgb(255, 255, 221);
  uint16_t edge = vial[0];
  mellipse(bx, TY + TH / 2, 22, 18, &edge, 1);
  mellipse(bx, TY + TH / 2, 18, 14, &air, 1);
  for (int x : { TC - 30, TC + 28 }) rect(x, TY, 3, TH, 0);

  const int RX = 528, RY = 88, RR = 64;
  mellipse(RX, RY, RR + 8, RR + 8, wood, 7);
  mdisc(RX, RY, RR, 0);
  mellipse(RX, RY, RR - 4, RR - 4, vial, 6);
  mdisc(RX, RY, 24, vial[0]);
  mdisc(RX, RY, 20, vial[2]);
  float ox = -ax * 5.0f, oy = -ay * 5.0f, len = sqrtf(ox * ox + oy * oy);
  if (len > RR - 20) ox *= (RR - 20) / len, oy *= (RR - 20) / len;
  mdisc(RX + (int)ox, RY + (int)oy, 16, 0);
  mdisc(RX + (int)ox, RY + (int)oy, 12, air);

  if (millis() < level_note_until) mtext(level_note_clear ? T("ZERO CLEARED") : T("ZERO SET"), 24, 14, 2, k.hot);
  else {
    mtext(T("SPIRIT LEVEL"), 24, 14, 2, k.dim);
    if (level) mtext(TCTX("readout", "LEVEL"), 404 - textWidth(TCTX("readout", "LEVEL"), 2), 14, 2, k.good);
  }
  char buf[24];
  snprintf(buf, sizeof(buf), "%+.1f°", ax);
  mtext(buf, 24, 122, 4, level ? k.good : k.bright);
  snprintf(buf, sizeof(buf), "%+.1f°", ay);
  mtext(buf, 262, 132, 3, k.dim);
  mtext(flat ? "X / Y" : "X / Z", 262, 110, 2, k.dim);
}

static void levelDraw(const DateTime &)
{
  clear();
  float ax, ay;
  bool flat;
  levelAngles(ax, ay, flat);
  ax -= zero_x;
  ay -= zero_y;
  bool level = fabsf(ax) < 0.5f && fabsf(ay) < 0.5f;
  if (machine()) return levelMachine(ax, ay, flat, level);
  uint16_t bubble = level ? HOT : LIT;

  // the long vial
  const int TX = 24, TY = 44, TW = 380, TH = 52, TC = TX + TW / 2;
  frame(TX, TY, TW, TH, DIM);
  frame(TX + 1, TY + 1, TW - 2, TH - 2, GHOST);
  rect(TC - 30, TY, 1, TH, DIM);
  rect(TC + 30, TY, 1, TH, DIM);
  rect(TC, TY, 1, 8, DIM);
  rect(TC, TY + TH - 8, 1, 8, DIM);
  int bx = TC - (int)(ax * 15.0f);
  bx = constrain(bx, TX + 22, TX + TW - 23);
  if (style().glow) Apps_Disc(bx, TY + TH / 2, 21, HALO);
  Apps_Disc(bx, TY + TH / 2, 18, bubble);

  // the round vial
  const int RX = 528, RY = 90, RR = 60;
  Apps_Disc(RX, RY, RR, DIM);
  Apps_Disc(RX, RY, RR - 2, BG);
  rect(RX - RR + 4, RY, 2 * RR - 8, 1, GHOST);
  rect(RX, RY - RR + 4, 1, 2 * RR - 8, GHOST);
  Apps_Disc(RX, RY, 20, GHOST);
  Apps_Disc(RX, RY, 18, BG);
  float ox = -ax * 5.0f, oy = -ay * 5.0f, len = sqrtf(ox * ox + oy * oy);
  if (len > RR - 16) ox *= (RR - 16) / len, oy *= (RR - 16) / len;
  if (style().glow) Apps_Disc(RX + (int)ox, RY + (int)oy, 14, HALO);
  Apps_Disc(RX + (int)ox, RY + (int)oy, 12, bubble);

  // angles
  text(T("SPIRIT LEVEL"), TX, 14, 2, DIM_INK);
  if (millis() < level_note_until) {
    const char *n = level_note_clear ? T("ZERO CLEARED") : T("ZERO SET");
    text(n, TX + TW - textWidth(n, 2), 14, 2, HOT_INK);
  } else if (level) {
    text(TCTX("readout", "LEVEL"), TX + TW - textWidth(TCTX("readout", "LEVEL"), 2), 14, 2, HOT_INK);
  }
  char buf[24];
  snprintf(buf, sizeof(buf), "%+.1f°", ax);
  text(buf, TX, 114, 4, LIT_INK);
  snprintf(buf, sizeof(buf), "%+.1f°", ay);
  text(buf, TX + 226, 124, 3, DIM_INK);
  text(flat ? "X / Y" : "X / Z", TX + 226, 102, 2, DIM_INK);
}

static uint32_t levelFrameMs() { return 60; }

// ---------------------------------------------------------------------------------------------------------------------
// Coin toss
// ---------------------------------------------------------------------------------------------------------------------

static constexpr uint32_t FLIP_MS = 1000;
static bool flipping = false;
static uint32_t flip_start = 0, flip_cool = 0;
static int flip_result = 0, last_face = -1;  // 0 = heads, 1 = tails
static uint16_t coin_count[2] = { 0, 0 };

static void coinFlip()
{
  if (flipping) return;
  flipping = true;
  flip_start = millis();
  flip_result = esp_random() & 1;
  state.dirty = true;
}

static void coinEnter()
{
  flipping = false;
  flip_cool = millis() + 600;
}

static void coinTick()
{
  if (flipping && millis() - flip_start >= FLIP_MS) {
    flipping = false;
    last_face = flip_result;
    coin_count[flip_result]++;
    flip_cool = millis() + 800;
    Radio_Beep(flip_result ? 660 : 880, 40);
    state.dirty = true;
  }
  if (!flipping && millis() > flip_cool && Motion_Shake() > 0.55f) coinFlip();
}

static bool coinGesture(Gesture g, int, int)
{
  if (g == Gesture::Tap) {
    coinFlip();
    return true;
  }
  if (g == Gesture::LongPress) {
    coin_count[0] = coin_count[1] = 0;
    last_face = -1;
    Radio_Beep(523, 50);
    return true;
  }
  return false;
}

// A gold coin over the night sky, on the machine's pixel grid.
static void coinMachine(int cx, int cy, int R, int ry, int face)
{
  MachineFont font;
  MachineInk k = minks();
  auto t = theme::current();
  static const uint16_t G64[] = { px::rgb(255, 255, 255), px::rgb(184, 199, 111), px::rgb(184, 199, 111), px::rgb(184, 199, 111), px::rgb(111, 79, 37), px::rgb(111, 79, 37) };
  static const uint16_t GAM[] = { px::rgb(255, 255, 204), px::rgb(255, 238, 136), px::rgb(255, 221, 85), px::rgb(255, 187, 51), px::rgb(238, 153, 17), px::rgb(204, 119, 0), px::rgb(153, 85, 0) };
  static const uint16_t GAT[] = { px::rgb(252, 224, 140), px::rgb(236, 200, 96), px::rgb(216, 172, 64), px::rgb(196, 148, 44), px::rgb(172, 124, 28), px::rgb(148, 100, 16) };
  static const uint16_t S64[] = { px::rgb(255, 255, 255), px::rgb(149, 149, 149), px::rgb(149, 149, 149), px::rgb(149, 149, 149), px::rgb(108, 108, 108), px::rgb(68, 68, 68) };
  static const uint16_t SAM[] = { px::rgb(255, 255, 255), px::rgb(221, 221, 238), px::rgb(204, 204, 221), px::rgb(170, 170, 187), px::rgb(136, 136, 153), px::rgb(102, 102, 119), px::rgb(68, 68, 85) };
  static const uint16_t SAT[] = { px::rgb(236, 236, 236), px::rgb(214, 214, 214), px::rgb(192, 192, 192), px::rgb(170, 170, 170), px::rgb(142, 142, 142), px::rgb(111, 111, 111) };
  const uint16_t *m = face ? (t == theme::C64 ? S64 : t == theme::AMIGA ? SAM : SAT) : t == theme::C64 ? G64 : t == theme::AMIGA ? GAM : GAT;  // heads gold, tails silver
  int n = t == theme::AMIGA ? 7 : 6;
  msky("stars");
  uint16_t black = 0, rim = m[n - 1], mid = m[2];
  mellipse(cx, cy + 4, R + 4, ry + 4, &black, 1);
  mellipse(cx, cy + 6, R, ry, &rim, 1);
  mellipse(cx, cy, R, ry, m, n);
  if (ry > 20) {
    mellipse(cx, cy, R - 8, ry - 8, &rim, 1);
    mellipse(cx, cy, R - 10, ry - 10, &mid, 1);
  }
  if (ry > 38) {
    const char *f = face ? T("T") : T("H");
    text(f, cx - textWidth(f, 6) / 2 + 2, cy - 19, 6, Ink{ m[0] });
    text(f, cx - textWidth(f, 6) / 2, cy - 21, 6, Ink{ rgb(0, 0, 0) });
  }
  const int X0 = 226;
  if (flipping) mtext("...", X0, 40, 6, k.dim);
  else if (last_face < 0) {
    mtext(T("TAP TO FLIP"), X0, 36, 4, k.bright, 400);
    mtext(T("OR SHAKE IT"), X0, 78, 2, k.dim);
  } else mtext(last_face ? T("TAILS") : T("HEADS"), X0, 34, 7, last_face ? k.bright : k.hot, 400);
  char buf[40];
  snprintf(buf, sizeof(buf), "%s %u", T("HEADS"), coin_count[0]);
  mtext(buf, X0, 128, 3, k.hot, 190);
  snprintf(buf, sizeof(buf), "%s %u", T("TAILS"), coin_count[1]);
  mtext(buf, X0 + 204, 128, 3, k.bright, 190);
}

static void coinDraw(const DateTime &)
{
  clear();
  const int CX = 118, CY = 94, R = 54;
  float c = 1, hop = 0;
  int face = last_face < 0 ? 0 : last_face;
  if (flipping) {
    float t = constrain((float)(millis() - flip_start) / FLIP_MS, 0.0f, 1.0f);
    float e = 1.0f - (1.0f - t) * (1.0f - t);
    float turns = 6 + flip_result;  // half turns: even ends heads up, odd tails
    c = cosf(3.14159265f * turns * e);
    face = c >= 0 ? 0 : 1;
    hop = sinf(3.14159265f * t) * 30.0f;
  }
  int ry = max(3, (int)(R * fabsf(c)));
  int cy = CY - (int)hop;
  if (machine()) return coinMachine(CX, cy - 6, R, ry, face);
  if (style().glow) ellipse(CX, cy, R + 3, ry + 3, HALO);
  ellipse(CX, cy + 4, R, ry, DIM);  // the rim
  ellipse(CX, cy, R, ry, face ? HOT : LIT);
  if (ry > 38) {
    const char *f = face ? T("T") : T("H");
    text(f, CX - 12, cy - 17, 5, Ink{ BG });
  }

  const int X0 = 226;
  if (flipping) {
    text("...", X0, 40, 6, DIM_INK);
  } else if (last_face < 0) {
    text(T("TAP TO FLIP"), X0, 48, 4, LIT_INK);
    text(T("OR SHAKE IT"), X0, 84, 3, DIM_INK);
  } else {
    const char *w = last_face ? T("TAILS") : T("HEADS");
    text(w, X0, 30, 7, last_face ? HOT_INK : LIT_INK);
  }
  char buf[40];
  snprintf(buf, sizeof(buf), "%s %u", T("HEADS"), coin_count[0]);
  text(buf, X0, 118, 3, LIT_INK);
  snprintf(buf, sizeof(buf), "%s %u", T("TAILS"), coin_count[1]);
  text(buf, X0 + 190, 118, 3, HOT_INK);
}

static uint32_t coinFrameMs() { return flipping ? 30 : 0; }

// ---------------------------------------------------------------------------------------------------------------------
// Breathing (box: in, hold, out, hold; four seconds each)
// ---------------------------------------------------------------------------------------------------------------------

static constexpr uint32_t PHASE_MS = 4000;
static bool br_running = false, br_beep = true;
static int br_phase = 0, br_cycles = 0;
static uint32_t br_elapsed = 0, br_last = 0;

static void breatheBeep()
{
  if (!br_beep || Radio_On()) return;
  static const int HZ[4] = { 523, 440, 392, 440 };
  Radio_Beep(HZ[br_phase], 45);
}

static void breatheEnter()
{
  br_running = false;
  br_phase = br_cycles = 0;
  br_elapsed = 0;
}

static void breatheTick()
{
  if (!br_running) return;
  uint32_t now = millis();
  br_elapsed += now - br_last;
  br_last = now;
  if (br_elapsed >= PHASE_MS) {
    br_elapsed -= PHASE_MS;
    br_phase = (br_phase + 1) % 4;
    if (br_phase == 0) br_cycles++;
    breatheBeep();
    state.dirty = true;
  }
}

static bool breatheGesture(Gesture g, int, int)
{
  if (g == Gesture::Tap) {
    br_running = !br_running;
    br_last = millis();
    if (br_running) breatheBeep();
    return true;
  }
  if (g == Gesture::LongPress) {
    br_beep = !br_beep;
    if (br_beep && !Radio_On()) Radio_Beep(660, 40);
    return true;
  }
  return false;
}

// The box as nested color frames over a quiet sky.
static void breatheMachine(int CX, int CY, int big, int sz)
{
  MachineFont font;
  MachineInk k = minks();
  auto t = theme::current();
  static const uint16_t A64[] = { px::rgb(108, 94, 181), px::rgb(112, 164, 178), px::rgb(154, 210, 132), px::rgb(255, 255, 255) };
  static const uint16_t B64[] = { px::rgb(104, 55, 43), px::rgb(154, 103, 89), px::rgb(184, 199, 111), px::rgb(255, 255, 255) };
  static const uint16_t AAM[] = { px::rgb(0, 68, 170), px::rgb(0, 119, 221), px::rgb(68, 187, 255), px::rgb(204, 238, 255) };
  static const uint16_t BAM[] = { px::rgb(170, 51, 0), px::rgb(221, 102, 0), px::rgb(255, 170, 34), px::rgb(255, 238, 170) };
  static const uint16_t AAT[] = { px::rgb(24, 60, 140), px::rgb(52, 100, 180), px::rgb(100, 148, 212), px::rgb(164, 208, 236) };
  static const uint16_t BAT[] = { px::rgb(140, 60, 28), px::rgb(184, 100, 52), px::rgb(224, 148, 92), px::rgb(252, 208, 164) };
  const uint16_t *c = br_phase % 2 ? (t == theme::C64 ? B64 : t == theme::AMIGA ? BAM : BAT) : (t == theme::C64 ? A64 : t == theme::AMIGA ? AAM : AAT);
  msky("stars");
  int bw = blockW();
  rect(CX - big / 2 - 4, CY - big / 2 - 4, big + 8, big + 8, 0);
  for (int i = 0; i < 4; i++) frame(CX - big / 2 - 4 + i, CY - big / 2 - 4 + i, big + 8 - 2 * i, big + 8 - 2 * i, i < 2 ? k.dim : 0);
  for (int i = 0; i < 4; i++) {
    int h = sz / 2 * (4 - i) / 4 / bw * bw;
    rect(CX - h, CY - h, 2 * h, 2 * h, c[i]);
  }
  const int X0 = 196;
  bool fresh = !br_running && !br_phase && !br_elapsed && !br_cycles;
  if (fresh) {
    mtext(T("TAP TO START"), X0, 48, 4, k.bright, 424);
    mtext(T("BOX BREATHING 4-4-4-4"), X0, 92, 2, k.dim);
  } else {
    static const char *const EN[4] = { N_("BREATHE IN"), N_("HOLD"), N_("BREATHE OUT"), N_("HOLD") };
    mtext(T(EN[br_phase]), X0, 18, 4, br_phase % 2 ? k.hot : k.bright, 424);
    MachineDigits_Draw(String(4 - (int)(br_elapsed / 1000)), X0, 64, 56, 84, 0, br_phase % 2);
    char buf[24];
    snprintf(buf, sizeof(buf), "%s %d", T("CYCLES"), br_cycles);
    mtext(buf, 290, 84, 3, k.bright, 330);
    if (!br_running) mtext(TCTX("breathe", "PAUSED"), 290, 116, 2, k.hot);
  }
  const char *b = br_beep ? T("BEEP ON") : T("BEEP OFF");
  mtext(b, 620 - textWidth(b, 2), 144, 2, k.dim);
}

static void breatheDraw(const DateTime &)
{
  clear();
  const int CX = 94, CY = 86, BIG = 132, SMALL = 36;
  float t = constrain((float)br_elapsed / PHASE_MS, 0.0f, 1.0f);
  float e = t * t * (3 - 2 * t);
  int sz = br_phase == 0 ? SMALL + (int)((BIG - SMALL) * e) : br_phase == 1 ? BIG : br_phase == 2 ? BIG - (int)((BIG - SMALL) * e) : SMALL;
  if (machine()) return breatheMachine(CX, CY, BIG, sz);
  frame(CX - BIG / 2 - 2, CY - BIG / 2 - 2, BIG + 4, BIG + 4, GHOST);
  if (style().glow) rect(CX - sz / 2 - 2, CY - sz / 2 - 2, sz + 4, sz + 4, HALO);
  rect(CX - sz / 2, CY - sz / 2, sz, sz, br_phase % 2 ? HOT : LIT);

  const int X0 = 190;
  bool fresh = !br_running && !br_phase && !br_elapsed && !br_cycles;
  static const char *const EN[4] = { N_("BREATHE IN"), N_("HOLD"), N_("BREATHE OUT"), N_("HOLD") };
  if (fresh) {
    text(T("TAP TO START"), X0, 52, 3, LIT_INK);
    text(T("BOX BREATHING 4-4-4-4"), X0, 90, 2, DIM_INK);
  } else {
    text(T(EN[br_phase]), X0, 24, 5, br_phase % 2 ? HOT_INK : LIT_INK);
    char n[4];
    snprintf(n, sizeof(n), "%d", 4 - (int)(br_elapsed / 1000));
    text(n, X0, 84, 8, br_running ? LIT_INK : DIM_INK);
    if (!br_running) text(TCTX("breathe", "PAUSED"), X0 + 70, 100, 2, DIM_INK);
  }
  if (!fresh) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%s %d", T("CYCLES"), br_cycles);
    text(buf, 360, 100, 3, DIM_INK);
  }
  text(br_beep ? T("BEEP ON") : T("BEEP OFF"), 440, 140, 2, br_beep ? DIM_INK : GHOST_INK);
}

static uint32_t breatheFrameMs() { return br_running ? 40 : 0; }

// ---------------------------------------------------------------------------------------------------------------------
// Scoreboard
// ---------------------------------------------------------------------------------------------------------------------

static uint8_t score[2] = { 0, 0 };
static uint32_t reset_armed_until = 0;
static constexpr int RESET_X = 262, RESET_Y = 8, RESET_W = 116, RESET_H = 28;

static bool inReset(int x, int y) { return x >= RESET_X - 10 && x < RESET_X + RESET_W + 10 && y < RESET_Y + RESET_H + 10; }

static void scoreTick()
{
  if (reset_armed_until && (int32_t)(millis() - reset_armed_until) >= 0) {
    reset_armed_until = 0;
    state.dirty = true;
  }
}

static bool scoreGesture(Gesture g, int x, int y)
{
  if (g == Gesture::Tap) {
    if (inReset(x, y)) {
      if (reset_armed_until && (int32_t)(millis() - reset_armed_until) < 0) {  // the second tap within 2 s
        score[0] = score[1] = 0;
        reset_armed_until = 0;
        Radio_Beep(523, 60);
      } else {
        reset_armed_until = millis() + 2000;
      }
      return true;
    }
    int h = x < W / 2 ? 0 : 1;
    if (score[h] < 99) score[h]++;
    return true;
  }
  if (g == Gesture::LongPress) {
    if (inReset(x, y)) return true;
    int h = x < W / 2 ? 0 : 1;
    if (score[h] > 0) score[h]--;
    return true;
  }
  return false;
}

// Two courts, one color each, big cartridge digits.
static void scoreMachine(bool armed)
{
  MachineFont font;
  MachineInk k = minks();
  auto t = theme::current();
  static const uint16_t L64[] = { px::rgb(53, 40, 121), px::rgb(108, 94, 181) }, R64[] = { px::rgb(104, 55, 43), px::rgb(154, 103, 89) };
  static const uint16_t LAM[] = { px::rgb(0, 17, 85), px::rgb(0, 34, 119), px::rgb(0, 68, 153), px::rgb(17, 102, 187), px::rgb(68, 136, 204) };
  static const uint16_t RAM[] = { px::rgb(68, 0, 17), px::rgb(102, 0, 17), px::rgb(136, 17, 17), px::rgb(170, 51, 17), px::rgb(204, 85, 34) };
  static const uint16_t LAT[] = { px::rgb(0, 28, 100), px::rgb(20, 48, 124), px::rgb(40, 68, 144), px::rgb(60, 88, 164) };
  static const uint16_t RAT[] = { px::rgb(92, 20, 12), px::rgb(116, 36, 20), px::rgb(140, 56, 32), px::rgb(164, 76, 44) };
  const int mid = W / 2;
  if (t == theme::C64) {  // two hires colors per court, blended by a checker of double-wide pixels
    for (int h = 0; h < 2; h++) {
      const uint16_t *c = h ? R64 : L64;
      rect(h * mid, 0, mid, H, c[0]);
      for (int y = 120; y < H; y += 4)
        for (int x = h * mid + ((y / 4) & 1) * 4; x < (h + 1) * mid; x += y < 144 ? 16 : 8) rect(x, y, 4, 4, c[1]);
    }
  } else {
    bool am = t == theme::AMIGA;
    mbands(0, 0, mid, H, am ? LAM : LAT, am ? 5 : 4);
    mbands(mid, 0, W - mid, H, am ? RAM : RAT, am ? 5 : 4);
  }
  for (int y = 44; y < H; y += 16) rect(mid - 4, y, 8, 8, k.bright);  // the net
  for (int h = 0; h < 2; h++) MachineDigits_Draw(String(score[h]), (h ? W * 3 / 4 : W / 4) - (score[h] > 9 ? 78 : 35), 40, 70, 104, 16, h && t != theme::C64);  // the C64's red would sink into the red court
  uint16_t face = armed ? k.hot : 0;
  rect(RESET_X, RESET_Y, RESET_W, RESET_H, face);
  if (t == theme::AMIGA && !armed) {
    rect(RESET_X, RESET_Y, RESET_W, RESET_H, rgb(85, 85, 102));
    rect(RESET_X, RESET_Y, RESET_W, 2, k.bright);
    rect(RESET_X, RESET_Y, 2, RESET_H, k.bright);
    rect(RESET_X, RESET_Y + RESET_H - 2, RESET_W, 2, 0);
    rect(RESET_X + RESET_W - 2, RESET_Y, 2, RESET_H, 0);
  } else frame(RESET_X, RESET_Y, RESET_W, RESET_H, k.bright);
  const char *label = armed ? T("AGAIN?") : T("RESET");
  text(label, textX(label, 2, RESET_X, RESET_X + RESET_W), RESET_Y + 7, 2, Ink{ armed ? rgb(0, 0, 0) : k.bright });
}

static void scoreDraw(const DateTime &)
{
  if (machine()) return scoreMachine(reset_armed_until && (int32_t)(millis() - reset_armed_until) < 0);
  clear();
  rect(W / 2 - 1, 46, 2, 112, GHOST);
  for (int h = 0; h < 2; h++) {
    char d[4];
    snprintf(d, sizeof(d), "%2d", score[h]);
    int cx = h ? W * 3 / 4 : W / 4;
    Ink ink = h ? Ink{ HOT, HOTH, GHOST } : Ink{ LIT, HALO, GHOST };
    seg(d, cx - 76, 44, 70, 112, 14, 12, ink);
  }
  // the reset key, top middle: tap it twice within two seconds
  bool armed = reset_armed_until && (int32_t)(millis() - reset_armed_until) < 0;
  uint16_t edge = armed ? HOT : DIM;
  frame(RESET_X, RESET_Y, RESET_W, RESET_H, edge);
  const char *label = armed ? T("AGAIN?") : T("RESET");
  text(label, textX(label, 2, RESET_X, RESET_X + RESET_W), RESET_Y + 7, 2, armed ? HOT_INK : DIM_INK);
}

static uint32_t scoreFrameMs() { return reset_armed_until ? 200 : 0; }

// ---------------------------------------------------------------------------------------------------------------------
// Lamp
// ---------------------------------------------------------------------------------------------------------------------

struct LampColor {
  const char *en;
  uint8_t r, g, b;
};
static const LampColor LAMP[] = {
  { N_("WARM WHITE"), 255, 190, 110 }, { N_("COOL WHITE"), 205, 225, 255 }, { N_("AMBER"), 255, 130, 0 },
  { N_("RED"), 255, 0, 0 },                { N_("GREEN"), 0, 230, 40 },                { N_("BLUE"), 20, 60, 255 },
  { N_("RAINBOW"), 0, 0, 0 },
};
static constexpr int LAMP_COUNT = sizeof(LAMP) / sizeof(LAMP[0]), LAMP_RAINBOW = LAMP_COUNT - 1;
static int lamp_idx = 0, lamp_prev_bright = -1;
static uint32_t lamp_name_until = 0;

static void lampEnter()
{
  lamp_prev_bright = board().brightness();
  lamp_name_until = millis() + 1500;
}

static void lampExit()
{
  if (lamp_prev_bright >= 0) board().setBrightness(lamp_prev_bright, false);
  lamp_prev_bright = -1;
}

static void lampTick()
{
  if (lamp_name_until && (int32_t)(millis() - lamp_name_until) >= 0) {
    lamp_name_until = 0;
    state.dirty = true;
  }
}

static bool lampGesture(Gesture g, int, int)
{
  int b = board().brightness();
  switch (g) {
    case Gesture::Tap:
      lamp_idx = (lamp_idx + 1) % LAMP_COUNT;
      lamp_name_until = millis() + 1500;
      return true;
    case Gesture::SwipeUp:
      if (b >= 0) board().setBrightness(min(100, (b / 10 + 1) * 10), true);
      return true;
    case Gesture::SwipeDown:
      if (b >= 0) board().setBrightness(max(5, (b + 9) / 10 * 10 - 10), true);
      return true;
    default: return true;
  }
}

// hue 0..359 at full saturation and value
static uint16_t hue565(int h)
{
  int s = h / 60, f = (h % 60) * 255 / 60;
  int up = f, down = 255 - f;
  int r, g, b;
  switch (s) {
    case 0: r = 255, g = up, b = 0; break;
    case 1: r = down, g = 255, b = 0; break;
    case 2: r = 0, g = 255, b = up; break;
    case 3: r = 0, g = down, b = 255; break;
    case 4: r = up, g = 0, b = 255; break;
    default: r = 255, g = 0, b = down; break;
  }
  return rgb(r, g, b);
}

static void lampDraw(const DateTime &)
{
  const LampColor &lc = LAMP[lamp_idx];
  uint16_t color = lamp_idx == LAMP_RAINBOW ? hue565((millis() / 45) % 360) : rgb(lc.r, lc.g, lc.b);
  clear(color);
  if (lamp_name_until && (int32_t)(millis() - lamp_name_until) < 0) {
    int lum = lamp_idx == LAMP_RAINBOW ? 200 : (lc.r * 3 + lc.g * 6 + lc.b) / 10;
    uint16_t ink = lum > 110 ? 0 : 0xFFFF;
    const char *n = T(lc.en);
    int p = 3;
    MachineFont font;  // the VFD look keeps its own style
    if (!machine()) setStyle(font.saved);
    rect(textX(n, p, 0, W) - 10, 126, textWidth(n, p) + 20, 34, color);
    text(n, textX(n, p, 0, W), 132, p, Ink{ ink });
  }
}

static uint32_t lampFrameMs() { return lamp_idx == LAMP_RAINBOW ? 50 : 0; }

// ---------------------------------------------------------------------------------------------------------------------

const App APP_LEVEL = { "level", N_("LEVEL"), iconLevel, false, false, levelEnter, nullptr, levelDraw, levelGesture, nullptr, levelFrameMs };
const App APP_COIN = { "coin", N_("COIN"), iconCoin, false, false, coinEnter, nullptr, coinDraw, coinGesture, coinTick, coinFrameMs };
const App APP_BREATHE = { "breathe", N_("BREATHE"), iconBreathe, false, false, breatheEnter, nullptr, breatheDraw, breatheGesture, breatheTick, breatheFrameMs };
const App APP_SCORE = { "score", N_("SCORE"), iconScore, false, false, nullptr, nullptr, scoreDraw, scoreGesture, scoreTick, scoreFrameMs };
const App APP_LAMP = { "lamp", N_("LAMP"), iconLamp, false, true, lampEnter, lampExit, lampDraw, lampGesture, lampTick, lampFrameMs };

#endif
