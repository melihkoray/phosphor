#ifdef BOARD_TOUCH349

#include "Vfd.h"
#include "VfdFont.h"

namespace vfd {

uint16_t BG = 0x0041, GHOST = 0x0904, FAINT = 0x0082, HALO = 0x0248, LIT = 0x77BC, DIM = 0x44B1, HOT = 0xFA16,
         HOTH = 0x6888, KELP = 0x4D72;
Ink LIT_INK = { LIT, HALO };
Ink HOT_INK = { HOT, HOTH };
Ink DIM_INK = { DIM };
Ink GHOST_INK = { GHOST };

static uint16_t *buf = nullptr;
static int cx0 = 0, cy0 = 0, cx1 = W, cy1 = H;
static Style cur_style;

void setStyle(const Style &s)
{
  cur_style = s;
  if (!cur_style.font) cur_style.font = &VFD_FONT;
}
const Style &style() { return cur_style; }

void begin(uint16_t *canvas) { buf = canvas; }
uint16_t *canvas() { return buf; }

void clip(int x0, int y0, int x1, int y1)
{
  cx0 = max(0, x0);
  cy0 = max(0, y0);
  cx1 = min(W, x1);
  cy1 = min(H, y1);
}

void noClip() { clip(0, 0, W, H); }

void clear(uint16_t color)
{
  for (int i = 0; i < W * H; i++) {
    buf[i] = color;
  }
}

void rect(int x, int y, int w, int h, uint16_t color)
{
  int x0 = max(x, cx0), y0 = max(y, cy0), x1 = min(x + w, cx1), y1 = min(y + h, cy1);
  for (int yy = y0; yy < y1; yy++) {
    uint16_t *row = buf + yy * W;
    for (int xx = x0; xx < x1; xx++) {
      row[xx] = color;
    }
  }
}

void frame(int x, int y, int w, int h, uint16_t color)
{
  rect(x, y, w, 1, color);
  rect(x, y + h - 1, w, 1, color);
  rect(x, y, 1, h, color);
  rect(x + w - 1, y, 1, h, color);
}

// ---------- text ----------

static uint32_t nextCodepoint(const char *&s)
{
  uint8_t c = (uint8_t)*s++;
  if (c < 0x80) {
    return c;
  }
  int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
  uint32_t cp = c & (0x3F >> extra);
  for (int i = 0; i < extra; i++) {
    uint8_t n = (uint8_t)*s;
    if ((n & 0xC0) != 0x80) {
      return '?';
    }
    cp = (cp << 6) | (n & 0x3F);
    s++;
  }
  return cp;
}

struct Resolved {
  uint32_t base;
  uint8_t top;  // accent dots two rows above the glyph (5 bits), 0 = none
  uint8_t bot;  // accent dots one row below
};

// Maps a code point onto a glyph of the font: upper case, Turkish and Polish accents kept, other accents dropped.
static Resolved resolve(uint32_t cp)
{
  if (cp >= 'a' && cp <= 'z') {
    cp -= 32;
  }
  switch (cp) {
    case 0x11E: case 0x11F: return { 'G', 0x0E, 0 };  // Ğ ğ
    case 0xDC: case 0xFC: return { 'U', 0x0A, 0 };    // Ü ü
    case 0xD6: case 0xF6: return { 'O', 0x0A, 0 };    // Ö ö
    case 0x130: return { 'I', 0x04, 0 };              // İ
    case 0x131: return { 'I', 0, 0 };                 // ı
    case 0x15E: case 0x15F: return { 'S', 0, 0x04 };  // Ş ş
    case 0xC7: case 0xE7: return { 'C', 0, 0x04 };    // Ç ç
    case 0x104: case 0x105: return { 'A', 0, 0x01 };  // Ą ą: the ogonek is a dot below on the right
    case 0x118: case 0x119: return { 'E', 0, 0x01 };  // Ę ę
    case 0x106: case 0x107: return { 'C', 0x02, 0 };  // Ć ć: the acute is a dot above on the right
    case 0x143: case 0x144: return { 'N', 0x02, 0 };  // Ń ń
    case 0xD3: case 0xF3: return { 'O', 0x02, 0 };    // Ó ó
    case 0x15A: case 0x15B: return { 'S', 0x02, 0 };  // Ś ś
    case 0x179: case 0x17A: return { 'Z', 0x02, 0 };  // Ź ź
    case 0x17B: case 0x17C: return { 'Z', 0x04, 0 };  // Ż ż: the dot above, in the middle
    case 0x141: case 0x142: return { 'L', 0x10, 0 };  // Ł ł: a dot above on the left stands for the stroke
    case 0x2018: case 0x2019: case 0x60: case 0xB4: return { '\'', 0, 0 };
    case 0x201C: case 0x201D: return { '"', 0, 0 };
    case 0x2013: case 0x2014: return { '-', 0, 0 };
    case 0x2026: return { '.', 0, 0 };
    case 0x2022: return { 0xB7, 0, 0 };
    case 0xDF: return { 'S', 0, 0 };
  }
  if (cp >= 0xC0 && cp <= 0xFF) {
    static const char LATIN1[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsAAAAAAACEEEEIIIIDNOOOOO/OUUUUYTY";
    char c = LATIN1[cp - 0xC0];
    return { (uint32_t)c, 0, 0 };
  }
  return { cp, 0, 0 };
}

static const uint8_t *vfdRows(uint32_t cp)
{
  for (int i = 0; i < VFD_GLYPH_COUNT; i++) {
    if (VFD_GLYPHS[i].cp == cp) {
      return VFD_GLYPHS[i].rows;
    }
  }
  return nullptr;
}

const Font VFD_FONT = { 5, 7, 6, 0, vfdRows };

static const uint8_t *glyphRows(const Font &f, uint32_t cp)
{
  static const uint8_t blank[16] = { 0 };
  if (cp == ' ') return blank;
  const uint8_t *r = f.rows(cp);
  if (!r) r = f.rows('?');
  return r ? r : blank;
}

// Calls dot(x, y) for every lit cell of the string.
template <typename F>
static int forEachDot(const char *s, int x, int y, int p, F dot)
{
  const Font &f = *cur_style.font;
  int cx = x;
  while (*s) {
    Resolved r = resolve(nextCodepoint(s));
    const uint8_t *rows = glyphRows(f, r.base);
    for (int row = 0; row < f.h; row++) {
      for (int col = 0; col < f.w; col++) {
        if (rows[row] & (1 << (f.w - 1 - col))) {
          dot(cx + col * p, y + row * p);
        }
      }
    }
    for (int col = 0; col < 5; col++) {
      int ax = cx + (col + f.accent_dx) * p;
      if (r.top & (0x10 >> col)) {
        dot(ax, y - 2 * p);
      }
      if (r.bot & (0x10 >> col)) {
        dot(ax, y + (f.h + 1) * p);
      }
    }
    cx += f.adv * p;
  }
  return cx;
}

int glyphCount(const char *s)
{
  int n = 0;
  while (*s) {
    nextCodepoint(s);
    n++;
  }
  return n;
}

int textWidth(const char *s, int p)
{
  const Font &f = *cur_style.font;
  int n = glyphCount(s);
  return n ? (n * f.adv - (f.adv - f.w)) * p : 0;
}

int text(const char *s, int x, int y, int p, int d, const Ink &ink)
{
  const Font &f = *cur_style.font;
  if (!cur_style.dots) {
    d = p;
  }
  if (ink.ghost != NONE && cur_style.ghosts) {
    int n = glyphCount(s);
    for (int g = 0; g < n; g++) {
      for (int row = 0; row < f.h; row++) {
        for (int col = 0; col < f.w; col++) {
          rect(x + g * f.adv * p + col * p, y + row * p, d, d, ink.ghost);
        }
      }
    }
  }
  if (ink.h != NONE && cur_style.glow) {
    forEachDot(s, x, y, p, [&](int px, int py) { rect(px - 1, py - 1, d + 2, d + 2, ink.h); });
  }
  return forEachDot(s, x, y, p, [&](int px, int py) { rect(px, py, d, d, ink.c); });
}

// ---------- 7 segments ----------

struct Box {
  int x, y, w, h;
};

// Segments a..g of one digit.
static void segBoxes(int x, int y, int w, int h, int t, Box out[7])
{
  int hh = (h + 1) / 2, v = hh - t - (t + 1) / 2 - 2;
  out[0] = { x + t + 1, y, w - 2 * t - 2, t };
  out[1] = { x + w - t, y + t + 1, t, v };
  out[2] = { x + w - t, y + hh + (t + 1) / 2 + 1, t, v };
  out[3] = { x + t + 1, y + h - t, w - 2 * t - 2, t };
  out[4] = { x, y + hh + (t + 1) / 2 + 1, t, v };
  out[5] = { x, y + t + 1, t, v };
  out[6] = { x + t + 1, y + hh - t / 2, w - 2 * t - 2, t };
}

// A segment with chamfered (hexagonal) ends like a VFD; grow > 0 draws the glow around it.
static void segShape(const Box &b, int grow, uint16_t color)
{
  bool horiz = b.w >= b.h;
  int t = horiz ? b.h : b.w;
  for (int k = -grow; k < t + grow; k++) {
    int kk = constrain(k, 0, t - 1);
    int inset = min(kk, t - 1 - kk) / 2 - grow;
    if (horiz) {
      rect(b.x + inset, b.y + k, b.w - 2 * inset, 1, color);
    } else {
      rect(b.x + k, b.y + inset, 1, b.h - 2 * inset, color);
    }
  }
}

static uint8_t segMask(char ch)
{
  // bit 0 = a ... bit 6 = g
  static const uint8_t DIGITS[] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };
  if (ch >= '0' && ch <= '9') {
    return DIGITS[ch - '0'];
  }
  return ch == '-' ? 0x40 : 0;
}

int seg(const char *s, int x, int y, int w, int h, int t, int gap, const Ink &ink)
{
  int cx = x;
  for (; *s; s++) {
    if (*s == ':') {
      int y1 = y + (h * 3 + 5) / 10, y2 = y + (h * 65 + 50) / 100;
      if (ink.h != NONE && cur_style.glow) {
        rect(cx - 2, y1 - 2, t + 4, t + 4, ink.h);
        rect(cx - 2, y2 - 2, t + 4, t + 4, ink.h);
      }
      rect(cx, y1, t, t, ink.c);
      rect(cx, y2, t, t, ink.c);
      cx += t + gap;
      continue;
    }
    Box b[7];
    segBoxes(cx, y, w, h, t, b);
    uint8_t lit = segMask(*s);
    for (int i = 0; i < 7; i++) {
      if (!(lit & (1 << i)) && ink.ghost != NONE && cur_style.ghosts) {
        segShape(b[i], 0, ink.ghost);
      }
    }
    if (ink.h != NONE && cur_style.glow) {
      for (int i = 0; i < 7; i++) {
        if (lit & (1 << i)) {
          segShape(b[i], 2, ink.h);
        }
      }
    }
    for (int i = 0; i < 7; i++) {
      if (lit & (1 << i)) {
        segShape(b[i], 0, ink.c);
      }
    }
    cx += w + gap;
  }
  return cx;
}

void sprite(const char *const *rows, int n, int x, int y, int s, uint16_t color, bool flip)
{
  for (int j = 0; j < n; j++) {
    int len = strlen(rows[j]);
    for (int i = 0; i < len; i++) {
      if (rows[j][i] == '#') {
        int ii = flip ? len - 1 - i : i;
        rect(x + ii * s, y + j * s, s, s, color);
      }
    }
  }
}

}  // namespace vfd

#endif
