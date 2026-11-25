#ifdef BOARD_TOUCH349

#include "SettingsUi.h"
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <ricmoo_qrcode.h>
#include "ClockFaces.h"
#include "../../../core/net.h"
#include "../../../core/Board.h"
#include "../../../core/state.h"
#include "../../../core/UpdateCheck.h"
#include "Gadget.h"
#include "Settings.h"
#include "Theme.h"
#include "Vfd.h"

using namespace vfd;

// ---------------------------------------------------------------------------------------------------------------------
// Items and texts
// ---------------------------------------------------------------------------------------------------------------------

enum Item : uint8_t { THEME, CLOCK, UNITS, LANG, BRIGHT, CITY, CYCLE, HOLIDAYS, NIGHT, PAGES, HELP, WIFI, ABOUT, ITEM_COUNT };

struct ItemText {
  const char *en;       // in the list
  const char *what_en;  // above the value
};

static const ItemText ITEMS[ITEM_COUNT] = {
  { N_("THEME"), N_("LOOK OF EVERY PAGE") },
  { N_("CLOCK"), N_("CLOCK STYLE") },
  { N_("UNITS"), N_("TEMPERATURE") },
  { N_("LANGUAGE"), N_("TEXT ON SCREEN") },
  { N_("BRIGHTNESS"), N_("SCREEN LIGHT") },
  { N_("CITY"), N_("WEATHER AND TIME ZONE") },
  { N_("AUTO PAGE"), N_("AMBIENT PAGES TURN BY THEMSELVES") },
  { N_("HOLIDAYS"), N_("WINTER LIGHTS ON THE SNOW PAGE") },
  { N_("NIGHT DIM"), N_("SCREEN DIMS AT NIGHT") },
  { N_("PAGES"), N_("TAP THE NAME: SHOW OR HIDE") },
  { N_("HELP"), nullptr },
  { N_("WI-FI"), nullptr },
  { N_("ABOUT"), nullptr },
};

// The tutorial, one screen per page of it. Lines fit the panel (33 glyphs at size 2).
struct HelpPage {
  const char *title;
  const char *en[4];
};

static const HelpPage HELP_PAGES[] = {
  { N_("GESTURES"),
    { N_("SWIPE LEFT/RIGHT: NEXT PAGE"), N_("SWIPE UP/DOWN: VOLUME"), N_("TAP OR HOLD THINGS ON SCREEN"), N_("IN AN APP: SWIPE SIDEWAYS = BACK") } },
  { N_("CLOCK"),
    { N_("TAP THE BELL: ALARM ON/OFF"), N_("TAP ALARM HOURS: +1 HOUR"), N_("TAP ALARM MINUTES: +5 MIN"), N_("HOLD: ALARM ON/OFF") } },
  { N_("RADIO"),
    { N_("TAP: PLAY / STOP"), N_("GENRE KEY: PLAY, AGAIN: NEXT"), N_("HOLD THE EQUALIZER: NEW LOOK"), N_("HOLD THE DANCERS: CHANGE") } },
  { N_("TIMERS"),
    { N_("TAP: START / PAUSE"), N_("HOLD: RESET"), N_("TIMER KEYS: ADD MINUTES"), N_("HOURGLASS: TILT TO POUR") } },
  { N_("SCENES"),
    { N_("FISH TANK: TAP TO FEED"), N_("FIREPLACE: TAP, CAT WAKES"), N_("LIGHTHOUSE: TAP, BIG WAVE"), N_("LAVA LAMP: TAP, NEW COLORS") } },
  { N_("MORE SCENES"),
    { N_("SNOW: TAP OR SHAKE TO STIR"), N_("SNOW: HOLD FOR THE GLOBE"), N_("SHIP: TAP THE WATER"), N_("TILT: WATER AND SAND MOVE") } },
  { N_("TOOLS"),
    { N_("TAP A TILE: OPEN THE APP"), N_("SWIPE LEFT/RIGHT: BACK TO TILES"), N_("LEVEL: TAP SETS ZERO, HOLD CLEARS"), N_("COIN: TAP OR SHAKE TO FLIP") } },
  { N_("MORE TOOLS"),
    { N_("BREATHE: TAP START, HOLD BEEP"), N_("SCORE: TAP +1, HOLD -1"), N_("LAMP: TAP COLOR, UP/DOWN LIGHT"), N_("SETTINGS > PAGES: HIDE PAGES") } },
  { N_("PLAY"),
    { N_("KEEPER: TAP A LANE, SAVE SHIPS"), N_("TAP LIGHTS THAT LANE AT ONCE"), N_("HOLD: PAUSE. NO SWIPES IN PLAY"), N_("PAUSE/TITLE: SWIPE SIDEWAYS=BACK") } },
  { N_("MORE PLAY"),
    { N_("21: 10 HANDS, MOST WINS TAKES IT"), N_("KNOCK ONCE: STAND, TWICE: HIT"), N_("RUNNER: TOP JUMPS, BOTTOM DUCKS"), N_("SWIPE SIDEWAYS: BACK TO TILES") } },
  { N_("BUTTONS"),
    { N_("BOOT: BRIGHTNESS"), N_("HOLD BOOT 5 S: NEW WI-FI"), N_("PWR: SCREEN ON/OFF"), N_("WI-FI VIEW: HOLD = NEW WI-FI") } },
};
static constexpr int HELP_COUNT = sizeof(HELP_PAGES) / sizeof(HELP_PAGES[0]);

