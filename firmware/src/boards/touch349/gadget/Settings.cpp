#ifdef BOARD_TOUCH349

#include "Settings.h"
#include <LittleFS.h>
#include <Preferences.h>
#include <esp_littlefs.h>
#include <esp_partition.h>
#include "Stations.h"
#include "../../../core/net.h"

namespace settings {

static const char *PATH = "/settings.txt";

struct Values {
  String city = "Istanbul";
  float lat = 41.01f, lon = 28.98f;
  lang::Id language = lang::Id::EN;
  bool h24 = true;
  bool fahr = false;
  int focus = 25, short_b = 5, long_b = 15;
  int night = 0;  // night dim window, index into NIGHTS (0 = off)
  String hide;    // comma list of hidden page keys
  RadioGroup groups[MAX_GROUPS];
  int n_groups = 0;
  RadioStation stations[MAX_STATIONS];
  int n_stations = 0;
};

static Values cur;
static bool fs_ok = false;
static uint32_t ver = 1;
static volatile int pending_off = 0x7fffffff;
static int applied_off = 0x7fffffff;

static void defaults(Values &v)
{
  v = Values();
  v.n_groups = DEFAULT_GROUP_COUNT;
  for (int i = 0; i < v.n_groups; i++) v.groups[i] = { DEFAULT_GROUPS[i].key, DEFAULT_GROUPS[i].name };
  v.n_stations = min(DEFAULT_STATION_COUNT, MAX_STATIONS);
  for (int i = 0; i < v.n_stations; i++)
    v.stations[i] = { DEFAULT_STATIONS[i].genre, DEFAULT_STATIONS[i].name, DEFAULT_STATIONS[i].url };
}

static String clean(String s, int max_len)
{
  s.replace("\r", "");
  s.replace("\n", " ");
  s.replace("|", "/");
  s.trim();
  if ((int)s.length() > max_len) s = s.substring(0, max_len);
  return s;
}

static String serialize(const Values &v)
{
  String out;
  out.reserve(256 + v.n_stations * 96);
  out += "city=" + v.city + "\n";
  out += "lat=" + String(v.lat, 4) + "\n";
  out += "lon=" + String(v.lon, 4) + "\n";
  out += String("lang=") + lang::data(v.language).code + "\n";
  out += String("h24=") + (v.h24 ? "1" : "0") + "\n";
  out += String("unit=") + (v.fahr ? "f" : "c") + "\n";
  out += "night=" + String(v.night) + "\n";
  out += "hide=" + v.hide + "\n";
  out += "focus=" + String(v.focus) + "\n";
  out += "short=" + String(v.short_b) + "\n";
  out += "long=" + String(v.long_b) + "\n";
  for (int i = 0; i < v.n_groups; i++) out += "group=" + v.groups[i].key + "|" + v.groups[i].name + "\n";
  for (int i = 0; i < v.n_stations; i++)
    out += "station=" + String(v.stations[i].group) + "|" + v.stations[i].name + "|" + v.stations[i].url + "\n";
  return out;
}

// Parses the file format into v. Unknown keys are skipped; groups and stations replace the lists when present.
static bool parse(const String &body, Values &v, String &err)
{
  bool any_group = false, any_station = false;
  int start = 0;
  while (start < (int)body.length()) {
    int end = body.indexOf('\n', start);
    if (end < 0) end = body.length();
    String line = body.substring(start, end);
    start = end + 1;
    line.replace("\r", "");
    int eq = line.indexOf('=');
    if (eq <= 0) continue;
    String k = line.substring(0, eq), val = line.substring(eq + 1);
    k.trim();
    if (k == "city") v.city = clean(val, 48);
    else if (k == "lat") v.lat = val.toFloat();
    else if (k == "lon") v.lon = val.toFloat();
    else if (k == "lang" && !lang::fromCode(val.c_str(), v.language)) v.language = lang::Id::EN;  // unknown: English, as before
    else if (k == "h24") v.h24 = val.toInt() != 0;
    else if (k == "unit") v.fahr = val.startsWith("f");
    else if (k == "night") v.night = constrain(val.toInt(), 0, NIGHT_MODES - 1);
    else if (k == "hide") {
      val.trim();
      val.toLowerCase();
      v.hide = clean(val, 120);
    }
    else if (k == "focus") v.focus = val.toInt();
    else if (k == "short") v.short_b = val.toInt();
    else if (k == "long") v.long_b = val.toInt();
    else if (k == "group") {
      if (!any_group) v.n_groups = 0, any_group = true;
      if (v.n_groups >= MAX_GROUPS) { err = "at most 7 radio groups"; return false; }
      int bar = val.indexOf('|');
      String key = clean(bar < 0 ? val : val.substring(0, bar), 6), name = clean(bar < 0 ? val : val.substring(bar + 1), 16);
      if (!key.length()) { err = "a radio group has no label"; return false; }
      v.groups[v.n_groups++] = { key, name.length() ? name : key };
    } else if (k == "station") {
      if (!any_station) v.n_stations = 0, any_station = true;
      if (v.n_stations >= MAX_STATIONS) { err = "at most 60 stations"; return false; }
      int b1 = val.indexOf('|'), b2 = b1 < 0 ? -1 : val.indexOf('|', b1 + 1);
      if (b2 < 0) { err = "station line needs group|name|url"; return false; }
      int g = val.substring(0, b1).toInt();
      String name = clean(val.substring(b1 + 1, b2), 16), url = clean(val.substring(b2 + 1), 240);
      if (!url.startsWith("http://") && !url.startsWith("https://")) { err = "station URL must start with http"; return false; }
      v.stations[v.n_stations++] = { (uint8_t)max(g, 0), name.length() ? name : String("STATION"), url };
    }
  }
  if (v.lat < -90 || v.lat > 90 || v.lon < -180 || v.lon > 180) { err = "latitude/longitude out of range"; return false; }
  v.focus = constrain(v.focus, 1, 180);
  v.short_b = constrain(v.short_b, 1, 60);
  v.long_b = constrain(v.long_b, 1, 120);
  if (v.n_groups < 1) { err = "need at least one radio group"; return false; }
  if (v.n_stations < 1) { err = "need at least one station"; return false; }
  for (int i = 0; i < v.n_stations; i++)
    if (v.stations[i].group >= v.n_groups) { err = "station '" + v.stations[i].name + "' points at a missing group"; return false; }
  return true;
}

static bool save(const Values &v)
{
  if (!fs_ok) return false;
  File f = LittleFS.open("/settings.tmp", "w");
  if (!f) return false;
  String s = serialize(v);
  bool ok = f.print(s) == s.length();
  f.close();
  if (!ok) return false;
  LittleFS.remove(PATH);
  return LittleFS.rename("/settings.tmp", PATH);
}

// v1.1 moved "ffat" (partitions.csv). The first boot on the new table reads settings.txt from where the old 9.9 MB
// LittleFS was (0x610000), read-only, before anything writes there; the installer leaves most of that flash as it was.
// Done once (NVS flag); a file the old table never had, or one the installer overwrote, just leaves the defaults.
static String rescueOldSettings()
{
  static const uint32_t OLD_ADDR = 0x610000, OLD_SIZE = 0x9E0000;
  const esp_partition_t *now = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "ffat");
  if (!now || now->address == OLD_ADDR) return "";  // still the old table: nothing moved yet
  Preferences p;
  p.begin("fsmove", false);
  bool done = p.getBool("done", false);
  p.putBool("done", true);
  p.end();
  if (done) return "";

