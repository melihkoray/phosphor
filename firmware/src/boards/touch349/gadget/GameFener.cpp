#ifdef BOARD_TOUCH349

#include "GameFener.h"
#include <Preferences.h>
#include <esp_random.h>
#include "../../../core/state.h"
#include "Radio.h"
#include "Settings.h"
#include "GameFenerArt.h"
#include "Px.h"
#include "Vfd.h"

using namespace vfd;

// Fener Bekçisi. A Game & Watch style LCD game: fixed segments, every one drawn every frame (unlit = ghost, lit = lit),
// ships walk one position left per tick, the keeper lights the lane a ship is about to hit. Original art and names;
// from Nintendo's games only the genre rules are used. The fairness rules and every number are in design/fener-bekcisi.md
// and were measured with design/fener-bekcisi-mc.py; step() keeps the order of that script on purpose.

// ---------------------------------------------------------------------------------------------------------------------
// Tuning: every number that changes how the game plays
// ---------------------------------------------------------------------------------------------------------------------

namespace FenerTuning {
struct Pace {
  float t0;      // tick (ms) at score 0
  float per_h;   // ms less per hundred points
  float per_w;   // ms less per point inside the hundred
  float t_min;   // fastest tick
  float p0;      // spawn chance per tick at score 0
  float per_h_p; // more chance per hundred points
  uint8_t max_lo, max_hi;  // ships on screen below / from 500 points
};
constexpr Pace MODES[2] = {
  { 650, 30, 0.9f, 320, 0.38f, 0.02f, 3, 4 },  // Game A
  { 520, 25, 0.7f, 260, 0.48f, 0.02f, 4, 5 },  // Game B
};
constexpr int POSITIONS = 5;          // P5 (spawn, right) .. P1 (danger, in front of the rocks)
constexpr int SPAWN_POS = POSITIONS + 1;  // a ship is born one tick before it shows at P5 (as in the Monte Carlo script)
constexpr int GAP_POS = POSITIONS - 1;    // a lane takes a new ship only when P4 and P5 (and the birth place) are empty
constexpr int FAST_TICK_MS = 400;     // below this tick a second ship may only follow in the lane of the previous one
constexpr int MAX_MISS = 3;
constexpr int WRAP = 1000;            // the score shows 3 digits and rolls over
constexpr int MIN_HUNDRED_AFTER_WRAP = 5;  // after 999 the speed keeps at least the 500 level
constexpr int BONUS_AT[2] = { 300, 700 };  // misses are cleared here
constexpr int BONUS_SAVES = 20;       // double points for this many saves when there was nothing to clear
constexpr int FAST_SHIPS_FROM = 500;  // the second ship limit starts here
constexpr uint32_t READY_MS = 1400;       // 2 beeps, then go
constexpr uint32_t CRASH_FREEZE_MS = 1200;
constexpr uint32_t OVER_BLINK_MS = 1200;  // all segments blink 3 times
constexpr uint32_t OVER_TOTAL_MS = 2600;  // then the title screen
constexpr uint32_t AUTO_PAUSE_MS = 30000;
constexpr uint32_t RESUME_DELAY_MS = 700; // first tick after a pause comes at least this late
constexpr int LANE_SPLIT_Y[2] = { 69, 117 };  // y below which a touch belongs to lane 0 / lane 1, else lane 2
}  // namespace FenerTuning
using namespace FenerTuning;

// ---------------------------------------------------------------------------------------------------------------------
// Menu icon (each '#' is an s x s block)
// ---------------------------------------------------------------------------------------------------------------------

static const char *const ICON[] = { "............", "...#.##.#...", "....####....", "....#..#....", "....####....", ".....##.....",
                                    "....####....", ".....##.....", "....####....", "...######...", "...#....#...", "..########.." };
template <size_t N> static constexpr int rowsOf(const char *const (&)[N]) { return (int)N; }

static constexpr int LANES = 3;

// ---------------------------------------------------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------------------------------------------------

enum class Ph : uint8_t { Title, Ready, Run, Crash, Over, Pause };
struct Ship {
  int8_t lane, pos;  // pos 1..5 shown at P1..P5, SPAWN_POS not yet shown
};

static Ph ph = Ph::Title;
static int game = 0;  // 0 = Game A, 1 = Game B
static int score, miss, keeper, bonus_left;
static uint16_t hi[2];
static bool tick_snd = true, new_hi = false;
static Ship ships[8];
static int nships;
static bool turned[LANES], wreck[LANES];
static uint8_t wave_phase;
static int8_t last_spawn_lane;
static uint32_t next_tick, ph_t0, crash_until, last_input, pause_rem;