// Cities for the list, west to east, so stepping through them walks the time zones.
struct City {
  const char *name;
  float lat, lon;
};

static const City CITIES[] = {
  { "Honolulu", 21.31f, -157.86f },     { "Anchorage", 61.22f, -149.90f },   { "Vancouver", 49.28f, -123.12f },
  { "San Francisco", 37.77f, -122.42f }, { "Seattle", 47.61f, -122.33f },     { "Los Angeles", 34.05f, -118.24f },
  { "Phoenix", 33.45f, -112.07f },      { "Denver", 39.74f, -104.99f },      { "Mexico City", 19.43f, -99.13f },
  { "Dallas", 32.78f, -96.80f },        { "Houston", 29.76f, -95.37f },      { "Chicago", 41.88f, -87.63f },
  { "Miami", 25.76f, -80.19f },         { "Toronto", 43.65f, -79.38f },      { "Lima", -12.05f, -77.04f },
  { "Bogota", 4.71f, -74.07f },         { "New York", 40.71f, -74.01f },     { "Boston", 42.36f, -71.06f },
  { "Santiago", -33.45f, -70.67f },     { "Buenos Aires", -34.60f, -58.38f }, { "Sao Paulo", -23.55f, -46.63f },
  { "Rio de Janeiro", -22.91f, -43.17f }, { "Reykjavik", 64.15f, -21.94f },  { "Lisbon", 38.72f, -9.14f },
  { "Dublin", 53.35f, -6.26f },         { "Madrid", 40.42f, -3.70f },        { "London", 51.51f, -0.13f },
  { "Paris", 48.86f, 2.35f },           { "Lagos", 6.52f, 3.38f },           { "Brussels", 50.85f, 4.35f },
  { "Amsterdam", 52.37f, 4.90f },       { "Rome", 41.90f, 12.50f },          { "Berlin", 52.52f, 13.40f },
  { "Vienna", 48.21f, 16.37f },         { "Stockholm", 59.33f, 18.07f },     { "Warsaw", 52.23f, 21.01f },
  { "Athens", 37.98f, 23.73f },         { "Helsinki", 60.17f, 24.94f },      { "Johannesburg", -26.20f, 28.05f },
  { "Istanbul", 41.01f, 28.98f },       { "Kyiv", 50.45f, 30.52f },          { "Cairo", 30.04f, 31.24f },
  { "Ankara", 39.93f, 32.86f },         { "Nairobi", -1.29f, 36.82f },       { "Moscow", 55.76f, 37.62f },
  { "Riyadh", 24.71f, 46.68f },         { "Tehran", 35.69f, 51.39f },        { "Dubai", 25.20f, 55.27f },
  { "Karachi", 24.86f, 67.01f },        { "Mumbai", 19.08f, 72.88f },        { "Delhi", 28.61f, 77.21f },
  { "Bangalore", 12.97f, 77.59f },      { "Dhaka", 23.81f, 90.41f },         { "Bangkok", 13.76f, 100.50f },
  { "Singapore", 1.35f, 103.82f },      { "Jakarta", -6.21f, 106.85f },      { "Hong Kong", 22.32f, 114.17f },
  { "Perth", -31.95f, 115.86f },        { "Beijing", 39.90f, 116.41f },      { "Manila", 14.60f, 120.98f },
  { "Shanghai", 31.23f, 121.47f },      { "Taipei", 25.03f, 121.57f },       { "Seoul", 37.57f, 126.98f },
  { "Tokyo", 35.68f, 139.69f },         { "Melbourne", -37.81f, 144.96f },   { "Sydney", -33.87f, 151.21f },
  { "Auckland", -36.85f, 174.76f },
};
static constexpr int CITY_COUNT = sizeof(CITIES) / sizeof(CITIES[0]);

