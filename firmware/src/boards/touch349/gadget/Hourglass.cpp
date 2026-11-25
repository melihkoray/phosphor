#ifdef BOARD_TOUCH349

#include "Hourglass.h"
#include "Motion.h"
#include "Px.h"
#include "Settings.h"
#include "Radio.h"
#include "Theme.h"

using namespace px;

// Grid of big cells, each a 3x3 dot in a 4x4 slot (LED-matrix look), centered in the 86x320 portrait.
static constexpr int S = 4, CW = 21, CH = 72, NECK = 36;  // rows < NECK: the "upper" bulb with the left edge down
static constexpr int OX = (PW - CW * S) / 2, OY = (PH - CH * S) / 2;
static const int DURATIONS[] = { 5, 10, 15, 20 };

static uint8_t grid[CH][CW];  // 0 empty, 1/2 sand shades, | 0x80 moved this frame
static int8_t half[CH];       // inside a row: |x - CW/2| <= half
static int dur_idx = 1, dir = 1, total = 0;
static float acc = 0;
static bool ready = false, beeped = false;
static uint32_t last_ms = 0, seed = 0x2545f491;

static uint32_t rnd()
{
  seed ^= seed << 13;
  seed ^= seed >> 17;
  seed ^= seed << 5;
  return seed;
}

static bool inside(int x, int y) { return y >= 0 && y < CH && abs(x - CW / 2) <= half[y]; }

static void refill()
{
  memset(grid, 0, sizeof(grid));
  int cap = 0;
  for (int y = 0; y < NECK; y++) cap += 2 * half[y] + 1;
  int want = cap * 72 / 100;
  total = 0;
  // settled in the bulb that is up now, from the neck outward
  for (int k = 0; k < NECK && total < want; k++) {
    int y = dir > 0 ? NECK - 1 - k : NECK + k;
    for (int x = 0; x < CW && total < want; x++)
      if (inside(x, y)) grid[y][x] = 1 + (rnd() % 5 == 0), total++;
  }
  acc = 0;
  beeped = false;
}

static void hgInit()
{
  for (int y = 0; y < CH; y++) {
    float d = fabsf(y + 0.5f - NECK) / NECK;  // 0 at the neck, 1 at the ends
    float hw = 0.4f + 9.6f * powf(sinf(d * PI / 2), 0.8f);
    if (d > 0.9f) hw *= 1 - (d - 0.9f) * 2.5f;  // round off the ends
    half[y] = (int8_t)hw;
  }
  refill();
  ready = true;
}

static int upperCount()
{
  int n = 0;
  for (int y = 0; y < CH; y++)
    if ((dir > 0) == (y < NECK))
      for (int x = 0; x < CW; x++) n += grid[y][x] != 0;
  return n;
}

static void step(float side)
{
  for (int y = 0; y < CH; y++)
    for (int x = 0; x < CW; x++) grid[y][x] &= 0x7f;
  bool ltr = rnd() & 1;
  for (int k = 0; k < CH; k++) {
    int y = dir > 0 ? CH - 1 - k : k;  // start from the side the sand falls toward
    int ny = y + dir;
    if (ny < 0 || ny >= CH) continue;
    bool crossing = (dir > 0 && y == NECK - 1) || (dir < 0 && y == NECK);
    for (int i = 0; i < CW; i++) {
      int x = ltr ? i : CW - 1 - i;
      uint8_t g = grid[y][x];
      if (!g) continue;
      int tx = -1;
      if (inside(x, ny) && !grid[ny][x]) {
        tx = x;
      } else {
        int s = (side > 0.15f) ? 1 : (side < -0.15f) ? -1 : ((rnd() & 1) ? 1 : -1);
        if (inside(x + s, ny) && !grid[ny][x + s]) tx = x + s;
        else if (inside(x - s, ny) && !grid[ny][x - s]) tx = x - s;
      }
      if (tx < 0) continue;
      if (crossing) {
        if (acc < 1) continue;
        acc -= 1;
      }
      grid[ny][tx] = g | 0x80;
      grid[y][x] = 0;
    }
  }
}