static Preferences prefs;
static bool prefs_open = false;

static void loadPrefs()
{
  if (!prefs_open) prefs_open = prefs.begin("play", false);
  hi[0] = prefs.getUShort("fener_hiA", 0);
  hi[1] = prefs.getUShort("fener_hiB", 0);
  tick_snd = prefs.getBool("fener_tick", true);
}

// ---------------------------------------------------------------------------------------------------------------------
// Sound: Radio_Beep plays one tone at a time, so melodies are a small queue stepped from tick()
// ---------------------------------------------------------------------------------------------------------------------

struct Note {
  uint16_t hz, ms;  // hz 0 = rest
};
static Note sq[8];
static int sq_n = 0, sq_i = 0;
static uint32_t sq_next = 0;

static void playSeq(const Note *n, int count)
{
  count = min(count, 8);
  for (int i = 0; i < count; i++) sq[i] = n[i];
  sq_n = count;
  sq_i = 0;
  sq_next = 0;
}
static bool soundBusy(uint32_t now) { return sq_i < sq_n || (int32_t)(now - sq_next) < 0; }
static void soundTick(uint32_t now)
{
  if (sq_i >= sq_n || (int32_t)(now - sq_next) < 0) return;
  const Note &n = sq[sq_i++];
  if (n.hz) Radio_Beep(n.hz, n.ms);
  sq_next = now + n.ms + 15;
}

static const Note SND_SAVE[] = { { 2000, 40 } };
static const Note SND_CRASH[] = { { 800, 90 }, { 500, 90 }, { 300, 90 } };
static const Note SND_BONUS[] = { { 1000, 70 }, { 1500, 70 }, { 2000, 90 } };
static const Note SND_OVER[] = { { 400, 600 } };
static const Note SND_HI[] = { { 400, 600 }, { 0, 100 }, { 2400, 70 }, { 0, 20 }, { 2400, 70 }, { 0, 20 }, { 2400, 70 } };

// ---------------------------------------------------------------------------------------------------------------------
// Rules
// ---------------------------------------------------------------------------------------------------------------------

static float rnd() { return (esp_random() >> 8) * (1.0f / 16777216.0f); }

static float tickMs()
{
  const Pace &m = MODES[game];
  int v = score % WRAP, h = v / 100, w = v % 100;
  if (score >= WRAP) h = max(h, MIN_HUNDRED_AFTER_WRAP);
  return max(m.t_min, m.t0 - m.per_h * h - m.per_w * w);
}

static void startGame(int g)
{
  game = g;
  score = miss = bonus_left = 0;
  keeper = 1;
  nships = 0;
  memset(turned, 0, sizeof(turned));
  memset(wreck, 0, sizeof(wreck));
  wave_phase = 0;
  last_spawn_lane = -1;
  new_hi = false;
  ph = Ph::Ready;
  ph_t0 = millis();
  last_input = ph_t0;
  Radio_Beep(1760, 50, 2, 90);
  sq_n = sq_i = 0;
  sq_next = ph_t0 + 260;
  state.dirty = true;
}

static void endGame(uint32_t now)
{
  ph = Ph::Over;
  ph_t0 = now;
  new_hi = score > hi[game];
  if (new_hi) {
    hi[game] = (uint16_t)min(score, 65535);
    if (!prefs_open) prefs_open = prefs.begin("play", false);
    prefs.putUShort(game ? "fener_hiB" : "fener_hiA", hi[game]);
    playSeq(SND_HI, sizeof(SND_HI) / sizeof(SND_HI[0]));
  } else {
    playSeq(SND_OVER, 1);
  }
  state.dirty = true;
}