static const uint8_t CYCLES[] = { 0, 1, 5, 15 };  // auto page, minutes

// ---------------------------------------------------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------------------------------------------------

static Preferences prefs;
static bool prefs_open = false;
static int sel = 0;       // selected item
static int help_page = 0;
static int page_sel = 1;  // PAGES item: the page the arrows picked (index in Gadget_Page*, 0 = the clock)
static int cycle_min = -1;  // -1 = not read yet
static int xmas = -1;       // holidays: 0 = by the date, 1 = always, 2 = never; -1 = not read yet

static int city_idx = -1;          // city picked from the list, -1 = the saved one
static uint32_t wifi_confirm_until = 0;  // "hold again to forget Wi-Fi" is waiting for the second hold until then
static uint32_t city_save_at = 0;  // when to save it (stepping stopped), 0 = nothing pending

enum class Look : uint8_t { Idle, Running, Found, Failed };
static volatile Look look = Look::Idle;
static uint32_t look_done_ms = 0;
static bool look_saved = false;
static String look_city;
static float look_lat = 0, look_lon = 0;
static constexpr uint32_t NOTE_MS = 4000;  // "found" / "not found" stays this long

static void openPrefs()
{
  if (prefs_open) return;
  prefs.begin("ui", false);
  prefs_open = true;
}

int SettingsUi_CycleMin()
{
  if (cycle_min < 0) {
    openPrefs();
    cycle_min = prefs.getUChar("cycle", 0);
  }
  return cycle_min;
}

static int xmasMode()
{
  if (xmas < 0) {
    openPrefs();
    xmas = prefs.getUChar("xmas", 0);
  }
  return xmas;
}

void SettingsUi_SetHolidays(int mode)
{
  xmas = constrain(mode, 0, 2);
  openPrefs();
  prefs.putUChar("xmas", xmas);
}

bool SettingsUi_Holidays()
{
  int m = xmasMode();
  if (m) return m == 1;
  time_t t = time(nullptr);
  if (t < 1700000000) return false;  // clock not set yet
  struct tm tm;
  localtime_r(&t, &tm);
  return tm.tm_mon == 11 || (tm.tm_mon == 0 && tm.tm_mday <= 6);  // Dec 1 - Jan 6
}

static void apply(const String &lines)
{
  String err;
  if (!settings::apply(lines, err)) Serial.printf("settings: %s\n", err.c_str());
}

// ---------------------------------------------------------------------------------------------------------------------
// City: from the list, or found by the IP address (ip-api.com, plain HTTP, no key)
// ---------------------------------------------------------------------------------------------------------------------

