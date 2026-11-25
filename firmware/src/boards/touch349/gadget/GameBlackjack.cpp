#ifdef BOARD_TOUCH349

#include "GameBlackjack.h"
#include <esp_random.h>
#include "../../../core/Board.h"
#include "../../../core/state.h"
#include "Motion.h"
#include "Radio.h"
#include "RetroScene.h"
#include "Settings.h"
#include "Theme.h"
#include "Vfd.h"

using namespace vfd;

// Blackjack ("21") against a cartoon dealer: a match of 10 hands, the side that wins more hands wins the match (a push
// counts for nobody, so a match can end level). No money, no bets. One 52-card deck, reshuffled when fewer than 15 cards
// are left. The dealer stands on every 17 (soft too); no double, no split, no insurance. A blackjack is just a won hand.
// The match lives in RAM: leave the app and come back, it carries on (a finished one starts over).
// HIT / STAND / DEAL: the buttons on the right, under the score and one pip per hand. Knocks on the table (the motion
// sensor): on your turn one knock is STAND and two quick knocks (within 450 ms) are HIT; between hands a knock deals.
// One knock waits out the 450 ms (the STAND button is ringed meanwhile); a missed second knock then stands, which never
// busts you, where the other way round it would draw a card. A knock that is just the finger landing on the glass is
// ignored.
// The dealer is a character on the left: six poses per theme (assets dealer_<pose>_<theme>, see RetroSprite_Draw), a
// placeholder drawn with rectangles and discs when the asset pack has none. Events make him talk in a speech bubble.