// One tick. Same order as design/fener-bekcisi-mc.py: everybody one step left, the ship that reaches the rocks is
// settled, then at most one new ship is born.
static void step(uint32_t now)
{
  memset(turned, 0, sizeof(turned));
  memset(wreck, 0, sizeof(wreck));
  for (int i = 0; i < nships; i++) ships[i].pos--;
  bool crashed = false, over = false, sound = false;
  for (int i = nships - 1; i >= 0; i--) {
    if (ships[i].pos != 0) continue;
    int lane = ships[i].lane;
    ships[i] = ships[--nships];
    sound = true;
    if (lane == keeper) {
      int pts = bonus_left > 0 ? 2 : 1;
      if (bonus_left > 0) bonus_left--;
      int before = score % WRAP;
      score += pts;
      turned[lane] = true;
      playSeq(SND_SAVE, 1);
      for (int b = 0; b < 2; b++) {
        if (before < BONUS_AT[b] && score % WRAP >= BONUS_AT[b]) {
          if (miss > 0) miss = 0;
          else bonus_left = BONUS_SAVES;
          playSeq(SND_BONUS, 3);
        }
      }
    } else {
      wreck[lane] = true;
      miss++;
      crashed = true;
      playSeq(SND_CRASH, 3);
      if (miss >= MAX_MISS) over = true;
    }
  }
  wave_phase ^= 1;
  if (over) {
    endGame(now);
    return;
  }

  // spawn: one per tick; the lane needs P5 and P4 free; right after a spawn and at a fast tick only the same lane
  const Pace &m = MODES[game];
  int v = score % WRAP;
  int nmax = v >= FAST_SHIPS_FROM ? m.max_hi : m.max_lo;
  float p = m.p0 + m.per_h_p * (v / 100);
  float T = tickMs();
  bool spawned = false;
  if (nships < nmax && rnd() < p) {
    int free_lane[LANES], nf = 0;
    for (int l = 0; l < LANES; l++) {
      bool busy = false;
      for (int i = 0; i < nships; i++) busy |= ships[i].lane == l && ships[i].pos >= GAP_POS;
      if (!busy && !(last_spawn_lane >= 0 && T < FAST_TICK_MS && l != last_spawn_lane)) free_lane[nf++] = l;
    }
    if (nf) {
      int l = free_lane[esp_random() % nf];
      ships[nships++] = { (int8_t)l, (int8_t)SPAWN_POS };
      last_spawn_lane = l;
      spawned = true;
    }
  }
  if (!spawned) last_spawn_lane = -1;

  if (crashed) {
    ph = Ph::Crash;
    crash_until = now + CRASH_FREEZE_MS;
  } else {
    next_tick = now + (uint32_t)T;
    if (tick_snd && !sound && !soundBusy(now)) Radio_Beep(1000, 6);
  }
  state.dirty = true;
}

static void pauseGame(uint32_t now)
{
  if (ph != Ph::Run) return;
  int32_t rem = (int32_t)(next_tick - now);
  pause_rem = rem > 0 ? rem : 0;
  ph = Ph::Pause;
  state.dirty = true;
}

static void resumeGame(uint32_t now)
{
  ph = Ph::Run;
  next_tick = now + max(pause_rem, RESUME_DELAY_MS);
  last_input = now;
  state.dirty = true;
}

static void moveKeeper(int y)
{
  int l = y < LANE_SPLIT_Y[0] ? 0 : y < LANE_SPLIT_Y[1] ? 1 : 2;
  last_input = millis();
  if (l != keeper) {
    keeper = l;
    state.dirty = true;
  }
}

// ---------------------------------------------------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------------------------------------------------

static bool playing() { return ph == Ph::Ready || ph == Ph::Run || ph == Ph::Crash || ph == Ph::Over; }

// A finger touched down: in play the lane changes right now.
static bool fenerTouchDown(int, int y)
{
  if (!playing()) return false;
  if (ph != Ph::Over) moveKeeper(y);
  return true;
}

static bool fenerSwipesLocked() { return playing(); }

static bool fenerGesture(Gesture g, int x, int y)
{
  uint32_t now = millis();
  last_input = now;
  if (playing()) {  // a release that was not consumed at touch-down (the /cmd touch test path), hold, or a vertical swipe
    if (g == Gesture::Tap && ph != Ph::Over) moveKeeper(y);
    if (g == Gesture::LongPress && ph == Ph::Run) pauseGame(now);
    return true;
  }
  if (ph == Ph::Pause) {
    if (g == Gesture::Tap) resumeGame(now);
    return g == Gesture::Tap || g == Gesture::LongPress;
  }
  // title
  if (g == Gesture::Tap) {
    if (y < 30 && x >= 480) {  // the click-sound switch, top right
      tick_snd = !tick_snd;
      if (!prefs_open) prefs_open = prefs.begin("play", false);
      prefs.putBool("fener_tick", tick_snd);
      Radio_Beep(tick_snd ? 1046 : 523, 40);
    } else {
      startGame(x < W / 2 + 56 ? 0 : 1);
    }
    return true;
  }
  return g == Gesture::LongPress;
}