// 3x5 digits, D and K, drawn 2x in the viewer's frame (upright whichever end is up).
static const uint8_t DIG[10][5] = { { 7, 5, 5, 5, 7 }, { 2, 6, 2, 2, 7 }, { 7, 1, 7, 4, 7 }, { 7, 1, 7, 1, 7 },
                                    { 5, 5, 7, 1, 1 }, { 7, 4, 7, 1, 7 }, { 7, 4, 7, 5, 7 }, { 7, 1, 2, 2, 2 },
                                    { 7, 5, 7, 5, 7 }, { 7, 5, 7, 1, 7 } };
static const uint8_t LD[5] = { 6, 5, 5, 5, 6 }, LK[5] = { 5, 5, 6, 5, 5 };

static void text(const char *s, int y, uint8_t c)
{
  int w = 0;
  for (const char *p = s; *p; p++) w += (*p == ':' ? 1 : *p == ' ' ? 2 : 3) * 2 + 2;
  int x = (PW - w + 2) / 2;
  bool up = dir > 0;
  for (; *s; s++) {
    const uint8_t *g = *s >= '0' && *s <= '9' ? DIG[*s - '0'] : *s == 'D' ? LD : *s == 'K' ? LK : nullptr;
    if (*s == ':') {
      for (int dy : { 2, 6 })
        for (int k = 0; k < 4; k++) pset(x + (k & 1), y + dy + (k >> 1), c, up);
      x += 4;
      continue;
    }
    if (g)
      for (int r = 0; r < 5; r++)
        for (int b = 0; b < 3; b++)
          if (g[r] & (4 >> b))
            for (int k = 0; k < 4; k++) pset(x + b * 2 + (k & 1), y + r * 2 + (k >> 1), c, up);
    x += (*s == ' ' ? 2 : 3) * 2 + 2;
  }
}

// Portrait canvas coordinates, matching pset(..., true), without another scene buffer.
static void portraitRect(int x, int y, int w, int h, uint16_t color)
{
  int x0 = max(0, x), y0 = max(0, y);
  int x1 = min(vfd::H, x + w), y1 = min(vfd::W, y + h);
  if (x1 > x0 && y1 > y0) vfd::rect(vfd::W - y1, x0, y1 - y0, x1 - x0, color);
}

static void portraitText(const char *s, int y, uint16_t color)
{
  const vfd::Font &font = *vfd::style().font;
  const int p = 2;
  int x = (vfd::H - vfd::textWidth(s, p)) / 2;
  // These labels contain only digits, colon and MIN/DK, all native font glyphs.
  for (; *s; s++, x += font.adv * p) {
    const uint8_t *rows = font.rows((uint8_t)*s);
    if (!rows) continue;
    for (int pass = 0; pass < 2; pass++)
      for (int r = 0; r < font.h; r++)
        for (int c = 0; c < font.w; c++)
          if (rows[r] & (1 << (font.w - 1 - c))) {
            int d = pass ? 0 : 1;
            portraitRect(x + c * p - d, y + r * p - d, p + d * 2, p + d * 2, pass ? color : 0);
          }
  }
}

// Machine pages draw straight on a black canvas, so everything outside the glass stays black and the glass stands out.
// Colors are RGB 0..15 per channel (12 bit) for the Amiga.
static uint16_t c12(int r, int g, int b) { return rgb(r * 17, g * 17, b * 17); }

static uint32_t hsh(int x, int y)
{
  uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u;
  h ^= h >> 13;
  h *= 0x5bd1e995u;
  return h >> 16;
}

static bool sandAt(int x, int y) { return y >= 0 && y < CH && x >= 0 && x < CW && grid[y][x]; }

