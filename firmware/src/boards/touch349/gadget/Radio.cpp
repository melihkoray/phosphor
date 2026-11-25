#ifdef BOARD_TOUCH349

#include "Radio.h"
#include <Audio.h>
#include <math.h>

// Waveshare ESP32-S3-Touch-LCD-3.49 audio: ES8311 DAC on the main I2C bus, I2S pins as in the vendor demo.
#define I2S_MCLK_PIN 7
#define I2S_BCLK_PIN 15
#define I2S_LRC_PIN 46
#define I2S_DOUT_PIN 45
#define ES8311_ADDR 0x18
#define CODEC_VOLUME 80      // DAC level; the stream volume does the rest (80 = loud, clean on this speaker)

#define STALL_RECONNECT_MS 9000
#define MAX_RETRIES 3
#define AMP_OFF_AFTER_MS 2000

static I2cMasterBus *bus = nullptr;
static i2c_master_dev_handle_t codec = nullptr;
static void (*amp)(bool) = nullptr;
static Audio *audio = nullptr;
static volatile bool ready = false;

static volatile bool cmd_pending = false;
static volatile bool want_on = false;
static volatile int want_station = 3;
static volatile int station = 3;
static char want_url[256] = "", url_cur[256] = "";  // set by the UI thread, copied by the radio task
static volatile RadioState rstate = RadioState::Off;
static volatile int volume_pct = 60;
static volatile uint32_t ramp_start_ms = 0, ramp_ms = 0;

static SemaphoreHandle_t title_mx = nullptr;
static char title[160] = "";

// ---------- ES8311 ----------

static bool codecWrite(uint8_t reg, uint8_t val) { return bus->i2c_write_buff(codec, reg, &val, 1) == ESP_OK; }

static uint8_t codecRead(uint8_t reg)
{
  uint8_t val = 0;
  bus->i2c_read_buff(codec, reg, &val, 1);
  return val;
}

// Same register sequence as the vendor driver, fixed at 48 kHz / 16 bit with MCLK = 256 fs.
static bool codecBegin()
{
  i2c_device_config_t cfg = {};
  cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  cfg.device_address = ES8311_ADDR;
  cfg.scl_speed_hz = 300000;
  if (i2c_master_bus_add_device(bus->Get_I2cBusHandle(), &cfg, &codec) != ESP_OK) {
    return false;
  }
  bool ok = codecWrite(0x00, 0x1F);  // reset
  delay(20);
  ok &= codecWrite(0x00, 0x00);
  ok &= codecWrite(0x00, 0x80);  // power on
  ok &= codecWrite(0x01, 0x3F);  // all clocks
  ok &= codecWrite(0x06, codecRead(0x06) & ~(1 << 5));
  // 12.288 MHz MCLK, 48 kHz: pre_div 1, x1, adc/dac div 1, single speed, LRCK 256, BCLK div 4, OSR 0x10
  ok &= codecWrite(0x02, codecRead(0x02) & 0x07);
  ok &= codecWrite(0x03, 0x10);
  ok &= codecWrite(0x04, 0x10);
  ok &= codecWrite(0x05, 0x00);
  ok &= codecWrite(0x06, (codecRead(0x06) & 0xE0) | 3);
  ok &= codecWrite(0x07, codecRead(0x07) & 0xC0);
  ok &= codecWrite(0x08, 0xFF);
  ok &= codecWrite(0x09, codecRead(0x09) | (3 << 2));  // 16 bit in
  ok &= codecWrite(0x0A, codecRead(0x0A) | (3 << 2));  // 16 bit out
  ok &= codecWrite(0x0D, 0x01);  // analog power
  ok &= codecWrite(0x0E, 0x02);
  ok &= codecWrite(0x12, 0x00);  // DAC on
  ok &= codecWrite(0x13, 0x10);  // output driver
  ok &= codecWrite(0x1C, 0x6A);
  ok &= codecWrite(0x37, 0x08);  // DAC EQ bypass
  ok &= codecWrite(0x32, CODEC_VOLUME * 256 / 100 - 1);
  return ok;
}

// ---------- spectrum ----------

