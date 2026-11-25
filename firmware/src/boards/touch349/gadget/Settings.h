#pragma once

// User settings of the desk gadget, kept in a small text file on the board's flash (LittleFS on the "ffat"
// partition) and edited from a phone at http://<name>.local/settings. Nothing here needs a rebuild: city, language,
// clock style, pomodoro lengths and the whole radio list.
//
// File format, one setting per line (the same text is what GET /api/settings returns and POST /api/settings takes):
//   city=Lisbon
//   lat=38.7167
//   lon=-9.1333
//   lang=tr                 en | tr | pl (the codes in the language files, Lang.cpp); an unknown code is English
//   h24=1                   24-hour clock
//   unit=c                  c | f, temperatures (and wind in mph with f)
//   focus=25  short=5  long=15   pomodoro minutes
//   hide=radio,snow         pages left out of the swipe ring (clock and settings cannot be hidden)
//   group=80S|80s           radio button label (up to 6 letters) | full genre name; order = button order
//   station=0|Heart 80s|http://...   group index | name | stream URL (MP3 or AAC over HTTP/HTTPS)

#include <Arduino.h>
#include "Lang.h"

struct RadioGroup {
  String key;   // on the preset button
  String name;  // above the station name
};

struct RadioStation {
  uint8_t group;
  String name;
  String url;
};

namespace settings {

constexpr int MAX_GROUPS = 7;
constexpr int MAX_STATIONS = 60;

void begin();                                // mount the file system, load the file or the defaults
String text();                               // all settings in the file format
bool apply(const String &body, String &err); // parse, check, save, take effect
uint32_t version();                          // goes up on every apply (weather refetches, radio reloads)

lang::Id language();  // of the texts on screen
bool h24();
bool fahrenheit();
// Night dim: 0 = off, 1 = 22:00-07:00, 2 = 23:00-07:00, 3 = 00:00-08:00. The screen goes down to NIGHT_DIM_PCT.
constexpr int NIGHT_MODES = 4;
constexpr int NIGHT_DIM_PCT = 15;
int nightMode();
bool nightWindow(int mode, int &start_min, int &end_min);  // false for "off"
String city();
// Pages hidden from the ring, by page key ("radio", "snow", ...), as the comma list of the file.
bool pageHidden(const char *key);
String hiddenPages();
float lat();
float lon();
int focusMin();
int shortMin();
int longMin();

int groupCount();
const RadioGroup &group(int i);
int stationCount();
const RadioStation &station(int i);

// UTC offset reported by the weather service for the chosen city; sets the clock's time zone.
void setUtcOffset(int seconds);
void tick();  // main loop: applies a new time zone

}  // namespace settings

// The /settings page (SettingsPage.cpp).
extern const char SETTINGS_PAGE[];