static bool jsonText(const String &b, const char *key, String &out)
{
  String k = String("\"") + key + "\":\"";
  int i = b.indexOf(k);
  if (i < 0) return false;
  i += k.length();
  int j = b.indexOf('"', i);
  if (j < 0) return false;
  out = b.substring(i, j);
  return out.length() > 0;
}

static bool jsonNumber(const String &b, const char *key, float &out)
{
  String k = String("\"") + key + "\":";
  int i = b.indexOf(k);
  if (i < 0) return false;
  out = b.substring(i + k.length()).toFloat();
  return true;
}

static void lookupTask(void *)
{
  bool ok = false;
  HTTPClient http;
  http.setTimeout(6000);
  if (http.begin("http://ip-api.com/json/?fields=status,city,lat,lon")) {
    if (http.GET() == 200) {
      String b = http.getString();
      ok = b.indexOf("\"success\"") >= 0 && jsonText(b, "city", look_city) && jsonNumber(b, "lat", look_lat) &&
           jsonNumber(b, "lon", look_lon);
    }
    http.end();
  }
  look_saved = false;
  look_done_ms = millis();
  look = ok ? Look::Found : Look::Failed;
  state.dirty = true;
  vTaskDelete(nullptr);
}

static void startLookup()
{
  if (look == Look::Running) return;
  if (WiFi.status() != WL_CONNECTED) {
    look = Look::Failed;
    look_done_ms = millis();
    return;
  }
  city_save_at = 0;
  city_idx = -1;
  look = Look::Running;
  if (xTaskCreatePinnedToCore(lookupTask, "iplookup", 6144, nullptr, 1, nullptr, 0) != pdPASS) look = Look::Failed;
}

static int nearestCity()
{
  String cur = settings::city();
  for (int i = 0; i < CITY_COUNT; i++) {
    if (cur.equalsIgnoreCase(CITIES[i].name)) return i;
  }
  float lon = settings::lon(), lat = settings::lat(), best = 1e9;
  int bi = 0;
  for (int i = 0; i < CITY_COUNT; i++) {
    float d = fabsf(CITIES[i].lon - lon) + 0.5f * fabsf(CITIES[i].lat - lat);
    if (d < best) best = d, bi = i;
  }
  return bi;
}

static String cityLine(const char *name, float lat, float lon)
{
  return String("city=") + name + "\nlat=" + String(lat, 4) + "\nlon=" + String(lon, 4) + "\n";
}

void SettingsUi_Tick()
{
  if (city_save_at && (int32_t)(millis() - city_save_at) >= 0) {
    city_save_at = 0;
    const City &c = CITIES[city_idx];
    apply(cityLine(c.name, c.lat, c.lon));
    city_idx = -1;
  }
  if (look == Look::Found && !look_saved) {
    look_saved = true;
    apply(cityLine(look_city.c_str(), look_lat, look_lon));
  }
}

static bool wifiConfirming() { return wifi_confirm_until && (int32_t)(wifi_confirm_until - millis()) > 0; }

bool SettingsUi_Busy()
{
  return wifiConfirming() || look == Look::Running || ((look == Look::Found || look == Look::Failed) && millis() - look_done_ms < NOTE_MS + 200);
}

// ---------------------------------------------------------------------------------------------------------------------
// Changing values
// ---------------------------------------------------------------------------------------------------------------------

