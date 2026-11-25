// Host test for the screen texts: the language tables (Lang*.cpp, the same files the device runs) and T(), TCTX(),
// lang::lookup(), lang::fromCode(), and the printf formats in the tables.
//   g++ -std=c++17 -O2 -Wall -Wextra -Wformat=2 -o /tmp/lang_test firmware/test/lang_test.cpp firmware/src/boards/touch349/gadget/Lang*.cpp && /tmp/lang_test
// tools/check_i18n.py checks the tables against the code; this checks that the code that reads them does what it says.

#include <cstdio>
#include <cstring>
#include <string>
#include "../src/boards/touch349/gadget/Lang.h"

static lang::Id cur = lang::Id::EN;
lang::Id lang::current() { return cur; }  // on the device: the settings (Settings.cpp)

static int fails = 0;
#define CHECK(c)                                                       \
  do {                                                                 \
    if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } \
  } while (0)
static bool eq(const char *a, const char *b) { return a && b && !strcmp(a, b); }

static bool validUtf8(const char *s)
{
  for (const unsigned char *p = (const unsigned char *)s; *p;) {
    int n = *p < 0x80 ? 0 : (*p & 0xE0) == 0xC0 ? 1 : (*p & 0xF0) == 0xE0 ? 2 : (*p & 0xF8) == 0xF0 ? 3 : -1;
    if (n < 0 || (n == 1 && *p < 0xC2)) return false;
    p++;
    for (int i = 0; i < n; i++, p++)
      if ((*p & 0xC0) != 0x80) return false;
  }
  return true;
}

static void tables()
{
  for (int i = 0; i < (int)lang::Id::COUNT; i++) {
    const lang::Data &d = lang::data((lang::Id)i);
    CHECK(d.code && d.name && validUtf8(d.name));
    CHECK((i == 0) == (d.table == nullptr));  // only English has no table
    for (int w = 0; w < 7; w++) CHECK(d.weekday_long[w] && d.weekday_short[w] && validUtf8(d.weekday_long[w]));
    for (int m = 0; m < 12; m++) CHECK(d.month_long[m] && d.month_upper[m] && d.month_title[m] && validUtf8(d.month_long[m]));
    for (int j = 0; j < i; j++) CHECK(strcmp(d.code, lang::data((lang::Id)j).code) != 0);
    for (size_t k = 0; k < d.count; k++) {
      const lang::Entry &e = d.table[k];
      if (k) CHECK(strcmp(d.table[k - 1].en, e.en) < 0);  // sorted, no key twice: the bisection needs it
      CHECK(validUtf8(e.en) && validUtf8(e.text));
      std::string copy = e.en;  // another pointer: the search compares the text, not the address
      CHECK(lang::lookup(d, copy.c_str()) == e.text);
    }
  }
  // every language has the same keys as the first table (check_i18n.py also checks them against the code)
  const lang::Data &tr = lang::data(lang::Id::TR);
  for (int i = 1; i < (int)lang::Id::COUNT; i++) {
    const lang::Data &d = lang::data((lang::Id)i);
    CHECK(d.count == tr.count);
    for (size_t k = 0; k < tr.count; k++) CHECK(lang::lookup(d, tr.table[k].en));
  }
}

static void lookups()
{
  const lang::Data &en = lang::data(lang::Id::EN), &tr = lang::data(lang::Id::TR);
  CHECK(lang::lookup(en, "BREATHE") == nullptr);  // English: no table
  CHECK(lang::lookup(tr, "NO SUCH TEXT") == nullptr);
  CHECK(lang::lookup(tr, nullptr) == nullptr);
  CHECK(lang::lookup(tr, "") == nullptr);
  CHECK(lang::lookup(tr, "breathe") == nullptr);    // a context alone is not a key
  CHECK(lang::lookup(tr, "BREATHE ") == nullptr);   // no prefix or trailing-space matches
  CHECK(lang::lookup(tr, "BREATH") == nullptr);
  CHECK(lang::lookup(tr, tr.table[0].en) == tr.table[0].text);                        // the ends of the bisection
  CHECK(lang::lookup(tr, tr.table[tr.count - 1].en) == tr.table[tr.count - 1].text);
  CHECK(&lang::data((lang::Id)200) == &en);  // out of range: English
  CHECK(&lang::data(lang::Id::COUNT) == &en);
}

