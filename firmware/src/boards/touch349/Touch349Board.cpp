#ifdef BOARD_TOUCH349

// Hardware side of the Waveshare ESP32-S3-Touch-LCD-3.49: pins, panel, touch, clock, battery. Pages are in Touch349Ui.cpp.
// Pin map and panel setup follow Waveshare's Arduino examples (Examples/Arduino/09_LVGL_V8_Test).

#include "Touch349Board.h"
#include "gadget/Apps.h"
#include "gadget/Settings.h"
#include <Preferences.h>
#include <driver/gpio.h>
#include <driver/ledc.h>
#include <driver/spi_master.h>
#include <esp_lcd_io_spi.h>
#include <esp_lcd_panel_io.h>
#include "ExternLib/axs15231b/esp_lcd_axs15231b.h"
#include "../../core/net.h"
#include "../../hw/adc_bsp.h"
#include "../../hw/i2c_equipment.h"

#define LCD_HOST SPI3_HOST
#define LCD_CS_PIN 9
#define LCD_SCK_PIN 10
#define LCD_D0_PIN 11
#define LCD_D1_PIN 12
#define LCD_D2_PIN 13
#define LCD_D3_PIN 14
// V1 and V2 (PCB "Rev1.1") swap two pairs: backlight <-> expander INT, panel reset <-> panel TE.
#define V1_LCD_RST_PIN 21
#define V1_LCD_BL_PIN 8
#define V2_LCD_BL_PIN 42

#define I2C_SCL_PIN 48
#define I2C_SDA_PIN 47
#define TOUCH_SCL_PIN 18
#define TOUCH_SDA_PIN 17

#define RTC_ADDR 0x51
#define TCA9554_ADDR 0x20
#define TOUCH_ADDR 0x3b
#define BOOT_PIN 0
#define PWR_PIN 16  // the PWR key, low while pressed (Waveshare demo 07_BATT_PWR_Test)

// TCA9554 registers and the pin that holds the battery power switch closed.
#define TCA_REG_OUTPUT 0x01
#define TCA_REG_CONFIG 0x03
#define TCA_EXIO_V2_BL_EN 1
#define TCA_EXIO_BATTERY_LATCH 6
#define TCA_EXIO_AMP 7  // speaker amplifier enable

// Native panel is portrait: 172 wide, 640 tall. Rows go out in chunks of this many native rows.
#define NATIVE_W 172
#define NATIVE_H 640
#define CHUNK_ROWS 32
#define SPI_CLOCK_HZ (40 * 1000 * 1000)

// The panel wants the high byte of each RGB565 pixel first.
#define SWAP_BYTES true
#define BRIGHTNESS_MIN_PCT 5  // below this the panel looks switched off
#define SWIPE_MIN_PX 50
#define VSWIPE_MIN_PX 34
#define LONG_PRESS_MS 700
#define TOUCH_POLL_MS 30

#define BUTTON_DEBOUNCE_MS 30
#define POWER_OFF_HOLD_MS 3000
#define WIFI_RESET_HOLD_MS 5000
#define LEVEL_SHOW_MS 1500

Touch349Board::Touch349Board()
  : i2c_(I2C_SCL_PIN, I2C_SDA_PIN, 0),
    touch_i2c_(TOUCH_SCL_PIN, TOUCH_SDA_PIN, 1)
{
}

static bool onFlushDone(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *ctx)
{
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR((SemaphoreHandle_t)ctx, &woken);
  return woken == pdTRUE;
}

void Touch349Board::begin()
{
  pinMode(BOOT_PIN, INPUT_PULLUP);
  pinMode(PWR_PIN, INPUT_PULLUP);
  pwr_armed_ = digitalRead(PWR_PIN) == HIGH;
  initExpander();
  Rtc_Setup(&i2c_, RTC_ADDR);
  Adc_PortInit();
  initTouch();

  canvas_ = (uint16_t *)ps_calloc((size_t)WIDTH * HEIGHT, sizeof(uint16_t));
  scratch_ = (uint16_t *)ps_malloc((size_t)WIDTH * HEIGHT * sizeof(uint16_t));
  anim_ = (uint16_t *)ps_malloc((size_t)WIDTH * HEIGHT * sizeof(uint16_t));
  for (auto &buf : dma_) {
    buf = (uint16_t *)heap_caps_malloc(NATIVE_W * CHUNK_ROWS * sizeof(uint16_t), MALLOC_CAP_DMA);
  }
  if (!canvas_ || !scratch_ || !anim_ || !dma_[0] || !dma_[1]) {
    Serial.println("touch349: out of memory for canvas");
    return;
  }
  page_.begin(canvas_, WIDTH, HEIGHT);
  // Light text on a near-black ground.
  page_.setColors(0xEF5D, 0x0841);

  initPanel();
  initBacklight();
  static Touch349Board *self = this;
  Gadget_Begin(canvas_, &i2c_, [](bool on) { self->setAmp(on); });
}

