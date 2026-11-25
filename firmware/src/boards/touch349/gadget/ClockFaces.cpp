#ifdef BOARD_TOUCH349
#include "ClockFaces.h"
#include "Theme.h"
#include "ThemeFonts.h"
#include "RadioFace.h"
#include "Radio.h"
#include "Settings.h"

using namespace vfd;
using px::rgb;

namespace {
constexpr uint16_t WHITE = rgb(255, 255, 255);
constexpr uint16_t GREEN = rgb(102, 204, 68);
constexpr uint16_t ORANGE = rgb(255, 153, 0);
constexpr uint16_t YELLOW = rgb(184, 199, 111);
constexpr uint16_t BASIC = rgb(108, 94, 181);
constexpr uint16_t BASIC_DIM = rgb(112, 164, 178);
constexpr uint16_t BASIC_OFF = rgb(80, 70, 150);
static const uint8_t DIGITS[10][5] = {
    { 7, 5, 5, 5, 7 }, { 2, 6, 2, 2, 7 }, { 7, 1, 7, 4, 7 }, { 7, 1, 7, 1, 7 },
    { 5, 5, 7, 1, 1 }, { 7, 4, 7, 1, 7 }, { 7, 4, 7, 5, 7 }, { 7, 1, 1, 1, 1 },
    { 7, 5, 7, 5, 7 }, { 7, 5, 7, 1, 7 }
};
const char *const BOLT[] = { "..##", ".##.", "####", ".##.", "##.." };
const char *const ARROW[] = { "..#..", ".###.", "#.#.#", "..#..", "..#.." };  // "new version" mark
const char *const BELL[] = { "...#...", "..###..", ".#####.", ".#####.", ".#####.", "#######", "...#..." };
const uint16_t CHROME[] = {
  rgb(255, 255, 255), rgb(255, 255, 255), rgb(255, 255, 255), rgb(255, 255, 255),
  rgb(255, 255, 238), rgb(255, 255, 238), rgb(255, 255, 238), rgb(255, 255, 238),
  rgb(255, 238, 221), rgb(255, 238, 204), rgb(255, 238, 187), rgb(255, 221, 187),
  rgb(255, 221, 170), rgb(255, 221, 153), rgb(255, 204, 136), rgb(255, 204, 119),
  rgb(255, 187, 102), rgb(187, 102, 68), rgb(170, 68, 68), rgb(238, 119, 102),
  rgb(255, 136, 102), rgb(238, 119, 119), rgb(238, 119, 119), rgb(238, 102, 119),
  rgb(221, 102, 136), rgb(221, 85, 136), rgb(221, 85, 136), rgb(204, 85, 136),
  rgb(204, 85, 153), rgb(187, 68, 153), rgb(170, 68, 153), rgb(170, 68, 153),
  rgb(153, 68, 153), rgb(136, 51, 136), rgb(136, 51, 136), rgb(119, 51, 136)
};

// Keep the mockup's own lowercase and Turkish glyphs, rather than VFD's accent synthesis.
uint32_t codepoint(const char *&s)
{
  uint8_t c = *s++;
  if (c < 128) return c;
  int n = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : 1;
  uint32_t cp = c & (0x3f >> n);
  while (n-- && *s) cp = (cp << 6) | (*s++ & 0x3f);
  return cp;
}

void label(const String &s, int x, int y, uint16_t color, bool shadow = false, int pitch = 2, int advance = 0)
{
  const Font &font = *style().font;
  if (&font == &VFD_FONT) {
    if (shadow) text(s.c_str(), x + 2, y + 2, pitch, { 0 });
    text(s.c_str(), x, y, pitch, { color });
    return;
  }
  auto draw = [&](int dx, int dy, uint16_t c) {
    const char *p = s.c_str();
    int cx = x;
    while (*p) {
      const uint8_t *rows = font.rows(codepoint(p));
      if (!rows) rows = font.rows('?');
      if (rows) for (int r = 0; r < 8; r++) for (int col = 0; col < 8; col++)
        if (rows[r] & (0x80 >> col)) rect(cx + col * pitch + dx, y + r * pitch + dy, pitch, pitch, c);
      cx += advance ? advance : 8 * pitch;
    }
  };
  if (shadow) draw(2, 2, 0);
  draw(0, 0, color);
}

void bigTime(const String &s)
{
  const Font &font = AMIGA_FONT;
  int sx = 11, y = 24;
  // Three passes over the whole mask: shadows must not cover adjacent glyphs.
  auto draw = [&](int pass) {
    int x = 20;
    for (unsigned i = 0; i < s.length(); i++) {
      char ch = s[i];
      const uint8_t *rows = font.rows(ch);
      int gx = ch == ':' ? x - 2 * sx : x;
      if (rows) for (int r = 0; r < 7; r++) {
        uint8_t bits = MachineGlyphRow(rows, ch, r);
        for (int c = 0; c < 8; c++) {
          if (!(bits & (0x80 >> c))) continue;
          int xx = gx + c * sx, yy = y + r * 124 / 7;
          int sy = (r + 1) * 124 / 7 - r * 124 / 7;
          if (pass == 0) rect(xx + 6, yy + 6, sx, sy, rgb(17, 0, 34));
          else if (pass == 1) rect(xx - 2, yy - 2, sx + 4, sy + 4, 0);
          else for (int j = 0; j < sy; j += 2) {
            int row = (yy + j - y) / 2;
            rect(xx, yy + j, sx, min(2, sy - j), CHROME[min(row * 36 / 62, 35)]);
          }
        }
      }
      x += ch == ':' ? 24 : 90;
    }
  };
  draw(0);
  draw(1);
  draw(2);
}

void panel(int x0, int y0, int w, int h)
{
  for (int y = y0; y < y0 + h; y++) for (int x = x0; x < x0 + w; x++) {
    uint16_t &c = canvas()[y * W + x];
    c = (c & 0xf7de) >> 1;
  }
  rect(x0, y0, w, 2, rgb(153, 153, 170));
  rect(x0, y0, 2, h, rgb(153, 153, 170));
  rect(x0, y0 + h - 2, w, 2, 0);
  rect(x0 + w - 2, y0, 2, h, 0);
}

void atariTime(const String &s)
{
  // Cool rows over the warm sunset bands, one color per block row like the 2600 changed colors per line.
  static const uint16_t COLORS[] = { rgb(236, 236, 236), rgb(164, 224, 252), rgb(144, 204, 232),
                                      rgb(124, 180, 212), rgb(104, 156, 192) };
  const uint16_t SHADOW = rgb(0, 24, 124);
  for (int pass = 0; pass < 2; pass++) {
    int x = 20, d = pass ? 0 : 4;
    for (unsigned i = 0; i < s.length(); i++) {
      char ch = s[i];
      if (ch == ':') {
        for (int r : { 1, 3 }) rect(x + d, 24 + r * 24 + d, 12, 12, pass ? COLORS[r] : SHADOW);
        x += 24;
      } else if (ch >= '0' && ch <= '9') {
        for (int r = 0; r < 5; r++) for (int c = 0; c < 3; c++)
          if (DIGITS[ch - '0'][r] & (4 >> c))
            rect(x + c * 26 + d, 24 + r * 24 + d, 26, 24, pass ? COLORS[r] : SHADOW);
        x += 90;
      }
    }
  }
}
}  // namespace

