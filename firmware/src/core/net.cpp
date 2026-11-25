// Wi-Fi setup portal, mDNS name, NTP time and the HTTP API.
//
//   GET  /         control page (send text or an image from any browser)
//   GET  /status   JSON status
//   GET  /screen   what is on screen now, local pages too, in the board's format (RGB565)
//   POST /text     text=..., title=..., ttl=seconds
//   POST /image    multipart file in the board's frame format (RGB565 640x172), ttl=seconds
//   POST /clock    back to the clock page
//   POST /page     name=clock|radio|weather|timer|aquarium|...: show that page
//   POST /cmd      name=..., arg=...: board commands (radio, vol, alarm, timer, state)
//   GET  /settings      settings page for boards that have one (city, language, radio list...)
//   GET  /api/settings  the settings as text, one key=value per line
//   POST /api/settings  the same text in the body (needs header X-Phosphor: 1); saved and applied at once
//   POST /beep     f=Hz, ms=duration, vol=0-100: test tone on the speaker
//   POST /update   multipart firmware .bin            (X-Token header)
//   POST /assets   multipart asset pack (build/assets.bin), restarts (X-Token header)
//   POST /name     name=phosphor-desk, restarts          (X-Token header)
//   POST /wifi-reset  forget Wi-Fi, restarts to setup (X-Token header)

#include <WiFi.h>
#include <WiFiManager.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <Update.h>
#include <Preferences.h>
#include <esp_mac.h>
#include <esp_sntp.h>
#include <atomic>
#include "net.h"
#include "Board.h"
#include "Assets.h"
#include "Audio.h"
#include "Improv.h"
#include "Watchdog.h"
#include "state.h"
#include "UpdateCheck.h"
#ifdef BOARD_TOUCH349
#include "../boards/touch349/gadget/Motion.h"
#endif
#if __has_include("../../secrets.h")
#include "../../secrets.h"
#endif
#ifndef OTA_TOKEN
#define OTA_TOKEN ""  // no secrets.h: OTA and the admin endpoints stay disabled
#endif

#define TZ_ISTANBUL "<+03>-3"
#define WIFI_RETRY_MS 30000

static WebServer server(80);
static Preferences prefs;
static bool services_started = false;
static uint32_t time_synced_ms = 0;  // when the RTC last got NTP time; 0 before the first answer
// Why the RTC should get the system time again, as bits: set by the SNTP callback (lwIP's task) or a new time zone,
// taken all at once by syncTime() in the main loop with exchange(), so no request is lost or taken twice.
enum : uint32_t { SYNC_NTP = 1, SYNC_ZONE = 2 };
static std::atomic<uint32_t> sync_wanted{ 0 };
// The core builds with -mdisable-hardware-atomics, so exchange() and fetch_or() are calls to __atomic_*_4 in IDF's
// newlib. For a variable in internal RAM, as this one, they are an S32C1I compare-and-swap loop without a lock.
static uint32_t last_retry_ms = 0;

static bool ota_authorized = false;
static bool assets_ok = false;  // the asset pack upload so far
static uint8_t *upload_buf = nullptr;
static size_t upload_len = 0;
static bool upload_overflow = false;

static String macSuffix()
{
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char text[8];
  snprintf(text, sizeof(text), "%02x%02x", mac[4], mac[5]);
  return text;
}

static String defaultName()
{
  return "phosphor-" + macSuffix();
}

static String apName()
{
  String suffix = macSuffix();
  suffix.toUpperCase();
  return "Phosphor-Setup-" + suffix;
}

// Lowercase a-z, 0-9 and '-' only, as mDNS names need.
static String sanitizeName(String raw)
{
  raw.trim();
  raw.toLowerCase();
  String out;
  for (size_t i = 0; i < raw.length() && out.length() < 31; i++) {
    char c = raw[i];
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
      out += c;
    } else if ((c == '-' || c == '_' || c == ' ') && out.length() && !out.endsWith("-")) {
      out += '-';
    }
  }
  while (out.endsWith("-")) {
    out.remove(out.length() - 1);
  }
  return out.length() ? out : defaultName();
}

static bool authorized()
{
  return strlen(OTA_TOKEN) > 0 && server.header("X-Token") == OTA_TOKEN;
}

static void setTimeout()
{
  uint32_t ttl = server.arg("ttl").toInt();
  state.mode_until_ms = ttl ? millis() + ttl * 1000 : 0;
}

