#ifdef BOARD_TOUCH349

#include "Study.h"
#include "Px.h"
#include "ClockFaces.h"
#include "MachineDigits.h"
#include "RetroScene.h"
#include "Radio.h"
#include "Vfd.h"
#include "Weather.h"
#include "Theme.h"

using namespace px;

#include "Settings.h"

// pomodoro lengths come from the settings page
#define FOCUS_MS (settings::focusMin() * 60000UL)
#define BREAK_MS (settings::shortMin() * 60000UL)
#define LONG_MS (settings::longMin() * 60000UL)
static constexpr int WX = 12, WY = 6, WW = 84, WH = 44;  // window (inside the frame)

static bool running = false, on_break = false;
static uint32_t left_ms = 25 * 60000UL, last_ms = 0;
static bool left_set = false;
static int done_count = 0;
static uint32_t frame_no = 0;

struct Drop {
  float x, y, v;
};
static Drop drops[70];
static bool drops_ready = false;

static uint32_t hash32(uint32_t v)
{
  v ^= v >> 16;
  v *= 0x7feb352d;
  v ^= v >> 15;
  v *= 0x846ca68b;
  return v ^ (v >> 16);
}
static float frand() { return (esp_random() & 0xffff) / 65535.0f; }

void Study_Tap()
{
  running = !running;
  last_ms = millis();
  Radio_Beep(running ? 1046 : 660, 40);
}

void Study_Reset()
{
  running = false;
  on_break = false;
  left_ms = FOCUS_MS;
  done_count = 0;
  Radio_Beep(523, 60);
}

void Study_Tick()
{
  static uint32_t seen_ver = 0;
  if (!running && !on_break && done_count == 0 && (!left_set || seen_ver != settings::version())) {
    left_ms = FOCUS_MS;  // fresh and idle: show the focus length from the settings
    left_set = true;
    seen_ver = settings::version();
  }
  uint32_t now = millis();
  uint32_t dt = now - last_ms;
  last_ms = now;
  if (!running) return;
  if (dt >= left_ms) {
    if (!on_break) {
      done_count++;
      on_break = true;
      left_ms = done_count % 4 == 0 ? LONG_MS : BREAK_MS;
      Radio_Beep(784, 120, 3, 110);
    } else {
      on_break = false;
      left_ms = FOCUS_MS;
      Radio_Beep(1046, 120, 2, 110);
    }
  } else {
    left_ms -= dt;
  }
}

static constexpr int DESK_Y = 60;  // desk top
static constexpr int FLOOR_Y = 80;  // where the floor starts
static constexpr int PANEL_X = 216;  // logical x where the pomodoro panel starts

static void window(bool rain)
{
  // night outside: sky, far rooftops with lit windows, snow on the roofs
  static const uint8_t SKY[] = { NIGHT, N1, N1, N2 };
  for (int y = WY; y < WY + WH; y++) {
    int band = (y - WY) * 4 / WH;
    for (int x = WX; x < WX + WW; x++) buf[y][x] = SKY[band];
  }
  disc(WX + 70, WY + 8, 3, N5);  // moon
  for (int b = 0; b < 7; b++) {
    uint32_t h = hash32(b * 41 + 7);
    int bx = WX + b * 13 - 2, bw = 11 + h % 5, top = WY + 24 + h % 12;
    for (int y = top; y < WY + WH; y++)
      for (int x = max(bx, WX); x < min(bx + bw, WX + WW); x++) buf[y][x] = N2;
    for (int x = max(bx, WX); x < min(bx + bw, WX + WW); x++) buf[top][x] = rain ? N3 : N5;
    for (int y = top + 3; y < WY + WH - 2; y += 4)
      for (int x = bx + 2; x < bx + bw - 2; x += 4)
        if (x >= WX && x < WX + WW - 1 && hash32(x * 13 + y * 7) % 3 == 0) set(x, y, F3), set(x + 1, y, F3);
  }
  if (!drops_ready) {
    for (auto &d : drops) d = { WX + frand() * WW, WY + frand() * WH, 0.15f + frand() * 0.25f };
    drops_ready = true;
  }
  float t = frame_no * 0.04f;
  for (int i = 0; i < 70; i++) {
    Drop &d = drops[i];
    if (rain) {
      d.y += d.v * 8;
      d.x -= 0.6f;
    } else {
      d.y += d.v;
      d.x += sinf(t + i) * 0.12f;
    }
    if (d.y >= WY + WH) d.y = WY, d.x = WX + frand() * WW;
    if (d.x < WX) d.x += WW;
    int x = (int)d.x, y = (int)d.y;
    if (x < WX || x >= WX + WW) continue;
    if (rain) {
      set(x, y, N4);
      if (y > WY) set(x + 1, y - 1, N3);
    } else {
      set(x, y, i % 3 ? N6 : N5);
    }
  }
  if (!rain)
    for (int x = WX; x < WX + WW; x++) set(x, WY + WH - 1, (x * 7 % 5) ? N6 : N5);
  // frame, mullions, sill
  rect(WX - 3, WY - 3, WW + 6, 3, D2);
  rect(WX - 3, WY, 3, WH, D2);
  rect(WX + WW, WY, 3, WH, D2);
  rect(WX + WW / 2 - 1, WY, 2, WH, D2);
  rect(WX, WY + WH / 2 - 1, WW, 2, D2);
  rect(WX - 5, WY + WH, WW + 10, 3, D3);
  // a little plant on the sill
  rect(WX + 4, WY + WH - 5, 7, 5, R2);
  for (int k = 0; k < 7; k++) set(WX + 4 + k, WY + WH - 6 - (k % 3), G3), set(WX + 7, WY + WH - 9 + k / 3, G2);
}