// The battery switch only stays closed while EXIO6 is high; on USB power it does not matter, on battery the board
// would switch itself off after release of the PWR key otherwise.
void Touch349Board::initExpander()
{
  i2c_device_config_t cfg = {};
  cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  cfg.device_address = TCA9554_ADDR;
  cfg.scl_speed_hz = 300000;
  if (i2c_master_bus_add_device(i2c_.Get_I2cBusHandle(), &cfg, &expander_) != ESP_OK) {
    Serial.println("touch349: TCA9554 not found");
    return;
  }
  expander_mx_ = xSemaphoreCreateMutex();
  setPowerLatch(true);  // level first, then make it an output
  setExpanderBit(TCA_EXIO_AMP, false);
  uint8_t config = 0xff;
  i2c_.i2c_read_buff(expander_, TCA_REG_CONFIG, &config, 1);
  config &= ~((1 << TCA_EXIO_BATTERY_LATCH) | (1 << TCA_EXIO_AMP));
  i2c_.i2c_write_buff(expander_, TCA_REG_CONFIG, &config, 1);
}

// V2 (PCB "Rev1.1") wires the panel's TE output to GPIO21, where V1 has the panel reset. With TE switched on (0x35) the
// panel pulses that pin once a frame; only those pulses mean V2. On V1 the pull-up just keeps the reset released.
void Touch349Board::detectRevision()
{
  const int PIN = V1_LCD_RST_PIN;
  int edges = 0, last = digitalRead(PIN);
  const uint32_t t0 = micros();
  while (micros() - t0 < 150000) {
    int v = digitalRead(PIN);
    if (v != last) edges++;
    last = v;
  }
  // 9 frames at 60 Hz = about 18 edges; far more would be noise
  v2_ = edges >= 6 && edges <= 80;
  snprintf(rev_info_, sizeof(rev_info_), "%s te=%d", v2_ ? "v2" : "v1", edges);
  Serial.printf("touch349: board %s\n", rev_info_);
}

void Touch349Board::setPowerLatch(bool closed) { setExpanderBit(TCA_EXIO_BATTERY_LATCH, closed); }

void Touch349Board::setAmp(bool on) { setExpanderBit(TCA_EXIO_AMP, on); }

// Read-modify-write of one expander output; the radio task and the UI loop both get here.
void Touch349Board::setExpanderBit(int bit, bool on)
{
  if (!expander_) {
    return;
  }
  if (expander_mx_) xSemaphoreTake(expander_mx_, portMAX_DELAY);
  uint8_t value = 0xff;
  i2c_.i2c_read_buff(expander_, TCA_REG_OUTPUT, &value, 1);
  if (on) {
    value |= 1 << bit;
  } else {
    value &= ~(1 << bit);
  }
  i2c_.i2c_write_buff(expander_, TCA_REG_OUTPUT, &value, 1);
  if (expander_mx_) xSemaphoreGive(expander_mx_);
}

void Touch349Board::initTouch()
{
  i2c_device_config_t cfg = {};
  cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  cfg.device_address = TOUCH_ADDR;
  cfg.scl_speed_hz = 300000;
  if (i2c_master_bus_add_device(touch_i2c_.Get_I2cBusHandle(), &cfg, &touch_) != ESP_OK) {
    touch_ = nullptr;
    Serial.println("touch349: touch controller not found");
  }
}

