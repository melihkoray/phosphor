#include "Lang.h"

#include <ctype.h>
#include <string.h>

namespace lang {

// In LangXx.cpp. All of these are constant-initialized (addresses of literals and of the tables), so any code may use
// them at any time, also from other files' static constructors.
extern const Data DATA_TR, DATA_PL;

static const Data DATA_EN = {
  "en", "ENGLISH", nullptr, 0,
  { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" },
  { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" },
  { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" },
  { "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" },
  { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" },
};

// Indexed by Id: one line per language.
static const Data *const ALL[] = { &DATA_EN, &DATA_TR, &DATA_PL };
static_assert(sizeof(ALL) / sizeof(ALL[0]) == (size_t)Id::COUNT, "one Data per lang::Id, in the order of Id");

const Data &data(Id id)
{
  return (size_t)id < (size_t)Id::COUNT ? *ALL[(size_t)id] : DATA_EN;
}

bool fromCode(const char *code, Id &id)
{
  if (!code) return false;
  while (isspace((unsigned char)*code)) code++;
  size_t n = strlen(code);
  while (n && isspace((unsigned char)code[n - 1])) n--;
  for (size_t i = 0; i < (size_t)Id::COUNT; i++) {
    const char *c = ALL[i]->code;
    if (strlen(c) != n) continue;
    size_t k = 0;
    while (k < n && tolower((unsigned char)code[k]) == c[k]) k++;
    if (k == n) {
      id = (Id)i;
      return true;
    }
  }
  return false;
}

const char *lookup(const Data &d, const char *key)
{
  if (!key || !d.table) return nullptr;
  size_t lo = 0, hi = d.count;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    int c = strcmp(key, d.table[mid].en);
    if (c == 0) return d.table[mid].text;
    if (c < 0) hi = mid;
    else lo = mid + 1;
  }
  return nullptr;
}

}  // namespace lang