// The room behind the student: a cork board with notes, a full bookshelf with a plant on top, a backpack on the floor.
static void room()
{
  // cork board
  rect(134, 10, 22, 24, D3);
  rect(136, 12, 18, 20, F1);
  static const uint8_t NOTE[] = { N6, Y1, C3, N6 };
  static const int NP[][2] = { { 138, 14 }, { 146, 15 }, { 139, 23 }, { 147, 24 } };
  for (int i = 0; i < 4; i++) {
    rect(NP[i][0], NP[i][1], 6, 6, NOTE[i]);
    rect(NP[i][0] + 1, NP[i][1] + 2, 4, 1, N3), rect(NP[i][0] + 1, NP[i][1] + 4, 3, 1, N3);
    set(NP[i][0] + 2, NP[i][1], M2);
  }
  // bookshelf
  const int sx = 162, sw = 46, top = 16;
  rect(sx, top, sw, FLOOR_Y - top, D2);
  rect(sx + 2, top + 2, sw - 4, FLOOR_Y - top - 2, D1);
  static const uint8_t BC[] = { U2, R2, G2, F2, P1, M1, C2, N4, R3, U1 };
  for (int sh = 0; sh < 4; sh++) {
    int base = top + 16 + sh * 16;  // shelf board
    rect(sx, base, sw, 2, D3);
    int x = sx + 3;
    for (int k = 0; x < sx + sw - 5; k++) {
      uint32_t r = hash32(sh * 97 + k * 13 + 5);
      if (sh == 1 && k == 4) {  // a globe on the second shelf
        disc(x + 4, base - 6, 4, U2);
        rect(x + 2, base - 7, 5, 1, G2), rect(x + 3, base - 5, 3, 1, G2);
        rect(x + 3, base - 1, 3, 1, N4);
        x += 10;
        continue;
      }
      int bw = 2 + r % 3, bh = 9 + (r >> 4) % 5;
      if (sh == 2 && k == 3) {  // one book leaning
        for (int j = 0; j < bh; j++) rect(x + j / 3, base - 1 - j, 3, 1, BC[(r >> 8) % 10]);
        x += 6;
        continue;
      }
      rect(x, base - bh, bw, bh, BC[(r >> 8) % 10]);
      rect(x, base - bh + 2, bw, 1, N5);  // spine band
      x += bw + ((r >> 12) % 4 == 0 ? 1 : 0);
    }
  }
  // plant on top of the shelf
  rect(sx + 30, top - 7, 9, 7, R2);
  rect(sx + 29, top - 8, 11, 2, R1);
  for (int k = 0; k < 5; k++)
    for (int j = 0; j < 12; j++) {
      float a = -2.6f + k * 0.55f;
      set(sx + 34 + (int)(cosf(a) * j * 0.7f), top - 9 - (int)(fabsf(sinf(a)) * j * 0.8f) + j * j / 24, j % 3 ? G2 : G3);
    }
  // backpack slumped against the wall
  rect(136, FLOOR_Y - 14, 16, 14, M1);
  rect(137, FLOOR_Y - 15, 14, 1, M1);
  rect(138, FLOOR_Y - 8, 12, 6, P1);
  rect(138, FLOOR_Y - 8, 12, 1, M2);
  rect(141, FLOOR_Y - 18, 6, 3, M1), rect(142, FLOOR_Y - 17, 4, 1, N1);  // handle
}