struct Biquad {
  float b0, b2, a1, a2, z1, z2;  // band-pass: b1 = 0, b2 = -b0
};

static Biquad bands[RADIO_BANDS];
static float energy[RADIO_BANDS];
static int energy_n = 0;
static float ref_db = 40;
static volatile uint8_t levels[RADIO_BANDS];
static int16_t decim_prev = 0;
static bool decim_odd = false;

// Beat tracking on the same blocks: spectral flux (rises in band energy, bass weighted) is the onset curve; its
// autocorrelation over the last ~5.5 s gives the tempo; a phase counter steps once per beat and onsets pull it in.
static constexpr float BLOCK_HZ = 24000.0f / 1024;  // ~23.4 blocks a second
static constexpr int ENV_N = 128, LAG_MIN = 7, LAG_MAX = 26;  // 200 .. 54 BPM
static float env[ENV_N];
static int env_i = 0, env_count = 0;
static float prev_db[RADIO_BANDS];
static float flux_mean = 0, flux_var = 0, lag_f = 11;
static float beat_conf = 0, beat_phase = 0;
static volatile float beat_bpm = 0, beat_phase_out = 0, beat_strength = 0;
static volatile uint32_t beat_count = 0, beat_block_ms = 0;
static volatile bool beat_on = false;

// Stereo loudness (VU) and a short trace of the wave (oscilloscope), both auto-gained like the bands.
static float vu_sum[2];
static volatile float vu_out[2];
static int16_t trace_buf[RADIO_TRACE];
static volatile int8_t trace_out[RADIO_TRACE];
static int trace_n = 0;
static float vu_ref = 2000, trace_ref = 2000;

static void tempoUpdate()
{
  // mean-removed autocorrelation of the onset curve; a soft preference around 120 BPM keeps octave jumps away
  float mean = 0;
  for (int i = 0; i < ENV_N; i++) mean += env[i];
  mean /= ENV_N;
  float r0 = 0;
  for (int i = 0; i < ENV_N; i++) r0 += (env[i] - mean) * (env[i] - mean);
  if (r0 < 1e-3f) {
    beat_conf *= 0.8f;
    return;
  }
  int best = 0;
  float best_v = 0, best_r = 0;
  static float rs[LAG_MAX + 1], vs[LAG_MAX + 1];
  for (int lag = LAG_MIN; lag <= LAG_MAX; lag++) {
    float r = 0;
    for (int i = 0; i + lag < ENV_N; i++) {
      int a = (env_i + i) % ENV_N, b = (env_i + i + lag) % ENV_N;
      r += (env[a] - mean) * (env[b] - mean);
    }
    r /= r0;
    float oct = log2f((60 * BLOCK_HZ / lag) / 105.0f);
    float v = r * expf(-oct * oct / (2 * 0.6f * 0.6f));
    rs[lag] = r;
    vs[lag] = v;
    if (v > best_v) {
      best_v = v;
      best_r = r;
      best = lag;
    }
  }
  if (!best) {
    beat_conf *= 0.8f;
    return;
  }
  // a song does not change speed by an octave: stay on the current tempo while it still scores close to the best
  int cur = constrain((int)lroundf(lag_f), LAG_MIN, LAG_MAX);
  float ratio = (float)best / cur;
  if (cur != best && (fabsf(ratio - 2) < 0.25f || fabsf(ratio - 0.5f) < 0.07f) && vs[cur] >= 0.7f * best_v) {
    best = cur;
    best_r = rs[cur];
  }
  // parabolic peak for a finer lag, then glide toward it (a jump of more than ~15% resets the glide)
  float lag = best;
  if (best > LAG_MIN && best < LAG_MAX) {
    float rm = 0, rp = 0;
    for (int i = 0; i + best + 1 < ENV_N; i++) {
      int a = (env_i + i) % ENV_N;
      rm += (env[a] - mean) * (env[(env_i + i + best - 1) % ENV_N] - mean);
      rp += (env[a] - mean) * (env[(env_i + i + best + 1) % ENV_N] - mean);
    }
    float rc = best_r * r0, den = rm - 2 * rc + rp;
    if (den < 0) lag += constrain(0.5f * (rm - rp) / den, -0.5f, 0.5f);
  }
  lag_f = fabsf(lag - lag_f) > lag_f * 0.15f ? lag : lag_f * 0.7f + lag * 0.3f;
  beat_conf = beat_conf * 0.6f + constrain(best_r, 0.0f, 1.0f) * 0.4f;
}

