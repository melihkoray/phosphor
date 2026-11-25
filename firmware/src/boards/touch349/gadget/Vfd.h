#pragma once

// Drawing kit for the 640x172 bar. Its default is the VFD look (design/retro/mockups.html, direction A): a near-black
// glass, unlit "ghost" segments and dots, lit cyan with a one-pixel glow, and synthwave magenta for what matters. Other
// themes (Theme.h) swap the palette, the font and turn the VFD effects off. Everything draws into one RGB565 canvas;
// the board pushes it to the panel.

#include <Arduino.h>

namespace vfd {

constexpr int W = 640;
constexpr int H = 172;

// Palette of the current theme (theme::apply sets it; these start as the VFD look of design/retro/mockups.html).
extern uint16_t BG;     // glass / screen
extern uint16_t GHOST;  // unlit segment / dot, inactive labels
extern uint16_t FAINT;  // unlit dot of the weather sky matrix
extern uint16_t HALO;   // glow around lit
extern uint16_t LIT;    // main ink
extern uint16_t DIM;    // secondary text
extern uint16_t HOT;    // accent (synthwave magenta in VFD)
extern uint16_t HOTH;   // glow around the accent
extern uint16_t KELP;
constexpr uint16_t NONE = 0;  // "no color" in Ink

struct Ink {
  uint16_t c;              // lit color
  uint16_t h = NONE;       // glow, NONE = no glow
  uint16_t ghost = NONE;   // unlit cells/segments, NONE = not drawn
};

// Rebuilt from the palette by theme::apply.
extern Ink LIT_INK;
extern Ink HOT_INK;
extern Ink DIM_INK;
extern Ink GHOST_INK;

// A bitmap font: glyph cells of w x h font pixels, advancing adv per glyph. rows(cp) returns h bytes, bit (w-1-col)
// lit = column col, or nullptr when the font has no such glyph. Turkish and Polish accents are dots drawn above/below the base
// letter from 5-wide masks, shifted right by accent_dx columns.
struct Font {
  uint8_t w, h, adv;
  uint8_t accent_dx;
  const uint8_t *(*rows)(uint32_t cp);
};
extern const Font VFD_FONT;

// How the kit draws: which font, and whether the VFD effects are on.
struct Style {
  const Font *font = &VFD_FONT;
  bool ghosts = true;  // draw Ink.ghost (unlit cells and segments)
  bool glow = true;    // draw Ink.h
  bool dots = true;    // text cells as dots of size d (gaps between them); false = solid pixels (d = p)
};
void setStyle(const Style &s);
const Style &style();

void begin(uint16_t *canvas);
uint16_t *canvas();
void clear(uint16_t color = BG);
// Drawing is clipped to this box (default: the whole screen).
void clip(int x0, int y0, int x1, int y1);
void noClip();
void rect(int x, int y, int w, int h, uint16_t color);
void frame(int x, int y, int w, int h, uint16_t color);  // 1 px outline

// Text in the current font: every font cell is a d x d dot at pitch p (5x7 VFD font: glyphs advance 6 p). UTF-8 in;
// lower case is shown as upper case, Turkish and Polish letters keep their accents, other accented letters lose them. Returns the
// end x.
int text(const char *s, int x, int y, int p, int d, const Ink &ink);
inline int text(const char *s, int x, int y, int p, const Ink &ink) { return text(s, x, y, p, p, ink); }
int textWidth(const char *s, int p);
int glyphCount(const char *s);

// 7-segment digits with chamfered ends. s holds digits, ' ', '-' and ':'. Returns the end x.
int seg(const char *s, int x, int y, int w, int h, int t, int gap, const Ink &ink);

// Sprite from strings of '#' and '.', each '#' an s x s block.
void sprite(const char *const *rows, int n, int x, int y, int s, uint16_t color, bool flip = false);

}  // namespace vfd
