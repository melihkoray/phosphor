#ifdef BOARD_TOUCH349
#include "ClockFaces.h"
#include "Theme.h"
#include "ThemeFonts.h"
#include "Settings.h"
#include "RetroScene.h"
#include "MachineDigits.h"

using namespace vfd;
using px::rgb;

namespace {
constexpr uint16_t WHITE = rgb(236, 236, 236);
constexpr uint16_t BLUE = rgb(136, 204, 255);
constexpr uint16_t GOLD = rgb(255, 153, 0);
constexpr uint16_t PEACH = rgb(252, 188, 148);

// Tiny forecast pictograms share the machine's rectangular pixel grid.
void miniSky(Sky sky, int x, int y, int grid, uint16_t ink, uint16_t accent)
{
  if (sky == Sky::Unknown) return;
  if (sky == Sky::Clear || sky == Sky::PartCloud) {
    rect(x + grid, y, grid * 3, 6, accent);
    rect(x + grid * 2, y - 2, grid, 10, accent);
    if (sky == Sky::Clear) return;
  }
  if (sky == Sky::Fog) {
    for (int row = 0; row < 3; row++) rect(x, y + row * 4, grid * (row == 1 ? 4 : 6), 2, ink);
    return;
  }
  rect(x + grid, y + 2, grid * 3, 4, ink);
  rect(x, y + 6, grid * 6, 4, ink);
  if (sky == Sky::Rain || sky == Sky::Drizzle || sky == Sky::Shower || sky == Sky::Snow) {
    for (int i = 0; i < 3; i++) rect(x + i * grid * 2, y + 12, grid, 2, sky == Sky::Snow ? ink : accent);
  } else if (sky == Sky::Storm) {
    rect(x + 3 * grid, y + 10, grid * 2, 2, accent);
    rect(x + 2 * grid, y + 12, grid * 2, 2, accent);
  }
}

const char *pictureSky(Sky sky, bool day)
{
  switch (sky) {
    case Sky::PartCloud: return day ? "part" : "partn";
    case Sky::Cloud: case Sky::Fog: return "cloud";
    case Sky::Drizzle: case Sky::Rain: case Sky::Shower: return "rain";
    case Sky::Snow: return "snow";
    case Sky::Storm: return "storm";
    default: return day ? "clear" : "night";
  }
}

void weatherLayer(Sky sky, bool amiga)
{
  int grid = amiga ? 2 : 4;
  clip(0, 0, W, 132);
  uint32_t now = millis(), tick = now / 100;
  bool rain = sky == Sky::Rain || sky == Sky::Drizzle || sky == Sky::Shower || sky == Sky::Storm;
  if (rain || sky == Sky::Snow) {
    for (int i = 0; i < 28; i++) {
      int x = ((i * 83 + tick * (rain ? 2 : 1)) % (W / grid)) * grid;
      int y = ((i * 37 + tick * (rain ? 4 : 1)) % 66) * 2;
      if (rain) {
        rect(x, y, grid, 6, BLUE);
        rect(x - grid, y + 6, grid, 2, BLUE);
      } else {
        rect(x, y, grid, 6, WHITE);
        rect(x - grid, y + 2, grid * 3, 2, WHITE);
      }
    }
  } else if (sky == Sky::Fog) {
    uint16_t grey = amiga ? rgb(187, 187, 204) : rgb(149, 149, 149);
    for (int i = 0; i < 5; i++) {
      int x = ((i * 53 + tick / 8) % (W / grid + 40)) * grid - 40 * grid;
      rect(x, 26 + i * 18, grid * (30 + i * 3), 2, grey);
    }
  }
  // A frame counter keeps the flash to two redraws, independent of the refresh interval.
  static uint32_t nextFlash = 0;
  static uint8_t flashFrames = 0;
  if (sky == Sky::Storm) {
    if (!nextFlash) nextFlash = now + 4000;
    if ((int32_t)(now - nextFlash) >= 0) {
      flashFrames = 2;
      nextFlash = now + 4000 + ((now / 17) % 5001);
    }
    if (flashFrames) {
      flashFrames--;
      for (int y = 8; y < 80; y += 18) rect(8, y, W - 16, 2, WHITE);
      for (int i = 0; i < 10; i++) {
        int x = (520 / grid + (i < 5 ? -i : i - 10)) * grid;
        rect(x, 10 + i * 6, grid * 3, 8, WHITE);
      }
    }
  } else { nextFlash = 0; flashFrames = 0; }
  noClip();
}
}  // namespace

