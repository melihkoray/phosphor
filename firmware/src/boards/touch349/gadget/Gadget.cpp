#ifdef BOARD_TOUCH349

#include "Gadget.h"
#include "ClockFaces.h"
#include "TimerFaces.h"
#include "Apps.h"
#include "RetroScene.h"
#include <Preferences.h>
#include "../../../core/Assets.h"
#include "../../../core/net.h"
#include "../../../core/state.h"
#include "../../../core/UpdateCheck.h"
#include "Fireplace.h"
#include "Fishtank.h"
#include "Forecast.h"
#include "Hourglass.h"
#include "Lava.h"
#include "Motion.h"
#include "Px.h"
#include "Snowglobe.h"
#include "Radio.h"
#include "RadioFace.h"
#include "Settings.h"
#include "SettingsUi.h"
#include "Seaside.h"
#include "Study.h"
#include "Theme.h"
#include "Vfd.h"
#include "Wave.h"
#include "Weather.h"

using namespace vfd;

// ---------------------------------------------------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------------------------------------------------

static Preferences prefs;

// The swipe ring, in order. The key is what "hide=" in the settings file and the web page use.
struct PageInfo {
  Mode mode;
  const char *key, *en;
  bool can_hide;
};
static const PageInfo PAGES[] = {
  { Mode::Clock, "clock", N_("CLOCK"), false },
  { Mode::Radio, "radio", N_("RADIO"), true },
  { Mode::Weather, "weather", N_("WEATHER"), true },
  { Mode::Timer, "timer", N_("TIMER"), true },
  { Mode::Study, "study", N_("FOCUS"), true },
  { Mode::Hourglass, "hourglass", N_("HOURGLASS"), true },
  { Mode::Aquarium, "aquarium", N_("FISH TANK"), true },
  { Mode::Snow, "snow", N_("SNOW"), true },
  { Mode::Fire, "fire", N_("FIREPLACE"), true },
  { Mode::Sea, "sea", N_("LIGHTHOUSE"), true },
  { Mode::Wave, "wave", N_("SHIP"), true },
  { Mode::Lava, "lava", N_("LAVA LAMP"), true },
  { Mode::Tools, "tools", N_("TOOLS"), true },
  { Mode::Play, "play", N_("PLAY"), true },
  { Mode::Settings, "settings", N_("SETTINGS"), false },
};
static constexpr int PAGE_COUNT = sizeof(PAGES) / sizeof(PAGES[0]);

// Whether the ring has the page: a menu page only while it lists apps, and not one the settings hide.
static bool pageInRing(int i)
{
  const PageInfo &p = PAGES[i];
  if ((p.mode == Mode::Play || p.mode == Mode::Tools) && Apps_Count(p.mode) == 0) return false;
  return !(p.can_hide && settings::pageHidden(p.key));
}
static bool pageListed(const PageInfo &p) { return (p.mode != Mode::Play && p.mode != Mode::Tools) || Apps_Count(p.mode) > 0; }

static const char *weekdayName(int d, bool long_name = false)
{
  d = constrain(d, 0, 6);
  return long_name ? Lang().weekday_long[d] : Lang().weekday_short[d];
}
static const char *monthName(int m) { return Lang().month_upper[constrain(m, 1, 12) - 1]; }
// For the long date of the machine themes, whose fonts have lower case ("Saturday 21 Nov").
static const char *monthTitle(int m) { return Lang().month_title[constrain(m, 1, 12) - 1]; }

// Alarm
static bool alarm_on = false;
static int alarm_h = 7, alarm_m = 30;
static bool ringing = false;
static uint32_t ring_start_ms = 0, ring_beep_ms = 0;
static bool radio_by_alarm = false;
static int fired_day = -1;
#define ALARM_MAX_MS (30 * 60 * 1000UL)
#define ALARM_RADIO_GRACE_MS 12000

// Volume overlay
static uint32_t volume_shown_ms = 0;
#define VOLUME_SHOW_MS 1500

// One-line banner over the bottom of the page: first-visit hints and prompts. Cleared when the page changes.
static String banner;
static uint32_t banner_until_ms = 0;
static void showBanner(const char *en, uint32_t ms)
{
  banner = T(en);
  banner_until_ms = millis() + ms;
  state.dirty = true;
}
static bool banner_prompt = false;  // the banner is the low-battery question: a tap answers yes
static bool bannerOn() { return banner_until_ms && (int32_t)(banner_until_ms - millis()) > 0; }

// First-visit hints: each page of the ring shows one line the first time it is entered, then never again. The seen
// bits (bit = Mode value) are kept in NVS ("hints2"); POST /cmd name=hints arg=reset brings them all back.
struct Hint {
  Mode mode;
  const char *en;  // same wording as the Settings > Help pages
};
static const Hint HINTS[] = {
  { Mode::Clock, N_("TAP THE BELL: ALARM ON/OFF") },
  { Mode::Radio, N_("TAP: PLAY / STOP") },
  { Mode::Weather, N_("SWIPE LEFT/RIGHT: NEXT PAGE") },
  { Mode::Timer, N_("TAP: START / PAUSE") },
  { Mode::Study, N_("TAP: START / PAUSE") },
  { Mode::Hourglass, N_("HOURGLASS: TILT TO POUR") },
  { Mode::Aquarium, N_("FISH TANK: TAP TO FEED") },
  { Mode::Snow, N_("SNOW: TAP OR SHAKE TO STIR") },
  { Mode::Fire, N_("FIREPLACE: TAP, CAT WAKES") },
  { Mode::Sea, N_("LIGHTHOUSE: TAP, BIG WAVE") },
  { Mode::Wave, N_("SHIP: TAP THE WATER") },
  { Mode::Lava, N_("LAVA LAMP: TAP, NEW COLORS") },
  { Mode::Tools, N_("TOOLS: TAP A TILE TO OPEN IT") },
  { Mode::Play, N_("PLAY: TAP A TILE TO OPEN IT") },
  { Mode::Settings, N_("SWIPE UP/DOWN: PICK AN ITEM") },
};
static uint32_t hints_seen = 0;
#define HINT_MS 3000

// Countdown timer: minutes set with the buttons, counts down, rings (the interval programs come back later)
enum class Phase { Idle, Running, Paused, Done };
static Phase phase = Phase::Idle;
static int32_t egg_set_s = 600;  // what it counts down from
static uint32_t egg_end_ms = 0;
static int32_t egg_left_ms = 0;  // while paused
static uint32_t egg_done_ms = 0, egg_beep_ms = 0;
#define EGG_RING_MS 60000

// Radio page spectrum (display side: smoothed, with falling peaks)


static void savePrefs()
{
  prefs.putBool("al_on", alarm_on);
  prefs.putUChar("al_h", alarm_h);
  prefs.putUChar("al_m", alarm_m);
  prefs.putUChar("st2", Radio_Station());
  prefs.putUChar("volume", Radio_Volume());
}