static void fenerTick()
{
  uint32_t now = millis();
  soundTick(now);
  switch (ph) {
    case Ph::Ready:
      if (now - ph_t0 >= READY_MS) {
        ph = Ph::Run;
        next_tick = now + (uint32_t)tickMs();
        last_input = now;
        state.dirty = true;
      }
      break;
    case Ph::Run:
      if (now - last_input >= AUTO_PAUSE_MS) pauseGame(now);
      else if ((int32_t)(now - next_tick) >= 0) step(now);
      break;
    case Ph::Crash:
      if ((int32_t)(now - crash_until) >= 0) {
        memset(wreck, 0, sizeof(wreck));
        ph = Ph::Run;
        next_tick = now + (uint32_t)tickMs();
        last_input = now;
        state.dirty = true;
      }
      break;
    case Ph::Over:
      if (now - ph_t0 >= OVER_TOTAL_MS) {
        ph = Ph::Title;
        state.dirty = true;
      }
      break;
    default: break;
  }
}

static void fenerEnter()
{
  loadPrefs();
  ph = Ph::Title;
  sq_n = sq_i = 0;
  sq_next = 0;
  state.dirty = true;
}

static void fenerExit()
{
  ph = Ph::Title;
  if (prefs_open) {
    prefs.end();
    prefs_open = false;
  }
}

// ---------------------------------------------------------------------------------------------------------------------
// Drawing: an LCD handheld, the same in every theme. The art (colour print behind the glass + one 1-bit mask per LCD
// segment) is generated by tools/fener_lcd.py into GameFenerArt.*; here we only decide which segments are lit.
// ---------------------------------------------------------------------------------------------------------------------

namespace {
bool lit[fener_art::SEG_COUNT];
constexpr uint8_t DIGIT[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };  // bit k = segment a..g
constexpr uint16_t PAPER = px::rgb(198, 203, 186);  // the bare LCD (title / pause cards)
}  // namespace