static void change(int dir)
{
  switch (sel) {
    case THEME: theme::set((theme::Id)((theme::current() + dir + theme::COUNT) % theme::COUNT)); break;
    case CLOCK: apply(settings::h24() ? "h24=0\n" : "h24=1\n"); break;
    case UNITS: apply(settings::fahrenheit() ? "unit=c\n" : "unit=f\n"); break;
    case LANG: {
      const int n = (int)lang::Id::COUNT;
      apply((String("lang=") + lang::data((lang::Id)(((int)settings::language() + dir + n) % n)).code + "\n").c_str());
      break;
    }
    case BRIGHT: {
      int b = board().brightness();
      if (b < 0) break;
      int to = dir > 0 ? (b / 10 + 1) * 10 : ((b + 9) / 10 - 1) * 10;
      board().setBrightness(constrain(to, 5, 100), false);  // the panel shows the level; no overlay
      break;
    }
    case CITY:
      if (city_idx < 0) city_idx = nearestCity();
      city_idx = (city_idx + dir + CITY_COUNT) % CITY_COUNT;
      city_save_at = millis() + 1500;  // save (and fetch the weather) once the stepping stops
      look = Look::Idle;
      break;
    case CYCLE: {
      int n = sizeof(CYCLES), i = 0;
      while (i < n && CYCLES[i] != SettingsUi_CycleMin()) i++;
      cycle_min = CYCLES[(i + dir + n) % n];
      openPrefs();
      prefs.putUChar("cycle", cycle_min);
      break;
    }
    case HOLIDAYS: SettingsUi_SetHolidays((xmasMode() + dir + 3) % 3); break;
    case NIGHT: apply((String("night=") + (settings::nightMode() + dir + settings::NIGHT_MODES) % settings::NIGHT_MODES + "\n").c_str()); break;
    case PAGES: page_sel = (page_sel + dir + Gadget_PageCount()) % Gadget_PageCount(); break;
    case HELP: help_page = (help_page + dir + HELP_COUNT) % HELP_COUNT; break;
    default: return;
  }
}

// ---------------------------------------------------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------------------------------------------------

static constexpr int LIST_X = 16, ROW_Y = 19, ROW_H = 30, ROWS = 5;
static constexpr int DIV_X = 206;               // line between list and panel
static constexpr int PX0 = 226, PX1 = 626;      // the panel
static constexpr int ZONE_L = 300, ZONE_R = 552;  // tap zones: back left of this, next right of that
static constexpr int VALUE_Y = 58;

static const char *const ARROW[] = { "...#", "..##", ".###", "####", ".###", "..##", "...#" };

static int listTop() { return constrain(sel - ROWS / 2, 0, ITEM_COUNT - ROWS); }

static void drawList()
{
  int top = listTop();
  for (int r = 0; r < ROWS; r++) {
    int i = top + r, y = ROW_Y + r * ROW_H;
    const char *name = T(ITEMS[i].en);
    if (i == sel) {
      sprite(ARROW, 7, LIST_X, y, 2, HOT, true);
      text(name, LIST_X + 18, y, 2, LIT_INK);
    } else {
      text(name, LIST_X + 18, y, 2, DIM_INK);
    }
  }
  // where the window is in the list
  rect(DIV_X, 12, 1, H - 24, GHOST);
  int th = (H - 24) * ROWS / ITEM_COUNT, ty = 12 + (H - 24) * top / ITEM_COUNT;
  rect(DIV_X - 1, ty, 3, th, DIM);
}

// A line of text centered in the panel at the biggest size that fits between the arrows.
static void centered(const char *s, int y, int max_p, const Ink &ink)
{
  int room = ZONE_R - ZONE_L + 40;
  int p = max_p;
  while (p > 2 && textWidth(s, p) > room) p--;
  int w = textWidth(s, p);
  text(s, (PX0 + PX1 - w) / 2, y + (4 - p) * 4, p, ink);
}

// A title and up to four lines; the first line bright when it is the answer (network name, version).
static void drawLines(const char *title, const char *const *lines, int n, bool first_lit, const char *corner = nullptr)
{
  text(title, PX0, 14, 2, HOT_INK);
  if (corner) text(corner, PX1 - textWidth(corner, 2), 14, 2, DIM_INK);
  for (int i = 0; i < n; i++) {
    if (lines[i] && *lines[i]) text(lines[i], PX0, 46 + i * 28, 2, i == 0 && first_lit ? LIT_INK : DIM_INK);
  }
}