void Touch349Board::initPanel()
{
  // Counts finished transfers; up to two chunks can be in flight (one per DMA buffer).
  flush_done_ = xSemaphoreCreateCounting(2, 0);

  pinMode(V1_LCD_RST_PIN, INPUT_PULLUP);  // TE on V2; driven only once V1 is known

  spi_bus_config_t bus = {};
  bus.data0_io_num = LCD_D0_PIN;
  bus.data1_io_num = LCD_D1_PIN;
  bus.sclk_io_num = LCD_SCK_PIN;
  bus.data2_io_num = LCD_D2_PIN;
  bus.data3_io_num = LCD_D3_PIN;
  bus.max_transfer_sz = NATIVE_W * CHUNK_ROWS * sizeof(uint16_t);
  ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO));

  esp_lcd_panel_io_handle_t io = nullptr;
  esp_lcd_panel_io_spi_config_t io_cfg = {};
  io_cfg.cs_gpio_num = LCD_CS_PIN;
  io_cfg.dc_gpio_num = -1;
  io_cfg.spi_mode = 3;
  io_cfg.pclk_hz = SPI_CLOCK_HZ;
  io_cfg.trans_queue_depth = 10;
  io_cfg.on_color_trans_done = onFlushDone;
  io_cfg.user_ctx = flush_done_;
  io_cfg.lcd_cmd_bits = 32;
  io_cfg.lcd_param_bits = 8;
  io_cfg.flags.quad_mode = true;
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(LCD_HOST, &io_cfg, &io));

  static const uint8_t no_param[] = { 0x00 };
  static const axs15231b_lcd_init_cmd_t init_cmds[] = {
    { 0x11, no_param, 0, 100 },  // sleep out
    { 0x29, no_param, 0, 100 },  // display on
    { 0x35, no_param, 1, 0 },    // TE on, for detectRevision()
  };
  axs15231b_vendor_config_t vendor = {};
  vendor.flags.use_qspi_interface = 1;
  vendor.init_cmds = init_cmds;
  vendor.init_cmds_size = sizeof(init_cmds) / sizeof(init_cmds[0]);

  esp_lcd_panel_dev_config_t panel_cfg = {};
  panel_cfg.reset_gpio_num = -1;
  panel_cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
  panel_cfg.bits_per_pixel = 16;
  panel_cfg.vendor_config = &vendor;
  ESP_ERROR_CHECK(esp_lcd_new_panel_axs15231b(io, &panel_cfg, &panel_));

  ESP_ERROR_CHECK(esp_lcd_panel_init(panel_));
  detectRevision();
  if (v2_) {
    // Reset is on the expander there and not needed; the backlight enable is too.
    setExpanderBit(TCA_EXIO_V2_BL_EN, true);
    if (expander_) {
      uint8_t config = 0xff;
      i2c_.i2c_read_buff(expander_, TCA_REG_CONFIG, &config, 1);
      config &= ~(1 << TCA_EXIO_V2_BL_EN);
      i2c_.i2c_write_buff(expander_, TCA_REG_CONFIG, &config, 1);
    }
    return;
  }
  // Pin and timing as in Waveshare's V1 demo (09_LVGL_V8_Test).
  const gpio_num_t rst = (gpio_num_t)V1_LCD_RST_PIN;
  digitalWrite(rst, HIGH);
  pinMode(rst, OUTPUT);
  gpio_set_level(rst, 1);
  vTaskDelay(pdMS_TO_TICKS(30));
  gpio_set_level(rst, 0);
  vTaskDelay(pdMS_TO_TICKS(250));
  gpio_set_level(rst, 1);
  vTaskDelay(pdMS_TO_TICKS(30));
  pinMode(rst, INPUT_PULLUP);  // let go: harmless on V1, and no fight with TE should a V2 ever be missed
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel_));
}

void Touch349Board::initBacklight()
{
  ledc_timer_config_t timer = {};
  timer.speed_mode = LEDC_LOW_SPEED_MODE;
  timer.duty_resolution = LEDC_TIMER_8_BIT;
  timer.timer_num = LEDC_TIMER_3;
  timer.freq_hz = 50 * 1000;
  timer.clk_cfg = LEDC_USE_RC_FAST_CLK;
  ledc_timer_config(&timer);

  ledc_channel_config_t channel = {};
  channel.gpio_num = v2_ ? V2_LCD_BL_PIN : V1_LCD_BL_PIN;
  channel.speed_mode = LEDC_LOW_SPEED_MODE;
  channel.channel = LEDC_CHANNEL_1;
  channel.intr_type = LEDC_INTR_DISABLE;
  channel.timer_sel = LEDC_TIMER_3;
  Preferences prefs;
  prefs.begin("rlcd", true);
  brightness_pct_ = constrain(prefs.getUChar("bright", brightness_pct_), BRIGHTNESS_MIN_PCT, 100);
  prefs.end();
  channel.duty = 256;  // dark (active low) until the first picture comes in with the power-on transition
  channel.hpoint = 0;
  ledc_channel_config(&channel);
}