void MachineWashText(const String &s, int x, int y, uint16_t colorOverride)
{
  const uint16_t wash[] = { rgb(255,255,255), rgb(154,210,132), rgb(184,199,111), rgb(112,164,178) };
  for (int pass = 0; pass < 3; pass++) {
    int cx = x;
    const char *p = s.c_str();
    while (*p) {
      uint8_t first = *p++;
      uint32_t cp = first;
      if (first >= 128) {
        int count = first >= 0xf0 ? 3 : first >= 0xe0 ? 2 : 1;
        cp = first & (0x3f >> count);
        while (count-- && *p) cp = (cp << 6) | (*p++ & 0x3f);
      }
      const uint8_t *rows = C64_FONT.rows(cp);
      int gx = cp == ':' ? cx - 16 : cx;
      if (rows) for (int r = 0; r < 8; r++) for (int c = 0; c < 8; c++) {
        if (!(MachineGlyphRow(rows, cp, r) & (0x80 >> c))) continue;
        int xx = gx + c * 8, yy = y + r * 8;
        if (pass == 0) rect(xx + 4, yy + 4, 8, 8, 0);
        else if (pass == 1) rect(xx - 2, yy - 2, 12, 12, colorOverride ? rgb(104,55,43) : 0);
        else rect(xx, yy, 8, 8, colorOverride ? colorOverride : wash[r / 2]);
      }
      cx += cp == ':' ? 32 : 64;
    }
  }
}

void WeatherFaces_Draw(const WeatherFace &f)
{
  Style saved = style();
  bool amiga = theme::current() == theme::AMIGA;
  bool atari = theme::current() == theme::ATARI;
  setStyle({ atari ? &VFD_FONT : amiga ? &AMIGA_FONT : &C64_FONT, false, false, false });
  char picture[32];
  snprintf(picture, sizeof(picture), "sky_%s_%s", pictureSky(f.sky, f.day), amiga ? "amiga" : atari ? "atari" : "c64");
  if (!RetroPicture_Draw(picture)) clear();
  weatherLayer(f.sky, amiga);
  if (atari) rect(0, 120, W, 52, 0);

  char temperature[8];
  if (f.weather.valid) snprintf(temperature, sizeof(temperature), "%2d", f.weather.temp);
  else strcpy(temperature, "--");
  MachineDigits_Draw(temperature, 20, 22, 56, 92, 10);
  int degree = 20 + strlen(temperature) * 66 - 10 + 8;
  // A hollow ring remains legible on the light sky pictures.
  rect(degree - 2, 18, 22, 22, 0);
  rect(degree + 4, 24, 10, 10, 0);
  rect(degree, 20, 18, 4, WHITE);
  rect(degree, 34, 18, 4, WHITE);
  rect(degree, 20, 4, 18, WHITE);
  rect(degree + 14, 20, 4, 18, WHITE);

  auto reading = [&](const String &s, int x, int y, int p, uint16_t color) {
    if (!atari) for (int dy = -2; dy <= 2; dy += 2)
      for (int dx = -2; dx <= 2; dx += 2)
        if (dx || dy) text(s.c_str(), x + dx, y + dy, p, { 0 });
    text(s.c_str(), x, y, p, { color });
  };
  uint16_t secondary = atari ? PEACH : BLUE;
  String hum = Percent(f.weather.humidity);
  String humLine = String(T("HUM ")) + hum, windLine = String(T("WIND ")) + String(f.weather.wind);
  // The VFD split (big condition, two reading lines beside it) only where the theme font leaves room before the
  // forecast at x 268; the 8 px machine fonts fall back to the two-line layout sooner.
  int readX = max(140, 20 + textWidth(f.condition.c_str(), 3) + 12);
  int readW = max(textWidth(humLine.c_str(), 2), textWidth(windLine.c_str(), 2));
  if (readX + readW <= 262) {
    reading(f.condition, 20, 132, 3, WHITE);
    if (f.weather.valid) {
      reading(humLine, readX, 126, 2, secondary);
      reading(windLine, readX, 150, 2, secondary);
    }
  } else {
    reading(f.condition, 20, 126, 2, WHITE);
    if (f.weather.valid) reading(hum + "  " + String(f.weather.wind) +
      (settings::fahrenheit() ? " MPH" : T(" KM/H")), 20, 150, 2, secondary);
  }
  for (int i = 0; i < 3; i++) {
    const WeatherDay &d = f.weather.next[i];
    if (d.weekday < 0) continue;
    int x = 268 + i * 122;
    miniSky(d.sky, x, 149, amiga ? 2 : 4, WHITE, atari ? PEACH : GOLD);
    reading(f.days[i], x + 26, 148, 2, secondary);
    // A small ring instead of the degree glyph keeps day, number and degree inside the 122 px column.
    String t = String(d.t_max);
    int tx = max(x + 72, x + 26 + textWidth(f.days[i].c_str(), 2) + 8);
    reading(t, tx, 148, 2, WHITE);
    int rx = tx + textWidth(t.c_str(), 2) + 2;
    if (!atari) rect(rx - 2, 146, 10, 10, 0);
    rect(rx, 148, 6, 2, WHITE); rect(rx, 152, 6, 2, WHITE);
    rect(rx, 148, 2, 6, WHITE); rect(rx + 4, 148, 2, 6, WHITE);
  }
  setStyle(saved);
}
#endif