static void translate()
{
  const char *k = "NO SUCH TEXT";
  for (int i = 0; i < (int)lang::Id::COUNT; i++) {
    cur = (lang::Id)i;
    CHECK(T(k) == k);  // missing: the English itself, the same pointer
  }
  cur = lang::Id::EN;
  CHECK(eq(T("BREATHE"), "BREATHE"));
  CHECK(eq(TCTX("breathe", "PAUSED"), "PAUSED"));
  CHECK(eq(TCTX("nosuch", "BREATHE"), "BREATHE"));
  cur = lang::Id::TR;
  CHECK(eq(T("BREATHE"), "NEFES"));
  CHECK(eq(T("PAUSED"), "DURDU"));                       // the two Turkish words HEAD had for PAUSED
  CHECK(eq(TCTX("breathe", "PAUSED"), "DURAKLADI"));
  CHECK(eq(T("LEVEL"), "TERAZİ"));                       // and for LEVEL
  CHECK(eq(TCTX("readout", "LEVEL"), "DÜZ"));
  CHECK(eq(TCTX("count", "RESET"), "SIFIR"));
  CHECK(eq(T("RESET"), "SIFIRLA"));
  CHECK(eq(TCTX("nosuch", "BREATHE"), "BREATHE"));        // no entry for the context: the English, not "NEFES"
  cur = lang::Id::PL;
  CHECK(eq(T("BREATHE"), "ODDECH"));
  CHECK(eq(TCTX("breathe", "PAUSED"), "PAUZA"));
  CHECK(eq(TCTX("nosuch", "PAUSED"), "PAUSED"));

  // edge cases: an empty text is itself; a context with an empty text is the empty tail of its own literal
  const char *empty = "";
  CHECK(T(empty) == empty);
  CHECK(eq(TCTX("count", ""), ""));
  // a returned text lives as long as the program: a pointer kept across a change of language still reads the same
  cur = lang::Id::TR;
  const char *kept = T("BREATHE"), *kept_ctx = TCTX("count", "RESET");
  cur = lang::Id::PL;
  CHECK(eq(kept, "NEFES") && eq(kept_ctx, "SIFIR"));
  CHECK(T("BREATHE") != kept);
  // a key with letters outside ASCII (the bisection compares bytes, as the tables are sorted)
  cur = lang::Id::TR;
  CHECK(eq(T("Tap: next page · PWR: screen on/off · BOOT: brightness, hold 5 s: reset Wi-Fi"),
           "Dokun: sonraki sayfa · PWR: ekran aç/kapa · BOOT: parlaklık, 5 sn: Wi-Fi sıfırla"));
}

static void codes()
{
  struct { const char *in; bool ok; lang::Id id; } cases[] = {
    { "en", true, lang::Id::EN }, { "tr", true, lang::Id::TR }, { "pl", true, lang::Id::PL },
    { " pl ", true, lang::Id::PL }, { "PL", true, lang::Id::PL }, { "Tr", true, lang::Id::TR }, { "tr\r", true, lang::Id::TR },
    // no prefix matches (HEAD took "trash" for Turkish), nothing empty, no other spelling
    { "trash", false, lang::Id::EN }, { "plx", false, lang::Id::EN }, { "pl_PL", false, lang::Id::EN },
    { "p", false, lang::Id::EN }, { "", false, lang::Id::EN }, { "   ", false, lang::Id::EN },
    { "e n", false, lang::Id::EN }, { "english", false, lang::Id::EN }, { "\xC3\xBC", false, lang::Id::EN },
  };
  for (auto &c : cases) {
    lang::Id id = lang::Id::TR;  // a wrong code must leave it as it was
    bool ok = lang::fromCode(c.in, id);
    CHECK(ok == c.ok);
    CHECK(id == (c.ok ? c.id : lang::Id::TR));
    if (ok != c.ok) printf("  fromCode(\"%s\")\n", c.in);
  }
  lang::Id id = lang::Id::PL;
  CHECK(!lang::fromCode(nullptr, id) && id == lang::Id::PL);
  for (int i = 0; i < (int)lang::Id::COUNT; i++) {  // every language can be written and read back
    lang::Id back = lang::Id::EN;
    CHECK(lang::fromCode(lang::data((lang::Id)i).code, back) && back == (lang::Id)i);
  }
}

// The printf formats in the tables, each with arguments of the types its call site passes (-Wformat checks the call
// sites against the English through format_arg; here every translation is run).
static void formats()
{
  char b[64];
  for (int i = 0; i < (int)lang::Id::COUNT; i++) {
    cur = (lang::Id)i;
    snprintf(b, sizeof(b), T("%d MIN"), 25);
    CHECK(strstr(b, "25") && !strchr(b, '%'));
    snprintf(b, sizeof(b), T("%lu min"), 4294967295UL);
    CHECK(strstr(b, "4294967295") && !strchr(b, '%'));
    snprintf(b, sizeof(b), T("%u / %u KB free"), 123u, 456u);
    CHECK(strstr(b, "123") && strstr(b, "456") && strstr(b, "123") < strstr(b, "456"));
    snprintf(b, sizeof(b), T("%u KB free"), 789u);
    CHECK(strstr(b, "789") && !strchr(b, '%'));
    snprintf(b, sizeof(b), T("%d%%"), 45);
    CHECK(strstr(b, "45") && strchr(b, '%') && strlen(b) == 3);
    snprintf(b, sizeof(b), T("SCREEN TO %d%%"), 15);
    CHECK(strstr(b, "15") && strstr(b, "%") && !strstr(b, "%%"));
    char small[16];  // Percent()'s buffer: the longest int fits in every language
    CHECK(snprintf(small, sizeof(small), T("%d%%"), -2147483647 - 1) < (int)sizeof(small));
  }
  cur = lang::Id::TR;
  snprintf(b, sizeof(b), T("%d%%"), 45);
  CHECK(eq(b, "%45"));  // Turkish writes the sign first
  snprintf(b, sizeof(b), T("SCREEN TO %d%%"), 15);
  CHECK(eq(b, "EKRAN %15"));
  cur = lang::Id::PL;
  snprintf(b, sizeof(b), T("SCREEN TO %d%%"), 15);
  CHECK(eq(b, "EKRAN NA 15%"));
}

int main()
{
  tables();
  lookups();
  translate();
  codes();
  formats();
  printf(fails ? "%d failure(s)\n" : "all passed\n", fails);
  return fails ? 1 : 0;
}