void Touch349Board::setDuty(int pct)
{
  // The backlight driver is active low (as in Waveshare's demo): full duty (256 of 8 bits) is dark, 0 is full light.
  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, (100 - constrain(pct, 0, 100)) * 256 / 100);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
}

// Blocking glide with an ease-out, in 10 ms steps.
void Touch349Board::fadeBacklight(int from_pct, int to_pct, int ms)
{
  const int steps = max(1, ms / 10);
  for (int i = 1; i <= steps; i++) {
    float t = 1 - powf(1 - (float)i / steps, 3);
    setDuty(from_pct + (int)lroundf((to_pct - from_pct) * t));
    delay(10);
  }
}

// Shows the level overlay at once (unless the caller shows the level itself), then the light glides to the new level.
// With the screen off it only stores it.
void Touch349Board::setBrightness(int pct, bool show_level)
{
  int from = lightPct();
  dim_pct_ = -1;  // a hand on the brightness wins over night mode / low-battery dimming
  brightness_pct_ = constrain(pct, BRIGHTNESS_MIN_PCT, 100);
  if (display_on_) {
    if (show_level) {
      level_until_ms_ = millis() + LEVEL_SHOW_MS;
      render(now());
    }
    fadeBacklight(from, lightPct(), 90);
  }
  Preferences prefs;
  prefs.begin("rlcd", false);
  prefs.putUChar("bright", brightness_pct_);
  prefs.end();
}

// Dims (or ends the dimming) without saving: night mode and the low-battery prompt. Never below the panel's minimum.
void Touch349Board::setDim(int pct)
{
  int from = lightPct();
  dim_pct_ = pct < 0 ? -1 : constrain(pct, BRIGHTNESS_MIN_PCT, 100);
  if (display_on_) fadeBacklight(from, lightPct(), 400);
}

// One step along LEVELS from the nearest one; at either end the direction turns, so a dark room never jumps
// straight to full light.
void Touch349Board::stepBrightness()
{
  int i = 0;
  for (int k = 1; k < LEVEL_COUNT; k++) {
    if (abs(LEVELS[k] - brightness_pct_) < abs(LEVELS[i] - brightness_pct_)) {
      i = k;
    }
  }
  if (i + level_dir_ < 0 || i + level_dir_ >= LEVEL_COUNT) {
    level_dir_ = -level_dir_;
  }
  setBrightness(LEVELS[i + level_dir_]);
}

// Off: the picture collapses and the light goes out; the board keeps running (Wi-Fi, HTTP).
// On: the next render unfolds the current page.
void Touch349Board::setDisplayOn(bool on)
{
  if (on == display_on_) {
    return;
  }
  level_until_ms_ = 0;
  if (on) {
    setPowerLatch(true);  // PWR hold may have opened it
    display_on_ = true;
    unfold_next_ = true;
    state.dirty = true;
    return;
  }
  if (shown_ && anim_) {
    collapse(shown_);
  }
  fadeBacklight(lightPct(), 0, 100);
  setDuty(0);
  display_on_ = false;
}

// PWR held: dark screen, then the battery latch opens. On battery the board is off once PWR is let go; on USB it keeps
// running with the screen off and the next PWR tap wakes it.
void Touch349Board::powerOff()
{
  setDisplayOn(false);
  setPowerLatch(false);
}

static void fill565(uint16_t *buf, uint16_t color, int count)
{
  for (int i = 0; i < count; i++) {
    buf[i] = color;
  }
}

static float easeOutQuart(float t)
{
  float u = 1 - t;
  return 1 - u * u * u * u;
}

#define SQUEEZE_FRAMES 6
#define SHRINK_FRAMES 4
// The squeeze was timed with a 21 ms push; pushes are faster now, so animation frames are padded to keep its pace.
#define ANIM_PUSH_MS 21

void Touch349Board::pushAnim(const uint16_t *frame)
{
  uint32_t t0 = millis();
  push(frame);
  uint32_t spent = millis() - t0;
  if (spent < ANIM_PUSH_MS) {
    delay(ANIM_PUSH_MS - spent);
  }
}