void ClockFaces_Draw(const ClockFace &f)
{
  Style saved = style();
  bool amiga = theme::current() == theme::AMIGA, atari = theme::current() == theme::ATARI;
  setStyle({ atari ? &VFD_FONT : amiga ? &AMIGA_FONT : &C64_FONT, false, false, false });
  uint16_t alarm = f.alarm_lit ? (amiga ? ORANGE : atari ? rgb(252, 188, 148) : YELLOW)
                              : amiga ? rgb(119, 68, 34) : atari ? rgb(112, 72, 48) : rgb(68, 68, 68);
  if (amiga || atari) {
    uint16_t bright = atari ? rgb(236, 236, 236) : WHITE;
    uint16_t dim = atari ? rgb(144, 144, 144) : rgb(153, 153, 170);
    uint16_t hot = atari ? rgb(252, 188, 148) : ORANGE;
    if (amiga) { bigTime(f.time); panel(402, 14, 230, 152); }
    else { atariTime(f.time); MachineShade(400, 0, 240, H); }
    clip(400, 14, 632, 166);
    label("AL", 412, 26, f.alarm_on ? hot : dim, amiga);
    label(f.period, 412, 48, dim, amiga);
    String day = textWidth(f.weekday.c_str(), 3) <= 138 ? f.weekday : f.short_weekday;
    label(day, 492, 26, bright, amiga, 3);
    // Keep full Turkish months inside the right column.
    label(f.calendar_date, 492, 54, bright, amiga, 2,
          MachineTextWidth(f.calendar_date) > 138 ? 14 : 0);
    rect(492, 80, 132, atari ? 2 : 1, dim);
    if (f.update.length()) {
      sprite(ARROW, 5, 412, 88, 2, hot);
      label(T("NEW"), 428, 88, hot, amiga);
    }
    sprite(BELL, 7, 492, 94, 2, alarm);
    label(f.ringing ? T("WAKE UP!") : "ALARM", 512, 96, alarm, amiga, 2,
          f.ringing && amiga ? 14 : 0);
    label(f.alarm_time, 492, 120, f.alarm_on ? hot : dim, amiga, 3);
    if (f.battery >= 0) {
      int pct = constrain(f.battery, 0, 100);
      uint16_t ink = pct <= 15 ? hot : atari ? rgb(184, 252, 184) : GREEN;
      rect(412, 122, 34, 2, bright); rect(412, 138, 34, 2, bright);
      rect(412, 122, 2, 18, bright); rect(444, 122, 2, 18, bright);
      rect(446, 128, 4, 8, bright);
      rect(416, 126, (26 * pct / 100) / (atari ? 4 : 1) * (atari ? 4 : 1), 10, ink);
      label(String(pct) + "%", 412, 148, bright, amiga);
      if (f.charging) sprite(BOLT, 5, 456, 126, 2, hot);
    }
    noClip();
    // Same 60-stop perimeter as the VFD, without the inactive marks.
    const float w = W - 9, h = H - 9, per = 2 * (w + h);
    float d = fmodf(w / 2 + f.second * per / 60, per);
    int x, y;
    if (d < w) x = 4 + d, y = 4;
    else if ((d -= w) < h) x = W - 5, y = 4 + d;
    else if ((d -= h) < w) x = W - 5 - d, y = H - 5;
    else x = 4, y = H - 5 - (d - w);
    if (amiga) { rect(x - 4, y - 4, 8, 8, 0); rect(x - 3, y - 3, 6, 6, ORANGE); }
    else rect((x - 4) / 4 * 4, (y - 2) / 2 * 2, 8, 4, hot);
  } else {
    label("READY.", 30, 16, BASIC);
    if (f.update.length()) {  // top right, where the cursor line is empty
      String u = String(T("NEW ")) + f.update;
      label(u, W - theme::def().border_w - 24 - MachineTextWidth(u), 16, BASIC_DIM);
    }
    if (f.cursor) rect(30, 32, 16, 16, BASIC);
    MachineBlockText(f.time, (W - MachineBlockText(f.time, 0, 0, false)) / 2, 56);
    String date = f.date, alarm = f.ringing ? f.alarm : "ALARM " + f.alarm;
    String batt = f.battery >= 0 ? "   " + String(T("BATT ")) + String(f.battery) + "%" : "";
    int available = W - 2 * (theme::def().border_w + 24);
    if (MachineTextWidth(date + "   " + alarm + batt) > available) batt = "";
    if (MachineTextWidth(date + "   " + alarm) > available)
      date = MachineFit(date, max(0, available / 16 - MachineTextWidth(alarm) / 16 - 3));
    int x = (W - MachineTextWidth(date + "   " + alarm + batt)) / 2;
    label(date, x, 128, BASIC); x += MachineTextWidth(date + "   ");
    if (!f.ringing || f.alarm_lit) label(alarm, x, 128, f.alarm_lit ? BASIC_DIM : BASIC_OFF);
    x += MachineTextWidth(alarm);
    label(batt, x, 128, BASIC);
    if (f.battery >= 0 && f.charging && batt.length()) {
      int bx = x + MachineTextWidth(batt) + 6;
      if (bx + 8 < W - (theme::def().border_w + 12)) sprite(BOLT, 5, bx, 130, 2, BASIC_DIM);
    }
  }
  setStyle(saved);
}
void ClockFaces_Banner(const String &line)
{
  Style saved = style();
  theme::Id t = theme::current();
  if (t != theme::VFD) setStyle({ t == theme::ATARI ? &VFD_FONT : t == theme::AMIGA ? &AMIGA_FONT : &C64_FONT, false, false, false });
  bool vfdFont = style().font == &VFD_FONT && t == theme::VFD;
  int pitch = 2, w = vfdFont ? textWidth(line.c_str(), 2) : MachineTextWidth(line);
  if (!vfdFont && w > W - 80) pitch = 1, w /= 2;  // a long line in the machine fonts: half-size glyphs
  int bw = min(W - 40, w + 40), bx = (W - bw) / 2, by = 128, bh = 32;
  uint16_t ink;
  noClip();
  if (t == theme::VFD) {
    rect(bx, by, bw, bh, BG);
    frame(bx, by, bw, bh, DIM);
    ink = LIT;
  } else if (t == theme::AMIGA) {
    panel(bx, by, bw, bh);
    ink = WHITE;
  } else if (t == theme::ATARI) {
    rect(bx, by, bw, bh, 0);
    frame(bx, by, bw, bh, rgb(144, 144, 144));
    ink = rgb(236, 236, 236);
  } else {
    rect(bx, by, bw, bh, 0);
    frame(bx, by, bw, bh, BASIC);
    ink = BASIC_DIM;
  }
  label(line, bx + (bw - w) / 2, by + (pitch == 1 ? 12 : 9), ink, t == theme::AMIGA && pitch == 2, pitch);
  setStyle(saved);
}
void MachineLabel(const String &s, int x, int y, uint16_t color, bool shadow) { label(s, x, y, color, shadow); }
void MachinePanel(int x, int y, int w, int h) { panel(x, y, w, h); }