// The glass closes each bulb at its own width (no caps or posts around it): cells from the left to the right outline
// column on the row just beyond either end.
static void glassEnds(void (*cell)(int x, int y, bool top))
{
  for (int end = 0; end < 2; end++) {
    int h = half[end ? CH - 1 : 0];
    for (int x = CW / 2 - h - 1; x <= CW / 2 + h + 1; x++) cell(x, end ? CH : -1, !end);
  }
}

static void labels(uint32_t secs, bool flowing, uint16_t flow, uint16_t idle, uint16_t duration)
{
  char label[12];
  snprintf(label, sizeof(label), "%lu:%02lu", (unsigned long)(secs / 60), (unsigned long)(secs % 60));
  portraitText(label, 4, flowing ? flow : idle);
  snprintf(label, sizeof(label), T("%d MIN"), DURATIONS[dur_idx]);
  portraitText(label, PH * 2 - 20, duration);
}

// C64: 4x2 pixels, a cell is one 8x8 block of portrait units, flat glass outline.
static void c64Glass(uint32_t secs, bool flowing)
{
  const uint16_t light = rgb(184, 199, 111), dark = rgb(111, 79, 37), glass = rgb(149, 149, 149);
  vfd::clear(0);
  for (int y = 0; y < CH; y++) {
    int left = CW / 2 - half[y] - 1, right = CW / 2 + half[y] + 1;
    portraitRect((OX + left * S) * 2, (OY + y * S) * 2, S * 2, S * 2, glass);
    portraitRect((OX + right * S) * 2, (OY + y * S) * 2, S * 2, S * 2, glass);
  }
  glassEnds([](int x, int y, bool) { portraitRect((OX + x * S) * 2, (OY + y * S) * 2, S * 2, S * 2, rgb(149, 149, 149)); });
  for (int y = 0; y < CH; y++)
    for (int x = 0; x < CW; x++) {
      uint8_t g = grid[y][x];
      if (g) portraitRect((OX + x * S) * 2, (OY + y * S) * 2, S * 2, S * 2,
                          (g & 0x80) || (g & 0x7f) != 2 ? light : dark);
    }
  labels(secs, flowing, glass, dark, light);
}

