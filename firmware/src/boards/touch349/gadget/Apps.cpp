#ifdef BOARD_TOUCH349

#include "Apps.h"
#include "../../../core/state.h"
#include "Radio.h"
#include "GameBlackjack.h"
#include "GameRunner.h"
#include "Settings.h"
#include "Theme.h"
#include "GameFener.h"
#include "ToolApps.h"
#include "Vfd.h"

using namespace vfd;

static const App *const TOOLS[] = { &APP_LEVEL, &APP_COIN, &APP_BREATHE, &APP_SCORE, &APP_LAMP };
// The Play page joins the ring once this lists a game.
static const App *const PLAY[] = {
  &APP_FENER,
  &APP_BLACKJACK,
  &APP_RUNNER,
};
static constexpr int TOOLS_COUNT = sizeof(TOOLS) / sizeof(TOOLS[0]);
static constexpr int PLAY_COUNT = sizeof(PLAY) / sizeof(PLAY[0]);
static constexpr int MAX_TILES = 6;

static const App *const *menuList(Mode m, int &n)
{
  if (m == Mode::Play) {
    n = PLAY_COUNT;
    return PLAY;
  }
  n = TOOLS_COUNT;
  return TOOLS;
}

int Apps_Count(Mode menu)
{
  int n;
  menuList(menu, n);
  return n;
}

// ---------------------------------------------------------------------------------------------------------------------
// Menu
// ---------------------------------------------------------------------------------------------------------------------

static constexpr int TILE_Y = 14, TILE_H = 144, TILE_GAP = 8, EDGE = 18, TILE_MAX_W = 116;

static void tileSpan(int i, int n, int &x, int &w)
{
  w = min(TILE_MAX_W, (W - 2 * EDGE - TILE_GAP * (n - 1)) / n);
  x = (W - (n * w + (n - 1) * TILE_GAP)) / 2 + i * (w + TILE_GAP);
}

void Apps_Disc(int cx, int cy, int r, uint16_t color)
{
  for (int dy = -r; dy <= r; dy++) {
    int dx = (int)sqrtf((float)(r * r - dy * dy) + 0.5f);
    rect(cx - dx, cy + dy, 2 * dx + 1, 1, color);
  }
}

void Apps_DrawMenu(Mode menu)
{
  int n;
  const App *const *list = menuList(menu, n);
  n = min(n, MAX_TILES);
  clear();
  bool c64 = theme::current() == theme::C64;
  uint16_t fill = c64 ? HALO : GHOST;
  for (int i = 0; i < n; i++) {
    const App &a = *list[i];
    int x, w;
    tileSpan(i, n, x, w);
    rect(x, TILE_Y, w, TILE_H, fill);
    frame(x, TILE_Y, w, TILE_H, DIM);
    int ix = x + (w - 48) / 2, iy = TILE_Y + 26;
    if (style().glow) {
      a.icon(ix - 1, iy, HALO);
      a.icon(ix + 1, iy, HALO);
      a.icon(ix, iy - 1, HALO);
      a.icon(ix, iy + 1, HALO);
    }
    a.icon(ix, iy, LIT);
    const char *name = T(a.en);
    int p = 2;
    int tw = textWidth(name, p);
    text(name, x + (w - tw) / 2, TILE_Y + TILE_H - 36, p, LIT_INK);
  }
}

void Apps_TapMenu(Mode menu, int x, int y)
{
  int n;
  const App *const *list = menuList(menu, n);
  n = min(n, MAX_TILES);
  if (n <= 0) return;
  int x0, w;
  tileSpan(0, n, x0, w);
  int i = constrain((x - x0 + TILE_GAP / 2) / (w + TILE_GAP), 0, n - 1);
  if (x < x0 - EDGE) return;
  Apps_Open(list[i]->key);
}

// ---------------------------------------------------------------------------------------------------------------------
// Running an app
// ---------------------------------------------------------------------------------------------------------------------

static const App *cur = nullptr;
static Mode from_menu = Mode::Tools;
static bool radio_paused = false;

bool Apps_Active() { return cur != nullptr; }
bool Apps_FullScreen() { return cur && cur->full_screen; }
const char *Apps_Key() { return cur ? cur->key : ""; }

static const App *find(const char *key, Mode &menu)
{
  for (int m = 0; m < 2; m++) {
    Mode mm = m ? Mode::Play : Mode::Tools;
    int n;
    const App *const *list = menuList(mm, n);
    for (int i = 0; i < n; i++) {
      if (!strcmp(list[i]->key, key)) {
        menu = mm;
        return list[i];
      }
    }
  }
  return nullptr;
}

bool Apps_Open(const char *key)
{
  Mode menu;
  const App *a = find(key, menu);
  if (!a) return false;
  if (cur) {  // another one is open (a /cmd): close it quietly first
    if (cur->exit) cur->exit();
    cur = nullptr;
  }
  from_menu = menu;
  cur = a;
  radio_paused = false;
  if (a->needs_speaker && Radio_On()) {
    radio_paused = true;
    Radio_Stop();
  }
  if (a->enter) a->enter();
  state.mode = Mode::App;
  state.mode_until_ms = 0;
  state.dirty = true;
  return true;
}

// Ends the app (exit callback, radio back); the caller decides which page is shown next.
static void finish()
{
  if (!cur) return;
  if (cur->exit) cur->exit();
  cur = nullptr;
  if (radio_paused) {
    radio_paused = false;
    Radio_Play(Radio_Station());
  }
}

void Apps_Close()
{
  finish();
  if (state.mode == Mode::App) state.mode = from_menu;
  state.dirty = true;
}

void Apps_Draw(const DateTime &now)
{
  if (!cur) {  // /page app without an app: show the menu
    state.mode = Mode::Tools;
    Apps_DrawMenu(Mode::Tools);
    return;
  }
  cur->draw(now);
}

bool Apps_Gesture(Gesture g, int x, int y)
{
  if (!cur) return false;
  if (g == Gesture::SwipeLeft || g == Gesture::SwipeRight) {
    if (cur->swipes_locked && cur->swipes_locked()) return true;  // a game in play: a sideways slide is no exit
    Apps_Close();
    return true;
  }
  return cur->gesture && cur->gesture(g, x, y);
}

bool Apps_TouchDown(int x, int y) { return cur && cur->touch_down && cur->touch_down(x, y); }

void Apps_Tick()
{
  if (!cur) return;
  if (state.mode != Mode::App) {  // the page was changed from outside (alarm, /page): the app is over
    finish();
    return;
  }
  if (cur->tick) cur->tick();
}

void Apps_Touch(bool down, int x, int y)
{
  if (cur && cur->touch && state.mode == Mode::App) cur->touch(down, x, y);
}

uint32_t Apps_FrameMs()
{
  if (!cur) return 0;
  return cur->frame_ms ? cur->frame_ms() : 50;
}

#endif