void MachineShade(int x0, int y0, int w, int h)
{
  // 3/8 of the picture: the 2600 column reads like a dark glass over the scene, still light enough to see through.
  for (int y = y0; y < y0 + h; y++) for (int x = x0; x < x0 + w; x++) {
    uint16_t &c = canvas()[y * W + x];
    c = ((c >> 2) & 0x39E7) + ((c >> 3) & 0x18E3);
  }
}

String MachineFit(const String &s, int cells)
{
  const char *p = s.c_str(), *begin = p;
  while (*p && cells-- > 0) codepoint(p);
  return s.substring(0, p - begin);
}

int MachineTextWidth(const String &s)
{
  const char *p = s.c_str(); int n = 0;
  while (*p) { codepoint(p); n++; }
  return n * 16;
}

uint8_t MachineGlyphRow(const uint8_t *rows, uint32_t ch, int r) { return ch == '0' && r >= 2 && r <= 5 ? rows[1] : rows[r]; }

int MachineBlockText(const String &s, int x, int y, bool draw)
{
  const char *p = s.c_str(); int advance = 0, right = 0;
  while (*p) {
    uint32_t ch = codepoint(p);
    const uint8_t *rows = C64_FONT.rows(ch);
    int shift = ch == ':' ? -16 : 0;
    if (rows) for (int r = 0; r < 7; r++) {
      uint8_t bits = MachineGlyphRow(rows, ch, r);
      for (int c = 0; c < 8; c++) if (bits & (0x80 >> c)) {
        right = max(right, advance + shift + (c + 1) * 8);
        if (draw) rect(x + advance + shift + c * 8, y + r * 8, 8, 8, BASIC);
      }
    }
    advance += ch == ':' ? 32 : 64;
  }
  return right;
}