// Amiga: 2x2 pixels, so one portrait pixel is one machine pixel; the showpiece with shaded sand and a glossy glass
// rim.
static void amigaGlass(uint32_t secs, bool flowing)
{
  const uint16_t sand[4] = { c12(15, 13, 7), c12(13, 10, 5), c12(10, 7, 3), c12(11, 8, 4) };  // lit, body, edge, grain
  const uint16_t stream = c12(15, 14, 9), streamGrain = c12(13, 12, 6);
  const uint16_t rimOut = c12(4, 6, 8), rim[3] = { c12(6, 8, 11), c12(9, 11, 14), c12(11, 13, 15) };
  const uint16_t tint1 = c12(1, 3, 6), tint2 = c12(0, 1, 3), shine = c12(15, 15, 15);
  vfd::clear(0);
  for (int y = 0; y < CH; y++) {
    int xl = OX + (CW / 2 - half[y]) * S, xr = OX + (CW / 2 + half[y] + 1) * S;
    for (int r = 0; r < S; r++) {
      int yy = OY + y * S + r;
      portraitRect(xl * 2, yy * 2, 2, 2, tint1);
      portraitRect((xl + 1) * 2, yy * 2, 2, 2, tint2);
      portraitRect((xr - 1) * 2, yy * 2, 2, 2, tint1);
      portraitRect((xr - 2) * 2, yy * 2, 2, 2, tint2);
    }
  }
  for (int y = 0; y < CH; y++)
    for (int x = 0; x < CW; x++) {
      uint8_t g = grid[y][x];
      if (!g) continue;
      bool moved = g & 0x80;
      // the surface faces the empty side: where the sand falls from
      bool surface = !sandAt(x, y - dir), lSide = !sandAt(x - 1, y), rSide = !sandAt(x + 1, y);
      for (int r = 0; r < S; r++)
        for (int c = 0; c < S; c++) {
          int px = OX + x * S + c, py = OY + y * S + r;
          uint32_t h = hsh(px, py);
          uint16_t col;
          if (moved) col = (h & 3) == 0 ? streamGrain : stream;
          else if (surface && r == (dir > 0 ? 0 : S - 1)) col = sand[0];
          else if ((lSide && c == 0) || (rSide && c == S - 1)) col = sand[2];
          else if ((h & 7) == 0) col = sand[(g & 0x7f) == 2 ? 2 : 0];
          else if ((h & 7) == 1) col = sand[3];
          else col = (g & 0x7f) == 2 ? sand[2] : sand[1];
          portraitRect(px * 2, py * 2, 2, 2, col);
        }
    }
  for (int y = 0; y < CH; y++) {
    int xl = OX + (CW / 2 - half[y] - 1) * S, xr = OX + (CW / 2 + half[y] + 1) * S;
    float d = fabsf(y + 0.5f - NECK) / NECK;
    bool gloss = d > 0.12f && d < 0.8f;
    for (int r = 0; r < S; r++) {
      int yy = OY + y * S + r;
      bool streak = gloss && yy % 40 < 30;  // a broken white highlight along the left side of each bulb
      portraitRect(xl * 2, yy * 2, 2, 2, rimOut);
      portraitRect((xl + 1) * 2, yy * 2, 2, 2, rim[0]);
      portraitRect((xl + 2) * 2, yy * 2, 2, 2, streak ? shine : rim[1]);
      portraitRect((xl + 3) * 2, yy * 2, 2, 2, rim[2]);
      portraitRect(xr * 2, yy * 2, 2, 2, rim[1]);
      portraitRect((xr + 1) * 2, yy * 2, 2, 2, rim[0]);
      portraitRect((xr + 2) * 2, yy * 2, 2, 2, rim[0]);
      portraitRect((xr + 3) * 2, yy * 2, 2, 2, rimOut);
    }
  }
  // the ends in the same rim shades, outermost row darkest
  glassEnds([](int x, int y, bool top) {
    const uint16_t shade[4] = { c12(4, 6, 8), c12(6, 8, 11), c12(9, 11, 14), c12(11, 13, 15) };
    for (int r = 0; r < S; r++)
      portraitRect((OX + x * S) * 2, (OY + y * S + r) * 2, S * 2, 2, shade[top ? r : S - 1 - r]);
  });
  labels(secs, flowing, c12(12, 14, 15), c12(13, 10, 5), c12(15, 13, 7));
}

// 2600: blocks of 8 (screen x) x 4 (screen y) pixels. Sand is merged to 2x2 cells (16 x 16 units), flat colors that
// change per block row like a scanline colour register, no shading.
static void atariGlass(uint32_t secs, bool flowing)
{
  const uint16_t band[3] = { rgb(252, 224, 140), rgb(236, 200, 120), rgb(212, 176, 96) };
  const uint16_t stream = rgb(252, 244, 188), glass = rgb(236, 236, 236);
  vfd::clear(0);
  const int c = S * 2;  // one sim cell in units; a column is 8 units wide, 8 long (two 8x4 blocks)
  for (int by = 0; by < CH / 2; by++)
    for (int bx = 0; bx < (CW + 1) / 2; bx++) {
      bool any = false, moved = false;
      for (int k = 0; k < 4; k++) {
        int x = bx * 2 + (k & 1), y = by * 2 + (k >> 1);
        if (x < CW && grid[y][x]) {
          any = true;
          moved |= grid[y][x] & 0x80;
        }
      }
      if (!any) continue;
      for (int r = 0; r < 2; r++) {
        int y = by * 2 + r;
        // keep the block inside the glass, so the black outside stays black
        int x0 = max(bx * 2, CW / 2 - half[y] - 1), x1 = min(bx * 2 + 1, CW / 2 + half[y] + 1);
        if (x1 >= x0)
          portraitRect(x0 * c, (OY + y * S) * 2, (x1 - x0 + 1) * c, c, moved ? stream : band[by % 3]);
      }
    }
  for (int y = 0; y < CH; y++) {
    int left = CW / 2 - half[y] - 1, right = CW / 2 + half[y] + 1;
    portraitRect(left * c, (OY + y * S) * 2, c, c, glass);
    portraitRect(right * c, (OY + y * S) * 2, c, c, glass);
  }
  glassEnds([](int x, int y, bool) { portraitRect(x * S * 2, (OY + y * S) * 2, S * 2, S * 2, rgb(236, 236, 236)); });
  labels(secs, flowing, glass, band[2], band[0]);
}