namespace {

// ---------------------------------------------------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------------------------------------------------

enum class Ph : uint8_t { Start, Deal, Player, Dealer, Result, Over };
enum class Out : uint8_t { None, Win, Lose, Push, Blackjack, Bust, DealerBust, DealerBlackjack };
enum Pose : uint8_t { P_IDLE, P_BLINK, P_TALK, P_SMUG, P_SAD, P_SHOCK, POSES };
enum Ev : uint8_t { EV_START, EV_DEAL, EV_HIT, EV_PBUST, EV_DBUST, EV_PBJ, EV_PUSH, EV_DWIN, EV_PWIN, EV_DBJ, EV_IDLE, EV_POKE, EV_SHUFFLE,
                    EV_MWIN, EV_MLOSE, EV_MDRAW, EV_COUNT };

constexpr int HANDS = 10;                       // hands in a match
constexpr int DLR_W = 150;                      // the dealer character
constexpr int CW = 60, CH = 70;                 // a card
constexpr int ROW_D = 7, ROW_P = 94;            // dealer row, player row (y)
constexpr int HAND_X = 206, HAND_W = 300;       // the cards of a hand
constexpr int TOT_X = 154;                      // hand total, left of the cards
constexpr int SIDE_X = 514, SIDE_W = 118;       // score, pips and the button column
constexpr int PIP_Y = 48, PIP = 9, PIP_PITCH = 11;
constexpr int BTN_Y0 = 64, BTN_H = 48, BTN_PITCH = 54;
constexpr int MAX_CARDS = 10;
constexpr uint32_t BUBBLE_MS = 2000;
constexpr uint32_t DEAL_GAP = 260;              // between the cards of the deal
constexpr uint32_t SLIDE_MS = 200;
constexpr uint32_t OVER_MS = 2200;              // the last hand's result shows this long before the match result

struct Hand {
  int c[MAX_CARDS];
  uint32_t born[MAX_CARDS];  // millis() at which the card starts sliding in
  int n = 0;
};

Ph ph = Ph::Start;
Out out = Out::None;
int deck[52];
int deck_pos = 52;  // all used: the first deal shuffles
Hand pl, dl;
bool hole_up = false;  // the dealer's second card is face up
uint32_t phase_ms = 0, lock_until = 0, last_input_ms = 0, dealer_next_ms = 0, result_ms = 0;

// the match
int8_t res[HANDS];  // per hand: 1 you won, -1 the dealer won, 0 push
int hands_done = 0, p_wins = 0, d_wins = 0;

// input
bool touching = false;
uint32_t last_down_ms = 0, last_up_ms = 0;
constexpr uint32_t KNOCK_PAIR_MS = 450;   // the second knock of a pair lands within this of the first
constexpr uint32_t KNOCK_TOUCH_MS = 140;  // the touch driver reports a finger up to ~140 ms after the knock it makes
uint32_t knock_t[2] = { 0, 0 };           // the knocks heard so far: one, or a pair
int knock_n = 0;

// the dealer
char bubble[96] = "";
uint32_t bubble_until = 0, blink_at = 0, nudge_ms = 0;
int8_t react = -1;  // the pose held while the bubble shows, -1 = just talk
int last_line[EV_COUNT];

// win jingle: Radio_Beep plays one tone at a time, so the notes are fed from tick
int jingle_hz[8], jingle_ms[8], jingle_n = 0, jingle_i = 0;
uint32_t jingle_next = 0;

void newMatch()
{
  hands_done = p_wins = d_wins = 0;
  for (int i = 0; i < HANDS; i++) res[i] = 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// Cards and hands
// ---------------------------------------------------------------------------------------------------------------------

int suitOf(int c) { return c / 13; }
int rankOf(int c) { return c % 13; }  // 0 = 2 ... 8 = 10, 9 = J, 10 = Q, 11 = K, 12 = A

void shuffle()
{
  for (int i = 0; i < 52; i++) deck[i] = i;
  for (int i = 51; i > 0; i--) {  // Fisher-Yates
    int j = (int)(esp_random() % (uint32_t)(i + 1));
    int t = deck[i];
    deck[i] = deck[j];
    deck[j] = t;
  }
  deck_pos = 0;
}

// Best total of the first n cards; soft = an ace counts as 11.
int total(const Hand &h, int n, bool *soft = nullptr)
{
  int sum = 0, aces = 0;
  for (int i = 0; i < n && i < h.n; i++) {
    int r = rankOf(h.c[i]);
    if (r == 12) {
      aces++;
      sum += 1;
    } else {
      sum += r >= 8 ? 10 : r + 2;
    }
  }
  bool s = false;
  if (aces && sum + 10 <= 21) {
    sum += 10;
    s = true;
  }
  if (soft) *soft = s;
  return sum;
}

bool natural(const Hand &h) { return h.n == 2 && total(h, 2) == 21; }

void addCard(Hand &h, uint32_t when)
{
  if (h.n >= MAX_CARDS) return;
  if (deck_pos >= 52) shuffle();
  h.c[h.n] = deck[deck_pos++];
  h.born[h.n] = when;
  h.n++;
}

// ---------------------------------------------------------------------------------------------------------------------
// The dealer talks
// ---------------------------------------------------------------------------------------------------------------------

struct Line {
  const char *en;
};

const Line LINES_START[] = { { N_("Ten hands. Most wins takes it.") },
                             { N_("Ten hands, you and me.") } };
const Line LINES_DEAL[] = { { N_("Cards are flying. Pray.") },
                            { N_("Let's see what luck brings.") },
                            { N_("Shuffled by hand. Mine.") } };
const Line LINES_HIT[] = { { N_("Another one? Brave.") },
                           { N_("Greedy. I like it.") },
                           { N_("Knock knock. Who's there? A card.") } };
const Line LINES_PBUST[] = { { N_("Busted! Delicious.") }, { N_("The house thanks you.") } };
const Line LINES_DBUST[] = { { N_("Oh no... I busted.") }, { N_("That was not in the plan.") } };
const Line LINES_PBJ[] = { { N_("Blackjack?! Who tipped you?") }, { N_("Okay, okay. Take it.") } };
const Line LINES_PUSH[] = { { N_("A tie. Nobody cries.") }, { N_("Push. We call it friendship.") } };
const Line LINES_DWIN[] = { { N_("The house always wins.") },
                            { N_("One more for the house.") },
                            { N_("Better luck next hand.") } };
const Line LINES_PWIN[] = { { N_("Beginner's luck.") }, { N_("Enjoy it. It won't last.") }, { N_("Hm. I let you win.") } };
const Line LINES_DBJ[] = { { N_("Blackjack. Sorry, not sorry.") } };
const Line LINES_IDLE[] = { { N_("Hello? I have a family.") },
                            { N_("Decide. The house is getting cold.") },
                            { N_("Are you shy? Don't be.") } };
const Line LINES_POKE[] = { { N_("Hands off the dealer.") }, { N_("Stop it. I'm ticklish.") } };
const Line LINES_SHUFFLE[] = { { N_("New deck. Fresh start.") } };
const Line LINES_MWIN[] = { { N_("You took the match. I need a holiday.") },
                            { N_("Fine. You win. This time.") } };
const Line LINES_MLOSE[] = { { N_("The match is mine. As always.") },
                             { N_("Better luck next match.") } };
const Line LINES_MDRAW[] = { { N_("A draw. Rematch?") } };

struct Table {
  const Line *l;
  int n;
  int8_t pose;
};
#define TABLE(a, p) { a, (int)(sizeof(a) / sizeof(a[0])), p }
const Table TABLES[EV_COUNT] = {
  TABLE(LINES_START, -1), TABLE(LINES_DEAL, -1), TABLE(LINES_HIT, -1), TABLE(LINES_PBUST, P_SMUG), TABLE(LINES_DBUST, P_SAD), TABLE(LINES_PBJ, P_SHOCK),
  TABLE(LINES_PUSH, -1), TABLE(LINES_DWIN, P_SMUG), TABLE(LINES_PWIN, P_SAD), TABLE(LINES_DBJ, P_SMUG), TABLE(LINES_IDLE, -1), TABLE(LINES_POKE, -1),
  TABLE(LINES_SHUFFLE, -1), TABLE(LINES_MWIN, P_SAD), TABLE(LINES_MLOSE, P_SMUG), TABLE(LINES_MDRAW, -1),
};
#undef TABLE

void say(Ev e)
{
  const Table &t = TABLES[e];
  int i = (int)(esp_random() % (uint32_t)t.n);
  if (t.n > 1 && i == last_line[e]) i = (i + 1) % t.n;
  last_line[e] = i;
  strlcpy(bubble, T(t.l[i].en), sizeof(bubble));
  bubble_until = millis() + BUBBLE_MS;
  react = t.pose;
  nudge_ms = millis();
}

bool bubbleOn(uint32_t now) { return bubble[0] && (int32_t)(bubble_until - now) > 0; }

Pose poseNow(uint32_t now)
{
  if (bubbleOn(now)) {
    if (react >= 0) return (Pose)react;
    return (now / 150) % 2 ? P_IDLE : P_TALK;
  }
  if (ph == Ph::Over && p_wins != d_wins) return p_wins > d_wins ? P_SAD : P_SMUG;  // the match result stays on his face
  if ((int32_t)(now - blink_at) >= 0) {
    if ((int32_t)(now - blink_at) < 150) return P_BLINK;
    blink_at = now + 3000 + esp_random() % 2000;
  }
  return P_IDLE;
}

// ---------------------------------------------------------------------------------------------------------------------
// Game
// ---------------------------------------------------------------------------------------------------------------------

// The speaker shakes the case: knocks are ignored while a beep plays and just after it.
uint32_t beep_quiet_until = 0;

void beep(int hz, int ms)
{
  Radio_Beep(hz, ms);
  beep_quiet_until = millis() + ms + 250;
}

void startJingle(bool big)
{
  static const int NOTES[8] = { 523, 659, 784, 1046, 1175, 1318, 1568, 2093 };
  jingle_n = big ? 8 : 4;
  for (int i = 0; i < jingle_n; i++) {
    jingle_hz[i] = NOTES[i];
    jingle_ms[i] = big ? 80 : 100;
  }
  jingle_i = 0;
  jingle_next = millis();
}

// The hand is over: score it, the dealer reacts.
void settle()
{
  hole_up = true;
  int pt = total(pl, pl.n), dt = total(dl, dl.n);
  bool pbj = natural(pl), dbj = natural(dl);
  if (pbj && dbj) out = Out::Push;
  else if (pbj) out = Out::Blackjack;
  else if (dbj) out = Out::DealerBlackjack;
  else if (pt > 21) out = Out::Bust;
  else if (dt > 21) out = Out::DealerBust;
  else if (pt > dt) out = Out::Win;
  else if (pt < dt) out = Out::Lose;
  else out = Out::Push;
  int8_t r = (out == Out::Blackjack || out == Out::DealerBust || out == Out::Win) ? 1 : (out == Out::Push ? 0 : -1);
  if (hands_done < HANDS) res[hands_done++] = r;
  if (r > 0) p_wins++;
  else if (r < 0) d_wins++;
  ph = Ph::Result;
  result_ms = millis();
  lock_until = millis() + 350;
  switch (out) {
    case Out::Blackjack: say(EV_PBJ); startJingle(true); break;
    case Out::DealerBust: say(EV_DBUST); startJingle(false); break;
    case Out::Win: say(EV_PWIN); startJingle(false); break;
    case Out::Push: say(EV_PUSH); beep(440, 90); break;
    case Out::Bust: say(EV_PBUST); beep(196, 140); break;
    case Out::DealerBlackjack: say(EV_DBJ); beep(196, 140); break;
    default: say(EV_DWIN); beep(196, 140); break;
  }
}

// The tenth hand is in: the match result.
void endMatch()
{
  ph = Ph::Over;
  result_ms = millis();
  lock_until = millis() + 600;
  jingle_n = 0;
  if (p_wins > d_wins) {
    say(EV_MWIN);
    startJingle(true);
  } else if (p_wins < d_wins) {
    say(EV_MLOSE);
    beep(165, 260);
  } else {
    say(EV_MDRAW);
    beep(440, 120);
  }
  bubble_until = millis() + 3000;
}

// The player is done (stand, 21): the dealer turns the hole card and draws.
void dealerTurn()
{
  ph = Ph::Dealer;
  phase_ms = millis();
  hole_up = false;
  dealer_next_ms = millis() + 450;  // a beat before the hole card turns
  lock_until = dealer_next_ms;
}

void deal()
{
  uint32_t now = millis();
  bool shuffled = false;
  if (52 - deck_pos < 15) {
    shuffle();
    shuffled = true;
  }
  out = Out::None;
  hole_up = false;
  pl.n = dl.n = 0;
  addCard(pl, now + 80);
  addCard(dl, now + 80 + DEAL_GAP);
  addCard(pl, now + 80 + 2 * DEAL_GAP);
  addCard(dl, now + 80 + 3 * DEAL_GAP);
  ph = Ph::Deal;
  phase_ms = now + 80 + 3 * DEAL_GAP + SLIDE_MS + 120;  // the deal is over
  lock_until = phase_ms;
  jingle_n = 0;
  say(shuffled ? EV_SHUFFLE : EV_DEAL);
  state.dirty = true;
}

void hit()
{
  uint32_t now = millis();
  addCard(pl, now);
  beep(520, 18);
  int t = total(pl, pl.n);
  lock_until = now + SLIDE_MS + 100;
  if (t > 21) {
    phase_ms = lock_until;  // see the card land, then the dealer's hole card
    ph = Ph::Dealer;
    dealer_next_ms = 0;  // nothing to draw: show the result when the card has landed
    return;
  }
  if (t == 21) {
    phase_ms = lock_until;
    ph = Ph::Dealer;
    dealer_next_ms = lock_until;
    hole_up = false;
    return;
  }
  say(EV_HIT);
}

void stand()
{
  dealerTurn();
  beep(330, 25);
}

// The big button between hands, and the knock there: deal the next hand, show the match result after the tenth, or
// start a new match.
void dealAction()
{
  uint32_t now = millis();
  if ((int32_t)(now - lock_until) < 0) return;
  if (ph == Ph::Over) {
    newMatch();
    deal();
  } else if (ph == Ph::Result && hands_done >= HANDS) {
    endMatch();
  } else if (ph == Ph::Start || ph == Ph::Result) {
    deal();
  }
  state.dirty = true;
}

// Knocks: on your turn one = stand, two = hit; between hands any knock deals.
void knockAction(int n)
{
  uint32_t now = millis();
  if ((int32_t)(now - lock_until) < 0) return;
  last_input_ms = now;
  if (ph == Ph::Player) {
    if (n >= 2) hit();
    else stand();
  } else {
    dealAction();
  }
  state.dirty = true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Drawing: the pieces that differ per theme
// ---------------------------------------------------------------------------------------------------------------------

// Suits as strings, 9 wide, each '#' a block.
const char *const SPADE[] = { "....#....", "...###...", "..#####..", ".#######.", "#########", "#########", "#########", ".##.#.##.", "....#....", "...###..." };
const char *const HEART[] = { ".##...##.", "####.####", "#########", "#########", ".#######.", "..#####..", "...###...", "....#...." };
const char *const DIAMOND[] = { "....#....", "...###...", "..#####..", ".#######.", "#########", ".#######.", "..#####..", "...###...", "....#...." };
const char *const CLUB[] = { "...###...", "..#####..", "..#####..", "...###...", ".###.###.", "#########", "#########", ".##.#.##.", "....#....", "...###..." };
const int SUIT_ROWS[4] = { 10, 8, 9, 10 };
const char *const *const SUITS[4] = { SPADE, HEART, DIAMOND, CLUB };
const char *const RANKS[13] = { "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K", "A" };

// The tile icon: a card with 21 on it.
const char *const ICON_21[] = { "..########..", ".#........#.", ".#.##..#..#.", ".##..###..#.", ".#...#.#..#.", ".#..#..#..#.",
                                ".#.#...#..#.", ".##....#..#.", ".########.#.", ".#........#.", ".#........#.", "..########.." };

void iconBlackjack(int x, int y, uint16_t c) { sprite(ICON_21, 12, x, y, 4, c); }

struct Pal {
  uint16_t face, edge, inkb, inkr, felt, felt2, back1, back2, wall, wall2, skin, chip, chip2;
};

Pal pal()
{
  using px::rgb;
  switch (theme::current()) {
    case theme::C64:
      return { 0xFFFF, rgb(149, 149, 149), rgb(0, 0, 0), rgb(136, 57, 57), rgb(88, 141, 67), rgb(68, 117, 52), HALO, BG, BG, HALO, rgb(184, 199, 111), rgb(136, 57, 57), 0xFFFF };
    case theme::AMIGA:
      return { 0xFFFF, rgb(0, 0, 34), rgb(0, 0, 34), rgb(221, 68, 0), rgb(0, 102, 68), rgb(0, 85, 68), rgb(0, 68, 136), rgb(255, 136, 0), rgb(0, 85, 170), rgb(0, 68, 136), rgb(255, 204, 153), rgb(221, 68, 0), 0xFFFF };
    case theme::ATARI:
      return { rgb(236, 236, 236), rgb(160, 160, 200), rgb(24, 26, 80), rgb(200, 72, 48), rgb(28, 88, 44), rgb(36, 104, 52), rgb(24, 26, 80), rgb(160, 160, 200), rgb(0, 0, 0), rgb(24, 26, 80), rgb(252, 188, 116), rgb(200, 72, 48), rgb(236, 236, 236) };
    default:
      return { GHOST, DIM, LIT, HOT, rgb(2, 14, 14), rgb(4, 24, 24), GHOST, DIM, BG, GHOST, rgb(20, 70, 76), HOT, LIT };
  }
}

void drawSuit(int s, int x, int y, int scale, uint16_t color)
{
  if (style().glow) {  // VFD: a thin halo around it
    sprite(SUITS[s], SUIT_ROWS[s], x - 1, y, scale, HALO);
    sprite(SUITS[s], SUIT_ROWS[s], x + 1, y, scale, HALO);
    sprite(SUITS[s], SUIT_ROWS[s], x, y - 1, scale, HALO);
    sprite(SUITS[s], SUIT_ROWS[s], x, y + 1, scale, HALO);
  }
  sprite(SUITS[s], SUIT_ROWS[s], x, y, scale, color);
}

void drawBack(int x, int y, const Pal &p)
{
  rect(x, y, CW, CH, p.back1);
  frame(x, y, CW, CH, p.edge);
  for (int j = 0; j < (CH - 8) / 6; j++) {
    for (int i = 0; i < (CW - 8) / 6; i++) {
      if ((i + j) & 1) rect(x + 4 + i * 6, y + 4 + j * 6, 6, 6, p.back2);
    }
  }
}

void drawFace(int c, int x, int y, const Pal &p)
{
  int s = suitOf(c), r = rankOf(c);
  bool red = s == 1 || s == 2;
  uint16_t ink = red ? p.inkr : p.inkb;
  rect(x, y, CW, CH, p.face);
  Ink t = style().glow ? (red ? HOT_INK : LIT_INK) : Ink{ ink };
  text(RANKS[r], x + 5, y + 4, 3, t);
  drawSuit(s, x + 6, y + 27, 2, ink);
  drawSuit(s, x + CW - 31, y + CH - 34, 3, ink);
}

// A card of a hand: slides in from the shoe at the top right.
void drawCard(const Hand &h, int i, int tx, int ty, bool down, const Pal &p, uint32_t now)
{
  int32_t age = (int32_t)(now - h.born[i]);
  if (age < 0) return;
  int x = tx, y = ty;
  if (age < (int32_t)SLIDE_MS) {
    float t = age / (float)SLIDE_MS, e = 1 - (1 - t) * (1 - t);
    x = tx + (int)((540 - tx) * (1 - e));
    y = ty + (int)((-80 - ty) * (1 - e));
  }
  if (style().glow) frame(x - 1, y - 1, CW + 2, CH + 2, HALO);
  if (down) {
    drawBack(x, y, p);
    return;
  }
  drawFace(h.c[i], x, y, p);
  frame(x, y, CW, CH, p.edge);
}

void drawHand(const Hand &h, int y, bool hole_down, const Pal &p, uint32_t now)
{
  if (!h.n) return;
  int step = h.n > 1 ? min(CW + 6, (HAND_W - CW) / (h.n - 1)) : 0;
  for (int i = 0; i < h.n; i++) drawCard(h, i, HAND_X + i * step, y, hole_down && i == 1, p, now);
}

// Total of a hand: "17", soft "7/17", dealer with the hole card down: only the up card.
void drawTotal(const Hand &h, int y, int shown, uint32_t now)
{
  int n = 0;
  for (int i = 0; i < h.n && i < shown; i++) {
    if ((int32_t)(now - h.born[i]) >= (int32_t)SLIDE_MS / 2) n = i + 1;
  }
  if (!n) return;
  bool soft;
  int t = total(h, n, &soft);
  char b[12];
  if (soft && t < 21) snprintf(b, sizeof(b), "%d/%d", t - 10, t);
  else snprintf(b, sizeof(b), "%d", t);
  Ink ink = t > 21 ? HOT_INK : (t == 21 ? HOT_INK : LIT_INK);
  int pt = textWidth(b, 2) <= 50 ? 2 : 1;
  text(b, TOT_X, y + 26, pt, ink);
}

void button(int x, int y, int w, int h, const char *label, bool on)
{
  int sc = textWidth(label, 3) <= w - 6 ? 3 : 2;
  int tw = textWidth(label, sc), th = sc * 7;
  switch (theme::current()) {
    case theme::C64:
      rect(x, y, w, h, on ? HALO : BG);
      frame(x, y, w, h, HALO);
      text(label, x + (w - tw) / 2, y + (h - th) / 2, sc, Ink{ on ? (uint16_t)0xFFFF : HALO });
      break;
    case theme::AMIGA: {
      uint16_t grey = px::rgb(153, 153, 170);
      rect(x, y, w, h, on ? grey : px::rgb(0, 68, 136));
      rect(x, y, w, 2, 0xFFFF);
      rect(x, y, 2, h, 0xFFFF);
      rect(x, y + h - 2, w, 2, px::rgb(0, 0, 34));
      rect(x + w - 2, y, 2, h, px::rgb(0, 0, 34));
      text(label, x + (w - tw) / 2, y + (h - th) / 2, sc, Ink{ on ? px::rgb(0, 0, 34) : px::rgb(100, 140, 190) });
      break;
    }
    case theme::ATARI:
      rect(x, y, w, h, on ? px::rgb(252, 188, 116) : px::rgb(24, 26, 80));
      rect(x, y + h - 4, w, 4, on ? px::rgb(208, 128, 92) : px::rgb(0, 0, 0));
      text(label, x + (w - tw) / 2, y + (h - th) / 2 - 2, sc, Ink{ on ? (uint16_t)0 : px::rgb(160, 160, 200) });
      break;
    default:
      if (on) {
        frame(x - 1, y - 1, w + 2, h + 2, HALO);
        rect(x, y, w, h, GHOST);
      }
      frame(x, y, w, h, on ? LIT : DIM);
      if (on) frame(x + 2, y + 2, w - 4, h - 4, HALO);
      text(label, x + (w - tw) / 2, y + (h - th) / 2, sc, on ? LIT_INK : DIM_INK);
      break;
  }
}

// The felt: the table in the theme's own look.
void drawFelt(const Pal &p)
{
  int x = DLR_W + 2, y = 2, w = SIDE_X - 6 - x, h = H - 4;
  switch (theme::current()) {
    case theme::C64:
      rect(x, y, w, h, p.felt);
      frame(x, y, w, h, HALO);
      frame(x + 1, y + 1, w - 2, h - 2, HALO);
      break;
    case theme::AMIGA:
      for (int j = 0; j < h; j += 4) {  // a little shading: the light falls from the top
        int k = j * 5 / h;
        uint16_t c = k == 0 ? px::rgb(0, 119, 85) : (k == 1 ? px::rgb(0, 110, 77) : (k == 2 ? p.felt : (k == 3 ? px::rgb(0, 94, 68) : p.felt2)));
        rect(x, y + j, w, min(4, h - j), c);
      }
      rect(x, y, w, 2, 0xFFFF);
      rect(x, y, 2, h, 0xFFFF);
      rect(x, y + h - 2, w, 2, px::rgb(0, 0, 34));
      rect(x + w - 2, y, 2, h, px::rgb(0, 0, 34));
      break;
    case theme::ATARI:
      for (int j = 0; j < h; j += 4) rect(x, y + j, w, min(4, h - j), (j / 4) & 1 ? p.felt2 : p.felt);
      rect(x, y, w, 4, px::rgb(236, 236, 236));
      rect(x, y + h - 4, w, 4, px::rgb(236, 236, 236));
      break;
    default:
      rect(x, y, w, h, p.felt);
      frame(x, y, w, h, DIM);
      frame(x + 2, y + 2, w - 4, h - 4, GHOST);
      for (int j = 8; j < h - 6; j += 8) {  // the glass grid: faint dots
        for (int i = 8; i < w - 6; i += 8) rect(x + i, y + j, 1, 1, GHOST);
      }
      break;
  }
}

// What stands behind the dealer.
void drawWall(const Pal &p)
{
  switch (theme::current()) {
    case theme::C64:
      rect(0, 0, DLR_W, H, p.wall);
      for (int j = 0; j < H; j += 16) rect(0, j, DLR_W, 2, px::rgb(68, 56, 140));
      break;
    case theme::AMIGA: {  // copper sky: bars that run from deep red to orange to blue
      static const uint16_t COPPER[] = { px::rgb(68, 0, 68), px::rgb(119, 0, 68), px::rgb(170, 34, 34), px::rgb(221, 85, 0), px::rgb(255, 136, 0),
                                         px::rgb(255, 187, 68), px::rgb(255, 221, 136), px::rgb(136, 187, 221), px::rgb(68, 136, 204) };
      for (int j = 0; j < H; j += 6) rect(0, j, DLR_W, 6, COPPER[min(8, j * 9 / H)]);
      break;
    }
    case theme::ATARI:
      rect(0, 0, DLR_W, H, p.wall);
      for (int j = 0; j < H; j += 24) rect(0, j + 4, DLR_W, 8, px::rgb(24, 26, 80));
      break;
    default:
      rect(0, 0, DLR_W, H, BG);
      for (int j = 10; j < H; j += 10) {
        for (int i = 6; i < DLR_W; i += 10) rect(i, j, 1, 1, GHOST);
      }
      rect(DLR_W - 1, 0, 1, H, GHOST);
      break;
  }
}

// ---------------------------------------------------------------------------------------------------------------------
// The dealer character
// ---------------------------------------------------------------------------------------------------------------------

const char *const POSE_NAMES[POSES] = { "idle", "blink", "talk", "smug", "sad", "shock" };

// A stand-in until the sprites are in the asset pack: the same six poses from discs and rectangles.
void drawPlaceholder(Pose ps, uint32_t now, const Pal &p)
{
  uint16_t line = theme::current() == theme::VFD ? LIT : px::rgb(24, 20, 40);
  uint16_t white = theme::current() == theme::VFD ? LIT : 0xFFFF;
  int cx = 75, cy = 92, bob = (ps == P_TALK && (now / 150) % 2) ? 2 : 0;
  cy += bob;
  // shoulders and bow tie
  rect(20, 140, 110, 32, theme::current() == theme::VFD ? GHOST : (theme::current() == theme::C64 ? HALO : px::rgb(40, 40, 60)));
  if (theme::current() == theme::VFD) frame(20, 140, 110, 32, DIM);
  rect(62, 136, 26, 12, theme::current() == theme::VFD ? HOT : p.chip);
  rect(72, 138, 6, 8, line);
  // head
  Apps_Disc(cx, cy, 36, theme::current() == theme::VFD ? DIM : line);
  Apps_Disc(cx, cy, 34, p.skin);
  // visor
  rect(cx - 40, cy - 38, 80, 12, theme::current() == theme::VFD ? HOT : p.chip);
  rect(cx - 30, cy - 50, 60, 14, theme::current() == theme::VFD ? HOT : p.chip);
  // eyes
  int ey = cy - 8;
  int er = ps == P_SHOCK ? 10 : 7;
  for (int s = -1; s <= 1; s += 2) {
    int ex = cx + s * 15;
    if (ps == P_BLINK) {
      rect(ex - 7, ey, 15, 3, line);
    } else {
      Apps_Disc(ex, ey, er, white);
      Apps_Disc(ex + (ps == P_SMUG ? s * -2 : 0), ey + (ps == P_SAD ? 3 : 0), ps == P_SHOCK ? 3 : 4, theme::current() == theme::VFD ? BG : line);
      if (ps == P_SMUG) rect(ex - 9, ey - 9, 19, 10, p.skin);  // half-lidded
      if (ps == P_SMUG) rect(ex - 9, ey, 19, 2, line);
    }
    // brows
    if (ps == P_SAD) rect(ex - 7 + s * 2, ey - 13 + (s > 0 ? 0 : 3), 14, 3, line);
    else if (ps == P_SHOCK) rect(ex - 7, ey - 18, 14, 3, line);
  }
  // mouth
  int my = cy + 20;
  switch (ps) {
    case P_TALK: rect(cx - 12, my - 2, 24, (now / 150) % 2 ? 12 : 7, line); break;
    case P_SHOCK: Apps_Disc(cx, my + 2, 9, line); break;
    case P_SMUG: rect(cx - 14, my, 26, 3, line); rect(cx + 10, my - 4, 6, 4, line); break;
    case P_SAD: rect(cx - 12, my + 4, 24, 3, line); rect(cx - 14, my + 7, 4, 4, line); rect(cx + 10, my + 7, 4, 4, line); break;
    default: rect(cx - 12, my, 24, 3, line); rect(cx - 14, my - 3, 4, 3, line); rect(cx + 10, my - 3, 4, 3, line); break;
  }
}

const char *themeKey()
{
  switch (theme::current()) {
    case theme::C64: return "c64";
    case theme::AMIGA: return "amiga";
    case theme::ATARI: return "atari";
    default: return "vfd";
  }
}

void drawDealer(uint32_t now, const Pal &p)
{
  Pose ps = poseNow(now);
  char name[24];
  snprintf(name, sizeof(name), "dealer_%s_%s", POSE_NAMES[ps], themeKey());
  if (RetroSprite_Draw(name, 0, H, true)) return;
  if (ps != P_IDLE) {  // a missing pose falls back to the idle one before the placeholder
    snprintf(name, sizeof(name), "dealer_idle_%s", themeKey());
    if (RetroSprite_Draw(name, 0, H, true)) return;
  }
  drawPlaceholder(ps, now, p);
}

// Words wrapped to lines of at most w px at size 2; returns the line count.
int wrap(const char *s, int w, char lines[][40], int max_lines)
{
  int n = 0;
  lines[0][0] = 0;
  const char *p = s;
  while (*p && n < max_lines) {
    const char *e = strchr(p, ' ');
    int len = e ? (int)(e - p) : (int)strlen(p);
    char word[40];
    len = min(len, 38);
    memcpy(word, p, len);
    word[len] = 0;
    char trial[80];
    snprintf(trial, sizeof(trial), "%s%s%s", lines[n], lines[n][0] ? " " : "", word);
    if (lines[n][0] && textWidth(trial, 2) > w) {
      n++;
      if (n >= max_lines) break;
      strlcpy(lines[n], word, 40);
    } else {
      strlcpy(lines[n], trial, 40);
    }
    p += len;
    if (*p == ' ') p++;
  }
  return min(n + 1, max_lines);
}

// The speech bubble over the felt, its tail toward the dealer's head.
void drawBubble(const Pal &p)
{
  static char lines[4][40];
  int n = wrap(bubble, 250, lines, 4);
  int tw = 0;
  for (int i = 0; i < n; i++) tw = max(tw, textWidth(lines[i], 2));
  int bw = tw + 20, bh = n * 18 + 12, bx = 144, by = 30;
  uint16_t fill, edge;
  Ink ink;
  if (theme::current() == theme::VFD) {
    fill = BG;
    edge = LIT;
    ink = LIT_INK;
  } else {
    fill = 0xFFFF;
    edge = theme::current() == theme::C64 ? HALO : theme::current() == theme::AMIGA ? px::rgb(0, 0, 34) : px::rgb(24, 26, 80);
    ink = Ink{ theme::current() == theme::C64 ? px::rgb(53, 40, 121) : edge };
  }
  rect(bx, by, bw, bh, fill);
  frame(bx, by, bw, bh, edge);
  frame(bx + 1, by + 1, bw - 2, bh - 2, edge);
  for (int i = 0; i < 10; i++) {  // tail: down and to the left, toward the dealer's head
    int x0 = bx + 22 - i * 2, w = 14 - i;
    rect(x0, by + bh - 2 + i, w, 1, fill);
    rect(x0, by + bh - 2 + i, 2, 1, edge);
    rect(x0 + w - 2, by + bh - 2 + i, 2, 1, edge);
  }
  for (int i = 0; i < n; i++) text(lines[i], bx + 10, by + 6 + i * 18, 2, ink);
  (void)p;
}

// ---------------------------------------------------------------------------------------------------------------------
// The screen
// ---------------------------------------------------------------------------------------------------------------------

const char *resultText()
{
  switch (out) {
    case Out::Blackjack: return T("BLACKJACK!");
    case Out::Win: return T("YOU WIN");
    case Out::DealerBust: return T("DEALER BUSTS");
    case Out::Push: return T("PUSH");
    case Out::Bust: return T("BUST");
    case Out::DealerBlackjack: return T("DEALER BLACKJACK");
    case Out::Lose: return T("DEALER WINS");
    default: return "";
  }
}

// Centre a line of text over the felt.
void feltText(const char *t, int y, int sc, Ink ink)
{
  int x0 = DLR_W + 2, w = SIDE_X - 6 - x0;
  text(t, x0 + (w - textWidth(t, sc)) / 2, y, sc, ink);
}

// The score: you on the left, the dealer on the right.
void drawScore(int x, int y, int w, int sc)
{
  char a[4], b[4];
  snprintf(a, sizeof(a), "%d", p_wins);
  snprintf(b, sizeof(b), "%d", d_wins);
  text(a, x, y, sc, LIT_INK);
  text(b, x + w - textWidth(b, sc), y, sc, HOT_INK);
  text("-", x + (w - textWidth("-", sc)) / 2, y, sc, DIM_INK);
}

// One pip per hand: won (lit), lost (accent), push (a bar), the hand on the table (a lit frame), still to come (faint).
void drawPips()
{
  int x0 = SIDE_X + (SIDE_W - (HANDS - 1) * PIP_PITCH - PIP) / 2;
  bool live = ph == Ph::Deal || ph == Ph::Player || ph == Ph::Dealer;
  for (int i = 0; i < HANDS; i++) {
    int x = x0 + i * PIP_PITCH;
    if (i < hands_done) {
      if (res[i] > 0) rect(x, PIP_Y, PIP, PIP, LIT);
      else if (res[i] < 0) rect(x, PIP_Y, PIP, PIP, HOT);
      else rect(x, PIP_Y + 3, PIP, 3, DIM);
    } else if (live && i == hands_done) {
      frame(x, PIP_Y, PIP, PIP, LIT);
    } else {
      frame(x, PIP_Y, PIP, PIP, DIM);
    }
  }
}

void drawSide(uint32_t now)
{
  drawScore(SIDE_X + 6, 6, SIDE_W - 12, 4);
  const char *you = T("YOU"), *him = T("HOUSE");
  text(you, SIDE_X + 6, 37, 1, DIM_INK);
  text(him, SIDE_X + SIDE_W - 6 - textWidth(him, 1), 37, 1, DIM_INK);
  drawPips();
  bool unlocked = (int32_t)(now - lock_until) >= 0;
  int big = H - 4 - BTN_Y0;
  if (ph == Ph::Start || ph == Ph::Result || ph == Ph::Over) {
    const char *label = ph == Ph::Over ? T("AGAIN") : (hands_done >= HANDS ? T("RESULT") : T("DEAL"));
    button(SIDE_X, BTN_Y0, SIDE_W, big, label, unlocked);
  } else {
    bool mine = ph == Ph::Player && unlocked;
    button(SIDE_X, BTN_Y0, SIDE_W, BTN_H, T("HIT"), mine);
    button(SIDE_X, BTN_Y0 + BTN_PITCH, SIDE_W, BTN_H, T("STAND"), mine);
    if (ph == Ph::Player && knock_n == 1) {  // one knock heard: STAND unless a second comes
      uint16_t c = theme::current() == theme::VFD ? HOT : 0xFFFF;
      frame(SIDE_X - 3, BTN_Y0 + BTN_PITCH - 3, SIDE_W + 6, BTN_H + 6, c);
      frame(SIDE_X - 2, BTN_Y0 + BTN_PITCH - 2, SIDE_W + 4, BTN_H + 4, c);
    }
  }
}

// Before the first hand: the name and the length of the match.
void drawStartScreen()
{
  const char *t = "21";
  text(t, 506 - textWidth(t, 7) - 6, 12, 7, HOT_INK);
  const char *n = T("10 HANDS");
  text(n, 506 - textWidth(n, 3) - 6, 132, 3, LIT_INK);  // low: the dealer's bubble covers the middle
}

// After the tenth hand: who took the match, and the score.
void drawOverScreen(uint32_t now)
{
  const char *r = p_wins > d_wins ? T("YOU WIN") : (p_wins < d_wins ? T("HOUSE WINS") : T("DRAW"));
  bool flash = p_wins > d_wins && (int32_t)(now - result_ms) < 2400 && ((now - result_ms) / 200) % 2;
  feltText(r, 112, 4, p_wins > d_wins ? (flash ? HOT_INK : LIT_INK) : (p_wins < d_wins ? HOT_INK : LIT_INK));
  int x0 = DLR_W + 2, w = SIDE_X - 6 - x0;
  drawScore(x0 + w / 2 - 90, 30, 180, 9);
}

void blackjackDraw(const DateTime &)
{
  uint32_t now = millis();
  Pal p = pal();
  clear();
  drawWall(p);
  drawDealer(now, p);
  drawFelt(p);
  if (ph == Ph::Start) {
    drawStartScreen();
  } else if (ph == Ph::Over) {
    drawOverScreen(now);
  } else {
    bool reveal = hole_up || ph == Ph::Result;
    drawHand(dl, ROW_D, !reveal, p, now);
    drawHand(pl, ROW_P, false, p, now);
    drawTotal(dl, ROW_D, reveal ? dl.n : 1, now);
    drawTotal(pl, ROW_P, pl.n, now);
    if (ph == Ph::Result) {
      const char *r = resultText();
      bool won = res[hands_done - 1] > 0;
      bool flash = won && (int32_t)(now - result_ms) < 1600 && ((now - result_ms) / 200) % 2;
      int w = textWidth(r, 2);
      text(r, HAND_X + (HAND_W - w) / 2, ROW_D + CH + 2, 2, won ? (flash ? LIT_INK : HOT_INK) : LIT_INK);
    }
  }
  drawSide(now);
  if (bubbleOn(now)) drawBubble(p);
  if (theme::current() == theme::C64) {  // the machine's border (the app draws the whole panel)
    const theme::Def &d = theme::def();
    rect(0, 0, W, d.border_w, d.border);
    rect(0, H - d.border_w, W, d.border_w, d.border);
    rect(0, 0, d.border_w, H, d.border);
    rect(W - d.border_w, 0, d.border_w, H, d.border);
  }
}

// ---------------------------------------------------------------------------------------------------------------------
// Input and loop
// ---------------------------------------------------------------------------------------------------------------------

void blackjackEnter()
{
  uint32_t now = millis();
  for (int i = 0; i < EV_COUNT; i++) last_line[i] = -1;
  if (ph == Ph::Deal || ph == Ph::Player || ph == Ph::Dealer) {
    // back in the middle of a hand: the cards stay on the table, the dealer carries on
    for (int i = 0; i < pl.n; i++) pl.born[i] = now - 1000;
    for (int i = 0; i < dl.n; i++) dl.born[i] = now - 1000;
    if (ph == Ph::Deal) ph = Ph::Player;
    if (ph == Ph::Dealer) {
      dealer_next_ms = now + 400;
      hole_up = false;
    }
  } else if (ph == Ph::Over) {  // a finished match: a new one
    newMatch();
    ph = Ph::Start;
  }
  lock_until = now + 300;
  touching = false;
  knock_n = 0;
  Motion_TakeKnock();  // forget old knocks
  jingle_n = 0;
  last_input_ms = now;
  nudge_ms = now;
  blink_at = now + 2000 + esp_random() % 2000;
  bubble[0] = 0;
  bubble_until = 0;
  react = -1;
  if (ph == Ph::Start) {
    say(EV_START);
    bubble_until = now + 3000;
  }
}

void blackjackExit() { jingle_n = 0; }

bool blackjackGesture(Gesture g, int x, int y)
{
  if (g == Gesture::LongPress) return true;
  // Tap, and an up/down swipe too: a sloppy tap. (Left/right swipes are the framework's: back to the Play menu.)
  if (g != Gesture::Tap && g != Gesture::SwipeUp && g != Gesture::SwipeDown) return false;
  uint32_t now = millis();
  last_input_ms = now;
  nudge_ms = now;
  state.dirty = true;
  if (x < DLR_W) {  // poke the dealer
    if (!bubbleOn(now)) say(EV_POKE);
    return true;
  }
  if (x < SIDE_X - 6) return true;
  if (y < BTN_Y0 - 2) return true;
  if ((int32_t)(now - lock_until) < 0) return true;
  if (ph == Ph::Start || ph == Ph::Result || ph == Ph::Over) {
    dealAction();
  } else if (ph == Ph::Player) {
    if (y < BTN_Y0 + BTN_H + (BTN_PITCH - BTN_H) / 2) hit();
    else stand();
  }
  return true;
}

void blackjackTouch(bool down, int, int)
{
  touching = down;
  if (down) {
    last_down_ms = millis();
    knock_n = 0;  // a finger on the glass settles it: no knock action after a tap
  } else {
    last_up_ms = millis();
  }
}

// Is the touch driver's last edge within 250 ms of t?
bool touchNear(uint32_t t)
{
  int32_t d = (int32_t)(last_down_ms - t), u = (int32_t)(last_up_ms - t);
  return touching || (d > -250 && d < 250) || (u > -250 && u < 250);
}

void blackjackTick()
{
  uint32_t now = millis();
  // Knocks. One that comes while a beep shakes the case, while the cards are moving or between hands' phases is dropped
  // (a double knock on the betting screen deals once; its second knock lands in the deal and is gone). A knock lands
  // slightly before the touch driver reports the finger (30 ms polling): a knock is acted on KNOCK_TOUCH_MS late at the
  // earliest, so touchNear() can tell a real knock from a finger landing on the glass.
  if (Motion_TakeKnock()) {
    bool listen = (int32_t)(now - beep_quiet_until) > 0 && (int32_t)(now - lock_until) >= 0 &&
                  (ph == Ph::Player || ph == Ph::Start || ph == Ph::Result || ph == Ph::Over);
    if (listen) {
      if (knock_n == 1 && (int32_t)(now - knock_t[0]) <= (int32_t)KNOCK_PAIR_MS) {
        knock_t[1] = now;
        knock_n = 2;
      } else if (knock_n == 0) {
        knock_t[0] = now;
        knock_n = 1;
      }
      state.dirty = true;
    }
  }
  if (knock_n) {
    uint32_t since0 = now - knock_t[0], last = knock_t[knock_n - 1];
    bool finger = touchNear(knock_t[0]) || (knock_n == 2 && touchNear(knock_t[1]));
    int n = 0;
    if (ph != Ph::Player) n = since0 >= KNOCK_TOUCH_MS ? 1 : 0;            // betting / result: deal now
    else if (knock_n == 2) n = now - last >= KNOCK_TOUCH_MS ? 2 : 0;       // a pair: hit
    else if (since0 > KNOCK_PAIR_MS) n = 1;                              // no second knock came: stand
    if (n) {
      knock_n = 0;
      state.dirty = true;
      if (!finger) knockAction(n);
    }
  }

  if (ph == Ph::Deal && (int32_t)(now - phase_ms) >= 0) {
    // the deal is down: a blackjack ends the hand at once, otherwise it is your turn
    if (natural(pl) || natural(dl)) {
      settle();
    } else {
      ph = Ph::Player;
    }
    state.dirty = true;
  }
  if (ph == Ph::Dealer && (int32_t)(now - phase_ms) >= 0) {
    if (total(pl, pl.n) > 21) {  // the player busted: no draw, show it
      settle();
      state.dirty = true;
    } else if (dealer_next_ms && (int32_t)(now - dealer_next_ms) >= 0) {
      if (!hole_up) {  // turn the hole card
        hole_up = true;
        beep(440, 18);
        dealer_next_ms = now + 650;
      } else if (total(dl, dl.n) < 17) {  // stands on every 17
        addCard(dl, now);
        beep(520, 18);
        dealer_next_ms = now + 700;
      } else {
        dealer_next_ms = 0;
        settle();
      }
      state.dirty = true;
    }
  }
  if (ph == Ph::Result && hands_done >= HANDS && (int32_t)(now - result_ms) > (int32_t)OVER_MS) {  // the match is over
    endMatch();
    state.dirty = true;
  }
  // the dealer gets bored
  if ((ph == Ph::Start || ph == Ph::Player || (ph == Ph::Result && hands_done < HANDS)) && (int32_t)(now - last_input_ms) > 20000 && (int32_t)(now - nudge_ms) > 20000 && !bubbleOn(now)) {
    say(EV_IDLE);
    state.dirty = true;
  }
  if (jingle_n && jingle_i < jingle_n && (int32_t)(now - jingle_next) >= 0) {
    beep(jingle_hz[jingle_i], jingle_ms[jingle_i]);
    jingle_next = now + jingle_ms[jingle_i] + 35;
    jingle_i++;
  }
}

uint32_t blackjackFrameMs()
{
  uint32_t now = millis();
  bool anim = ph == Ph::Deal || ph == Ph::Dealer || bubbleOn(now) || ((ph == Ph::Result || ph == Ph::Over) && (int32_t)(now - result_ms) < 2500);
  for (int i = 0; i < pl.n && !anim; i++) anim = (int32_t)(now - pl.born[i]) < (int32_t)SLIDE_MS + 50;
  for (int i = 0; i < dl.n && !anim; i++) anim = (int32_t)(now - dl.born[i]) < (int32_t)SLIDE_MS + 50;
  return anim ? 40 : 70;  // 70: still fine enough for the 150 ms blink
}

}  // namespace

const App APP_BLACKJACK = { "blackjack", N_("21"), iconBlackjack, true, true, blackjackEnter, blackjackExit, blackjackDraw, blackjackGesture, blackjackTick, blackjackFrameMs, nullptr, nullptr, blackjackTouch };

#endif