void MachineTemperature(const String &s, int x, int y, bool amiga, bool atari)
{
  int sx = atari ? 16 : 8, sy = atari ? 14 : 8;
  const Font &font = amiga ? AMIGA_FONT : C64_FONT;
  for (int pass = 0; pass < 3; pass++) {
    int cx = x;
    for (unsigned i = 0; i < s.length(); i++) {
      char ch = s[i];
      const uint8_t *rows = atari ? nullptr : font.rows(ch);
      for (int r = 0; r < (atari ? 5 : 8); r++) for (int c = 0; c < (atari ? 3 : 8); c++) {
        bool on = atari ? (ch == '-' ? r == 2 : ch >= '0' && ch <= '9' && (DIGITS[ch-'0'][r] & (4 >> c)))
                        : rows && (MachineGlyphRow(rows, ch, r) & (0x80 >> c));
        if (!on) continue;
        int xx = cx + c*sx, yy = y + r*sy;
        if (pass == 0) rect(xx+4, yy+4, sx, sy, 0);
        else if (pass == 1) rect(xx-2, yy-2, sx+4, sy+4, 0);
        else if (pass == 2) for (int j=0;j<sy;j+=2)
          rect(xx, yy+j, sx, 2, atari ? rgb(164-r*15,224-r*17,252-r*15) : CHROME[min((r*sy+j)/2,35)]);
      }
      cx += 64;
    }
    int dx = cx + 4;
    uint16_t col = atari ? rgb(236,236,236) : WHITE;
    int d = pass == 0 ? 4 : 0, edge = pass == 1 ? 2 : 0;
    uint16_t ink = pass == 2 ? col : 0;
    rect(dx+d-edge, y+d-edge, 16+2*edge, 4+2*edge, ink);
    rect(dx+d-edge, y+12+d-edge, 16+2*edge, 4+2*edge, ink);
    rect(dx+d-edge, y+d-edge, 4+2*edge, 16+2*edge, ink);
    rect(dx+12+d-edge, y+d-edge, 4+2*edge, 16+2*edge, ink);
  }
}