static void drawValue()
{
  const ItemText &it = ITEMS[sel];
  text(T(it.what_en), PX0, 14, 2, DIM_INK);
  sprite(ARROW, 7, PX0, VALUE_Y + 3, 3, HOT);
  sprite(ARROW, 7, PX1 - 12, VALUE_Y + 3, 3, HOT, true);

  String value, note;
  bool note_hot = false;
  switch (sel) {
    case THEME: value = theme::def().name; break;
    case CLOCK: value = settings::h24() ? T("24 HOUR") : T("12 HOUR"); break;
    case UNITS:
      value = settings::fahrenheit() ? "°F" : "°C";
      note = settings::fahrenheit() ? "FAHRENHEIT" : T("CELSIUS");
      break;
    case LANG: value = Lang().name; break;
    case BRIGHT: {
      int b = board().brightness();
      value = b < 0 ? String("-") : String(b) + "%";
      break;
    }
    case CITY:
      value = city_idx >= 0 ? CITIES[city_idx].name : settings::city().c_str();
      if (look == Look::Running) note = T("LOOKING UP...");
      else if (look == Look::Found && millis() - look_done_ms < NOTE_MS) note = T("FOUND BY YOUR IP");
      else if (look == Look::Failed && millis() - look_done_ms < NOTE_MS) note = T("NOT FOUND, TRY THE LIST");
      else note = T("TAP THE NAME: FIND ME BY IP");
      break;
    case CYCLE: {
      int m = SettingsUi_CycleMin();
      value = m ? String(m) + T(" MIN") : String(T("OFF"));
      break;
    }
    case PAGES: {
      page_sel = constrain(page_sel, 0, Gadget_PageCount() - 1);
      value = Gadget_PageName(page_sel);
      bool shown = Gadget_PageShown(page_sel);
      note_hot = !shown;
      note = !Gadget_PageCanHide(page_sel) ? T("ALWAYS SHOWN") : shown ? T("SHOWN") : T("HIDDEN");
      break;
    }
    case HOLIDAYS: {
      static const char *const EN[] = { N_("BY DATE"), N_("ALWAYS"), N_("OFF") };
      value = T(EN[xmasMode()]);
      if (xmasMode() == 0) note = T("DEC 1 - JAN 6");
      break;
    }
    case NIGHT: {
      int a, b;
      if (!settings::nightWindow(settings::nightMode(), a, b)) {
        value = T("OFF");
      } else {
        char w[16];
        snprintf(w, sizeof(w), "%02d:%02d-%02d:%02d", a / 60, a % 60, b / 60, b % 60);
        value = w;
        char n[32];
        snprintf(n, sizeof(n), T("SCREEN TO %d%%"), settings::NIGHT_DIM_PCT);
        note = n;
      }
      break;
    }
    default: break;
  }
  centered(value.c_str(), VALUE_Y, 4, LIT_INK);

  if (sel == BRIGHT && board().brightness() >= 0) {
    int b = board().brightness(), x0 = (PX0 + PX1) / 2 - 119;
    for (int i = 0; i < 20; i++) {
      bool on = b >= (i + 1) * 5;
      rect(x0 + i * 12, 120, 8, 14, on ? LIT : GHOST);
    }
  } else if (note.length()) {
    int w = textWidth(note.c_str(), 2);
    text(note.c_str(), (PX0 + PX1 - w) / 2, 124, 2, look == Look::Running || note_hot ? HOT_INK : sel == PAGES ? LIT_INK : DIM_INK);
  }
}

// A QR code of the text at the right end of the panel: dark modules on a light square even in the dark themes, 5 px per
// module (a phone camera reads that off this panel from arm's length), with a quiet zone of the rest of the 172 px.
static void drawQr(const String &url)
{
  QRCode qr;
  static uint8_t modules[160];
  if (qrcode_getBufferSize(3) > sizeof(modules) || qrcode_initText(&qr, modules, 3, ECC_MEDIUM, url.c_str()) != 0) return;
  const int M = 5, SIDE = 172, X0 = PX1 - SIDE, off = (SIDE - qr.size * M) / 2;
  rect(X0, 0, SIDE, SIDE, 0xFFFF);
  for (int y = 0; y < qr.size; y++) {
    for (int x = 0; x < qr.size; x++) {
      if (qrcode_getModule(&qr, x, y)) rect(X0 + off + x * M, off + y * M, M, M, 0x0000);
    }
  }
}

