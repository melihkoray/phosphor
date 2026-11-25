#ifdef BOARD_TOUCH349

// Pages drawn on the 640x172 strip itself with U8g2: clock, system info, text message, setup, notices, and the
// brightness overlay. Pushed images are full RGB565 pictures and go to the panel untouched (the overlay
// is drawn over a copy).

#include <qrcode.h>
#include "Touch349Board.h"
#include "../../core/net.h"

#include "gadget/Settings.h"

static constexpr int W = Touch349Board::WIDTH;
static constexpr int H = Touch349Board::HEIGHT;
static constexpr int MARGIN = 20;

// Set by each public entry point; the drawing helpers below share it.
static U8G2 *u8g2 = nullptr;


// Largest first; the message page picks the biggest one that fits.
static const uint8_t *MESSAGE_FONTS[] = { u8g2_font_helvB24_te, u8g2_font_helvB18_te, u8g2_font_helvB14_te,
                                          u8g2_font_helvB12_te, u8g2_font_helvR10_te };
#define MAX_LINES 8

static int weekday(int y, int m, int d)
{
  static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
  if (m < 3) {
    y -= 1;
  }
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

static void drawCentered(int y, const char *text)
{
  int x = (W - u8g2->getUTF8Width(text)) / 2;
  u8g2->drawUTF8(x < 0 ? 0 : x, y, text);
}

static void drawBattery(int x, int y)
{
  int level = max(state.batt_pct, 0);
  u8g2->drawFrame(x, y, 34, 16);
  u8g2->drawBox(x + 34, y + 4, 3, 8);
  u8g2->drawBox(x + 2, y + 2, (30 * level) / 100, 12);
  char text[8];
  snprintf(text, sizeof(text), "%d%%", level);
  u8g2->setFont(u8g2_font_helvB12_te);
  u8g2->drawUTF8(x - u8g2->getUTF8Width(text) - 6, y + 14, text);
  if (state.charging) {  // a small bolt over the cell, in the background colour so it cuts out of the fill
    u8g2->setDrawColor(2);
    u8g2->drawTriangle(x + 18, y + 2, x + 12, y + 9, x + 18, y + 9);
    u8g2->drawTriangle(x + 16, y + 7, x + 22, y + 7, x + 16, y + 14);
    u8g2->setDrawColor(1);
  }
}

// Three bars by signal strength; a crossed box when offline. Returns the left edge.
static int drawWifi(int right, int y)
{
  int x = right - 20;
  if (!Net_Connected()) {
    u8g2->drawFrame(x, y + 2, 14, 14);
    u8g2->drawLine(x, y + 2, x + 13, y + 15);
    u8g2->drawLine(x, y + 15, x + 13, y + 2);
    return x;
  }
  int rssi = Net_Rssi();
  int bars = rssi > -60 ? 3 : rssi > -70 ? 2 : 1;
  for (int i = 0; i < 3; i++) {
    int h = 6 + i * 5;
    int bx = x + i * 7;
    if (i < bars) {
      u8g2->drawBox(bx, y + 16 - h, 5, h);
    } else {
      u8g2->drawFrame(bx, y + 16 - h, 5, h);
    }
  }
  return x;
}

// Battery and Wi-Fi status at the right edge, starting at y.
static void drawStatus(int y)
{
  drawBattery(W - MARGIN - 38, y);
  u8g2->setFont(u8g2_font_helvB12_te);
  char pct[8];
  snprintf(pct, sizeof(pct), "%d%%", max(state.batt_pct, 0));
  int battery_left = W - MARGIN - 38 - u8g2->getUTF8Width(pct) - 6;
  drawWifi(battery_left - 12, y - 1);
}

static void drawHeader(const char *title)
{
  u8g2->setFont(u8g2_font_helvB14_te);
  u8g2->drawUTF8(MARGIN, 28, title);
  drawStatus(12);
  u8g2->drawHLine(MARGIN, 38, W - 2 * MARGIN);
}

static void drawClockPage(const DateTime &now)
{
  char text[48];

  u8g2->setFont(u8g2_font_logisoso92_tn);
  snprintf(text, sizeof(text), "%02d:%02d", now.hour, now.minute);
  u8g2->drawStr(MARGIN, 128, text);
  int right_x = MARGIN + u8g2->getStrWidth(text) + 44;

  drawStatus(12);

  u8g2->setFont(u8g2_font_helvB24_te);
  u8g2->drawUTF8(right_x, 86, Lang().weekday_long[weekday(now.year, now.month, now.day)]);
  u8g2->setFont(u8g2_font_helvB18_te);
  snprintf(text, sizeof(text), "%d %s %d", now.day, Lang().month_long[now.month - 1], now.year);
  u8g2->drawUTF8(right_x, 116, text);

  int bar_w = W - 2 * MARGIN;
  u8g2->drawFrame(MARGIN, 150, bar_w, 6);
  u8g2->drawBox(MARGIN, 150, (bar_w * now.second) / 59, 6);
}

static void drawInfoPage(const DateTime &now)
{
  char text[80];

  drawHeader("Sistem");
  u8g2->setFont(u8g2_font_6x13_te);

  int y = 62;
  int x = MARGIN;
  const int step = 20;
  const int value_dx = 82;
  auto row = [&](const char *label, const char *value) {
    u8g2->drawUTF8(x, y, label);
    u8g2->drawUTF8(x + value_dx, y, value);
    y += step;
  };

  snprintf(text, sizeof(text), "%s.local", state.name.c_str());
  row(T("Name"), text);
  if (Net_Connected()) {
    snprintf(text, sizeof(text), "%s  (%d dBm)", Net_Ip().c_str(), Net_Rssi());
  } else {
    snprintf(text, sizeof(text), "%s", T("not connected"));
  }
  row("Wi-Fi", text);
  row(T("Firmware"), FW_VERSION);
  snprintf(text, sizeof(text), "ESP32-S3 rev %d, %lu MHz", ESP.getChipRevision(), (unsigned long)ESP.getCpuFreqMHz());
  row(T("Chip"), text);
  snprintf(text, sizeof(text), "%04d-%02d-%02d %02d:%02d:%02d", now.year, now.month, now.day, now.hour, now.minute, now.second);
  row(T("Time"), text);

  y = 62;
  x = W / 2 + 10;
  snprintf(text, sizeof(text), T("%u / %u KB free"), (unsigned)(ESP.getFreePsram() / 1024), (unsigned)(ESP.getPsramSize() / 1024));
  row("PSRAM", text);
  snprintf(text, sizeof(text), T("%u KB free"), (unsigned)(ESP.getFreeHeap() / 1024));
  row("Heap", text);
  snprintf(text, sizeof(text), "%.2f V (%%%d)", state.batt_v, state.batt_pct);
  row(T("Battery"), text);
  snprintf(text, sizeof(text), T("%lu min"), (unsigned long)(millis() / 60000));
  row(T("Uptime"), text);

  u8g2->drawUTF8(MARGIN, 160, T("Tap: next page · PWR: screen on/off · BOOT: brightness, hold 5 s: reset Wi-Fi"));
}

// Word-wraps text for the current font. Returns the line count, or -1 if it needs more than max_lines.
static int wrapText(const String &text, int max_w, String *lines, int max_lines)
{
  int count = 0;
  String line;
  int n = text.length();
  int i = 0;
  while (true) {
    int j = i;
    while (j < n && text[j] != ' ' && text[j] != '\n') {
      j++;
    }
    String word = text.substring(i, j);
    String candidate = line.length() ? line + " " + word : word;
    if (!line.length() || u8g2->getUTF8Width(candidate.c_str()) <= max_w) {
      line = candidate;
    } else {
      if (count >= max_lines) {
        return -1;
      }
      lines[count++] = line;
      line = word;
    }
    if (j >= n || text[j] == '\n') {
      if (count >= max_lines) {
        return -1;
      }
      lines[count++] = line;
      line = "";
      if (j >= n) {
        break;
      }
    }
    i = j + 1;
  }
  return count;
}

static void drawMessagePage()
{
  int top = 14;
  if (state.message_title.length()) {
    drawHeader(state.message_title.c_str());
    top = 46;
  }
  const int bottom = 148;
  const int max_w = W - 2 * MARGIN;

  String lines[MAX_LINES];
  int count = 0;
  int line_h = 0;
  const int font_count = sizeof(MESSAGE_FONTS) / sizeof(MESSAGE_FONTS[0]);
  for (int f = 0; f < font_count; f++) {
    u8g2->setFont(MESSAGE_FONTS[f]);
    line_h = u8g2->getAscent() - u8g2->getDescent() + 6;
    int max_lines = min((bottom - top) / line_h, MAX_LINES);
    count = wrapText(state.message, max_w, lines, max_lines);
    bool fits = count > 0;
    for (int k = 0; fits && k < count; k++) {
      fits = u8g2->getUTF8Width(lines[k].c_str()) <= max_w;
    }
    if (fits || f == font_count - 1) {
      if (count < 0) {
        count = max_lines;  // smallest font still too long: show what fits
      }
      break;
    }
  }

  int block_h = count * line_h;
  int y = top + ((bottom - top) - block_h) / 2 + u8g2->getAscent();
  for (int k = 0; k < count; k++) {
    if (count <= 2) {
      drawCentered(y, lines[k].c_str());
    } else {
      u8g2->drawUTF8(MARGIN, y, lines[k].c_str());
    }
    y += line_h;
  }

  u8g2->setFont(u8g2_font_6x13_te);
  drawCentered(166, T("Tap: back to the clock"));
}

void Touch349Board::render(const DateTime &now)
{
  if (Gadget_Draws(state.mode)) {
    Gadget_Render(now);
    pushCanvas();
    return;
  }
  if (state.mode == Mode::Image) {
    show((const uint16_t *)state.image);
    return;
  }
  u8g2 = page_.u8g2();
  u8g2->clearBuffer();
  u8g2->setDrawColor(1);
  switch (state.mode) {
    case Mode::Clock: drawClockPage(now); break;
    case Mode::Info: drawInfoPage(now); break;
    case Mode::Message: drawMessagePage(); break;
    default: break;
  }
  u8g2->sendBuffer();
  pushCanvas();
}

// Brightness overlay, in a lamp language: the picture sinks into the bar's ground (a trace of it stays),
// then one line across the middle: the label, five segments (lit ones amber up to the level, the rest dark slots) and
// the level. Each color is a U8g2 layer painted onto the copy.
void Touch349Board::drawLevelOverlay(const uint16_t *under)
{
  const int n = W * H;
  for (int i = 0; i < n; i++) {
    scratch_[i] = mix565(under[i], BG, 236);
  }

  const int seg_w = 60, seg_h = 26, gap = 8, value_gap = 28;
  const int meter_w = LEVEL_COUNT * seg_w + (LEVEL_COUNT - 1) * gap;
  u8g2 = page_.u8g2();
  u8g2->setFont(u8g2_font_helvB24_te);
  const int value_w = u8g2->getUTF8Width("%100");  // a fixed slot, so the meter never shifts between levels
  const int left = (W - (meter_w + value_gap + value_w)) / 2;
  const int top = 84;
  int lit = 0;
  for (int v : LEVELS) {
    lit += brightness_pct_ >= v;
  }
  lit = max(lit, 1);

  u8g2->clearBuffer();
  for (int k = lit; k < LEVEL_COUNT; k++) {
    u8g2->drawRFrame(left + k * (seg_w + gap), top, seg_w, seg_h, 3);
  }
  page_.paint(scratch_, RULE);

  u8g2->clearBuffer();
  for (int k = 0; k < lit; k++) {
    u8g2->drawRBox(left + k * (seg_w + gap), top, seg_w, seg_h, 3);
  }
  page_.paint(scratch_, AMBER);

  u8g2->clearBuffer();
  u8g2->setFont(u8g2_font_helvB10_te);
  u8g2->drawUTF8(left, top - 14, T("BRIGHTNESS"));
  page_.paint(scratch_, DIM);

  char value[8];
  snprintf(value, sizeof(value), "%%%d", brightness_pct_);
  u8g2->clearBuffer();
  u8g2->setFont(u8g2_font_helvB24_te);
  u8g2->drawUTF8(left + meter_w + value_gap, top + seg_h - 1, value);  // baseline on the segments' bottom edge
  page_.paint(scratch_, INK);
}

static int qr_x, qr_y, qr_box;

static void drawQr(esp_qrcode_handle_t qrcode)
{
  int size = esp_qrcode_get_size(qrcode);
  int scale = qr_box / (size + 2);
  int offset = (qr_box - size * scale) / 2;
  for (int my = 0; my < size; my++) {
    for (int mx = 0; mx < size; mx++) {
      if (esp_qrcode_get_module(qrcode, mx, my)) {
        u8g2->drawBox(qr_x + offset + mx * scale, qr_y + offset + my * scale, scale, scale);
      }
    }
  }
}

void Touch349Board::showSetup(const char *ap_name)
{
  setDisplayOn(true);  // the portal needs someone to read it
  u8g2 = page_.u8g2();
  u8g2->clearBuffer();
  u8g2->setDrawColor(1);
  u8g2->setFont(u8g2_font_helvB18_te);
  u8g2->drawUTF8(MARGIN, 30, T("Wi-Fi setup"));
  u8g2->drawHLine(MARGIN, 40, W - 2 * MARGIN);

  u8g2->setFont(u8g2_font_helvR10_te);
  int y = 64;
  u8g2->drawUTF8(MARGIN, y, T("1. Join this network with your phone, or scan the QR code:"));
  y += 20;
  u8g2->setFont(u8g2_font_helvB12_te);
  u8g2->drawUTF8(MARGIN + 14, y, ap_name);
  y += 22;
  u8g2->setFont(u8g2_font_helvR10_te);
  u8g2->drawUTF8(MARGIN, y, T("2. On the page that opens: Configure WiFi, pick your network, enter its password"));
  y += 20;
  u8g2->drawUTF8(MARGIN, y, T("3. Device name, e.g. desk (then open desk.local/settings)"));
  y += 20;
  u8g2->drawUTF8(MARGIN, y, T("4. Save"));

  u8g2->setFont(u8g2_font_6x13_te);
  u8g2->drawUTF8(MARGIN, 162, T("If no page opens, go to 192.168.4.1 in a browser"));

  qr_box = 116;
  qr_x = W - qr_box - MARGIN;
  qr_y = 48;
  char payload[64];
  snprintf(payload, sizeof(payload), "WIFI:T:nopass;S:%s;;", ap_name);
  esp_qrcode_config_t cfg = {};
  cfg.display_func = drawQr;
  cfg.max_qrcode_version = 6;
  cfg.qrcode_ecc_level = ESP_QRCODE_ECC_LOW;
  esp_qrcode_generate(&cfg, payload);

  u8g2->sendBuffer();
  pushCanvas();
}

void Touch349Board::showNotice(const char *title, const char *line)
{
  u8g2 = page_.u8g2();
  u8g2->clearBuffer();
  u8g2->setDrawColor(1);
  u8g2->setFont(u8g2_font_helvB24_te);
  drawCentered(84, title);
  u8g2->setFont(u8g2_font_helvR12_te);
  drawCentered(118, line);
  u8g2->sendBuffer();
  pushCanvas();
}

#endif  // BOARD_TOUCH349