// VFD keeps its dot sand; the glass is dim dots with a halo in the gaps of the 4x4 slots.
static void vfdDot(int x, int y)
{
  for (int dy = 0; dy < S; dy++)
    for (int dx = -1; dx < S; dx++)
      if (dx >= 0 && dx < S - 1 && dy < S - 1) pset(x + dx, y + dy, C2, true);
      else pset(x + dx, y + dy, C1, true);
}

static void vfdGlass()
{
  for (int y = 0; y < CH; y++) {
    vfdDot(OX + (CW / 2 - half[y] - 1) * S, OY + y * S);
    vfdDot(OX + (CW / 2 + half[y] + 1) * S, OY + y * S);
  }
  glassEnds([](int x, int y, bool) { vfdDot(OX + x * S, OY + y * S); });
}

void Hourglass_Draw(const char *)
{
  if (!ready) hgInit();
  uint32_t now = millis();
  float dt = constrain((now - last_ms) / 1000.0f, 0.0f, 0.2f);
  last_ms = now;

  float gx, gy, gz;
  Motion_Gravity(gx, gy, gz);
  float along = -gx, side = gy;  // portrait frame: +x = screen +y, +y = screen -x
  bool flowing = fabsf(along) >= 0.4f;
  if (flowing) {
    int nd = along > 0 ? 1 : -1;
    if (nd != dir) {
      dir = nd;
      beeped = false;
    }
    acc = min(acc + total / (DURATIONS[dur_idx] * 60.0f) * dt, 2.0f);
    step(dir > 0 ? side : -side);
  }

  int up = upperCount();
  if (flowing && up == 0 && !beeped && total > 0) {
    beeped = true;
    Radio_Beep(1318, 110, 2, 90);
  }

  uint32_t secs = total ? (uint32_t)(up * DURATIONS[dur_idx] * 60.0f / total + 0.5f) : 0;
  switch (theme::current()) {
    case theme::C64: c64Glass(secs, flowing); return;
    case theme::AMIGA: amigaGlass(secs, flowing); return;
    case theme::ATARI: atariGlass(secs, flowing); return;
    default: break;
  }

  clear(NIGHT);
  vfdGlass();
  for (int y = 0; y < CH; y++)
    for (int x = 0; x < CW; x++) {
      uint8_t g = grid[y][x];
      if (!g) continue;
      uint8_t c = (g & 0x7f) == 2 ? R3 : M2;
      for (int k = 0; k < (S - 1) * (S - 1); k++) pset(OX + x * S + k % (S - 1), OY + y * S + k / (S - 1), c, true);
    }

  // time left at the top, the chosen duration at the bottom (viewer's frame)
  char buf[12];
  snprintf(buf, sizeof(buf), "%lu:%02lu", (unsigned long)(secs / 60), (unsigned long)(secs % 60));
  text(buf, 3, flowing ? C3 : C1);
  snprintf(buf, sizeof(buf), T("%d MIN"), DURATIONS[dur_idx]);
  text(buf, PH - 13, C2);
  present();
}

void Hourglass_Tap()
{
  dur_idx = (dur_idx + 1) % 4;
  if (!ready) hgInit();
  refill();
  Radio_Beep(1046, 30);
}

void Hourglass_Reset()
{
  if (!ready) hgInit();
  refill();
  Radio_Beep(523, 60);
}

void Hourglass_Tick() {}

#endif