static void beatBlock(const float *db, float top)
{
  float flux = 0;
  for (int i = 0; i < RADIO_BANDS; i++) {
    float d = db[i] - prev_db[i];
    if (d > 0) flux += i < 5 ? 2 * d : d;
    prev_db[i] = db[i];
  }
  env[env_i] = flux;
  env_i = (env_i + 1) % ENV_N;
  if (env_count < ENV_N) env_count++;
  if (env_count == ENV_N && env_i % 12 == 0) tempoUpdate();  // twice a second

  float dev = flux - flux_mean;
  flux_mean += dev * 0.05f;
  flux_var += (dev * dev - flux_var) * 0.05f;
  bool onset = dev > 1.5f * sqrtf(flux_var) && flux > 2;

  beat_phase += 1.0f / lag_f;
  if (onset && beat_conf > 0.15f) {  // pull the phase toward the onset, harder when it lands near a beat
    float err = beat_phase < 0.5f ? -beat_phase : 1 - beat_phase;
    beat_phase += err * (fabsf(err) < 0.2f ? 0.35f : 0.1f);
  }
  if (beat_phase >= 1) {
    beat_phase -= 1;
    beat_count = beat_count + 1;
  } else if (beat_phase < 0) {
    beat_phase += 1;
  }
  bool loud = top > 25;
  beat_on = loud && beat_conf > 0.12f && env_count == ENV_N;
  beat_bpm = 60 * BLOCK_HZ / lag_f;
  beat_strength = constrain(beat_conf * 2, 0.0f, 1.0f);
  beat_phase_out = beat_phase;
  beat_block_ms = millis();
}

static void spectrumInit()
{
  const float fs = 24000, q = 1.8f;
  for (int i = 0; i < RADIO_BANDS; i++) {
    float f = 55.0f * powf(11000.0f / 55.0f, (float)i / (RADIO_BANDS - 1));
    float w0 = 2 * PI * f / fs, alpha = sinf(w0) / (2 * q), a0 = 1 + alpha;
    bands[i] = { alpha / a0, -alpha / a0, -2 * cosf(w0) / a0, (1 - alpha) / a0, 0, 0 };
  }
}