// One bounded text path for all machine HUDs, including UTF-8 titles and weather readings.
void MachineReading(const String &s, int x, int y, int w, uint16_t col, bool scroll, bool outline)
{
  int width = textWidth(s.c_str(), 2);
  int span = width + 48, off = scroll && width > w ? (millis()/35)%span : 0;
  clip(x,y-4,x+w,y+20);
  auto draw = [&](int xx) {
    if (outline) {
      label(s,xx-2,y,0); label(s,xx+2,y,0);
      label(s,xx,y-2,0); label(s,xx,y+2,0);
    }
    label(s,xx,y,col);
  };
  draw(x-off);
  if (scroll && width>w) draw(x-off+span);
  noClip();
}

// Timer glyphs reuse the clock's chrome and 2600 scanline colors at a smaller pitch.
void MachineTimerDigits(const String &s, bool hot)
{
  if (theme::current() == theme::C64) {
    MachineWashText(s, 24, 12, hot ? rgb(184,105,98) : 0);
    return;
  }
  bool atari = theme::current() == theme::ATARI;
  for (int pass = 0; pass < 3; pass++) {
    int x = 24;
    for (unsigned i = 0; i < s.length(); i++) {
      char ch = s[i];
      const uint8_t *rows = AMIGA_FONT.rows(ch);
      int sx = atari ? 20 : 8, sy = atari ? 14 : 8;
      for (int r = 0; r < (atari ? 5 : 8); r++) for (int c = 0; c < (atari ? 3 : 8); c++) {
        bool on = atari ? (ch == ':' ? c == 0 && (r == 1 || r == 3) :
                         ch >= '0' && ch <= '9' && (DIGITS[ch-'0'][r] & (4 >> c))) :
                         rows && (MachineGlyphRow(rows, ch, r) & (0x80 >> c));
        if (!on) continue;
        int xx = (ch == ':' && !atari ? x - 2*sx : x) + c*sx, yy = 12 + r*sy;  // the font's colon sits in columns 2..3
        if (pass == 0) rect(xx+4, yy+4, sx, sy, 0);
        else if (pass == 1) rect(xx-2, yy-2, sx+4, sy+4, 0);
        else for (int j=0;j<sy;j+=2)
          rect(xx, yy+j, sx, min(2,sy-j), hot ? (atari ? (r < 3 ? rgb(252,188,148) : rgb(208,128,92)) : ORANGE) :
               atari ? rgb(164-r*15,224-r*17,252-r*15) : CHROME[min((r*sy+j)/2,35)]);
      }
      x += ch == ':' ? (atari ? 24 : 32) : (atari ? 72 : 64);
    }
  }
}