// Frame k of 0..SQUEEZE_FRAMES: the picture squeezed to a band of rows around the middle, heating toward ink as it
// narrows (k = 0: untouched, last: a 2 px line).
static void drawSqueeze(uint16_t *dst, const uint16_t *src, int w, int h, float t, uint16_t bg, uint16_t ink)
{
  int band = max(2, (int)lroundf(h * (1 - t)));
  int glow = (int)(t * t * 256);
  int top = (h - band) / 2;
  fill565(dst, bg, w * h);
  for (int y = 0; y < band; y++) {
    const uint16_t *row = src + ((y * h) / band) * w;
    uint16_t *out = dst + (top + y) * w;
    for (int x = 0; x < w; x++) {
      out[x] = mix565(row[x], ink, glow);
    }
  }
}

// The 2 px line shortened to a given width, centered.
static void drawLine(uint16_t *dst, int w, int h, int line_w, uint16_t bg, uint16_t ink)
{
  fill565(dst, bg, w * h);
  int x0 = (w - line_w) / 2;
  for (int y = h / 2 - 1; y <= h / 2; y++) {
    fill565(dst + y * w + x0, ink, line_w);
  }
}

void Touch349Board::collapse(const uint16_t *from)
{
  for (int f = 1; f <= SQUEEZE_FRAMES; f++) {
    drawSqueeze(anim_, from, WIDTH, HEIGHT, easeOutQuart((float)f / SQUEEZE_FRAMES), BG, INK);
    pushAnim(anim_);
  }
  for (int f = 1; f <= SHRINK_FRAMES; f++) {
    float t = easeOutQuart((float)f / SHRINK_FRAMES);
    drawLine(anim_, WIDTH, HEIGHT, max(6, (int)lroundf(WIDTH * (1 - t))), BG, INK);
    pushAnim(anim_);
  }
}

// The reverse: a dot widens into a line while the light comes up, the line opens into the picture.
void Touch349Board::unfold(const uint16_t *to)
{
  drawLine(anim_, WIDTH, HEIGHT, 6, BG, INK);
  pushAnim(anim_);
  fadeBacklight(0, lightPct(), 60);
  for (int f = 1; f <= SHRINK_FRAMES; f++) {
    float t = easeOutQuart((float)f / SHRINK_FRAMES);
    drawLine(anim_, WIDTH, HEIGHT, max(6, (int)lroundf(WIDTH * t)), BG, INK);
    pushAnim(anim_);
  }
  for (int f = SQUEEZE_FRAMES - 1; f >= 1; f--) {
    drawSqueeze(anim_, to, WIDTH, HEIGHT, 1 - easeOutQuart(1 - (float)f / SQUEEZE_FRAMES), BG, INK);
    pushAnim(anim_);
  }
}

void Touch349Board::show(const uint16_t *landscape)
{
  if (!landscape) {
    return;
  }
  const uint16_t *out = landscape;
  if (level_until_ms_ && scratch_) {
    drawLevelOverlay(landscape);
    out = scratch_;
  }
  if (!display_on_) {
    return;
  }
  if (unfold_next_ && anim_) {
    unfold(out);
  } else if (unfold_next_) {
    setDuty(lightPct());
  }
  unfold_next_ = false;
  push(out);
  shown_ = out;
}

bool Touch349Board::snapshot(uint8_t *out)
{
  if (!shown_) {
    return false;
  }
  memcpy(out, shown_, frameBytes());
  return true;
}