static void spectrumFeed(const int16_t *stereo, int frames)
{
  for (int n = 0; n < frames; n++) {
    int16_t mono = (stereo[2 * n] + stereo[2 * n + 1]) / 2;
    vu_sum[0] += (float)stereo[2 * n] * stereo[2 * n];
    vu_sum[1] += (float)stereo[2 * n + 1] * stereo[2 * n + 1];
    decim_odd = !decim_odd;
    if (decim_odd) {
      decim_prev = mono;
      continue;
    }
    float x = (mono + decim_prev) * 0.5f;
    if (trace_n < RADIO_TRACE * 2) {  // every other decimated sample: ~10 ms of wave at the start of each block
      if (!(trace_n & 1)) trace_buf[trace_n / 2] = (int16_t)x;
      trace_n++;
    }
    for (int i = 0; i < RADIO_BANDS; i++) {
      Biquad &b = bands[i];
      float y = b.b0 * x + b.z1;
      b.z1 = -b.a1 * y + b.z2;
      b.z2 = b.b2 * x - b.a2 * y;
      energy[i] += y * y;
    }
    if (++energy_n >= 1024) {  // ~43 ms
      float db[RADIO_BANDS], top = 0;
      for (int i = 0; i < RADIO_BANDS; i++) {
        db[i] = 10 * log10f(energy[i] / energy_n + 1) + i * 0.9f;  // tilt: treble carries less energy
        top = max(top, db[i]);
        energy[i] = 0;
      }
      ref_db = top > ref_db ? top : ref_db - 0.08f;  // auto gain: the loudest band sits near the top
      for (int i = 0; i < RADIO_BANDS; i++) {
        levels[i] = (uint8_t)constrain((int)((db[i] - (ref_db - 30)) / 3.0f + 0.5f), 0, 10);
      }
      beatBlock(db, top);

      // VU: RMS per channel against a slowly falling reference, so the needles sit around 70% on a steady song
      float rms[2], peak = 0;
      for (int c = 0; c < 2; c++) {
        rms[c] = sqrtf(vu_sum[c] / (energy_n * 2));
        peak = max(peak, rms[c]);
        vu_sum[c] = 0;
      }
      // the reference follows the louder blocks of the last few seconds; 0 dB on the meter sits a little above it
      vu_ref = peak > vu_ref ? vu_ref * 0.9f + peak * 0.1f : max(vu_ref * 0.995f, 1.0f);
      for (int c = 0; c < 2; c++) vu_out[c] = constrain(rms[c] / (vu_ref * 1.25f), 0.0f, 1.0f);

      int16_t tmax = 1;
      for (int i = 0; i < RADIO_TRACE; i++) tmax = max(tmax, (int16_t)abs(trace_buf[i]));
      trace_ref = tmax > trace_ref ? trace_ref * 0.7f + tmax * 0.3f : max(trace_ref * 0.99f, 2.0f);
      for (int i = 0; i < RADIO_TRACE; i++) trace_out[i] = (int8_t)constrain((int)(trace_buf[i] * 90 / trace_ref), -100, 100);
      trace_n = 0;
      energy_n = 0;
    }
  }
}

// ---------- beeps ----------

static portMUX_TYPE beep_mux = portMUX_INITIALIZER_UNLOCKED;
static int beep_hz = 0, beep_on_frames = 0, beep_gap_frames = 0, beep_left = 0;  // beeps still to start
static int beep_pos = 0;           // frames into the current beep (negative = still in the gap before it)
static float beep_phase = 0;
static volatile uint32_t last_sound_ms = 0;

static bool beepActive()
{
  portENTER_CRITICAL(&beep_mux);
  bool on = beep_left > 0 || beep_pos < beep_on_frames;
  portEXIT_CRITICAL(&beep_mux);
  return on && beep_hz > 0;
}

// Adds the beep to (or writes it into) 48 kHz stereo frames. Works on a copy of the beep state so interrupts stay on
// while the samples are computed; a Radio_Beep in the meantime wins (generation check).
static uint32_t beep_gen = 0;

static void beepMix(int16_t *stereo, int frames, bool replace)
{
  portENTER_CRITICAL(&beep_mux);
  int hz = beep_hz, on = beep_on_frames, gap = beep_gap_frames, left = beep_left, pos = beep_pos;
  float phase = beep_phase;
  uint32_t gen = beep_gen;
  portEXIT_CRITICAL(&beep_mux);

  // follows the volume on the same square-law feel as the stream, so a quiet night stays quiet
  float v = volume_pct / 100.0f;
  float level = max(40.0f, 9000 * v * v);
  float step = 2 * PI * hz / 48000.0f;
  for (int n = 0; n < frames; n++) {
    float s = 0;
    if (pos >= 0 && pos < on) {
      int edge = min(pos, on - 1 - pos);  // short fade in/out against clicks
      float env = edge < 240 ? edge / 240.0f : 1;
      s = sinf(phase) * level * env;
      phase += step;
      if (phase > 2 * PI) {
        phase -= 2 * PI;
      }
    }
    pos++;
    if (pos >= on && left > 0) {
      left--;
      pos = -gap;
    }
    for (int c = 0; c < 2; c++) {
      int v = (replace ? 0 : stereo[2 * n + c]) + (int)s;
      stereo[2 * n + c] = (int16_t)constrain(v, -32768, 32767);
    }
  }

  portENTER_CRITICAL(&beep_mux);
  if (gen == beep_gen) {
    beep_left = left;
    beep_pos = pos;
    beep_phase = phase;
  }
  portEXIT_CRITICAL(&beep_mux);
}

