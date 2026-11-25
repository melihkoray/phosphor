#pragma once

// Screen texts in more than one language. The code holds the English text and asks for it with T("..."); a language
// other than English has a table that maps each English text to its own (LangTr.cpp, LangPl.cpp). A text missing from
// the table shows in English. tools/check_i18n.py checks the tables against the code (build.sh runs it).
//
// Adding a language: a LangXx.cpp in the shape of LangPl.cpp, its Id below, its DATA_XX in ALL (Lang.cpp), the glyphs
// its letters need (Vfd.cpp resolve(), design/themes/src/fonts.js), and the /settings page (SettingsPage.cpp).
//
// This header needs no Arduino and no settings: the language on screen comes from lang::current(), which the settings
// define (Settings.cpp). The host tests (firmware/test/lang_test.cpp) define it themselves.

#include <stddef.h>
#include <stdint.h>

namespace lang {

struct Entry {
  const char *en;    // the key: the English text as written in the code
  const char *text;  // the same text in the language of the table
};

enum class Id : uint8_t { EN, TR, PL, COUNT };  // the order is the order of the language switch on the settings screen

// Everything one language brings: its name, the table of texts and the words the dates are written with.
struct Data {
  const char *code;        // in the settings file: lang=<code>
  const char *name;        // of the language in itself, for the settings screen
  const Entry *table;      // sorted by strcmp order of en; null for English: the code holds its texts
  size_t count;
  const char *weekday_long[7];   // Sunday first: "Sunday"
  const char *weekday_short[7];  // "SUN"
  const char *month_long[12];    // after a day number: "24 January" (Polish: genitive, "24 stycznia")
  const char *month_upper[12];   // "JAN" (the whole name where the language does not shorten it)
  const char *month_title[12];   // "Jan"
};

const Data &data(Id id);  // English for an id out of range
Id current();             // the language on screen; defined by the settings (Settings.cpp)

// The language whose code is the text (exactly, after trimming spaces, any case). Returns false and leaves id as it was
// for anything else.
bool fromCode(const char *code, Id &id);

// Binary search in a table sorted by strcmp order of "en". Null when the table has no such key (or key is null).
const char *lookup(const Data &d, const char *key);

}  // namespace lang

inline const lang::Data &Lang() { return lang::data(lang::current()); }

// The text for the current language; en is the English text, which is also the key of the language tables.
// format_arg: a T("%d MIN") given to printf is checked by the compiler against the English text (-Wformat), and
// tools/check_i18n.py checks that every translation has the same conversions.
__attribute__((format_arg(1))) inline const char *T(const char *en)
{
  const char *r = lang::lookup(Lang(), en);
  return r ? r : en;
}

// The same, for an English text that needs two different translations: the context tells the uses apart.
// TCTX("breathe", "PAUSED") has the key "breathe\004PAUSED" in the tables; without an entry it shows PAUSED (the
// English, not the translation of the bare "PAUSED"). Both arguments must be string literals.
__attribute__((format_arg(1))) inline const char *Tc(const char *key, size_t ctx_size)
{
  const char *r = lang::lookup(Lang(), key);
  return r ? r : key + ctx_size;
}
#define TCTX(ctx, en) Tc(ctx "\004" en, sizeof(ctx))

// Marks an English text that is kept in a table or a struct and translated later with T(variable): it is the text
// itself, the mark lets tools/check_i18n.py see it as a key.
#define N_(en) en

#ifdef ARDUINO
#include <Arduino.h>
// A percentage as the language writes it: 45% or %45.
inline String Percent(int v)
{
  char b[16];
  snprintf(b, sizeof(b), T("%d%%"), v);
  return b;
}
#endif