// Landscape (x, y) maps to native (171 - y, x): the long edge of the bar runs down the native panel.
// Two DMA buffers take turns: the next chunk is rotated into one while the other is still on its way to the panel,
// so the rotation (about a third of the frame) hides behind the transfer.
void Touch349Board::push(const uint16_t *landscape)
{
  if (!panel_ || !landscape) {
    return;
  }
  while (xSemaphoreTake(flush_done_, 0) == pdTRUE) {
  }  // drop stale "done" counts from a frame that was cut short
  int pending = 0;
  for (int x0 = 0, n = 0; x0 < WIDTH; x0 += CHUNK_ROWS, n++) {
    uint16_t *buf = dma_[n & 1];
    if (pending == 2) {
      if (!waitFlush()) {
        return;  // a chunk never finished: drop this frame, the next one starts clean
      }
      pending--;  // the transfer that last used this buffer is done
    }
    // Read the source row by row (sequential in PSRAM) and scatter into the small internal buffer.
    for (int y = 0; y < HEIGHT; y++) {
      const uint16_t *src = landscape + y * WIDTH + x0;
      uint16_t *dst = buf + (NATIVE_W - 1 - y);
      for (int i = 0; i < CHUNK_ROWS; i++) {
        uint16_t px = src[i];
        dst[i * NATIVE_W] = SWAP_BYTES ? __builtin_bswap16(px) : px;
      }
    }
    esp_lcd_panel_draw_bitmap(panel_, 0, x0, NATIVE_W, x0 + CHUNK_ROWS, buf);
    pending++;
  }
  while (pending-- > 0) {
    if (!waitFlush()) {
      return;
    }
  }
}

// A chunk is 11 KB over a 40 MHz quad bus: milliseconds. Waiting forever for a lost "done" would freeze the whole
// board (HTTP included), so give up, and after repeated failures restart.
bool Touch349Board::waitFlush()
{
  if (xSemaphoreTake(flush_done_, pdMS_TO_TICKS(FLUSH_TIMEOUT_MS)) == pdTRUE) {
    flush_fails_ = 0;
    return true;
  }
  flush_fails_++;
  Serial.printf("display: flush timeout (%d)\n", flush_fails_);
  if (flush_fails_ >= FLUSH_MAX_FAILS) {
    Serial.println("display: stuck, restarting");
    delay(50);
    ESP.restart();
  }
  return false;
}

bool Touch349Board::acceptFrame(const uint8_t *buf, size_t len, uint8_t *out) const
{
  if (len != frameBytes()) {
    return false;
  }
  memcpy(out, buf, len);
  return true;
}

DateTime Touch349Board::now()
{
  rtcTimeStruct_t t;
  Rtc_GetTime(&t);
  return { t.year, t.month, t.day, t.hour, t.minute, t.second };
}

void Touch349Board::setTime(const DateTime &t)
{
  Rtc_SetTime(t.year, t.month, t.day, t.hour, t.minute, t.second);
}

Sensors Touch349Board::readSensors()
{
  Sensors s;  // no temperature/humidity sensor on this board
  Adc_Update();
  s.battery_v = Adc_GetSmoothedVoltage();
  s.battery_pct = Adc_GetBatteryLevel();
  s.charging = Adc_IsCharging();
  return s;
}

// True while a finger is on the glass.
bool Touch349Board::readTouch(int &x, int &y)
{
  if (!touch_) {
    return false;
  }
  uint8_t cmd[11] = { 0xb5, 0xab, 0xa5, 0x5a, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00 };
  uint8_t buf[32] = { 0 };
  if (touch_i2c_.i2c_master_write_read_dev(touch_, cmd, sizeof(cmd), buf, sizeof(buf)) != ESP_OK) {
    return false;
  }
  if (!(buf[1] > 0 && buf[1] < 5)) {  // number of touch points
    return false;
  }
  // The controller reports along the long edge first (0..639), then the short one (0..171), both mirrored against
  // the landscape strip (the same mapping as Waveshare's own demo, whose rotation matches push()).
  int along = ((buf[2] & 0x0f) << 8) | buf[3];
  int across = ((buf[4] & 0x0f) << 8) | buf[5];
  x = constrain(WIDTH - 1 - along, 0, WIDTH - 1);
  y = constrain(NATIVE_W - 1 - across, 0, HEIGHT - 1);
  return true;
}