void Radio_Beep(int hz, int ms, int count, int gap_ms)
{
  portENTER_CRITICAL(&beep_mux);
  beep_hz = hz;
  beep_on_frames = ms * 48;
  beep_gap_frames = gap_ms * 48;
  beep_left = max(0, count - 1);
  beep_pos = 0;
  beep_phase = 0;
  beep_gen++;
  portEXIT_CRITICAL(&beep_mux);
}

// Called by the audio library with every decoded block (48 kHz stereo, after volume).
void audio_process_i2s(int16_t *outBuff, int32_t validSamples, bool *continueI2S)
{
  spectrumFeed(outBuff, validSamples);
  if (beepActive()) {
    beepMix(outBuff, validSamples, false);
  }
  *continueI2S = true;
}

// ---------- stream ----------

static void applyVolume()
{
  int v = volume_pct;
  if (ramp_ms) {
    uint32_t t = millis() - ramp_start_ms;
    if (t >= ramp_ms) {
      ramp_ms = 0;
    } else {
      v = 4 + (v - 4) * (int)t / (int)ramp_ms;
    }
  }
  static int last = -1;
  if (v != last) {
    audio->setVolume(v);
    last = v;
  }
}

static void connect(int idx)
{
  if (idx >= 0) station = idx;
  rstate = RadioState::Connecting;
  xSemaphoreTake(title_mx, portMAX_DELAY);
  title[0] = 0;
  xSemaphoreGive(title_mx);
  amp(true);
#if CORE_DEBUG_LEVEL >= 3
  Serial.printf("radio: connect, internal free=%u largest=%u\n",
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
#endif
  audio->connecttohost(url_cur);
}

static void radioTask(void *)
{
  uint32_t last_ok_ms = 0;
  int retries = 0;
  bool amp_on = false;
  static int16_t beep_buf[480 * 2];
  for (;;) {
    if (cmd_pending) {
      cmd_pending = false;
      if (want_on) {
        retries = 0;
        last_ok_ms = millis();
        strlcpy(url_cur, want_url, sizeof(url_cur));
        connect(want_station);
      } else {
        audio->stopSong();
        rstate = RadioState::Off;
      }
    }
    bool streaming = rstate == RadioState::Connecting || rstate == RadioState::Playing;
    if (streaming) {
      applyVolume();
      audio->loop();
      if (audio->isRunning()) {
        if (rstate == RadioState::Connecting) {
          rstate = RadioState::Playing;
        }
        last_ok_ms = millis();
        last_sound_ms = millis();
        retries = 0;
      } else if (millis() - last_ok_ms > STALL_RECONNECT_MS) {
        if (retries++ < MAX_RETRIES) {
          last_ok_ms = millis();
          connect(station);
        } else {
          audio->stopSong();
          rstate = RadioState::Failed;
        }
      }
    }
    if (beepActive() && !audio->isRunning()) {
      if (!amp_on) {
        amp(true);
        amp_on = true;
        delay(30);
      }
      beepMix(beep_buf, 480, true);
      size_t written = 0;
      i2s_channel_write(audio->getTxHandle(), beep_buf, sizeof(beep_buf), &written, 100);
      last_sound_ms = millis();
      continue;
    }
    bool sounding = streaming || millis() - last_sound_ms < AMP_OFF_AFTER_MS;
    if (sounding != amp_on) {
      amp(sounding);
      amp_on = sounding;
    }
    delay(streaming ? 1 : 15);
  }
}

void Radio_Begin(I2cMasterBus *i2c, void (*amp_fn)(bool))
{
  bus = i2c;
  amp = amp_fn;
  title_mx = xSemaphoreCreateMutex();
  spectrumInit();
  if (!codecBegin()) {
    Serial.println("radio: ES8311 not answering");
  }
  Audio::audio_info_callback = [](Audio::msg_t m) {
    if (m.e == Audio::evt_log && m.msg) Serial.printf("radio: %s\n", m.msg);
#if CORE_DEBUG_LEVEL >= 3
    if (m.e == Audio::evt_info && m.msg) Serial.printf("radio: %s\n", m.msg);
#endif
    if (m.e == Audio::evt_streamtitle && m.msg) {
      xSemaphoreTake(title_mx, portMAX_DELAY);
      strlcpy(title, m.msg, sizeof(title));
      xSemaphoreGive(title_mx);
    }
  };
  // This board only plays audio. RX alone would reserve ~32 KiB of internal DMA RAM needed by TLS.
  audio = new Audio(0, false);
  // No setAudioTaskCore(0) here: the constructor already starts the audio task on core 0. The call stops and restarts it, and
  // both tasks share one static TCB and stack, so a delete that was still pending when the new task was created corrupted
  // the scheduler lists (IDLE0 stack canary / uxListRemove panic at boot, depending on layout and timing).
  if (!audio->setPinout(I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DOUT_PIN, I2S_MCLK_PIN)) {
    Serial.println("radio: I2S pinout failed");
  }
  audio->forceMono(true);  // one speaker
  audio->setConnectionTimeout(3000, 6000);
  audio->setVolumeSteps(100);
  audio->setVolume(volume_pct);
  xTaskCreatePinnedToCore(radioTask, "radio", 8192, nullptr, 3, nullptr, 0);
  ready = true;
  Serial.println("radio: ready");
}

bool Radio_Ready() { return ready; }

void Radio_Play(int idx)
{
  if (idx < 0 || idx >= settings::stationCount()) {
    return;
  }
  strlcpy(want_url, settings::station(idx).url.c_str(), sizeof(want_url));
  want_station = idx;
  station = idx;
  want_on = true;
  cmd_pending = true;
}

void Radio_PlayUrl(const char *url)
{
  strlcpy(want_url, url, sizeof(want_url));
  want_station = -1;
  want_on = true;
  cmd_pending = true;
}

void Radio_Stop()
{
  want_on = false;
  ramp_ms = 0;
  cmd_pending = true;
}

void Radio_Toggle()
{
  if (Radio_On()) {
    Radio_Stop();
  } else {
    Radio_Play(station);
  }
}

RadioState Radio_State() { return cmd_pending ? (want_on ? RadioState::Connecting : RadioState::Off) : (RadioState)rstate; }
bool Radio_On() { return want_on; }
int Radio_Station() { return station; }

String Radio_Title()
{
  if (!title_mx) {
    return String();
  }
  xSemaphoreTake(title_mx, portMAX_DELAY);
  String t = title;
  xSemaphoreGive(title_mx);
  t.trim();
  return t;
}

int Radio_Volume() { return volume_pct; }

void Radio_SetVolume(int pct)
{
  volume_pct = constrain(pct, 0, 100);
  ramp_ms = 0;
}

void Radio_RampVolume(uint32_t ms)
{
  ramp_start_ms = millis();
  ramp_ms = ms;
}

void Radio_Levels(uint8_t out[RADIO_BANDS])
{
  bool live = rstate == RadioState::Playing;
  for (int i = 0; i < RADIO_BANDS; i++) {
    out[i] = live ? levels[i] : 0;
  }
}

RadioBeat Radio_Beat()
{
  RadioBeat b{};
  bool live = rstate == RadioState::Playing && millis() - beat_block_ms < 300;
  b.on = live && beat_on;
  b.bpm = beat_bpm;
  b.strength = live ? beat_strength : 0;
  b.count = beat_count;
  // the tracker runs per block (~43 ms); carry the phase forward to now so the dancers move smoothly
  float per_ms = beat_bpm / 60000.0f;
  float ph = beat_phase_out + (millis() - beat_block_ms) * per_ms;
  b.phase = ph - floorf(ph);
  if (ph >= 1) b.count += (uint32_t)ph;
  return b;
}

void Radio_Vu(float &left, float &right)
{
  bool live = rstate == RadioState::Playing;
  left = live ? vu_out[0] : 0;
  right = live ? vu_out[1] : 0;
}

void Radio_Trace(int8_t out[RADIO_TRACE])
{
  bool live = rstate == RadioState::Playing;
  for (int i = 0; i < RADIO_TRACE; i++) out[i] = live ? trace_out[i] : 0;
}

#endif