void SettingsUi_Draw()
{
  clear();
  drawList();
  clip(PX0 - 4, 0, W, H);
  if (sel == HELP) {
    const HelpPage &h = HELP_PAGES[help_page];
    char corner[8];
    snprintf(corner, sizeof(corner), "%d/%d", help_page + 1, HELP_COUNT);
    const char *lines[4] = { T(h.en[0]), T(h.en[1]), T(h.en[2]), T(h.en[3]) };
    drawLines(T(h.title), lines, 4, false, corner);
  } else if (sel == WIFI) {
    String l[4];
    bool up = WiFi.status() == WL_CONNECTED;
    l[0] = up ? WiFi.SSID() : String(T("NOT CONNECTED"));
    if (up && l[0].length() > 17) l[0] = l[0].substring(0, 17);  // the QR code takes the right side
    l[1] = up ? WiFi.localIP().toString() : String();
    l[2] = up ? String(T("SIGNAL ")) + WiFi.RSSI() + " DBM" : String();
    l[3] = T("HOLD: NEW WI-FI");
    const char *p[4] = { l[0].c_str(), l[1].c_str(), l[2].c_str(), l[3].c_str() };
    drawLines(up ? T("SCAN FOR SETTINGS") : T("NETWORK"), p, 4, true);
    if (up) drawQr("http://" + l[1] + "/settings");  // the IP: Android often cannot resolve .local
  } else if (sel == ABOUT) {
    String host = state.name + ".local/settings";
    String news = String(T("NEW: ")) + UpdateCheck_Newer();
    const char *p[4] = { "PHOSPHOR " FW_VERSION, T("MORE ON YOUR PHONE:"), host.c_str(),
                         "github.com/melihkoray/phosphor" };
    if (*UpdateCheck_Newer()) {  // a newer release is out: say so, with where to get it (the device never installs it)
      p[1] = news.c_str();
      p[2] = UpdateCheck_Site();
      p[3] = host.c_str();
    }
    drawLines(T("ABOUT"), p, 4, true);
  } else {
    drawValue();
  }
  noClip();
  if (sel == WIFI && wifiConfirming()) {
    ClockFaces_Banner(T("HOLD AGAIN: FORGET WI-FI + SETUP"));
  }
}

// ---------------------------------------------------------------------------------------------------------------------
// Touch
// ---------------------------------------------------------------------------------------------------------------------

void SettingsUi_Step(int dir)
{
  sel = constrain(sel + dir, 0, ITEM_COUNT - 1);
}

// Same path as holding BOOT for 5 s: notice, then forget the network and open the setup portal.
void SettingsUi_Hold()
{
  if (sel != WIFI) return;
  if (!wifiConfirming()) {
    wifi_confirm_until = millis() + 5000;
    return;
  }
  wifi_confirm_until = 0;
  board().setDisplayOn(true);
  board().showNotice(T("Resetting Wi-Fi"), T("The setup network will open"));
  delay(1500);
  Net_ResetWifi();
}

void SettingsUi_Select(int item, int page)
{
  sel = constrain(item, 0, ITEM_COUNT - 1);
  help_page = constrain(page, 0, HELP_COUNT - 1);
}

void SettingsUi_Tap(int x, int y)
{
  if (x < DIV_X + 8) {
    int r = constrain((y - ROW_Y + ROW_H / 4) / ROW_H, 0, ROWS - 1);
    sel = listTop() + r;
    return;
  }
  if (sel == WIFI || sel == ABOUT) return;
  if (sel == HELP) {
    change(x < (PX0 + PX1) / 2 ? -1 : 1);
    return;
  }
  if (x < ZONE_L) change(-1);
  else if (x > ZONE_R) change(1);
  else if (sel == CITY) startLookup();
  else if (sel == PAGES) Gadget_PageSetShown(page_sel, !Gadget_PageShown(page_sel));
  else change(1);
}

#endif