// Touch down/up into gestures for the gadget: horizontal swipe, vertical swipe, long press (fires while held), tap.
void Touch349Board::trackGesture(bool down, int x, int y, uint32_t now_ms)
{
  if (down && !touching_) {
    touch_x0_ = touch_x_ = x;
    touch_y0_ = touch_y_ = y;
    touch_down_ms_ = now_ms;
    long_sent_ = false;
    Apps_Touch(true, x, y);
    down_used_ = Gadget_TouchDown(x, y);
  } else if (down) {
    touch_x_ = x;
    touch_y_ = y;
    int moved = abs(x - touch_x0_) + abs(y - touch_y0_);
    if (!long_sent_ && moved < 20 && now_ms - touch_down_ms_ >= LONG_PRESS_MS) {
      long_sent_ = true;
      Gadget_Gesture(Gesture::LongPress, touch_x0_, touch_y0_);
    }
  } else if (touching_) {
    Apps_Touch(false, touch_x_, touch_y_);
    int dx = touch_x_ - touch_x0_, dy = touch_y_ - touch_y0_;
    if (long_sent_ || down_used_) {
      // already acted (the hold, or the app at touch-down)
    } else if (abs(dx) >= SWIPE_MIN_PX && abs(dx) > abs(dy) * 6 / 5) {
      Gadget_Gesture(dx < 0 ? Gesture::SwipeLeft : Gesture::SwipeRight, touch_x0_, touch_y0_);
    } else if (abs(dy) >= VSWIPE_MIN_PX && abs(dy) > abs(dx)) {
      Gadget_Gesture(dy < 0 ? Gesture::SwipeUp : Gesture::SwipeDown, touch_x0_, touch_y0_);
    } else {
      Gadget_Gesture(Gesture::Tap, touch_x0_, touch_y0_);
    }
  }
  touching_ = down;
}

// PWR: tap = screen off/on, hold = power off. BOOT: tap = next brightness level (or wake), hold = Wi-Fi reset.
// Both act on release, so a hold never also counts as a tap. A PWR press still down from switching the board on is
// ignored until it is let go.
void Touch349Board::checkButtons(uint32_t now_ms)
{
  bool pwr = digitalRead(PWR_PIN) == LOW;
  if (!pwr_armed_) {
    pwr_armed_ = !pwr;
  } else if (pwr) {
    if (!pwr_down_ms_) {
      pwr_down_ms_ = now_ms;
      pwr_held_ = false;
    }
    if (!pwr_held_ && now_ms - pwr_down_ms_ >= POWER_OFF_HOLD_MS) {
      pwr_held_ = true;
      powerOff();
    }
  } else if (pwr_down_ms_) {
    bool tap = !pwr_held_ && now_ms - pwr_down_ms_ > BUTTON_DEBOUNCE_MS;
    pwr_down_ms_ = 0;
    if (tap && Gadget_ButtonPressed()) {
      setDisplayOn(true);
    } else if (tap) {
      setDisplayOn(!display_on_);
    }
  }

  bool boot = digitalRead(BOOT_PIN) == LOW;
  if (boot) {
    if (!boot_down_ms_) {
      boot_down_ms_ = now_ms;
      boot_held_ = false;
    }
    if (!boot_held_ && now_ms - boot_down_ms_ >= WIFI_RESET_HOLD_MS) {
      boot_held_ = true;
      setDisplayOn(true);
      showNotice(T("Resetting Wi-Fi"), T("The setup network will open"));
      delay(1500);
      Net_ResetWifi();
    }
  } else if (boot_down_ms_) {
    bool tap = !boot_held_ && now_ms - boot_down_ms_ > BUTTON_DEBOUNCE_MS;
    boot_down_ms_ = 0;
    if (tap && Gadget_ButtonPressed()) {
      // stopped the alarm
    } else if (tap && display_on_) {
      stepBrightness();
    } else if (tap) {
      setDisplayOn(true);
    }
  }
}

// A tap on the glass is reported as a short key press; with the screen off the glass does nothing.
bool Touch349Board::keyDown()
{
  uint32_t now_ms = millis();
  checkButtons(now_ms);
  if (level_until_ms_ && (int32_t)(now_ms - level_until_ms_) > 0) {
    level_until_ms_ = 0;
    state.dirty = true;  // overlay gone: redraw what is under it
  }
  if (now_ms - last_poll_ms_ >= TOUCH_POLL_MS) {
    last_poll_ms_ = now_ms;
    int x = 0, y = 0;
    bool down = readTouch(x, y);
    if (!display_on_) {
      if (down && !touching_) {
        setDisplayOn(true);  // a touch wakes the dark screen, and does nothing else
      }
      touching_ = down;
      touch_x0_ = touch_x_ = -1000;  // the release must not turn into a gesture
    } else if (touch_x0_ == -1000) {
      touching_ = down;
      if (!down) touch_x0_ = 0;
    } else {
      trackGesture(down, x, y, now_ms);
    }
  }
  return false;  // the gadget handles touches itself
}

#endif  // BOARD_TOUCH349