void RadioFaces_Draw(const RadioFaceData &f)
{
  Style saved = style();
  bool amiga = theme::current() == theme::AMIGA, atari = theme::current() == theme::ATARI;
  setStyle({ atari ? &VFD_FONT : amiga ? &AMIGA_FONT : &C64_FONT, false, false, false });
  const uint16_t peach = rgb(252, 188, 148), grey = rgb(149, 149, 149);
  const uint16_t bright = atari ? rgb(236, 236, 236) : WHITE;
  const uint16_t dim = atari ? rgb(144, 144, 144) : amiga ? rgb(153, 153, 170) : BASIC;
  const uint16_t accent = amiga ? ORANGE : atari ? peach : YELLOW;
  const uint16_t green = atari ? rgb(184, 252, 184) : rgb(154, 210, 132);
  const bool slot = RadioFace_Slot();
  const int right = slot ? 336 : 400;
  if (amiga) {
    panel(8, 4, right + 6, 126);
    panel(422, 4, 210, 126);
  }
  clip(16, 4, right + 12, 130);
  label(f.channel, 16, 10, amiga ? ORANGE : dim, amiga);
  label(f.genre, max(96, 16 + textWidth(f.channel.c_str(), 2) + 14), 10, accent, amiga);  // 8 px fonts run longer
  label(MachineFit(f.station, slot ? 13 : 16), 16, 38, bright, amiga, 3);
  if (f.show_volume) {
    label(f.volume_label, 16, 78, bright, amiga);
    int end = 16 + textWidth(f.volume_label.c_str(), 2) + 10;
    for (int i = 0; i < 20; i++)
      rect(end + i * 12, 78, 8, 14, f.volume >= (i + 1) * 5 ? accent : dim);
  } else {
    uint16_t ink = f.failed ? (atari ? rgb(224, 136, 136) : amiga ? ORANGE : rgb(154, 103, 89))
                            : atari ? peach : amiga ? WHITE : grey;
    int width = textWidth(f.line.c_str(), 2), box = right + 12 - 16;
    int span = width + 60, off = width > box ? (millis() / 30) % span : 0;
    clip(16, 70, right + 12, 100);
    int ly = amiga ? 74 : 80;  // the Amiga tracker rows start higher than the dial
    label(f.line, 16 - off, ly, ink, amiga);
    if (width > box) label(f.line, 16 - off + span, ly, ink, amiga);
  }
  noClip();
  // The right title strip stays outside the companion's drawing area.
  clip(422, 4, 632, 28);
  label("STEREO", 440, 10, f.playing ? (amiga ? WHITE : green) : dim, amiga);
  label(f.time, atari ? 568 : 549, 10, amiga ? WHITE : dim, amiga);
  noClip();
  if (amiga) {
    static uint32_t tick = 0, last = 0;
    uint32_t now = millis();
    if (f.playing && last) tick += min(uint32_t(200), uint32_t(now - last));
    last = now;
    clip(16, 96, right + 12, 128);
    for (int k = 0; k < 2; k++) {
      int row = (tick / 150 + k) % 64, y = 96 + k * 16;
      if (!k) rect(16, y, right - 4, 16, ORANGE);
      char line[24];
      snprintf(line, sizeof(line), "%02X  %s %02d  %s", row, row % 3 ? "C-4" : "---", row % 16,
               row % 2 ? "A#3" : "F-2");
      label(line, 16, y + 1, k ? WHITE : 0, k != 0);
    }
    noClip();
  } else {
    if (!atari) {
      const uint16_t bars[] = { rgb(53, 40, 121), BASIC, rgb(112, 164, 178), WHITE,
                               rgb(112, 164, 178), BASIC, rgb(53, 40, 121) };
      for (int y = 98; y < 124; y += 2) rect(16, y, right - 16 + 2, 2, bars[((y - 98) / 2) % 7]);
    }
    for (int x = 16; x <= right; x += 8) {
      bool major = (x - 16) % 64 == 0;
      rect(x, major ? 106 : 110, atari ? 4 : major ? 2 : 1, major ? 10 : 6,
           atari ? (major ? rgb(144, 144, 144) : rgb(68, 68, 68)) : major ? WHITE : BASIC_DIM);
    }
    int nx = 16 + f.station_index * (right - 16) / max(f.station_count - 1, 1);
    if (atari) rect(nx / 4 * 4 - 4, 100, 8, 22, rgb(252, 252, 104));
    else { rect(nx - 3, 100, 6, 22, 0); rect(nx - 1, 102, 2, 18, WHITE); }
  }
  RadioFace_DrawSlot();
  RadioFace_DrawEq();
  for (int i = 0; i < f.groups; i++) {
    int x = 16 + i * 87;
    bool cur = i == f.group;
    uint16_t fill = amiga ? ORANGE : atari ? peach : green;
    if (amiga) {
      if (cur) rect(x, 136, 83, 28, fill); else panel(x, 136, 83, 28);
      uint16_t hi = cur ? 0 : WHITE, lo = cur ? WHITE : 0;
      rect(x, 136, 83, 2, hi); rect(x, 136, 2, 28, hi);
      rect(x, 162, 83, 2, lo); rect(x + 81, 136, 2, 28, lo);
    } else {
      if (cur) rect(x, 136, 83, 28, fill);
      if (!atari) frame(x, 136, 83, 28, BASIC);
    }
    String key = MachineFit(f.genres[i], 5);
    int width = textWidth(key.c_str(), 2);
    label(key, x + (83 - width) / 2, 143, cur ? 0 : atari || amiga ? bright : BASIC, amiga && !cur);
  }
  setStyle(saved);
}

void RadioFaces_Border()
{
  if(theme::current()!=theme::C64 || Radio_State()!=RadioState::Playing) return;
  uint8_t lv[RADIO_BANDS]; Radio_Levels(lv);
  int sum=0; for(int i=0;i<RADIO_BANDS;i++) sum+=lv[i];
  static const uint16_t LOAD[] = {
    rgb(53,40,121), rgb(108,94,181), rgb(112,164,178), rgb(149,149,149),
    rgb(154,210,132), rgb(184,199,111), rgb(111,79,37), rgb(184,105,98)
  };
  int b=theme::def().border_w, n=constrain(2+sum/20,2,8);
  for(int y=0;y<H;y+=4) {
    uint16_t c=LOAD[(y/4+millis()/80)%n];
    rect(0,y,b,min(4,H-y),c); rect(W-b,y,b,min(4,H-y),c);
    if(y<b) rect(b,y,W-2*b,min(4,b-y),c);
    if(y+4>H-b) rect(b,max(y,H-b),W-2*b,min(4,H-y)-(max(y,H-b)-y),c);
  }
}
#endif