static const char CONTROL_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Phosphor</title>
<style>
body{font:16px/1.5 -apple-system,system-ui,sans-serif;max-width:460px;margin:24px auto;padding:0 16px;color:#1d1c1a;background:#f4f2ec}
h1{font-size:20px;margin:0 0 4px}small{color:#6b6860}
section{background:#fff;border:1px solid #dedbd2;border-radius:10px;padding:14px;margin:16px 0}
textarea,input[type=text]{width:100%;box-sizing:border-box;font:inherit;padding:8px;border:1px solid #cfccc2;border-radius:6px}
button{font:inherit;padding:8px 14px;border:0;border-radius:6px;background:#1d1c1a;color:#fff;margin-top:8px;cursor:pointer}
#msg{min-height:1.5em;color:#6b6860}
a{color:#1d1c1a}
</style></head><body>
<h1 id="name">Phosphor</h1><small id="info"></small>
<section><b>Send text</b>
<input type="text" id="title" placeholder="Title (optional)" style="margin-top:8px">
<textarea id="text" rows="4" placeholder="What should the screen say?" style="margin-top:8px"></textarea>
<button onclick="sendText()">Send</button> <button onclick="post('/clock')">Back to clock</button></section>
<section><b>Settings</b><br><small>City, language, clock, timers, radio stations, pages</small><br><a href="/settings">Open the settings page</a></section>
<div id="msg"></div>
<script>
const $=id=>document.getElementById(id);
function say(t){$('msg').textContent=t}
async function post(url,body){const r=await fetch(url,{method:'POST',body});say(r.ok?'Sent':'Error: '+await r.text())}
function sendText(){const p=new URLSearchParams({text:$('text').value,title:$('title').value});post('/text',p)}
fetch('/status').then(r=>r.json()).then(s=>{$('name').textContent=s.name+'.local';
$('info').textContent=`${s.ip} · ${s.temp_c.toFixed(1)}° · ${Math.round(s.humidity)}% humidity · firmware ${s.fw}`});
</script></body></html>)HTML";

static void sendStatus()
{
  char json[960];
  static const char *MODE_NAMES[] = { "clock", "info", "message", "image", "aquarium", "aquarium-night", "radio", "weather", "timer", "snow", "fire", "sea", "study", "wave", "lava", "hourglass", "settings", "tools", "play", "app" };
  const char *mode = MODE_NAMES[(int)state.mode];
  uint32_t uh0, uh1;
  UpdateCheck_Heap(uh0, uh1);
  char knock[64] = "";
#ifdef BOARD_TOUCH349
  // knock diagnostics: knocks since boot, and the biggest jump off the baseline (g) in the last ~4 s
  snprintf(knock, sizeof(knock), ",\"knock_n\":%lu,\"knock_peak\":%.3f", (unsigned long)Motion_KnockCount(), Motion_KnockPeak());
#endif
  snprintf(json, sizeof(json),
           "{\"name\":\"%s\",\"fw\":\"%s\",\"ip\":\"%s\",\"rssi\":%d,\"mode\":\"%s\","
           "\"temp_c\":%.2f,\"humidity\":%.1f,\"battery_v\":%.2f,\"uptime_s\":%lu,"
           "\"heap_free\":%u,\"psram_free\":%u,\"update\":\"%s\",\"update_heap\":[%u,%u],\"update_code\":%d,"
           "\"model\":\"%s\",\"width\":%d,\"height\":%d,\"format\":\"%s\",\"speaker\":%s,\"volume\":%d,\"inverted\":%s,\"display\":%d,\"brightness\":%d,\"dim\":%d,\"reset\":\"%s\",\"board_rev\":\"%s\"%s}",
           state.name.c_str(), FW_VERSION, Net_Ip().c_str(), Net_Rssi(), mode,
           isnan(state.temp_c) ? 0.0 : state.temp_c, isnan(state.humi) ? 0.0 : state.humi, state.batt_v,
           (unsigned long)(millis() / 1000), (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram(), UpdateCheck_Newer(), (unsigned)uh0, (unsigned)uh1, UpdateCheck_Code(),
           board().model(), board().width(), board().height(), board().frameFormat(),
           Audio_Ready() ? "true" : "false", Audio_Volume(), state.inverted ? "true" : "false", board().displayOn(), board().brightness(), board().dimLevel(),
           Watchdog_ResetReason(), board().boardRevision(), knock);
  server.send(200, "application/json", json);
}

static void restartSoon(const char *reply)
{
  server.send(200, "text/plain", reply);
  delay(300);
  ESP.restart();
}

static void setupRoutes()
{
  static const char *header_keys[] = { "X-Token", "X-Phosphor" };
  server.collectHeaders(header_keys, 2);

  server.on("/", HTTP_GET, []() {
    server.send_P(200, "text/html; charset=utf-8", CONTROL_PAGE);
  });

  server.on("/status", HTTP_GET, sendStatus);

  server.on("/screen", HTTP_GET, []() {
    const uint8_t *buf = state.mode == Mode::Image ? state.image : nullptr;
    static uint8_t *shot = nullptr;
    if (!buf) {
      if (!shot) {
        shot = (uint8_t *)ps_malloc(board().frameBytes());
      }
      if (shot && board().snapshot(shot)) {
        buf = shot;
      }
    }
    if (!buf) {
      server.send(404, "text/plain", "local page on screen, no frame\n");
      return;
    }
    server.setContentLength(board().frameBytes());
    server.send(200, "application/octet-stream", "");
    server.sendContent((const char *)buf, board().frameBytes());
  });

  server.on("/text", HTTP_POST, []() {
    String text = server.hasArg("text") ? server.arg("text") : server.arg("plain");
    text.trim();
    if (!text.length()) {
      server.send(400, "text/plain", "text missing\n");
      return;
    }
    state.message = text;
    state.message_title = server.arg("title");
    setTimeout();
    state.mode = Mode::Message;
    state.dirty = true;
    server.send(200, "text/plain", "OK\n");
  });

  server.on(
    "/image", HTTP_POST,
    []() {
      bool ok = !upload_overflow && upload_buf && state.image && board().acceptFrame(upload_buf, upload_len, state.image);
      if (!ok) {
        server.send(400, "text/plain", "not a frame for this board\n");
        return;
      }
      setTimeout();
      state.mode = Mode::Image;
      state.dirty = true;
      server.send(200, "text/plain", "OK\n");
    },
    []() {
      HTTPUpload &up = server.upload();
      const size_t cap = board().frameBytes() + 512;
      if (up.status == UPLOAD_FILE_START) {
        if (!upload_buf) {
          upload_buf = (uint8_t *)ps_malloc(cap);
        }
        upload_len = 0;
        upload_overflow = false;
      } else if (up.status == UPLOAD_FILE_WRITE && upload_buf) {
        if (upload_len + up.currentSize > cap) {
          upload_overflow = true;
        } else {
          memcpy(upload_buf + upload_len, up.buf, up.currentSize);
          upload_len += up.currentSize;
        }
      }
    });

  // Backlight: POST /brightness?pct=5..100; GET reads it. Boards without a backlight answer 501.
  server.on("/brightness", HTTP_ANY, []() {
    if (board().brightness() < 0) {
      server.send(501, "text/plain", "no backlight\n");
      return;
    }
    if (server.hasArg("pct")) {
      board().setBrightness(server.arg("pct").toInt());
    }
    server.send(200, "text/plain", String(board().brightness()) + "\n");
  });

  // Screen off/on (the board keeps running): POST /display?on=0|1; GET reads it. 501 where the board cannot.
  server.on("/display", HTTP_ANY, []() {
    if (board().displayOn() < 0) {
      server.send(501, "text/plain", "no display control\n");
      return;
    }
    if (server.hasArg("on")) {
      board().setDisplayOn(server.arg("on").toInt() != 0);
    }
    server.send(200, "text/plain", String(board().displayOn()) + "\n");
  });

  server.on("/beep", HTTP_POST, []() {
    if (!Audio_Ready()) {
      server.send(501, "text/plain", "no speaker\n");
      return;
    }
    if (server.hasArg("vol")) {
      Audio_SetVolume(server.arg("vol").toInt());
    }
    Audio_Beep(server.hasArg("f") ? server.arg("f").toInt() : 880, server.hasArg("ms") ? server.arg("ms").toInt() : 300);
    server.send(200, "text/plain", "OK\n");
  });

  // Any page by name (see MODE_NAMES); the board's own pages included.
  server.on("/page", HTTP_POST, []() {
    static const char *NAMES[] = { "clock", "info", "message", "image", "aquarium", "aquarium-night", "radio", "weather", "timer", "snow", "fire", "sea", "study", "wave", "lava", "hourglass", "settings", "tools", "play", "app" };
    String name = server.arg("name");
    for (int i = 0; i < (int)(sizeof(NAMES) / sizeof(NAMES[0])); i++) {
      if (name == NAMES[i]) {
        state.mode = (Mode)i;
        state.mode_until_ms = 0;
        state.dirty = true;
        server.send(200, "text/plain", "OK\n");
        return;
      }
    }
    server.send(400, "text/plain", "unknown page\n");
  });

  // Board commands (the bar gadget: radio, vol, alarm, timer); the board answers with JSON or an error line.
  server.on("/settings", HTTP_GET, []() {
    const char *page = board().settingsPage();
    if (!page) {
      server.send(404, "text/plain", "no settings on this board\n");
      return;
    }
    server.send_P(200, "text/html; charset=utf-8", page);
  });

  server.on("/api/settings", HTTP_GET, []() {
    server.send(200, "text/plain; charset=utf-8", board().settingsText());
  });

  server.on("/api/settings", HTTP_POST, []() {
    // A custom header cannot be sent cross-site without a CORS preflight (which this server never approves), so a web
    // page on some other site cannot rewrite the settings through the visitor's browser.
    if (!server.hasHeader("X-Phosphor")) {
      server.send(403, "text/plain", "missing X-Phosphor header\n");
      return;
    }
    String err;
    if (board().applySettings(server.arg("plain"), err)) {
      state.dirty = true;
      server.send(200, "text/plain; charset=utf-8", "OK\n");
    } else {
      server.send(400, "text/plain; charset=utf-8", (err.length() ? err : String("not supported")) + "\n");
    }
  });

  server.on("/cmd", HTTP_POST, []() {
    String reply = board().command(server.arg("name"), server.arg("arg"));
    if (reply.length()) {
      server.send(200, "application/json", reply + "\n");
    } else {
      server.send(400, "text/plain", "unknown command\n");
    }
  });

  server.on("/clock", HTTP_POST, []() {
    state.mode = state.home;
    state.mode_until_ms = 0;
    state.dirty = true;
    server.send(200, "text/plain", "OK\n");
  });

  server.on(
    "/update", HTTP_POST,
    []() {
      if (!ota_authorized) {
        server.send(401, "text/plain", "unauthorized\n");
        return;
      }
      if (Update.hasError()) {
        server.send(500, "text/plain", String(Update.errorString()) + "\n");
        state.dirty = true;
        return;
      }
      restartSoon("OK, restarting\n");
    },
    []() {
      HTTPUpload &up = server.upload();
      if (up.status == UPLOAD_FILE_START) {
        ota_authorized = authorized();
        if (ota_authorized) {
          board().showNotice("Updating", "Installing new firmware...");
          Update.begin(UPDATE_SIZE_UNKNOWN);
        }
      } else if (up.status == UPLOAD_FILE_WRITE && ota_authorized) {
        Watchdog_Feed();  // a firmware upload keeps loop() inside the handler for many seconds
        Update.write(up.buf, up.currentSize);
      } else if (up.status == UPLOAD_FILE_END && ota_authorized) {
        Update.end(true);
      }
    });

  server.on(
    "/assets", HTTP_POST,
    []() {
      if (!ota_authorized) {
        server.send(401, "text/plain", "unauthorized\n");
        return;
      }
      if (!assets_ok) {
        server.send(500, "text/plain", "asset pack not written (no assets partition, or the pack is invalid)\n");
        state.dirty = true;
        return;
      }
      restartSoon("OK, restarting\n");
    },
    []() {
      HTTPUpload &up = server.upload();
      if (up.status == UPLOAD_FILE_START) {
        ota_authorized = authorized();
        assets_ok = false;
        if (ota_authorized) {
          board().showNotice("Updating", "Installing pictures...");
          assets_ok = assets::writeBegin();
        }
      } else if (up.status == UPLOAD_FILE_WRITE && ota_authorized) {
        Watchdog_Feed();
        if (assets_ok) assets_ok = assets::write(up.buf, up.currentSize);
      } else if (up.status == UPLOAD_FILE_END && ota_authorized) {
        assets_ok = assets_ok && assets::writeEnd();
      }
    });

  server.on("/name", HTTP_POST, []() {
    if (!authorized()) {
      server.send(401, "text/plain", "unauthorized\n");
      return;
    }
    String name = sanitizeName(server.arg("name"));
    prefs.putString("name", name);
    restartSoon(("OK, new name " + name + ".local, restarting\n").c_str());
  });

  server.on("/wifi-reset", HTTP_POST, []() {
    if (!authorized()) {
      server.send(401, "text/plain", "unauthorized\n");
      return;
    }
    server.send(200, "text/plain", "OK, opening setup portal\n");
    delay(300);
    Net_ResetWifi();
  });

  server.onNotFound([]() {
    server.send(404, "text/plain", "not found\n");
  });
}

static void startServices()
{
  MDNS.begin(state.name.c_str());
  MDNS.addService("http", "tcp", 80);
  MDNS.addServiceTxt("http", "tcp", "device", "phosphor");
  sntp_set_time_sync_notification_cb([](struct timeval *) {  // lwIP's task: no I2C, no printing, no waiting
    sync_wanted.fetch_or(SYNC_NTP);
  });
  configTzTime(prefs.getString("tz", TZ_ISTANBUL).c_str(), "pool.ntp.org", "time.google.com");
  setupRoutes();
  server.begin();
  services_started = true;
  Serial.printf("Online: http://%s.local  (%s)\n", state.name.c_str(), Net_Ip().c_str());
}

// Copies NTP time into the RTC so the clock stays right offline. Only after an answer from the server: after a software
// restart (OTA, settings) the system clock still runs, but on the internal RC oscillator over the restart, so it may be
// minutes off until SNTP sets it; copied then, the RTC kept that error until the next copy.
static void syncTime()
{
  if (!sync_wanted.exchange(0)) {
    return;
  }
  struct tm t;
  if (!getLocalTime(&t, 0) || t.tm_year < 124) {
    return;
  }
  board().setTime({ t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec });
  time_synced_ms = millis() | 1;
  Serial.printf("RTC synced from NTP: %02d:%02d:%02d\n", t.tm_hour, t.tm_min, t.tm_sec);
}

void Net_Begin(bool force_portal)
{
  prefs.begin("rlcd", false);
  state.name = prefs.getString("name", defaultName());
  state.home = Mode::Clock;
  state.mode = Mode::Clock;
  state.image = (uint8_t *)ps_calloc(1, board().frameBytes());

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(state.name.c_str());
  WiFi.setAutoReconnect(true);

  WiFiManager wm;
  if (force_portal) {
    wm.resetSettings();
  }
  WiFiManagerParameter name_param("name", "Device name (e.g. desk)", state.name.c_str(), 31);
  wm.addParameter(&name_param);
  wm.setTitle("Setup");
  wm.setHostname(state.name.c_str());
  wm.setConnectTimeout(20);
  // Never configured: keep the portal open until someone sets it up.
  // Configured but the network is down: give up after 3 minutes and keep retrying in the background.
  wm.setConfigPortalTimeout(wm.getWiFiIsSaved() ? 180 : 0);
  wm.setAPCallback([](WiFiManager *) {
    board().showSetup(apName().c_str());
  });
  // The portal does not block: autoConnect returns as soon as it is up and the loop below serves it, so the web
  // installer can still set the Wi-Fi over USB (Improv) while the portal is open.
  wm.setConfigPortalBlocking(false);

  bool connected = wm.autoConnect(apName().c_str());
  while (!connected && wm.getConfigPortalActive()) {  // ends on a save, a timeout, or when Improv got us online
    wm.process();
    Improv_Poll();
    if (Net_Connected() && !Improv_Busy()) {
      wm.stopConfigPortal();
      break;
    }
    delay(5);
  }
  connected = Net_Connected();

  String name = sanitizeName(name_param.getValue());
  if (name != state.name) {
    prefs.putString("name", name);
    board().showNotice("Saved", (name + ".local").c_str());
    delay(1500);
    ESP.restart();
  }
  if (connected) {
    startServices();
  }
  state.dirty = true;
}

void Net_Loop()
{
  Improv_Poll();
  if (WiFi.status() == WL_CONNECTED) {
    if (!services_started) {
      startServices();
    }
    syncTime();
  } else if (!Improv_Busy() && millis() - last_retry_ms > WIFI_RETRY_MS) {
    last_retry_ms = millis();
    WiFi.begin();  // retries the saved network
  }
  if (services_started) {
    server.handleClient();
  }
}

bool Net_Connected()
{
  return WiFi.status() == WL_CONNECTED;
}

String Net_Ip()
{
  return WiFi.localIP().toString();
}

int Net_Rssi()
{
  return WiFi.RSSI();
}

void Net_ResetWifi()
{
  WiFiManager wm;
  wm.resetSettings();
  ESP.restart();
}

void Net_SetTimezone(const char *posix_tz)
{
  if (prefs.getString("tz", "") != posix_tz) prefs.putString("tz", posix_tz);
  setenv("TZ", posix_tz, 1);
  tzset();
  if (time_synced_ms) sync_wanted.fetch_or(SYNC_ZONE);  // the system clock has NTP time: copy it again, in the new zone
  Serial.printf("time zone: %s\n", posix_tz);
}