static int weekday(int y, int m, int d)
{
  static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
  if (m < 3) y--;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

static String two(int v)
{
  char b[4];
  snprintf(b, sizeof(b), "%02d", v);
  return b;
}

// "14:05", or "02:05" with the 12-hour clock chosen in settings
static String hhmm(const DateTime &now)
{
  int h = now.hour;
  if (!settings::h24()) h = h % 12 == 0 ? 12 : h % 12;
  return two(h) + ":" + two(now.minute);
}

static String mss(int32_t secs)
{
  if (secs < 0) secs = 0;
  char b[12];
  snprintf(b, sizeof(b), "%d:%02d", (int)(secs / 60), (int)(secs % 60));
  return b;
}

static bool blinkOn(uint32_t period = 500) { return (millis() / period) % 2 == 0; }

// ---------------------------------------------------------------------------------------------------------------------
// Alarm and timer logic
// ---------------------------------------------------------------------------------------------------------------------

static void stopAlarm()
{
  ringing = false;
  if (radio_by_alarm) {
    Radio_Stop();
    Radio_SetVolume(prefs.getUChar("volume", 30));
  }
  radio_by_alarm = false;
  state.dirty = true;
}

static void startAlarm()
{
  ringing = true;
  ring_start_ms = millis();
  ring_beep_ms = 0;
  board().setDisplayOn(true);
  state.mode = Mode::Clock;
  state.mode_until_ms = 0;
  state.dirty = true;
  if (!Radio_On()) {
    radio_by_alarm = true;
    Radio_Play(Radio_Station());
    Radio_RampVolume(60000);
  }
}

static int32_t eggLeftMs()
{
  switch (phase) {
    case Phase::Idle: return egg_set_s * 1000;
    case Phase::Paused: return egg_left_ms;
    case Phase::Running: return max<int32_t>(0, (int32_t)(egg_end_ms - millis()));
    default: return 0;
  }
}

static void timerTick()
{
  if (phase == Phase::Running && (int32_t)(egg_end_ms - millis()) <= 0) {
    phase = Phase::Done;
    egg_done_ms = millis();
    egg_beep_ms = 0;
    board().setDisplayOn(true);
    state.mode = Mode::Timer;
    state.mode_until_ms = 0;
    state.dirty = true;
  }
  if (phase == Phase::Done) {
    if (millis() - egg_done_ms > EGG_RING_MS) {
      phase = Phase::Idle;
      state.dirty = true;
    } else if (millis() - egg_beep_ms > 1600) {
      egg_beep_ms = millis();
      Radio_Beep(1318, 110, 2, 90);
    }
  }
}

static void timerStartPause()
{
  switch (phase) {
    case Phase::Idle:
      if (egg_set_s <= 0) return;
      egg_end_ms = millis() + egg_set_s * 1000;
      phase = Phase::Running;
      Radio_Beep(880, 60);
      break;
    case Phase::Running:
      egg_left_ms = eggLeftMs();
      phase = Phase::Paused;
      break;
    case Phase::Paused:
      egg_end_ms = millis() + egg_left_ms;
      phase = Phase::Running;
      break;
    case Phase::Done: phase = Phase::Idle; break;
  }
  state.dirty = true;
}

static void timerReset()
{
  phase = Phase::Idle;
  state.dirty = true;
}

static void eggSet(int32_t secs)
{
  egg_set_s = constrain(secs, 0, 99 * 60);
  prefs.putUShort("egg", egg_set_s);
  phase = Phase::Idle;
  state.dirty = true;
}

// Minutes on or off: the running clock, the paused one, a ringing one (counts again) or the setting.
static void eggAddMinutes(int m)
{
  int32_t d = m * 60000;
  if (phase == Phase::Running) {
    int32_t left = max<int32_t>(0, (int32_t)(egg_end_ms - millis()));
    egg_end_ms = millis() + constrain(left + d, 1000, 99 * 60000);
  } else if (phase == Phase::Paused) {
    egg_left_ms = constrain(egg_left_ms + d, 1000, 99 * 60000);
  } else if (phase == Phase::Done) {
    if (m > 0) {
      egg_end_ms = millis() + d;
      phase = Phase::Running;
    }
  } else {
    eggSet(egg_set_s + m * 60);
  }
  state.dirty = true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Pages
// ---------------------------------------------------------------------------------------------------------------------

// Battery level under the seconds: outline, fill by percent, the number; a bolt while charging (state.charging, which
// the battery code infers from the voltage: the board has no charge pin).
static void drawBatteryBadge(const DateTime &now)
{
  if (state.batt_pct < 0 || state.batt_v <= 0) return;
  bool charging = state.charging;
  int pct = constrain(state.batt_pct, 0, 100);
  int x = 412, y = 122;
  uint16_t ink = pct <= 15 && !charging ? HOT : DIM;
  frame(x, y, 34, 18, ink);
  rect(x + 34, y + 5, 3, 8, ink);
  rect(x + 3, y + 3, 28 * pct / 100, 12, ink);
  if (charging) {
    static const char *const BOLT[] = { "..##", ".##.", "####", ".##.", "##.." };
    sprite(BOLT, 5, x + 42, y + 4, 2, HOT);
  }
  char buf[8];
  snprintf(buf, sizeof(buf), "%d%%", pct);
  text(buf, x, y + 26, 2, DIM_INK);
}

// Seconds as one magenta dot walking clockwise around the screen edge: 60 stops from the top middle, faint marks at
// the empty ones.
static void drawSecondDot(int second)
{
  const int x0 = 4, y0 = 4, x1 = W - 5, y1 = H - 5;
  const float w = x1 - x0, h = y1 - y0, per = 2 * (w + h);
  auto at = [&](int i, int &x, int &y) {
    float d = fmodf(w / 2 + i * per / 60, per);  // start at the top middle
    if (d < w) x = x0 + d, y = y0;
    else if ((d -= w) < h) x = x1, y = y0 + d;
    else if ((d -= h) < w) x = x1 - d, y = y1;
    else x = x0, y = y1 - (d - w);
  };
  for (int i = 0; i < 60; i++) {
    int x, y;
    at(i, x, y);
    rect(x - 1, y - 1, i % 5 ? 2 : 3, i % 5 ? 2 : 3, i % 15 ? GHOST : DIM);
  }
  int x, y;
  at(second, x, y);
  rect(x - 6, y - 6, 12, 12, HOTH);
  rect(x - 4, y - 4, 8, 8, HOT);
}

static void drawClock(const DateTime &now)
{
  if (theme::current() != theme::VFD) {
    if (theme::current() == theme::C64 || !RetroWorld_Draw()) clear();
    int wd = weekday(now.year, now.month, now.day);
    String date = String(now.day) + " " + monthName(now.month);
    ClockFace face = { hhmm(now), String(weekdayName(wd)) + " " + date,
                       String(weekdayName(wd, true)) + " " + String(now.day) + " " + monthTitle(now.month),
                       ringing ? String(T("WAKE UP!")) : two(alarm_h) + ":" + two(alarm_m),
                       alarm_on && (!ringing || blinkOn(300)), ringing, now.second % 2 == 0, state.batt_pct, state.charging,
                       String(weekdayName(wd, true)), String(weekdayName(wd)), date,
                       settings::h24() ? "24H" : now.hour < 12 ? "AM" : "PM",
                       two(alarm_h) + ":" + two(alarm_m), alarm_on, now.second,
                       UpdateCheck_Newer() };
    ClockFaces_Draw(face);
    return;
  }
  clear();
  String hm = hhmm(now);
  // VFD draws ghost 8s, so the digits fill their cells; the machines show only the lit ones, which leaves a gap on the left
  seg(hm.c_str(), theme::current() == theme::VFD ? 20 : 10, 24, 78, 124, 12, 12, { LIT, HALO, GHOST });
  drawSecondDot(now.second);
  bool alarm_lit = alarm_on && (!ringing || blinkOn(300));
  text("AL", 412, 26, 2, alarm_on ? HOT_INK : GHOST_INK);
  if (settings::h24()) text("24H", 412, 48, 2, GHOST_INK);
  else text(now.hour < 12 ? "AM" : "PM", 412, 48, 2, DIM_INK);
  text(weekdayName(weekday(now.year, now.month, now.day)), 492, 26, 3, 2, LIT_INK);
  String date = String(now.day) + " " + monthName(now.month);
  text(date.c_str(), 492, 54, 2, DIM_INK);
  rect(492, 80, 132, 1, GHOST);

  static const char *const BELL[] = { "...#...", "..###..", ".#####.", ".#####.", ".#####.", "#######", "...#..." };
  if (alarm_lit) {
    sprite(BELL, 7, 491, 93, 2, HOTH);
    sprite(BELL, 7, 493, 95, 2, HOTH);
    sprite(BELL, 7, 492, 94, 2, HOT);
  } else {
    sprite(BELL, 7, 492, 94, 2, GHOST);
  }
  if (*UpdateCheck_Newer()) {  // a newer release exists (Settings > About has the details)
    static const char *const ARROW[] = { "..#..", ".###.", "#.#.#", "..#..", "..#.." };
    sprite(ARROW, 5, 412, 88, 2, HOT);
    text(T("NEW"), 428, 88, 2, DIM_INK);
  }
  text(ringing ? T("WAKE UP!") : "ALARM", 512, 96, 2, alarm_lit ? HOT_INK : GHOST_INK);
  String at = two(alarm_h) + ":" + two(alarm_m);
  seg(at.c_str(), 492, 120, 24, 40, 5, 5, alarm_on ? HOT_INK : Ink{ DIM });
  drawBatteryBadge(now);
}

static void drawVolumeBar(int x, int y)
{
  int v = Radio_Volume();
  int end = text(T("VOL"), x, y, 2, LIT_INK) + 10;
  for (int i = 0; i < 20; i++) {
    bool on = v >= (i + 1) * 5;
    if (on) rect(end + i * 12 - 1, y - 1, 10, 16, HALO);
    rect(end + i * 12, y, 8, 14, on ? LIT : GHOST);
  }
}

static bool radio_world = false;

static void drawRadio(const DateTime &now)
{
  radio_world = false;
  int st = constrain(Radio_Station(), 0, settings::stationCount() - 1);
  const RadioStation &s = settings::station(st);
  RadioState rs = Radio_State();
  String gname = settings::group(s.group).name;
  gname.toUpperCase();

  if (theme::current() != theme::VFD) {
    if (theme::current() == theme::AMIGA) {
      if (!RetroPicture_Draw("radio_amiga") && !RetroWorld_Draw()) clear();
    } else clear(theme::current() == theme::C64 ? px::rgb(53, 40, 121) : 0);
    RadioFaceData f;
    f.channel = "CH " + two(st + 1); f.genre = gname; f.station = s.name; f.time = hhmm(now);
    f.playing = rs == RadioState::Playing; f.failed = rs == RadioState::Failed;
    f.station_index = st; f.station_count = settings::stationCount();
    f.show_volume = volume_shown_ms && millis() - volume_shown_ms < VOLUME_SHOW_MS;
    f.volume = Radio_Volume(); f.volume_label = T("VOL");
    f.group = s.group; f.groups = settings::groupCount();
    for (int i = 0; i < f.groups; i++) { f.genres[i] = settings::group(i).key; f.genres[i].toUpperCase(); }
    switch (rs) {
      case RadioState::Off: f.line = T("STOPPED: TAP TO PLAY"); break;  // 20 machine cells fit beside the companion
      case RadioState::Connecting: f.line = String(T("CONNECTING")) + String("...").substring(0, (millis() / 400) % 4); break;
      case RadioState::Failed: f.line = T("NO SIGNAL: TAP AGAIN"); break;
      case RadioState::Playing: f.line = Radio_Title(); if (!f.line.length()) f.line = T("LIVE"); break;
    }
    radio_world = true;
    RadioFaces_Draw(f);
    return;
  }
  clear();
  text(("CH " + two(st + 1)).c_str(), 16, 10, 2, DIM_INK);
  text(gname.c_str(), 96, 10, 2, LIT_INK);
  text("STEREO", 440, 10, 2, rs == RadioState::Playing ? HOT_INK : GHOST_INK);
  text(hhmm(now).c_str(), 568, 10, 2, DIM_INK);

  bool slot = RadioFace_Slot();  // the companion left of the equalizer takes the right end of the name and dial
  int right = slot ? 336 : 400;
  char name[17];
  snprintf(name, sizeof(name), slot ? "%-13.13s" : "%-16s", s.name.c_str());
  text(name, 16, 36, 4, 3, { LIT, HALO, GHOST });

  // line under the name: volume while it changes, else what is playing
  if (volume_shown_ms && millis() - volume_shown_ms < VOLUME_SHOW_MS) {
    drawVolumeBar(16, 78);
  } else {
    String line;
    Ink ink = DIM_INK;
    switch (rs) {
      case RadioState::Off: line = T("STOPPED · TAP TO PLAY"); break;
      case RadioState::Connecting: line = String(T("CONNECTING")) + String("...").substring(0, (millis() / 400) % 4); break;
      case RadioState::Failed:
        line = T("NO SIGNAL · TAP TO RETRY");
        ink = HOT_INK;
        break;
      case RadioState::Playing:
        line = Radio_Title();
        if (!line.length()) line = T("LIVE");
        break;
    }
    int w = textWidth(line.c_str(), 2), box = right + 12 - 16;
    clip(16, 70, right + 12, 100);
    if (w <= box) {
      text(line.c_str(), 16, 80, 2, ink);
    } else {
      int span = w + 60, off = (millis() / 30) % span;
      text(line.c_str(), 16 - off, 80, 2, ink);
      text(line.c_str(), 16 - off + span, 80, 2, ink);
    }
    noClip();
  }

  // tuning dial with the needle on this station
  for (int x = 16; x <= right; x += 8) {
    bool major = (x - 16) % 64 == 0;
    rect(x, major ? 106 : 110, major ? 2 : 1, major ? 10 : 6, major ? DIM : GHOST);
  }
  int nx = 16 + st * (right - 16) / max(settings::stationCount() - 1, 1);
  rect(nx - 3, 100, 6, 22, HOTH);
  rect(nx - 1, 102, 2, 18, HOT);

  RadioFace_DrawSlot();
  RadioFace_DrawEq();

  // genre presets
  for (int i = 0; i < settings::groupCount(); i++) {
    int x = 16 + i * 87;
    bool cur = i == s.group;
    frame(x, 136, 83, 28, cur ? DIM : GHOST);
    String key = settings::group(i).key;
    key.toUpperCase();
    const char *n = key.c_str();
    text(n, x + 41 - textWidth(n, 2) / 2, 143, 2, cur ? LIT_INK : GHOST_INK);
  }
}

// The sky over the desk, one 100x34 dot matrix (4 px pitch) right of the readings: the weather itself fills it, a
// striped synthwave sun, a crescent with stars, drifting clouds in two layers, rain, snow, fog bands, lightning.

static constexpr int SKY_X = 232, SKY_C = 100, SKY_R = 34;
static uint16_t sky[SKY_R][SKY_C];

static uint32_t hash32(uint32_t v)
{
  v ^= v >> 16;
  v *= 0x7feb352d;
  v ^= v >> 15;
  v *= 0x846ca68b;
  return v ^ (v >> 16);
}

static void put(int c, int r, uint16_t col)
{
  if (c >= 0 && c < SKY_C && r >= 0 && r < SKY_R) sky[r][c] = col;
}

static float wrap(float v, float span)
{
  v = fmodf(v, span);
  return v < 0 ? v + span : v;
}

// Puffy cloud shape inside a w x h box: four overlapping puffs on a flat base.
static bool inCloud(float lx, float ly, float w, float h)
{
  if (ly < 0 || ly > h - 1 || lx < 0 || lx > w) return false;
  auto in = [&](float cx, float cy, float r) { return (lx - cx) * (lx - cx) + (ly - cy) * (ly - cy) <= r * r; };
  return in(0.20f * w, 0.70f * h, 0.30f * h) || in(0.42f * w, 0.45f * h, 0.45f * h) ||
         in(0.66f * w, 0.52f * h, 0.40f * h) || in(0.84f * w, 0.72f * h, 0.28f * h) ||
         (lx >= 0.20f * w && lx <= 0.84f * w && ly >= 0.70f * h);
}

// Filled cloud with a lit rim; it hides whatever was stamped behind it.
static void stampCloud(float x, int y, int w, int h, uint16_t fill, uint16_t edge)
{
  for (int r = max(0, y); r < min(SKY_R, y + h); r++) {
    for (int c = max(0, (int)floorf(x)); c < min(SKY_C, (int)ceilf(x + w) + 1); c++) {
      float lx = c - x, ly = r - y;
      if (!inCloud(lx, ly, w, h)) continue;
      bool rim = !inCloud(lx - 1, ly, w, h) || !inCloud(lx + 1, ly, w, h) || !inCloud(lx, ly - 1, w, h) ||
                 !inCloud(lx, ly + 1, w, h);
      sky[r][c] = rim ? edge : fill;
    }
  }
}

// A row of clouds wrapping around the sky: n clouds of w x h at row y, drifting right at speed cells per second.
static void cloudRow(uint32_t t, int n, int w, int h, int y, float speed, int seed, uint16_t fill, uint16_t edge)
{
  float span = SKY_C + w + 6;
  for (int i = 0; i < n; i++) {
    float x = wrap(i * span / n + seed + t * speed / 1000.0f, span) - w - 3;
    int dy = (int)(hash32(seed * 7 + i) % 5) - 2;
    stampCloud(x, y + dy, w, h, fill, edge);
  }
}

// A low, closed cloud deck for rain and snow: two layers, the front one lit.
static void overcast(uint32_t t, bool flash)
{
  cloudRow(t, 4, 40, 16, -6, 0.5f, 11, GHOST, DIM);
  cloudRow(t, 3, 48, 18, -2, 0.9f, 40, flash ? DIM : HALO, flash ? HOT : LIT);
}

static void sun(int cx, int cy, int rad, uint32_t t)
{
  for (int k = 0; k < 12; k++) {  // rays, turning slowly
    float a = k * PI / 6 + t * 0.00012f;
    int len = k % 2 ? 2 : 4;
    for (int d = rad + 3; d < rad + 3 + len; d++) put(cx + lroundf(cosf(a) * d), cy + lroundf(sinf(a) * d), DIM);
  }
  for (int r = -rad; r <= rad; r++) {
    // synthwave stripes, wider towards the bottom
    if (r > rad / 4 && ((r - rad / 4) % 3 == 1 || (r > rad / 2 && (r - rad / 4) % 3 == 2))) continue;
    for (int c = -rad; c <= rad; c++) {
      if (c * c + r * r <= rad * rad + rad) put(cx + c, cy + r, HOT);
    }
  }
}

static void stars(uint32_t t, int n)
{
  for (int i = 0; i < n; i++) {
    uint32_t h = hash32(i + 1);
    int c = h % SKY_C, r = (h >> 8) % SKY_R;
    uint32_t tw = (t / 400 + (h >> 16)) % 9;
    if (tw < 7) put(c, r, tw == 0 ? LIT : (i % 3 ? GHOST : DIM));
  }
  uint32_t ph = t % 11000;  // a shooting star every 11 s
  if (ph < 700) {
    uint32_t h = hash32(t / 11000);
    float p = ph / 700.0f;
    int c0 = 10 + h % 50, r0 = 2 + (h >> 8) % 6;
    for (int k = 0; k < 6; k++) {
      float q = p - k * 0.04f;
      if (q < 0) break;
      put(c0 + lroundf(q * 40), r0 + lroundf(q * 14), k == 0 ? HOT : DIM);
    }
  }
}

static void moon(int cx, int cy, int rad)
{
  for (int r = -rad; r <= rad; r++) {
    for (int c = -rad; c <= rad; c++) {
      if (c * c + r * r > rad * rad + rad) continue;
      int bc = c - rad / 2 - 1, br = r + rad / 3;  // the bite that makes it a crescent
      put(cx + c, cy + r, bc * bc + br * br <= rad * rad ? GHOST : LIT);
    }
  }
}

static void rain(uint32_t t, int n, float rows_per_s, float slant, bool streak)
{
  for (int i = 0; i < n; i++) {
    uint32_t h = hash32(i * 13 + 5);
    float y = 8 + wrap((h >> 8) % 26 + t * rows_per_s / 1000.0f, 27);
    float x = (h % (SKY_C + 12)) - (y - 8) * slant;
    int c = lroundf(x), r = (int)y;
    put(c, r, LIT);
    if (streak) put(lroundf(x + slant), r - 1, DIM);
  }
}

static void snow(uint32_t t)
{
  for (int i = 0; i < 42; i++) {
    uint32_t h = hash32(i * 29 + 3);
    float y = 9 + wrap((h >> 8) % 25 + t * 3.2f / 1000.0f * (1 + (h >> 20) % 3 * 0.3f), 26);
    int c = lroundf((h % SKY_C) + sinf(t * 0.0012f + i) * 1.6f), r = (int)y;
    if (i % 3 == 0) {  // a big flake
      put(c, r, LIT);
      put(c - 1, r, DIM);
      put(c + 1, r, DIM);
      put(c, r - 1, DIM);
      put(c, r + 1, DIM);
    } else {
      put(c, r, i % 2 ? LIT : DIM);
    }
  }
}

// A forked bolt every 4.6 s; true during its first flash.
static bool bolt(uint32_t t)
{
  uint32_t ph = t % 4600;
  if (ph > 300) return false;
  uint32_t seed = t / 4600;
  int c = 14 + hash32(seed) % 76;
  for (int r = 12; r < SKY_R; r++) {
    c += (int)(hash32(seed * 31 + r) % 3) - 1;
    put(c, r, HOT);
    if (r == 20) {  // a short fork
      for (int k = 1; k < 6; k++) put(c + k, r + k, HOT);
    }
  }
  return ph < 90;
}

static void fog(uint32_t t)
{
  for (int k = 0; k < 8; k++) {
    int r = 2 + k * 4 + (k % 2);
    float off = t * (0.8f + k * 0.25f) / 1000.0f * (k % 2 ? 1 : -1);
    int len = 9 + (k * 5) % 8;
    for (int c = 0; c < SKY_C; c++) {
      if (wrap(c + off + k * 7, 24) < len) put(c, r, k % 2 ? DIM : HALO);
    }
  }
}

static void drawSky(Sky s, bool day)
{
  uint32_t t = millis();
  memset(sky, 0, sizeof(sky));
  switch (s) {
    case Sky::Clear:
      if (day) {
        sun(50, 17, 10, t);
      } else {
        stars(t, 60);
        moon(66, 13, 9);
      }
      break;
    case Sky::PartCloud:
      if (day) {
        sun(34, 15, 9, t);
      } else {
        stars(t, 40);
        moon(34, 12, 8);
      }
      cloudRow(t, 2, 40, 16, 13, 1.1f, 30, HALO, LIT);
      break;
    case Sky::Cloud:
      cloudRow(t, 4, 40, 16, 0, 0.5f, 11, GHOST, DIM);
      cloudRow(t, 3, 48, 18, 9, 0.9f, 40, HALO, LIT);
      cloudRow(t, 2, 44, 17, 18, 1.4f, 77, HALO, LIT);
      break;
    case Sky::Fog: fog(t); break;
    case Sky::Drizzle:
      rain(t, 28, 9, 0.15f, false);
      overcast(t, false);
      break;
    case Sky::Rain:
      rain(t, 55, 16, 0.3f, true);
      overcast(t, false);
      break;
    case Sky::Shower:
      rain(t, 90, 22, 0.5f, true);
      overcast(t, false);
      break;
    case Sky::Snow:
      snow(t);
      overcast(t, false);
      break;
    case Sky::Storm: {
      rain(t, 80, 22, 0.5f, true);
      bool flash = bolt(t);
      overcast(t, flash);
      break;
    }
    default: stars(t, 20); break;
  }

  // matrix: unlit dots faint, lit ones with their glow
  for (int r = 0; r < SKY_R; r++) {
    for (int c = 0; c < SKY_C; c++) {
      uint16_t col = sky[r][c];
      if (col == LIT || col == HOT) rect(SKY_X + c * 4 - 1, r * 4 - 1, 5, 5, col == HOT ? HOTH : HALO);
    }
  }
  for (int r = 0; r < SKY_R; r++) {
    for (int c = 0; c < SKY_C; c++) {
      uint16_t col = sky[r][c];
      rect(SKY_X + c * 4, r * 4, 3, 3, col ? col : FAINT);
    }
  }
}

// Small icons for the next days, 9 dots wide.
static void skyIcon(Sky s, int x, int y)
{
  static const char *const SUN[] = { ".#..#..#.", "..#####..", "#.#####.#", "..#####..", ".#..#..#." };
  static const char *const PART[] = { "..#......", "#.###....", ".####.##.", "#.##....#", "..#######" };
  static const char *const CLOUD[] = { "...###...", ".##...##.", "#.......#", "#########", "........." };
  static const char *const FOG[] = { "#########", ".........", ".#######.", ".........", "#########" };
  static const char *const RAIN[] = { "...###...", ".##...##.", "#########", ".#..#..#.", "#..#..#.." };
  static const char *const SNOW[] = { "...###...", ".##...##.", "#########", ".#..#..#.", "...#..#.." };
  static const char *const STORM[] = { "...###...", ".##...##.", "#########", "....##...", "...##...." };
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
  sprite(rows, 5, x, y, 2, col);
}

static int sky_force = -1;   // /cmd sky: preview a sky (Sky index), -1 = the real one
static bool sky_night = false;

static uint32_t forecast_until = 0;  // tap on the weather page: the next days, for a while

static void drawWeather()
{
  WeatherNow w = Weather_Get();
  if (forecast_until && (int32_t)(millis() - forecast_until) < 0) {
    const char *days[3];
    for (int i = 0; i < 3; i++) days[i] = w.next[i].weekday < 0 ? "--" : weekdayName(w.next[i].weekday);
    Forecast_Draw(w, days);
    return;
  }
  forecast_until = 0;
  if (theme::current() != theme::VFD) {
    WeatherFace f;
    f.weather = w; f.day = sky_force >= 0 ? !sky_night : w.day; f.sky = sky_force >= 0 ? (Sky)sky_force : w.valid ? w.sky : Sky::Unknown;
    f.condition = f.sky != Sky::Unknown ? Weather_SkyName(f.sky) : T("WAIT");
    for (int i = 0; i < 3; i++) f.days[i] = weekdayName(w.next[i].weekday);
    WeatherFaces_Draw(f);
    return;
  }
  clear();

  // temperature: two 7-segment cells, right aligned, the empty one ghosted
  char tbuf[6];
  if (w.valid) {
    snprintf(tbuf, sizeof(tbuf), "%2d", w.temp);
  } else {
    strcpy(tbuf, "--");
  }
  int end = seg(tbuf, 20, 22, 56, 92, 10, 10, { w.valid ? LIT : DIM, w.valid ? HALO : NONE, GHOST });
  int dx = end - 10 + 8;
  rect(dx, 20, 18, 18, HALO);
  rect(dx + 2, 22, 14, 14, LIT);
  rect(dx + 6, 26, 6, 6, BG);

  Sky s = sky_force >= 0 ? (Sky)sky_force : w.valid ? w.sky : Sky::Unknown;
  bool day = sky_force >= 0 ? !sky_night : w.day;
  const char *cond = s != Sky::Unknown ? Weather_SkyName(s) : T("WAIT");
  String hum = Percent(w.humidity);
  if (glyphCount(cond) <= 6) {
    // short sky name in big letters, humidity and wind beside it
    text(cond, 20, 132, 3, 2, LIT_INK);
    if (w.valid) {
      text((String(T("HUM ")) + hum).c_str(), 140, 126, 2, DIM_INK);
      text((String(T("WIND ")) + String(w.wind)).c_str(), 140, 150, 2, DIM_INK);
    }
  } else {
    // long name ("PARTLY CLOUDY") takes the line; humidity and wind go under it
    text(cond, 20, 126, 2, LIT_INK);
    if (w.valid) text((hum + "  " + String(w.wind) + (settings::fahrenheit() ? " MPH" : T(" KM/H"))).c_str(), 20, 150, 2, DIM_INK);
  }

  drawSky(s, day);

  for (int i = 0; i < 3; i++) {
    const WeatherDay &d = w.next[i];
    if (d.weekday < 0) continue;
    int x = 268 + i * 122;
    skyIcon(d.sky, x, 149);
    text(weekdayName(d.weekday), x + 26, 148, 2, DIM_INK);
    text((String(d.t_max) + "°").c_str(), x + 72, 148, 2, LIT_INK);
  }
}

static const int8_t COUNT_STEPS[] = { -1, 1, 5, 10 };
static const char *const COUNT_LABELS[] = { N_("-1 MIN"), N_("+1 MIN"), N_("+5 MIN"), N_("+10 MIN"), "RESET" };
static const char *countLabel(int i) { return i == 4 ? TCTX("count", "RESET") : T(COUNT_LABELS[i]); }

static void drawTimer()
{
  bool running = phase == Phase::Running, done = phase == Phase::Done;

  int32_t left_s = (eggLeftMs() + 999) / 1000;
  bool show = phase != Phase::Paused || blinkOn(400);
  Ink big = done ? Ink{ HOT, HOTH, GHOST } : Ink{ LIT, HALO, GHOST };
  if (!show || (done && !blinkOn(300))) big = Ink{ GHOST, NONE, GHOST };
  String digits = mss(left_s);
  if (digits.length() < 5) digits = " " + digits;
  if (theme::current() != theme::VFD) {
    TimerFace f;
    f.digits = digits;
    f.title = T("TIMER");
    f.launch_ms = done ? millis() - egg_done_ms : 0;
    f.setting = String(T("SET ")) + mss(egg_set_s);
    f.running = running; f.paused = phase == Phase::Paused; f.done = done;
    f.visible = show && (!done || blinkOn(300));
    f.state = running ? T("RUNNING") : f.paused ? T("PAUSED") :
              done ? T("TIME'S UP!") : egg_set_s ? T("TAP: START") : T("PICK A TIME");
    int32_t total = max<int32_t>(1, egg_set_s * 1000);
    f.progress = phase == Phase::Idle ? 0 : done ? 604 : constrain((int)(604 * (1 - (float)eggLeftMs()/total)),0,604);
    for (int i=0;i<5;i++) f.buttons[i] = countLabel(i);
    if (TimerFaces_Draw(f)) return;
  }
  clear();
  seg(digits.c_str(), 20, 12, 70, 100, 12, 12, big);
  Rocket_Draw(done, done ? millis() - egg_done_ms : 0);

  text(T("TIMER"), 470, 16, 3, 2, LIT_INK);
  text((String(T("SET ")) + mss(egg_set_s)).c_str(), 470, 50, 2, DIM_INK);
  const char *l1 = egg_set_s ? T("TAP: START") : T("PICK A TIME");
  Ink ink = GHOST_INK;
  if (running) {
    l1 = T("RUNNING");
    ink = LIT_INK;
  } else if (phase == Phase::Paused) {
    l1 = T("PAUSED");
    ink = DIM_INK;
  } else if (done) {
    l1 = T("TIME'S UP!");
    ink = HOT_INK;
  }
  text(l1, 470, 84, 2, ink);

  // progress
  int32_t total = max<int32_t>(1, egg_set_s * 1000);
  int w = 604, fill = phase == Phase::Idle ? 0 : done ? w : constrain((int)(w * (1 - (float)eggLeftMs() / total)), 0, w);
  rect(20, 122, w, 4, GHOST);
  if (fill > 0) {
    rect(19, 121, fill + 2, 6, HOTH);
    rect(20, 122, fill, 4, HOT);
  }

  for (int i = 0; i < 5; i++) {
    int x = 16 + i * 122;
    const char *n = countLabel(i);
    frame(x, 136, 116, 28, i == 4 ? GHOST : DIM);
    text(n, x + 58 - textWidth(n, 2) / 2, 143, 2, i == 4 ? DIM_INK : LIT_INK);
  }
}

// ---------------------------------------------------------------------------------------------------------------------
// Interface
// ---------------------------------------------------------------------------------------------------------------------

void Gadget_Begin(uint16_t *canvas, I2cMasterBus *bus, void (*amp)(bool))
{
  vfd::begin(canvas);
  assets::begin();
  settings::begin();
  prefs.begin("gadget", false);
  px::begin();
  int old_theme = prefs.isKey("theme") ? prefs.getUChar("theme", 2) : -1;  // pre-1.1 scene colors, migrated once
  theme::begin(old_theme);
  if (old_theme >= 0) prefs.remove("theme");
  alarm_on = prefs.getBool("al_on", false);
  alarm_h = prefs.getUChar("al_h", 7);
  alarm_m = prefs.getUChar("al_m", 30);
  egg_set_s = constrain(prefs.getUShort("egg", 600), 0, 99 * 60);
  Radio_Begin(bus, amp);
  Radio_SetVolume(prefs.getUChar("volume", 30));
  int st = prefs.getUChar("st2", 6);
  if (st < settings::stationCount()) {
    Radio_Play(st);  // selects it; stop right away, the radio starts silent
    Radio_Stop();
  }
  hints_seen = prefs.getUInt("hints2", 0);
  if (prefs.isKey("hints")) {  // before the Tools page the bits counted ring positions: carry them over by page
    static const Mode OLD[] = { Mode::Clock, Mode::Radio, Mode::Weather, Mode::Timer, Mode::Study, Mode::Hourglass,
                                Mode::Aquarium, Mode::Snow, Mode::Fire, Mode::Sea, Mode::Wave, Mode::Lava, Mode::Settings };
    uint16_t old = prefs.getUShort("hints", 0);
    for (int i = 0; i < 13; i++) {
      if (old & (1 << i)) hints_seen |= 1u << (int)OLD[i];
    }
    prefs.putUInt("hints2", hints_seen);
    prefs.remove("hints");
  }
  Weather_Begin();
  UpdateCheck_Begin([]() { return Radio_On(); });  // check only; waits while the radio plays
  Motion_Begin(bus);
}

// Pages the auto page setting turns through: the ones that only show something.
static bool ambient(Mode m)
{
  return m == Mode::Clock || m == Mode::Weather || m == Mode::Aquarium || m == Mode::Snow || m == Mode::Fire ||
         m == Mode::Sea || m == Mode::Wave || m == Mode::Lava;
}

static uint32_t last_touch_ms = 0, last_cycle_ms = 0;

static void autoPage()
{
  int minutes = SettingsUi_CycleMin();
  if (!minutes || ringing || !ambient(state.mode)) return;
  uint32_t per = minutes * 60000UL;
  if (millis() - last_touch_ms < per || millis() - last_cycle_ms < per) return;
  last_cycle_ms = millis();
  int idx = 0;
  for (int i = 0; i < PAGE_COUNT; i++) {
    if (PAGES[i].mode == state.mode) idx = i;
  }
  for (int k = 1; k < PAGE_COUNT; k++) {
    int j = (idx + k) % PAGE_COUNT;
    Mode m = PAGES[j].mode;
    if (ambient(m) && pageInRing(j)) {
      state.mode = m;
      state.dirty = true;
      return;
    }
  }
}

static void hintTick()
{
  static Mode last = Mode::Info;  // not a ring page: the first tick counts as entering the current one
  if (state.mode != last) {
    last = state.mode;
    banner_until_ms = 0;
    uint32_t bit = 1u << (int)state.mode;
    if (!(hints_seen & bit)) {
      for (const Hint &h : HINTS) {
        if (h.mode != state.mode) continue;
        hints_seen |= bit;
        prefs.putUInt("hints2", hints_seen);
        showBanner(h.en, HINT_MS);
      }
    }
    state.dirty = true;
  }
  if (banner_until_ms && !bannerOn()) {
    banner_until_ms = 0;
    state.dirty = true;
  }
  if (!banner_until_ms) banner_prompt = false;
}

// Night dim and the low-battery question. Both use the board's temporary dimming, so the saved brightness is never
// touched, and a manual brightness change (which ends the dimming) wins until the next night window / the next charge.
#define LOW_BATT_PCT 20
#define LOW_BATT_DIM_PCT 20
#define LOW_BATT_PROMPT_MS 8000
static bool night_in = false;     // inside the night window (edge detection: dim once per window)
static bool lowbatt_armed = true; // ask again only after a charge
static bool lowbatt_dim = false;  // the user said yes: stay dim until charging

static void applyDim()
{
  board().setDim(lowbatt_dim ? LOW_BATT_DIM_PCT : night_in ? settings::NIGHT_DIM_PCT : -1);
}

static void dimTick(const DateTime &now)
{
  static int last_sec = -1;
  if (now.second == last_sec) return;
  last_sec = now.second;

  int a, b, m = now.hour * 60 + now.minute;
  bool in = settings::nightWindow(settings::nightMode(), a, b) && (a < b ? m >= a && m < b : m >= a || m < b);
  if (in != night_in) {
    night_in = in;
    if (in) {
      board().setDim(lowbatt_dim ? LOW_BATT_DIM_PCT : settings::NIGHT_DIM_PCT);
    } else if (board().dimLevel() >= 0) {  // still dim from the night (not a hand change): back to the saved level
      applyDim();
    }
  }

  if (state.charging) {
    lowbatt_armed = true;
    if (lowbatt_dim) {  // charged (or plugged in): the low-battery dimming ends
      lowbatt_dim = false;
      if (board().dimLevel() >= 0) applyDim();
    }
  } else if (lowbatt_armed && state.batt_pct >= 0 && state.batt_pct < LOW_BATT_PCT && state.batt_v >= 3.2f &&
             millis() > 30000 && !ringing && !bannerOn() && board().displayOn() != 0) {
    lowbatt_armed = false;  // one question per battery session
    banner_prompt = true;
    showBanner(N_("LOW BATTERY: DIM SCREEN? TAP = YES"), LOW_BATT_PROMPT_MS);
  }
}

void Gadget_Tick(const DateTime &now)
{
  settings::tick();
  hintTick();
  dimTick(now);
  SettingsUi_Tick();
  autoPage();
  int day = now.year * 400 + now.month * 32 + now.day;
  if (alarm_on && !ringing && now.hour == alarm_h && now.minute == alarm_m && fired_day != day) {
    fired_day = day;
    startAlarm();
  }
  if (ringing) {
    uint32_t t = millis() - ring_start_ms;
    if (t > ALARM_MAX_MS) {
      stopAlarm();
    } else if ((!radio_by_alarm || Radio_State() != RadioState::Playing) && t > ALARM_RADIO_GRACE_MS &&
               millis() - ring_beep_ms > 2000) {
      ring_beep_ms = millis();  // no stream: plain beeps
      Radio_Beep(1046, 140, 3, 90);
    }
  }
  for (int i = 0; i < PAGE_COUNT; i++) {  // the page on screen was just hidden (web settings): back to the clock
    if (PAGES[i].mode == state.mode && !pageInRing(i) && !ringing) {
      state.mode = Mode::Clock;
      state.dirty = true;
    }
  }
  timerTick();
  Apps_Tick();
  Study_Tick();
  Motion_Update();
}

bool Gadget_Draws(Mode m)
{
  return m == Mode::Clock || m == Mode::Radio || m == Mode::Weather || m == Mode::Aquarium || m == Mode::Snow || m == Mode::Timer ||
         m == Mode::Fire || m == Mode::Sea || m == Mode::Study || m == Mode::Wave || m == Mode::Lava ||
         m == Mode::Hourglass || m == Mode::Settings || m == Mode::Tools || m == Mode::Play || m == Mode::App;
}

void Gadget_Render(const DateTime &now)
{
  switch (state.mode) {
    case Mode::Clock: drawClock(now); break;
    case Mode::Radio: drawRadio(now); break;
    case Mode::Weather: drawWeather(); break;
    case Mode::Timer: drawTimer(); break;
    case Mode::Aquarium: {
      WeatherNow w = Weather_Get();
      String corner = hhmm(now);
      if (w.valid) corner += "  " + String(w.temp) + "°";
      Fishtank_Draw(corner.c_str());
      break;
    }
    case Mode::Snow: Snowglobe_Draw(hhmm(now).c_str()); break;
    case Mode::Fire: Fireplace_Draw(hhmm(now).c_str()); break;
    case Mode::Sea: Seaside_Draw(hhmm(now).c_str()); break;
    case Mode::Study: Study_Draw(hhmm(now).c_str()); break;
    case Mode::Wave: Wave_Draw(hhmm(now).c_str()); break;
    case Mode::Lava: Lava_Draw(""); break;
    case Mode::Hourglass: Hourglass_Draw(""); break;
    case Mode::Settings: SettingsUi_Draw(); break;
    case Mode::Tools:
    case Mode::Play:
      if (Apps_Count(state.mode) > 0) Apps_DrawMenu(state.mode);
      else state.mode = Mode::Clock, state.dirty = true;  // /page play while no app is there
      break;
    case Mode::App: Apps_Draw(now); break;
    default: break;
  }
  // volume bar over the other pages while it changes (the radio page has it in its title line)
  if (state.mode != Mode::Radio && volume_shown_ms && millis() - volume_shown_ms < VOLUME_SHOW_MS) {
    rect(140, 62, 360, 46, BG);
    frame(140, 62, 360, 46, DIM);
    drawVolumeBar(158, 78);
  }
  bool full = state.mode == Mode::App && Apps_FullScreen();  // the lamp is one color all over
  if (state.mode != Mode::Hourglass && !full) theme::decorate();
  if (bannerOn() && !ringing && !full) ClockFaces_Banner(banner);  // the hourglass stands alone on black, no machine border
  if (state.mode == Mode::Radio && radio_world) RadioFaces_Border();
}

static uint32_t frameMsBase(Mode m);
uint32_t Gadget_FrameMs(Mode m)
{
  uint32_t ms = frameMsBase(m);
  if (banner_until_ms && (!ms || ms > 100)) ms = 100;  // so the banner leaves on time
  return ms;
}

static uint32_t frameMsBase(Mode m)
{
  if (volume_shown_ms && millis() - volume_shown_ms < VOLUME_SHOW_MS + 100) return 100;
  switch (m) {
    case Mode::Clock: return theme::current() == theme::C64 ? 50 : ringing ? 150 : 500;
    case Mode::Radio: return 50;
    case Mode::Weather: return 60;
    case Mode::Aquarium: return 50;
    case Mode::Snow:
    case Mode::Fire:
    case Mode::Sea:
    case Mode::Study:
    case Mode::Wave:
    case Mode::Lava:
    case Mode::Hourglass: return 50;
    case Mode::Timer: return phase == Phase::Running || phase == Phase::Done ? 60 : 400;
    case Mode::Settings: return SettingsUi_Busy() ? 150 : 1000;
    case Mode::App: return Apps_FrameMs();
    default: return 0;
  }
}

static void goPage(int dir)
{
  int idx = 0;
  for (int i = 0; i < PAGE_COUNT; i++) {
    if (PAGES[i].mode == state.mode) idx = i;
  }
  do {
    idx = (idx + dir + PAGE_COUNT) % PAGE_COUNT;
  } while (!pageInRing(idx));
  Mode next = PAGES[idx].mode;
  state.mode = next;
  state.mode_until_ms = 0;
  state.dirty = true;
}

static void changeVolume(int delta)
{
  Radio_SetVolume(Radio_Volume() + delta);
  prefs.putUChar("volume", Radio_Volume());
  volume_shown_ms = millis();
  if (!Radio_On()) Radio_Beep(880, 60);  // something to hear the new level by
  state.dirty = true;
}

static void tapClock(int x, int y)
{
  if (x < 480) return;
  if (y >= 88 && y < 116) {
    alarm_on = !alarm_on;
  } else if (y >= 116 && x < 553) {
    alarm_h = (alarm_h + 1) % 24;
    alarm_on = true;
  } else if (y >= 116) {
    alarm_m = (alarm_m + 5) % 60;
    alarm_on = true;
  } else {
    return;
  }
  savePrefs();
  Radio_Beep(alarm_on ? 1046 : 523, 60);
}

static void tapRadio(int x, int y)
{
  if (y >= 130) {
    int n = settings::stationCount();
    int g = (x - 16) / 87;
    if (g >= settings::groupCount()) return;
    int cur = constrain(Radio_Station(), 0, n - 1);
    int pick = -1;
    if (settings::station(cur).group == g && Radio_On()) {  // same genre again: next station in it
      for (int i = 1; i <= n && pick < 0; i++) {
        int k = (cur + i) % n;
        if (settings::station(k).group == g) pick = k;
      }
    } else {
      for (int k = 0; k < n && pick < 0; k++) {
        if (settings::station(k).group == g) pick = k;
      }
    }
    if (pick >= 0) Radio_Play(pick);
  } else {
    Radio_Toggle();
  }
  prefs.putUChar("st2", Radio_Station());
}

static void tapTimer(int x, int y)
{
  if (y >= 130) {
    int i = constrain((x - 16) / 122, 0, 4);
    if (i == 4) {
      eggSet(0);
    } else {
      eggAddMinutes(COUNT_STEPS[i]);
    }
    Radio_Beep(1046, 30);
  } else {
    timerStartPause();
  }
}

bool Gadget_TouchDown(int x, int y)
{
  if (state.mode != Mode::App || ringing || phase == Phase::Done || (banner_prompt && bannerOn())) return false;
  if (!Apps_TouchDown(x, y)) return false;
  last_touch_ms = millis();
  state.dirty = true;
  return true;
}

void Gadget_Gesture(Gesture g, int x, int y)
{
  last_touch_ms = millis();
  if (banner_prompt && bannerOn() && g == Gesture::Tap) {  // yes to "dim the screen?"
    banner_prompt = false;
    banner_until_ms = 0;
    lowbatt_dim = true;
    applyDim();
    state.dirty = true;
    return;
  }
  if (ringing) {
    stopAlarm();
    return;
  }
  if (phase == Phase::Done) {  // any touch silences the egg timer
    phase = Phase::Idle;
    state.dirty = true;
    return;
  }
  if (state.mode == Mode::App && Apps_Gesture(g, x, y)) {  // an open app owns taps, holds and its own swipes
    state.dirty = true;
    return;
  }
  switch (g) {
    case Gesture::SwipeLeft: goPage(1); return;
    case Gesture::SwipeRight: goPage(-1); return;
    case Gesture::SwipeUp:  // on the settings page: walk the list (finger up = further down it)
      if (state.mode == Mode::Settings) {
        SettingsUi_Step(1);
        break;
      }
      changeVolume(Radio_Volume() < 20 ? 5 : 10);
      return;
    case Gesture::SwipeDown:
      if (state.mode == Mode::Settings) {
        SettingsUi_Step(-1);
        break;
      }
      changeVolume(Radio_Volume() <= 20 ? -5 : -10);
      return;
    case Gesture::LongPress:
      if (state.mode == Mode::Clock) {
        alarm_on = !alarm_on;
        savePrefs();
        Radio_Beep(alarm_on ? 1046 : 523, 60);
      } else if (state.mode == Mode::Timer) {
        timerReset();
      } else if (state.mode == Mode::Study) {
        Study_Reset();
      } else if (state.mode == Mode::Hourglass) {
        Hourglass_Reset();
      } else if (state.mode == Mode::Snow) {
        Snowglobe_Hold();
      } else if (state.mode == Mode::Aquarium) {
        Fishtank_Hold();
      } else if (state.mode == Mode::Fire) {
        Fireplace_Hold();
      } else if (state.mode == Mode::Sea) {
        Seaside_Hold();
      } else if (state.mode == Mode::Wave) {
        Wave_Hold();
      } else if (state.mode == Mode::Radio) {
        RadioFace_Hold(x, y);
      } else if (state.mode == Mode::Settings) {
        SettingsUi_Hold();
      }
      break;
    case Gesture::Tap:
      switch (state.mode) {
        case Mode::Clock: tapClock(x, y); break;
        case Mode::Radio: tapRadio(x, y); break;
        case Mode::Weather:  // the next days for 12 s; a second tap goes back
          forecast_until = forecast_until && (int32_t)(millis() - forecast_until) < 0 ? 0 : millis() + 12000;
          break;
        case Mode::Aquarium: Fishtank_Feed(x); break;
        case Mode::Snow: Snowglobe_Tap(x, y); break;
        case Mode::Fire: Fireplace_Tap(x, y); break;
        case Mode::Sea: Seaside_Tap(x, y); break;
        case Mode::Study: Study_Tap(); break;
        case Mode::Wave: Wave_Tap(x); break;
        case Mode::Lava: Lava_Tap(); break;
        case Mode::Hourglass: Hourglass_Tap(); break;
        case Mode::Timer: tapTimer(x, y); break;
        case Mode::Settings: SettingsUi_Tap(x, y); break;
        case Mode::Tools:
        case Mode::Play: Apps_TapMenu(state.mode, x, y); break;
        default:
          if (!Gadget_Draws(state.mode)) {  // message, info, image: back home
            state.mode = Mode::Clock;
          }
          break;
      }
      break;
  }
  state.dirty = true;
}

String Gadget_Command(const String &name, const String &arg)
{
  if (name == "hints") {  // "reset": every first-visit hint shows again
    if (arg == "reset") {
      hints_seen = 0;
      prefs.putUInt("hints2", 0);
    }
    return "{\"hints_seen\":" + String(hints_seen) + "}";
  }
  if (name == "touch") {  // "tap,x,y" or "hold,x,y" in screen pixels: a gesture without a finger (screenshots, tests)
    int c1 = arg.indexOf(','), c2 = arg.indexOf(',', c1 + 1);
    if (c1 < 0 || c2 < 0) return "{\"error\":\"tap|hold|left|right|up|down,x,y\"}";
    String kind = arg.substring(0, c1);
    if (kind == "press" || kind == "release") {  // bare finger edges (the runner jumps on these)
      Apps_Touch(kind == "press", arg.substring(c1 + 1, c2).toInt(), arg.substring(c2 + 1).toInt());
      return "{\"ok\":true}";
    }
    Gesture g = kind == "hold" ? Gesture::LongPress : kind == "left" ? Gesture::SwipeLeft : kind == "right" ? Gesture::SwipeRight
              : kind == "up" ? Gesture::SwipeUp : kind == "down" ? Gesture::SwipeDown : Gesture::Tap;
    Gadget_Gesture(g, arg.substring(c1 + 1, c2).toInt(), arg.substring(c2 + 1).toInt());
    return "{\"ok\":true}";
  }
  if (name == "app") {  // open an app by key (level | coin | breathe | score | lamp), "close" or empty = back to its menu
    if (arg == "close" || !arg.length()) Apps_Close();
    else if (!Apps_Open(arg.c_str())) return "{\"error\":\"unknown app\"}";
    return "{\"ok\":true}";
  }
  if (name == "radiotest") {  // settings page: play a stream before adding it
    if (arg.startsWith("http")) Radio_PlayUrl(arg.c_str());
    else Radio_Stop();
    state.dirty = true;
    return "ok";
  }
  if (name == "radio") {
    if (arg == "stop") {
      Radio_Stop();
    } else if (arg == "toggle") {
      Radio_Toggle();
    } else if (arg == "next") {
      Radio_Play((Radio_Station() + 1) % settings::stationCount());
    } else if (arg.length()) {
      Radio_Play(constrain(arg.toInt(), 0, settings::stationCount() - 1));
    }
    prefs.putUChar("st2", Radio_Station());
  } else if (name == "vol") {
    if (arg.length()) {
      Radio_SetVolume(arg.toInt());
      prefs.putUChar("volume", Radio_Volume());
      volume_shown_ms = millis();
    }
  } else if (name == "alarm") {
    if (arg == "on" || arg == "off") {
      alarm_on = arg == "on";
    } else if (arg.indexOf(':') > 0) {
      alarm_h = constrain(arg.toInt(), 0, 23);
      alarm_m = constrain(arg.substring(arg.indexOf(':') + 1).toInt(), 0, 59);
      alarm_on = true;
    } else if (arg == "ring") {
      startAlarm();
    } else if (arg == "stop") {
      stopAlarm();
    }
    savePrefs();
  } else if (name == "timer") {
    if (arg == "start") {
      timerStartPause();
    } else if (arg == "reset") {
      timerReset();
    } else if (arg.startsWith("+") || arg.startsWith("-")) {
      eggAddMinutes(arg.toInt());  // minutes
    } else if (arg.length()) {
      eggSet(arg.toInt());  // seconds
    }
  } else if (name == "theme") {
    // the whole look: vfd | c64 | amiga | atari (or 0..3), empty = next; "natural" / "themed" = scene colors
    if (arg == "natural" || arg == "themed") {
      theme::setNaturalScenes(arg == "natural");
    } else {
      theme::Id t = arg.length() ? theme::fromName(arg) : (theme::Id)((theme::current() + 1) % theme::COUNT);
      if (t < theme::COUNT) theme::set(t);
    }
  } else if (name == "menu") {
    // settings page at item N (help page after a dot: "8.3"), for screenshots
    SettingsUi_Select(arg.toInt(), arg.indexOf('.') > 0 ? arg.substring(arg.indexOf('.') + 1).toInt() : 0);
    state.mode = Mode::Settings;
  } else if (name == "snow") {
    // snow page: "globe" toggles the snow globe; "auto" | "on" | "off" = holiday lights; "sleigh" | "star" = now
    if (arg == "globe") Snowglobe_Hold();
    else if (arg == "sleigh" || arg == "star" || arg == "deer") Snowglobe_Show(arg.c_str());
    else if (arg == "auto" || arg == "on" || arg == "off") SettingsUi_SetHolidays(arg == "auto" ? 0 : arg == "on" ? 1 : 2);
    state.mode = Mode::Snow;
  } else if (name == "radioface") {
    // radio page looks: "eq" | "comp" = next equalizer / companion, "eq<n>" | "comp<n>" = that one
    RadioFace_Command(arg);
    state.mode = Mode::Radio;
  } else if (name == "sky") {
    // preview: Sky index (1 clear .. 9 storm), "n" after it for night; empty or -1 = the real sky
    sky_force = arg.length() ? constrain(arg.toInt(), -1, 9) : -1;
    sky_night = arg.endsWith("n");
  } else if (name != "state") {
    return String();
  }
  state.dirty = true;
  static const char *const RS[] = { "off", "connecting", "playing", "failed" };
  String title = Radio_Title();
  title.replace("\\", "");
  title.replace("\"", "'");
  float ax, ay, az;
  Motion_Raw(ax, ay, az);
  char json[720];
  snprintf(json, sizeof(json),
           "{\"theme\":\"%s\",\"natural_scenes\":%s,\"radio\":\"%s\",\"station\":%d,\"station_name\":\"%s\",\"title\":\"%s\",\"volume\":%d,"
           "\"alarm\":\"%02d:%02d\",\"alarm_on\":%s,\"ringing\":%s,\"timer\":\"%s\",\"timer_state\":%d,\"accel\":[%.2f,%.2f,%.2f],%s}",
           theme::def().key, theme::naturalScenes() ? "true" : "false", RS[(int)Radio_State()], Radio_Station(), settings::station(Radio_Station()).name.c_str(), title.c_str(), Radio_Volume(), alarm_h,
           alarm_m, alarm_on ? "true" : "false", ringing ? "true" : "false", mss((eggLeftMs() + 999) / 1000).c_str(), (int)phase, ax, ay, az, RadioFace_Status().c_str());
  return json;
}

// The i-th listed page (see Gadget.h) as an index into PAGES, -1 past the end.
static int listedPage(int i)
{
  for (int k = 0; k < PAGE_COUNT; k++) {
    if (pageListed(PAGES[k]) && i-- == 0) return k;
  }
  return -1;
}

int Gadget_PageCount()
{
  int n = 0;
  for (int k = 0; k < PAGE_COUNT; k++) n += pageListed(PAGES[k]);
  return n;
}

const char *Gadget_PageName(int i)
{
  int k = listedPage(i);
  return k < 0 ? "" : T(PAGES[k].en);
}

bool Gadget_PageCanHide(int i)
{
  int k = listedPage(i);
  return k >= 0 && PAGES[k].can_hide;
}

bool Gadget_PageShown(int i)
{
  int k = listedPage(i);
  return k >= 0 && !(PAGES[k].can_hide && settings::pageHidden(PAGES[k].key));
}

void Gadget_PageSetShown(int i, bool shown)
{
  int k = listedPage(i);
  if (k < 0 || !PAGES[k].can_hide) return;
  String list;
  for (int j = 0; j < PAGE_COUNT; j++) {
    bool hidden = PAGES[j].can_hide && settings::pageHidden(PAGES[j].key);
    if (j == k) hidden = !shown;
    if (!hidden) continue;
    if (list.length()) list += ",";
    list += PAGES[j].key;
  }
  String err;
  if (!settings::apply("hide=" + list + "\n", err)) Serial.printf("settings: %s\n", err.c_str());
  state.dirty = true;
}

bool Gadget_ButtonPressed()
{
  if (phase == Phase::Done) {
    phase = Phase::Idle;
    state.dirty = true;
    return true;
  }
  if (!ringing) return false;
  stopAlarm();
  return true;
}

#endif