  esp_partition_t old = {};
  old.flash_chip = now->flash_chip;
  old.type = ESP_PARTITION_TYPE_DATA;
  old.subtype = ESP_PARTITION_SUBTYPE_DATA_FAT;
  old.address = OLD_ADDR;
  old.size = OLD_SIZE;
  old.erase_size = now->erase_size;
  strcpy(old.label, "ffat_old");
  old.readonly = true;

  esp_vfs_littlefs_conf_t conf = {};
  conf.base_path = "/old";
  conf.partition = &old;
  conf.format_if_mount_failed = false;
  conf.read_only = true;
  String body;
  if (esp_vfs_littlefs_register(&conf) == ESP_OK) {
    FILE *f = fopen("/old/settings.txt", "r");
    if (f) {
      char buf[256];
      size_t n;
      while ((n = fread(buf, 1, sizeof(buf), f)) > 0) body.concat(buf, n);
      fclose(f);
    }
    esp_vfs_littlefs_unregister_partition(&old);
  }
  Serial.printf("settings: old file system %s\n", body.length() ? "found, carried over" : "not readable");
  return body;
}

void begin()
{
  defaults(cur);
  String rescued = rescueOldSettings();  // before the new "ffat" is mounted (and maybe formatted)
  fs_ok = LittleFS.begin(true, "/fs", 5, "ffat");
  if (fs_ok && rescued.length() && !LittleFS.exists(PATH)) {
    File f = LittleFS.open(PATH, "w");
    if (f) {
      f.print(rescued);
      f.close();
    }
  }
  if (!fs_ok) {
    Serial.println("settings: no file system, using defaults");
    return;
  }
  File f = LittleFS.open(PATH, "r");
  if (!f) return;
  String body = f.readString();
  f.close();
  Values v;
  defaults(v);
  String err;
  if (parse(body, v, err)) cur = v;
  else Serial.printf("settings: bad file (%s), using defaults\n", err.c_str());
}