static void desk()
{
  // seen from the side: a tabletop, a small drawer under it, two legs to the floor
  rect(4, DESK_Y, 104, 3, D3);
  rect(4, DESK_Y, 104, 1, N4);
  rect(6, DESK_Y + 3, 78, 2, D2);
  rect(10, DESK_Y + 5, 34, 7, D2);
  rect(10, DESK_Y + 11, 34, 1, D1);
  rect(25, DESK_Y + 8, 4, 1, N4);
  rect(8, DESK_Y + 3, 4, FLOOR_Y - DESK_Y - 3, D2);
  rect(80, DESK_Y + 3, 4, FLOOR_Y - DESK_Y - 3, D2);
  rect(12, FLOOR_Y - 6, 68, 1, D1);
  // books, stacked under the window
  static const uint8_t BK[] = { U2, R2, G2, F2, P1 };
  for (int b = 0; b < 5; b++) {
    int w = 18 + (b * 7) % 9;
    rect(16 + (b % 2) * 2, DESK_Y - 3 - b * 3, w, 3, BK[b]);
    rect(16 + (b % 2) * 2, DESK_Y - 3 - b * 3, w, 1, N5);
  }
  // mug with steam
  rect(46, DESK_Y - 8, 7, 8, C2);
  rect(46, DESK_Y - 8, 7, 1, C3);
  rect(53, DESK_Y - 6, 2, 4, C2);
  for (int s = 0; s < 2; s++)
    for (int k = 0; k < 8; k++) {
      int y = DESK_Y - 10 - k - (int)(frame_no * 0.3f + s * 5) % 6;
      int x = 48 + s * 3 + (int)(sinf(frame_no * 0.08f + k * 0.5f + s * 2) * 1.6f);
      set(x, y, k < 4 ? N4 : N3);
    }
  // open notebook where the hand writes
  rect(76, DESK_Y - 2, 26, 2, N6);
  rect(88, DESK_Y - 2, 1, 2, N4);
  for (int k = 0; k < 3; k++) rect(78 + k * 3, DESK_Y - 2, 2, 1, N4);
  // desk lamp: base, two arms, shade over the notebook, and its light
  rect(60, DESK_Y - 2, 10, 2, N3);
  for (int k = 0; k < 16; k++) set(64 + k / 4, DESK_Y - 3 - k, N4);    // lower arm up
  for (int k = 0; k < 14; k++) set(68 + k, DESK_Y - 19 + k / 3, N4);    // upper arm across
  for (int y = 0; y < 5; y++) rect(80 - y, DESK_Y - 16 + y, 6 + y * 2, 1, y < 4 ? F2 : F4);
  rect(77, DESK_Y - 11, 12, 1, F5);
  for (int y = DESK_Y - 10; y < DESK_Y; y++) {  // light cone onto the page
    int hw = 6 + (y - (DESK_Y - 10)) * 7 / 10;
    for (int x = 83 - hw; x <= 83 + hw; x++)
      if (((x + y) & 1) == 0 && get(x, y) == N1) set(x, y, F1);
  }
}

