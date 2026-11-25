#ifdef BOARD_TOUCH349

#include "Forecast.h"
#include "Settings.h"
#include "Vfd.h"

using namespace vfd;

// The weather page after a tap: the next three days side by side, big. Same ink as the rest of the page (the theme's).
static const char *const SUN[] = { ".#..#..#.", "..#####..", "#.#####.#", "..#####..", ".#..#..#." };
static const char *const PART[] = { "..#......", "#.###....", ".####.##.", "#.##....#", "..#######" };
static const char *const CLOUD[] = { "...###...", ".##...##.", "#.......#", "#########", "........." };
static const char *const FOG[] = { "#########", ".........", ".#######.", ".........", "#########" };
static const char *const RAIN[] = { "...###...", ".##...##.", "#########", ".#..#..#.", "#..#..#.." };
static const char *const SNOW[] = { "...###...", ".##...##.", "#########", ".#..#..#.", "...#..#.." };
static const char *const STORM[] = { "...###...", ".##...##.", "#########", "....##...", "...##...." };

static void bigIcon(Sky s, int cx, int y)
{
  const char *const *rows = CLOUD;
  uint16_t col = LIT;
  switch (s) {
    case Sky::Clear: rows = SUN; col = HOT; break;
    case Sky::PartCloud: rows = PART; break;
    case Sky::Fog: rows = FOG; break;
    case Sky::Drizzle:
    case Sky::Rain:
    case Sky::Shower: rows = RAIN; break;
    case Sky::Snow: rows = SNOW; break;
    case Sky::Storm: rows = STORM; break;
    default: break;
  }
  const int S = 6;  // 9 x 5 dots at 6 px
  int x = cx - 9 * S / 2;
  if (style().glow) sprite(rows, 5, x - 1, y - 1, S, s == Sky::Clear ? HOTH : HALO);
  sprite(rows, 5, x, y, S - 1, col);
}

static int centered(const char *s, int cx, int y, int p, const Ink &ink)
{
  return text(s, cx - textWidth(s, p) / 2, y, p, ink);
}

void Forecast_Draw(const WeatherNow &w, const char *const days[3])
{
  clear();
  for (int i = 0; i < 3; i++) {
    const WeatherDay &d = w.next[i];
    int cx = 106 + i * 214;
    if (i) rect(cx - 107, 14, 1, 144, GHOST);
    if (d.weekday < 0) {
      centered("--", cx, 70, 3, GHOST_INK);
      continue;
    }
    centered(days[i], cx, 12, 3, DIM_INK);
    bigIcon(d.sky, cx, 52);
    centered((String(d.t_max) + "°").c_str(), cx - 36, 100, 4, LIT_INK);
    centered((String(d.t_min) + "°").c_str(), cx + 56, 108, 3, DIM_INK);
    if (d.rain >= 0) {
      String r = Percent(d.rain);
      centered((String(T("RAIN ")) + r).c_str(), cx, 142, 2, d.rain >= 50 ? HOT_INK : DIM_INK);
    }
  }
}

#endif