String text() { return serialize(cur); }

bool apply(const String &body, String &err)
{
  Values v = cur;
  if (!parse(body, v, err)) return false;
  bool moved = fabsf(v.lat - cur.lat) > 0.0001f || fabsf(v.lon - cur.lon) > 0.0001f;
  cur = v;
  if (!save(cur)) {
    err = "could not write the settings file";
    return false;
  }
  if (moved) applied_off = 0x7fffffff;  // new city: take the next offset the weather reports
  ver++;
  return true;
}

uint32_t version() { return ver; }
lang::Id language() { return cur.language; }
bool h24() { return cur.h24; }
bool fahrenheit() { return cur.fahr; }
int nightMode() { return cur.night; }
// Window of each night mode as start and end minute of the day (it wraps past midnight when start > end).
bool nightWindow(int mode, int &start_min, int &end_min)
{
  static const int W[NIGHT_MODES][2] = { { 0, 0 }, { 22 * 60, 7 * 60 }, { 23 * 60, 7 * 60 }, { 0, 8 * 60 } };
  mode = constrain(mode, 0, NIGHT_MODES - 1);
  start_min = W[mode][0];
  end_min = W[mode][1];
  return mode > 0;
}
String city() { return cur.city; }
String hiddenPages() { return cur.hide; }
bool pageHidden(const char *key)
{
  int n = strlen(key), i = 0;
  while (i < (int)cur.hide.length()) {  // whole words of the comma list
    int j = cur.hide.indexOf(',', i);
    if (j < 0) j = cur.hide.length();
    if (j - i == n && !strncmp(cur.hide.c_str() + i, key, n)) return true;
    i = j + 1;
  }
  return false;
}
float lat() { return cur.lat; }
float lon() { return cur.lon; }
int focusMin() { return cur.focus; }
int shortMin() { return cur.short_b; }
int longMin() { return cur.long_b; }
int groupCount() { return cur.n_groups; }
const RadioGroup &group(int i) { return cur.groups[constrain(i, 0, cur.n_groups - 1)]; }
int stationCount() { return cur.n_stations; }
const RadioStation &station(int i) { return cur.stations[constrain(i, 0, cur.n_stations - 1)]; }

void setUtcOffset(int seconds) { pending_off = seconds; }

void tick()
{
  int off = pending_off;
  if (off == 0x7fffffff || off == applied_off) return;
  applied_off = off;
  // POSIX TZ counts the other way: UTC+3 is "<+03>-3", UTC+5:30 is "<+0530>-5:30"
  int a = abs(off) / 60, h = a / 60, m = a % 60;
  char tz[32];
  const char *sign = off >= 0 ? "+" : "-", *posix_sign = off >= 0 ? "-" : "";
  if (m) snprintf(tz, sizeof(tz), "<%s%02d%02d>%s%d:%02d", sign, h, m, posix_sign, h, m);
  else snprintf(tz, sizeof(tz), "<%s%02d>%s%d", sign, h, posix_sign, h);
  Net_SetTimezone(tz);
}

}  // namespace settings

// The language on screen for T() (Lang.h): the settings own it, so there is one copy of it and it is right from the first
// call on (the defaults until begin() has read the file).
lang::Id lang::current() { return settings::language(); }

#endif