static const char *const PERSON[] = {
  "......hhhhkkh...........",
  ".....hhhhhkkhhh.........",
  "....hhhhhhkkhhhh........",
  "...hhhhhhhkkhhhhh.......",
  "...hhhhhhKKKKhhhh.......",
  "..zshhhhhKqqKhhhh.......",
  "..sesshhhKqqKhhhh.......",
  ".ssssssshKKKKhhh........",
  "..sssssssshhhhh.........",
  "..zsssssssshhh..........",
  "...zssssss.hh...........",
  "....sssss...............",
  ".....zsss..MMM..........",
  "....mmmmmmMMMMMm........",
  "...mmmmmmmmmMMMMm.......",
  "...mmmmmmmmmmMMMm.......",
  "..mmmMmmmmmmmmMMmm......",
  "..mmmMmmmmmmmmmMmm......",
  "..mmmMMmmmmmmmmmmm......",
  "..mmmmMmmmmmmmmmmm......",
  "..mmmmMMmmmmmmmmmm......",
  "..wmmmmMmmmmmmmmmm......",
  "..wmmmmMMmmmmmmmmm......",
  "...mmmmmMmmmmmmmmm......",
  "...mmmmmMMmmmmmmmm......",
  "...mmmmmmMmmmmmmm.......",
  "...mmmmmmMMmmmmmm.......",
  "...mmmmmmmMmmmmmm.......",
  "...mmmmmmmMMmmmmm.......",
  "...mmmmmmmmMmmmmm.......",
  "...mmmmmmmmMMmmmm.......",
  "....mmmmmmmmMmmmm.......",
  "....mmmmmmmmmmmmm.......",
  "....mmmmmmmmmmmmm.......",
  "....mmmmmmmmmmmmm.......",
  "....mmmmmmmmmmmmm......."
};
static const char PERSON_KEY[] = "hzsekKqmMw";
static const uint8_t PERSON_VALS[] = { N2, S2, S3, N1, C1, C2, C3, P1, M1, N6 };

static void person()
{
  float t = frame_no * 0.05f;
  const int ox = 104, oy = DESK_Y - 36;
  // chair: backrest behind the back, seat, two legs
  rect(ox + 19, oy + 12, 3, DESK_Y + 8 - oy - 12, D2);
  rect(ox + 18, oy + 11, 5, 2, D3);
  rect(ox + 1, DESK_Y + 8, 24, 2, D3);
  rect(ox + 3, DESK_Y + 10, 2, FLOOR_Y - DESK_Y - 10, D2);
  rect(ox + 20, DESK_Y + 10, 2, FLOOR_Y - DESK_Y - 10, D2);
  // sitting: hips on the seat, thigh under the desk, shin down, shoe on the floor
  rect(ox + 3, DESK_Y, 15, 8, U2);
  rect(ox - 12, DESK_Y + 4, 18, 4, U2);
  rect(ox - 12, DESK_Y + 4, 18, 1, U1);
  rect(ox - 12, DESK_Y + 4, 4, FLOOR_Y - DESK_Y - 6, U2);
  rect(ox - 17, FLOOR_Y - 2, 9, 2, N2);
  int bob = on_break ? 0 : ((int)(t * 1.9f) % 2);  // nodding to the music
  if (on_break) {  // leaning back, arms up
    sprite(PERSON + 12, 24, ox + 1, oy + 12, PERSON_KEY, PERSON_VALS);
    sprite(PERSON, 12, ox + 2, oy, PERSON_KEY, PERSON_VALS);
    for (int k = 0; k < 12; k++) set(ox + 6 - k / 3, oy + 14 - k, P1), set(ox + 7 - k / 3, oy + 14 - k, P1);
    rect(ox + 1, oy, 3, 3, S3);
    return;
  }
  sprite(PERSON + 12, 24, ox, oy + 12, PERSON_KEY, PERSON_VALS);
  sprite(PERSON, 12, ox, oy + bob, PERSON_KEY, PERSON_VALS);
  // forearm on the desk to the notebook, the hand writing
  int hx = 92 + (int)(sinf(t * 2.6f) * 3);
  for (int x = hx + 3; x < ox + 12; x++) set(x, DESK_Y - 4, M1), set(x, DESK_Y - 3, P1);
  rect(hx, DESK_Y - 4, 3, 2, S3);
  set(hx - 1, DESK_Y - 5, N5), set(hx - 2, DESK_Y - 6, N5);  // pen
}