// pct percent of b over a
static inline uint16_t mix565(uint16_t a, uint16_t b, int pct)
{
  int q = 100 - pct;
  int r = ((a >> 11) * q + (b >> 11) * pct) / 100;
  int g = (((a >> 5) & 63) * q + ((b >> 5) & 63) * pct) / 100;
  int bl = ((a & 31) * q + (b & 31) * pct) / 100;
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

enum class SegPass : uint8_t { Shadow, Ghost, Ink };

static void paintSeg(const fener_art::Seg &sg, SegPass pass)
{
  namespace fa = fener_art;
  const int W = fa::W, H = fa::H;
  uint16_t *fb = canvas();
  int dx = pass == SegPass::Shadow ? fa::SHADOW_DX : 0, dy = pass == SegPass::Shadow ? fa::SHADOW_DY : 0;
  int stride = (sg.w + 7) >> 3;
  for (int y = 0; y < sg.h; y++) {
    int sy = sg.y + y + dy;
    if (sy < 0 || sy >= H) continue;
    const uint8_t *row = fa::BITS + sg.off + y * stride;
    uint16_t *line = fb + sy * W;
    for (int x = 0; x < sg.w; x++) {
      if (!(row[x >> 3] & (0x80 >> (x & 7)))) continue;
      int sx = sg.x + x + dx;
      if (sx < 0 || sx >= W) continue;
      uint16_t &c = line[sx];
      c = pass == SegPass::Ink         ? fa::INK
           : pass == SegPass::Ghost     ? mix565(c, fa::INK, fa::GHOST_PCT)
                                        : mix565(c, 0, fa::SHADOW_PCT);
    }
  }
}

static void litDigits(uint8_t base, int v, bool lead_zeros)
{
  int d[3] = { v / 100 % 10, v / 10 % 10, v % 10 };
  for (int k = 0; k < 3; k++) {
    if (!lead_zeros && k < 2 && d[0] == 0 && (k == 0 || d[1] == 0)) continue;
    for (int b = 0; b < 7; b++) {
      if (DIGIT[d[k]] & (1 << b)) lit[base + k * 7 + b] = true;
    }
  }
}

static void litState(uint32_t now)
{
  using namespace fener_art;
  memset(lit, 0, sizeof(lit));
  if (ph == Ph::Over && now - ph_t0 < OVER_BLINK_MS && ((now - ph_t0) / 200) % 2 == 0) {  // all segments blink 3 times
    memset(lit, 1, sizeof(lit));
    return;
  }
  if (ph == Ph::Title) {  // attract: Game A and B take turns showing their hi-score, the beacon and the sea keep moving
    int g = (now / 1500) & 1;
    int beat = (now / 600) & 1;
    lit[g ? GAME_B : GAME_A] = true;
    litDigits(HI_D0_A, hi[g] % WRAP, false);
    lit[beat ? BEACON_B : BEACON_A] = true;
    for (int l = 0; l < LANES; l++) lit[WAVE_L0_A + l * 2 + beat] = true;
    return;
  }
  bool blink = ((now / 160) & 1) != 0;
  lit[game ? GAME_B : GAME_A] = !(ph == Ph::Pause && ((now / 250) & 1));
  lit[wave_phase ? BEACON_B : BEACON_A] = true;
  for (int l = 0; l < LANES; l++) {
    bool kp = keeper == l;
    lit[KEEPER_L0 + l] = lit[LAMP_L0 + l] = lit[BEAM_L0 + l] = kp;
    lit[WAVE_L0_A + l * 2 + (wave_phase & 1)] = true;
    lit[SAVED_L0 + l] = turned[l];
    lit[WRECK_L0 + l] = wreck[l] && (ph != Ph::Crash || blink);
  }
  for (int i = 0; i < nships; i++) {
    if (ships[i].pos >= 1 && ships[i].pos <= POSITIONS) lit[SHIP_L0_P1 + ships[i].lane * POSITIONS + ships[i].pos - 1] = true;
  }
  // hi-score: the digits blink while a new one stands; score: blinks while the double points last
  if (!(new_hi && ph == Ph::Over && ((now / 250) & 1))) {
    litDigits(HI_D0_A, (new_hi && ph == Ph::Over ? score : hi[game]) % WRAP, false);
  }
  if (!(bonus_left > 0 && ph == Ph::Run && ((now / 250) & 1))) litDigits(SCORE_D0_A, score % WRAP, true);
  for (int i = 0; i < MAX_MISS; i++) lit[MISS_1 + i] = i < miss;
}

static void renderLcd()
{
  using namespace fener_art;
  decodeBackdrop(canvas());
  for (int i = 0; i < SEG_COUNT; i++) {
    if (lit[i]) paintSeg(SEGS[i], SegPass::Shadow);
  }
  for (int i = 0; i < SEG_COUNT; i++) {
    if (!lit[i] && ghosted(i)) paintSeg(SEGS[i], SegPass::Ghost);
  }
  for (int i = 0; i < SEG_COUNT; i++) {
    if (lit[i]) paintSeg(SEGS[i], SegPass::Ink);
  }
}

// A printed card on the bare LCD, for the title and pause texts (these two screens need words; play needs none).
static void card(int x, int y, int w, int h)
{
  rect(x, y, w, h, PAPER);
  frame(x, y, w, h, fener_art::INK);
  frame(x + 1, y + 1, w - 2, h - 2, fener_art::INK);
}

static void centered(const char *s, int cx, int y, int p, const Ink &ink) { text(s, cx - textWidth(s, p) / 2, y, p, ink); }

static void drawTitle()
{
  const Ink ink = { fener_art::INK };
  const int split = W / 2 + 56;  // fenerGesture: left of this starts Game A
  const int cx[2] = { (110 + split) / 2, (split + W) / 2 };
  for (int g = 0; g < 2; g++) {
    card(cx[g] - 96, 52, 192, 66);
    centered(g ? "GAME B" : "GAME A", cx[g], 60, 3, ink);
    centered(g ? T("BUSY SEAS") : T("CALM SEAS"), cx[g], 94, 2, ink);
  }
  const char *c = tick_snd ? T("CLICK ON") : T("CLICK OFF");
  int w = textWidth(c, 2) + 12;
  card(W - 8 - w, 3, w, 22);
  text(c, W - 8 - w + 6, 7, 2, ink);
}

static void drawPause()
{
  const Ink ink = { fener_art::INK };
  card(150, 46, 340, 82);
  centered(T("PAUSE"), W / 2, 54, 4, ink);
  centered(T("TAP: CONTINUE"), W / 2, 90, 2, ink);
  centered(T("SWIPE SIDEWAYS: MENU"), W / 2, 108, 2, ink);
}

static void fenerDraw(const DateTime &)
{
  uint32_t now = millis();
  litState(now);
  renderLcd();
  Style old = style(), st;  // same plain font and no VFD glow in every theme
  st.font = &VFD_FONT;
  st.ghosts = st.glow = st.dots = false;
  setStyle(st);
  if (ph == Ph::Title) drawTitle();
  if (ph == Ph::Pause) drawPause();
  setStyle(old);
}

static uint32_t fenerFrameMs() { return 40; }

static void iconFener(int x, int y, uint16_t c) { sprite(ICON, rowsOf(ICON), x, y, 4, c); }

const App APP_FENER = { "fener", N_("KEEPER"), iconFener, true, true, fenerEnter, fenerExit, fenerDraw, fenerGesture, fenerTick,
                        fenerFrameMs, fenerTouchDown, fenerSwipesLocked };

#endif
