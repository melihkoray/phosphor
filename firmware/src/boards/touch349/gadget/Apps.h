#pragma once

// Menu pages ("Tools", "Play") and the small apps opened from them. A menu is a horizontal row of big tiles, one per app,
// drawn in the current theme like the settings page. A tap on a tile opens the app. While an app is open it owns tap,
// hold, vertical swipes and the motion sensor; a left/right swipe closes it and returns to its menu.
// To add an app: define an App (ToolApps.cpp has five) and list it in TOOLS or PLAY in Apps.cpp. A menu page is in the
// page ring only while it lists at least one app.

#include <Arduino.h>
#include "Gadget.h"

struct App {
  const char *key;          // /cmd name=app arg=<key>
  const char *en;           // tile name: up to 9 letters
  // The tile icon, drawn in a 48 x 48 box at (x, y) in one color; the menu adds the glow.
  void (*icon)(int x, int y, uint16_t color);
  bool needs_speaker;       // the radio is paused while the app is open and resumes when it closes
  bool full_screen;         // draws the whole panel: no theme border or banner over it
  void (*enter)();          // may be null
  void (*exit)();
  void (*draw)(const DateTime &now);
  // Tap, hold and vertical swipes; true = handled (an unhandled up/down swipe changes the volume).
  bool (*gesture)(Gesture g, int x, int y);
  void (*tick)();           // every loop while open (motion, timers)
  uint32_t (*frame_ms)();   // redraw period, null or 0 = every 50 ms
  // Games only (null = off): act on a finger touching down, not on its release (true = used, then the touch makes no tap or
  // swipe); and, while it returns true, a left/right swipe is swallowed instead of closing the app.
  bool (*touch_down)(int x, int y);
  bool (*swipes_locked)();
  // The finger touching down / lifting (games that need the press length, not just the tap on release); may be null.
  void (*touch)(bool down, int x, int y);
};

// Menu pages: Mode::Tools or Mode::Play.
int Apps_Count(Mode menu);
void Apps_DrawMenu(Mode menu);
void Apps_TapMenu(Mode menu, int x, int y);  // a tile was tapped: open its app

bool Apps_Open(const char *key);  // HTTP /cmd name=app; false for an unknown key
void Apps_Close();                // back to the menu it was opened from
bool Apps_Active();
bool Apps_FullScreen();
void Apps_Draw(const DateTime &now);
// Gesture while Mode::App; true = used (a left/right swipe closes the app).
bool Apps_Gesture(Gesture g, int x, int y);
bool Apps_TouchDown(int x, int y);  // see App::touch_down
void Apps_Tick();
void Apps_Touch(bool down, int x, int y);  // finger down / up edge, from the touch driver; ignored unless an app is open
uint32_t Apps_FrameMs();
const char *Apps_Key();  // key of the open app, "" if none

// Drawing helpers for apps (flat colors, no clipping to the circle's bounding box).
void Apps_Disc(int cx, int cy, int r, uint16_t color);