namespace {

// Where the mug's rim is on each study picture (measured on design/retro-proto/out/study_*.png), and the machine's
// pixel grid: the steam and every panel rect snap to it.
struct MachineStudy {
  const char *picture;
  int mug_x, mug_y;
  int cw, ch;
};
const MachineStudy MACHINE_STUDY[] = {
  { "study_c64", 178, 126, 4, 2 },
  { "study_amiga", 155, 124, 2, 2 },
  { "study_atari", 212, 128, 8, 4 },
};

constexpr int MPX = 440;  // the machine panel starts here (the pictures end at ~435)

uint16_t C(int r, int g, int b) { return px::rgb(r, g, b); }

// Halves every channel: Amiga extra-half-brite, the picture dimmed inside the window.
void halve(int x0, int y0, int x1, int y1)
{
  uint16_t *cv = vfd::canvas();
  for (int y = y0; y < y1; y++)
    for (int x = x0; x < x1; x++) {
      uint16_t &p = cv[y * vfd::W + x];
      p = (p >> 1) & 0x7BEF;
    }
}

// Two or three puffs rising from the mug and thinning out, drawn as the machine's own pixels.
void steam(const MachineStudy &m, theme::Id id)
{
  for (int i = 0; i < 3; i++) {
    float p = ((frame_no + i * 16) % 48) / 48.0f;
    if (p > 0.9f) continue;
    int x = m.mug_x + (i - 1) * 5 + (int)(sinf(p * 6.28f + i * 2.0f) * 3);
    int y = m.mug_y - 4 - (int)(p * 28);
    x -= x % m.cw;
    y -= y % m.ch;
    uint16_t c;
    int w = 2 * m.cw, h = 2 * m.ch;
    if (id == theme::AMIGA) {  // three soft whites
      c = p < 0.3f ? C(255, 255, 255) : p < 0.6f ? C(204, 204, 221) : C(136, 136, 170);
      w = 3 * m.cw;
    } else if (id == theme::C64) {
      c = p < 0.55f ? C(255, 255, 255) : C(149, 149, 149);
    } else {
      c = C(236, 236, 236);  // one flat block
      w = m.cw, h = m.ch;
    }
    vfd::rect(x, y, w, h, c);
  }
}

// Shared panel numbers.
void clockText(char *out, size_t n)
{
  uint32_t secs = (left_ms + 999) / 1000;
  snprintf(out, n, "%02lu:%02lu", (unsigned long)(secs / 60), (unsigned long)(secs % 60));
}
uint32_t phaseTotal() { return on_break ? (done_count % 4 == 0 ? LONG_MS : BREAK_MS) : FOCUS_MS; }
int progressFill(int width)
{
  uint32_t total = phaseTotal();
  return (int)((uint64_t)(total - min(left_ms, total)) * width / total);
}
bool roundDone(int i) { return i < done_count % 4 || (done_count > 0 && done_count % 4 == 0 && on_break); }
const char *phaseLabel() { return on_break ? (done_count % 4 == 0 ? T("LONG BREAK") : T("BREAK")) : T("FOCUS"); }

void hints(int x)
{
  vfd::text(T("TAP: START / PAUSE"), x, 148, 1, vfd::Ink{ vfd::DIM });
  vfd::text(T("HOLD: RESET"), x, 158, 1, vfd::Ink{ vfd::DIM });
}

void cornerText(const char *s, int right, int y, uint16_t color)
{
  if (s && *s) vfd::text(s, right - vfd::textWidth(s, 2), y, 2, vfd::Ink{ color });
}

// C64: black panel, wash-blocks digits, two greens and a grey.
void panelC64(const char *corner)
{
  const uint16_t green = C(154, 210, 132), cyan = C(112, 164, 178), grey = C(68, 68, 68);
  vfd::rect(MPX, 0, vfd::W - MPX, vfd::H, 0);
  uint16_t pc = on_break ? cyan : green;
  int lx = vfd::text(phaseLabel(), 448, 10, 2, vfd::Ink{ pc });
  if (running) {
    for (int k = 0; k < 7; k++) vfd::rect(lx + 10 + k, 10 + k, 1, 14 - 2 * k, C(255, 255, 255));
  } else {
    vfd::rect(lx + 10, 10, 4, 14, C(149, 149, 149)), vfd::rect(lx + 18, 10, 4, 14, C(149, 149, 149));
  }
  cornerText(corner, 630, 10, C(149, 149, 149));
  char digits[8];
  clockText(digits, sizeof(digits));
  MachineDigits_Draw(digits, 448, 36, 34, 56, 3);
  const int pw = 184, fill = progressFill(pw) / 4 * 4;
  vfd::rect(448, 104, pw, 6, grey);
  vfd::rect(448, 104, fill, 6, green);
  int tx = vfd::text(T("ROUND"), 448, 122, 2, vfd::Ink{ pc });
  for (int i = 0; i < 4; i++) vfd::rect(tx + 12 + i * 24, 122, 16, 14, roundDone(i) ? green : grey);
  hints(448);
}

// Amiga: a Workbench window over the (dimmed) picture, chrome digits, bevelled copper bar and buttons.
void panelAmiga(const char *corner)
{
  const uint16_t light = C(238, 238, 255), white = C(255, 255, 255);
  const int x0 = 438, y0 = 2, x1 = 634, y1 = 170;
  halve(x0, y0, x1, y1);
  vfd::rect(x0, y0, x1 - x0, 1, light), vfd::rect(x0, y0, 1, y1 - y0, light);
  vfd::rect(x0, y1 - 1, x1 - x0, 1, 0), vfd::rect(x1 - 1, y0, 1, y1 - y0, 0);
  // title strip: orange while focusing, cyan on a break, a light line under it
  vfd::rect(x0 + 1, y0 + 1, x1 - x0 - 2, 22, on_break ? C(0, 119, 153) : C(204, 102, 0));
  vfd::rect(x0 + 1, y0 + 23, x1 - x0 - 2, 1, light);
  int lx = vfd::text(phaseLabel(), 446, 6, 2, vfd::Ink{ white });
  if (running) {
    for (int k = 0; k < 7; k++) vfd::rect(lx + 10 + k, 6 + k, 1, 14 - 2 * k, white);
  } else {
    vfd::rect(lx + 10, 6, 4, 14, white), vfd::rect(lx + 18, 6, 4, 14, white);
  }
  cornerText(corner, 626, 6, white);
  char digits[8];
  clockText(digits, sizeof(digits));
  MachineDigits_Draw(digits, 450, 36, 32, 56, 4);
  // progress: a sunken trough with a copper gradient (orange to yellow, 12-bit steps)
  const int bx = 448, bw = 180, by = 100, bh = 12;
  vfd::rect(bx, by, bw, bh, 0);
  vfd::rect(bx, by, bw, 1, C(34, 34, 51)), vfd::rect(bx, by, 1, bh, C(34, 34, 51));
  vfd::rect(bx, by + bh - 1, bw, 1, light), vfd::rect(bx + bw - 1, by, 1, bh, light);
  const int in = bw - 4, fill = progressFill(in) & ~1;
  for (int x = 0; x < fill; x += 2) {
    int g = 136 + 85 * x / in;
    vfd::rect(bx + 2 + x, by + 2, 2, bh - 4, C(255, (g & 0xF0) | (g >> 4), 0));
  }
  int tx = vfd::text(T("ROUND"), 446, 122, 2, vfd::Ink{ light });
  for (int i = 0; i < 4; i++) {
    int x = tx + 12 + i * 24, y = 121;
    bool on = roundDone(i);
    vfd::rect(x, y, 18, 16, on ? C(255, 153, 0) : C(102, 102, 119));
    vfd::rect(x, y, 18, 1, on ? C(255, 221, 136) : C(34, 34, 51)), vfd::rect(x, y, 1, 16, on ? C(255, 221, 136) : C(34, 34, 51));
    vfd::rect(x, y + 15, 18, 1, on ? C(153, 68, 0) : C(170, 170, 187)), vfd::rect(x + 17, y, 1, 16, on ? C(153, 68, 0) : C(170, 170, 187));
  }
  hints(446);
}

// 2600: solid black, block digits, 8x4 blocks everywhere.
void panelAtari(const char *corner)
{
  const uint16_t warm = C(252, 188, 148), mint = C(184, 252, 184), grey = C(68, 68, 68);
  vfd::rect(MPX, 0, vfd::W - MPX, vfd::H, 0);
  uint16_t pc = on_break ? mint : warm;
  int lx = vfd::text(phaseLabel(), 448, 10, 2, vfd::Ink{ pc });
  if (running) {  // stepped play triangle
    static const int8_t TW[4] = { 8, 16, 16, 8 };
    for (int j = 0; j < 4; j++) vfd::rect(lx + 8, 8 + j * 4, TW[j], 4, warm);
  } else {
    vfd::rect(lx + 8, 8, 8, 16, grey), vfd::rect(lx + 24, 8, 8, 16, grey);
  }
  cornerText(corner, 632, 10, warm);
  char digits[8];
  clockText(digits, sizeof(digits));
  MachineDigits_Draw(digits, 456, 36, 24, 60, 8);
  const int pw = 176, fill = progressFill(pw) / 8 * 8;
  vfd::rect(448, 104, pw, 8, grey);
  vfd::rect(448, 104, fill, 8, warm);
  int tx = vfd::text(T("ROUND"), 448, 122, 2, vfd::Ink{ pc });
  for (int i = 0; i < 4; i++) vfd::rect(tx + 8 + i * 24, 122, 16, 12, roundDone(i) ? warm : grey);
  hints(448);
}

}  // namespace

void Study_Draw(const char *corner_text)
{
  frame_no++;
  Study_Tick();
  const theme::Id id = theme::current();
  if (id != theme::VFD) {
    const MachineStudy &m = MACHINE_STUDY[id - theme::C64];
    if (RetroPicture_Draw(m.picture)) {
      steam(m, id);
      (id == theme::C64 ? panelC64 : id == theme::AMIGA ? panelAmiga : panelAtari)(corner_text);
      return;
    }
  }
  WeatherNow w = Weather_Get();
  bool rain = w.valid && (w.sky == Sky::Rain || w.sky == Sky::Drizzle || w.sky == Sky::Shower || w.sky == Sky::Storm);
  // wall
  rect(0, 0, PANEL_X, FLOOR_Y, N1);
  rect(0, FLOOR_Y - 2, PANEL_X, 2, N2);  // skirting board
  for (int y = FLOOR_Y; y < H; y++)
    for (int x = 0; x < PANEL_X; x++) buf[y][x] = ((x + (y - FLOOR_Y) / 3 * 23) % 40 == 0) ? NIGHT : D1;
  rect(PANEL_X, 0, W - PANEL_X, H, NIGHT);
  rect(PANEL_X - 1, 0, 1, H, N2);
  window(rain);
  room();
  desk();
  person();
  present();

  // the pomodoro panel on the right, VFD style: phase + state, big time, phase progress, round, how to use it
  using namespace vfd;
  const int px0 = PANEL_X * 2 + 14, pw = vfd::W - px0 - 12;
  uint16_t phase_c = on_break ? px::ink(C3) : HOT;
  uint16_t phase_h = on_break ? HALO : HOTH;
  const bool machine = id != theme::VFD;
  if (machine) phase_c = id == theme::C64 ? rgb(184, 199, 111) : rgb(236, 236, 236);
  int lx = text(phaseLabel(), px0, 10, 2, Ink{ phase_c, phase_h });
  if (running) {  // play triangle
    for (int k = 0; k < 7; k++) rect(lx + 10 + k, 10 + k, 1, 14 - 2 * k, LIT);
  } else {  // pause bars
    rect(lx + 10, 10, 4, 14, DIM), rect(lx + 18, 10, 4, 14, DIM);
  }
  cornerText(corner_text, vfd::W - 12, 10, DIM);
  char digits[8];
  clockText(digits, sizeof(digits));
  Ink big = running ? Ink{ LIT, HALO, GHOST } : Ink{ LIT, NONE, GHOST };
  seg(digits, px0 + 2, 36, 32, 56, 7, 7, big);
  rect(px0, 104, pw, 6, GHOST);
  rect(px0, 104, progressFill(pw), 6, phase_c);
  int tx = text(T("ROUND"), px0, 122, 2, Ink{ machine ? phase_c : DIM });
  for (int i = 0; i < 4; i++) rect(tx + 10 + i * 22, 122, 16, 14, roundDone(i) ? HOT : GHOST);
  hints(px0);
}

#endif
