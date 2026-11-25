#!/usr/bin/env python3
"""Checks the screen texts of the firmware against its language tables (firmware/src/boards/touch349/gadget/Lang*.cpp).

build.sh runs it before compiling; any error stops the build (exit 1). Checked:
- the language registry: lang::Id in Lang.h, ALL[] in Lang.cpp and the DATA_XX of each LangXx.cpp agree, codes unique;
- each table: every line parses, sorted in strcmp (byte) order, no key twice (Lang.cpp searches it by bisection);
- the keys: the texts of T("..."), TCTX("ctx", "...") and N_("...") in the code. Every language table has every key
  (a text that is the same in a language is written out, { "WI-FI", "WI-FI" }) and no key the code does not use;
- calls: T()/TCTX()/N_() with one string literal; a name like T..., Tc or N_ that is not one of them (a typo such as
  TC("x", "y") would not compile, or would not be a key); the old L("en", "tr") and settings::tr();
- printf conversions: a translation has the same % conversions in the same order as its key;
- characters: valid UTF-8, no control characters, and every letter can be drawn: by the VFD font (after Vfd.cpp
  resolve(): upper case, accents as dots) and by the C64 and Amiga fonts (ThemeFonts.h);
- the /settings page (SettingsPage.cpp): its language buttons and its tables are the languages of the registry.
Warnings (no failure): translations much longer than the English, page names of the web page that differ from the screen.

  tools/check_i18n.py [--root DIR] [-v]
"""
import argparse, pathlib, re, sys

S = r'"((?:[^"\\\n]|\\.)*)"'  # one C string literal
FMT = re.compile(r'%(%|[-+ #0]*(?:\d+|\*)?(?:\.(?:\d+|\*))?(?:hh|h|ll|l|z|j|t|L)?[diouxXeEfgGcsp])|%')
KNOWN_CALLS = {'T', 'TCTX', 'N_'}


def unescape(s):
    def one(m):
        e = m[1]
        if e[0] in '01234567': return chr(int(e, 8))
        if e[0] == 'x': return chr(int(e[1:], 16))
        return {'n': '\n', 't': '\t', 'r': '\r', '0': '\0'}.get(e, e)
    return re.sub(r'\\([0-7]{1,3}|x[0-9a-fA-F]+|.)', one, s)


def strip_comments(src):
    """Blanks out // and /* */ comments, keeping string literals and line numbers."""
    def repl(m):
        t = m[0]
        return t if t[0] in '"\'' else re.sub(r'[^\n]', ' ', t)
    return re.sub(r'"(?:[^"\\\n]|\\.)*"|\'(?:[^\'\\\n]|\\.)*\'|//[^\n]*|/\*.*?\*/', repl, src, flags=re.S)


def specs(s):
    """The printf conversions of s, in order, without %%; a lone % that starts no conversion is '%?'."""
    return [m[0] if m[1] else '%?' for m in FMT.finditer(s) if m[1] != '%']


class Check:
    def __init__(self, root, verbose=False):
        self.root = pathlib.Path(root)
        self.src = self.root / 'firmware/src/boards/touch349'
        self.gadget = self.src / 'gadget'
        self.verbose = verbose
        self.errors, self.warnings = [], []

    def error(self, msg): self.errors.append(msg)
    def warn(self, msg): self.warnings.append(msg)

    def read(self, p):
        b = p.read_bytes()
        try:
            return b.decode('utf-8')
        except UnicodeDecodeError as e:
            line = b[:e.start].count(b'\n') + 1
            self.error(f'{p.relative_to(self.root)}:{line}: not valid UTF-8 (byte 0x{b[e.start]:02X})')
            return b.decode('utf-8', 'replace')

    def rel(self, p): return p.relative_to(self.root)

    # ---- the registry and the tables ---------------------------------------------------------------------------

    def load_tables(self):
        self.langs = {}  # name (TR) -> dict(code, keys, entries, data arrays, file)
        for f in sorted(self.gadget.glob('Lang?*.cpp')):
            text = self.read(f)
            m = re.search(r'^static const lang::Entry LANG_(\w+)\[\] = \{\n(.*?)^\};', text, re.M | re.S)
            d = re.search(r'^extern const Data DATA_(\w+) = \{\n(.*?)^\};', text, re.M | re.S)
            if not m or not d:
                self.error(f'{self.rel(f)}: no "static const lang::Entry LANG_XX[] = {{" table or "extern const Data DATA_XX"')
                continue
            name = m[1]
            if d[1] != name: self.error(f'{self.rel(f)}: LANG_{name} but DATA_{d[1]}')
            first = text[:m.start(2)].count('\n') + 1
            entries = []
            for i, line in enumerate(m[2].split('\n')):
                if not line.strip() or line.strip().startswith('//'): continue
                e = re.fullmatch(r'  \{ ' + S + ', ' + S + r' \},', line)
                if not e:
                    self.error(f'{self.rel(f)}:{first + i}: not a table line {{ "en", "text" }}: {line.strip()}')
                    continue
                entries.append((unescape(e[1]), unescape(e[2]), first + i))
            head = re.match(r'\s*' + S + r', ' + S + r', LANG_(\w+), ', d[2])
            arrays = [[unescape(x) for x in re.findall(S, a)] for a in re.findall(r'^\s*\{(.*)\},?\s*$', d[2], re.M)]
            if not head or head[3] != name:
                self.error(f'{self.rel(f)}: DATA_{name} must start with "code", "NAME", LANG_{name}, ...')
                continue
            self.langs[name] = dict(file=f, code=head[1], name=unescape(head[2]), entries=entries,
                                    keys={k: t for k, t, _ in entries}, arrays=arrays)
            for want, arr in zip((7, 7, 12, 12, 12), arrays):
                if len(arr) != want: self.error(f'{self.rel(f)}: DATA_{name}: a date word list has {len(arr)} words, not {want}')
            if len(arrays) != 5: self.error(f'{self.rel(f)}: DATA_{name}: {len(arrays)} date word lists, not 5')
            for (a, _, la), (b, _, lb) in zip(entries, entries[1:]):
                if a == b: self.error(f'{self.rel(f)}:{lb}: {a!r} twice')
                elif a.encode() > b.encode(): self.error(f'{self.rel(f)}:{lb}: {b!r} is not after {a!r} (strcmp order)')

    def check_registry(self):
        h = self.read(self.gadget / 'Lang.h')
        c = self.read(self.gadget / 'Lang.cpp')
        e = re.search(r'enum class Id : uint8_t \{([^}]*)\}', h)
        a = re.search(r'ALL\[\] = \{([^}]*)\}', c)
        if not e or not a:
            self.error('Lang.h/Lang.cpp: no "enum class Id" or no "ALL[] = { ... }"')
            return
        ids = [x.strip() for x in e[1].split(',') if x.strip()]
        if ids[-1:] != ['COUNT']: self.error('Lang.h: lang::Id must end with COUNT')
        ids = [x for x in ids if x != 'COUNT']
        all_ = [x.strip().lstrip('&') for x in a[1].split(',') if x.strip()]
        if all_ != ['DATA_' + i for i in ids]:
            self.error(f'Lang.cpp: ALL[] is {all_}, lang::Id wants {["DATA_" + i for i in ids]}')
        for name in self.langs:
            if name not in ids: self.error(f'Lang{name.title()}.cpp: {name} is not in lang::Id (Lang.h) and ALL[] (Lang.cpp)')
        for i in ids[1:]:
            if i not in self.langs: self.error(f'lang::Id::{i} has no Lang{i.title()}.cpp table')
        codes = ['en'] + [self.langs[i]['code'] for i in ids[1:] if i in self.langs]
        if len(set(codes)) != len(codes): self.error(f'language codes twice: {codes}')
        for code in codes:
            if not re.fullmatch(r'[a-z]{2,3}', code): self.error(f'language code {code!r}: two or three lower case letters')
        self.codes = codes
        self.order = ids

    # ---- the code ----------------------------------------------------------------------------------------------

    def scan_code(self):
        self.used = {}  # key -> first place
        self.dynamic = []
        for p in sorted(list(self.src.rglob('*.cpp')) + list(self.src.rglob('*.h'))):
            if p.name.startswith('Lang') and p.suffix == '.cpp' or 'ExternLib' in p.parts: continue
            src = strip_comments(self.read(p))
            if p.name == 'Lang.h':  # not the definitions of T, Tc, TCTX and N_ themselves
                src = re.sub(r'^#define (TCTX|N_)\(.*$|^(?:__attribute__\(\(format_arg\(1\)\)\) )?inline const char \*(T|Tc)\(', lambda m: ' ' * len(m[0]), src, flags=re.M)
            def where(pos): return f'{self.rel(p)}:{src.count(chr(10), 0, pos) + 1}'
            for m in re.finditer(r'\b([A-Za-z_]\w*)\s*\(\s*"', src):
                name = m[1]
                if name in KNOWN_CALLS: continue
                if name == 'L' or re.fullmatch(r'T[A-Z_]*|Tc|N_?\w*_', name):
                    self.error(f'{where(m.start())}: {name}("...") is not T, TCTX or N_ (Lang.h)')
            for m in re.finditer(r'\bsettings::tr\s*\(', src):
                self.error(f'{where(m.start())}: settings::tr() is gone: T("English") (Lang.h)')
            for m in re.finditer(r'\b(T|N_)\(', src):
                lit = re.match(r'\s*' + S + r'\s*\)', src[m.end():])
                if lit:
                    key = unescape(lit[1])
                    if '\x04' in key: self.error(f'{where(m.start())}: {m[1]}() with \\004 in it: a context is TCTX("ctx", "...")')
                    self.used.setdefault(key, where(m.start()))
                elif re.match(r'\s*"', src[m.end():]):
                    self.error(f'{where(m.start())}: {m[1]}() takes one string literal (no "a" "b", no expression)')
                elif m[1] == 'T':
                    self.dynamic.append(where(m.start()))
                else:
                    self.error(f'{where(m.start())}: N_() takes one string literal')
            for m in re.finditer(r'\bTCTX\(', src):
                lit = re.match(r'\s*' + S + r'\s*,\s*' + S + r'\s*\)', src[m.end():])
                if not lit:
                    self.error(f'{where(m.start())}: TCTX() takes two string literals')
                    continue
                ctx, en = unescape(lit[1]), unescape(lit[2])
                if not re.fullmatch(r'[a-z]+', ctx): self.error(f'{where(m.start())}: TCTX context {ctx!r}: lower case letters only')
                if not en: self.error(f'{where(m.start())}: TCTX with an empty text')
                self.used.setdefault(ctx + '\x04' + en, where(m.start()))

    # ---- keys, formats, characters -----------------------------------------------------------------------------

    def check_keys(self):
        for name, l in self.langs.items():
            f = self.rel(l['file'])
            for k in sorted(set(self.used) - set(l['keys'])):
                self.error(f'{f}: no entry for {show(k)} ({self.used[k]})')
            for k, _, line in l['entries']:
                if k not in self.used: self.error(f'{f}:{line}: {show(k)} is used nowhere in the code')
                if '\x04' in k and not re.fullmatch(r'[a-z]+\x04.+', k, re.S): self.error(f'{f}:{line}: bad context key {show(k)}')

    def check_formats(self):
        for k, where in self.used.items():
            if '%?' in specs(k): self.error(f'{where}: {show(k)}: a % that starts no printf conversion (write %%)')
        for name, l in self.langs.items():
            for k, t, line in l['entries']:
                if specs(k) != specs(t):
                    self.error(f'{self.rel(l["file"])}:{line}: {show(k)} has conversions {specs(k)}, the translation {specs(t)}')

    def glyph_sets(self):
        def cps(path, pattern):
            return {int(x, 16) for x in re.findall(pattern, self.read(path), re.M)}
        self.vfd = cps(self.gadget / 'VfdFont.h', r'^\s*\{ 0x([0-9A-Fa-f]+),')
        tf = self.read(self.gadget / 'ThemeFonts.h')
        self.machine = {}
        for font in ('c64', 'amiga'):
            body = re.search(r'Glyph ' + font + r'_glyphs\[\] = \{(.*?)^\};', tf, re.M | re.S)
            self.machine[font] = {int(x, 16) for x in re.findall(r'\{ 0x([0-9A-Fa-f]+),', body[1])} if body else set()
        vfd = self.read(self.gadget / 'Vfd.cpp')
        res = re.search(r'static Resolved resolve\(uint32_t cp\)\n\{(.*?)^\}', vfd, re.M | re.S)
        self.resolve = {}
        for line in (res[1] if res else '').split('\n'):
            r = re.search(r"return \{ (?:'(\\?.)'|(0x[0-9A-Fa-f]+))", line)
            if not r: continue
            base = ord(r[1][-1]) if r[1] else int(r[2], 16)
            for c in re.findall(r'case (0x[0-9A-Fa-f]+|\'.\'):', line):
                self.resolve[int(c, 16) if c.startswith('0x') else ord(c[1])] = base
        latin = re.search(r'LATIN1\[\] = "([^"]*)"', vfd)
        self.latin1 = latin[1] if latin else ''
        if not res or not self.vfd or not all(self.machine.values()) or len(self.latin1) != 64:
            self.error('Vfd.cpp/VfdFont.h/ThemeFonts.h: the glyph tables or resolve() did not parse')

    def vfd_base(self, cp):
        if ord('a') <= cp <= ord('z'): cp -= 32
        if cp in self.resolve: return self.resolve[cp]
        if 0xC0 <= cp <= 0xFF: return ord(self.latin1[cp - 0xC0])
        return cp

    def check_chars(self, text, where):
        for ch in sorted(set(text)):
            cp = ord(ch)
            if cp < 0x20 and ch != '\x04': self.error(f'{where}: control character U+{cp:04X}')
            if cp < 0x20 or ch == '\x04': continue
            missing = [] if self.vfd_base(cp) in self.vfd else ['VFD']
            missing += [f for f, s in self.machine.items() if cp not in s]
            if missing: self.error(f'{where}: {ch!r} (U+{cp:04X}) has no glyph in {", ".join(missing)}')

    def check_texts(self):
        self.glyph_sets()
        for k, where in self.used.items(): self.check_chars(k.split('\x04')[-1], where)
        long = []
        for name, l in self.langs.items():
            f = self.rel(l['file'])
            for k, t, line in l['entries']:
                self.check_chars(t, f'{f}:{line}')
                en = k.split('\x04')[-1]
                if len(t) > 12 and len(t) > 1.5 * len(en): long.append((len(t) - len(en), f'{f}:{line}: {en!r} {len(en)} -> {len(t)} letters'))
            for arr in l['arrays']:
                for w in arr: self.check_chars(w, f'{f}: DATA_{name}')
            self.check_chars(l['name'], f'{f}: DATA_{name}')
        for _, msg in sorted(long, reverse=True):
            self.warn('much longer than the English: ' + msg)

    # ---- the web page ------------------------------------------------------------------------------------------

    def check_web(self):
        p = self.gadget / 'SettingsPage.cpp'
        page = self.read(p)
        seg = re.search(r'<div class="seg" id="lang">(.*?)</div>', page)
        buttons = re.findall(r'data-v="(\w+)"', seg[1]) if seg else []
        if buttons != self.codes: self.error(f'{self.rel(p)}: language buttons {buttons}, the firmware has {self.codes}')
        for code in self.codes[1:]:
            if not re.search(r'^const ' + code.upper() + r' = \{', page, re.M): self.error(f'{self.rel(p)}: no "const {code.upper()} = {{" texts')
            if not re.search(r'^\s*' + code + r': \{', page, re.M): self.error(f'{self.rel(p)}: no "{code}: {{" in W')
        rows = re.search(r'const PAGES = \[(.*?)\];', page, re.S)
        fw = dict(re.findall(r'\{ Mode::\w+, "(\w+)", N_\("([^"]*)"\)', self.read(self.gadget / 'Gadget.cpp')))
        for row in re.findall(r'\[([^\[\]]*)\]', rows[1] if rows else ''):
            cells = re.findall(r"'([^']*)'", row)
            if len(cells) != 1 + len(self.codes):
                self.error(f'{self.rel(p)}: PAGES row {cells[:1]} has {len(cells) - 1} names, not {len(self.codes)}')
                continue
            en = fw.get(cells[0])
            if not en: continue
            for i, name in enumerate(self.order[1:], 2):
                screen = self.langs.get(name, {}).get('keys', {}).get(en)
                if screen and upper(cells[i], self.codes[i - 1]) != screen:
                    self.warn(f'{self.rel(p)}: page {cells[0]}: the web page says {cells[i]!r}, the screen {screen!r}')

    def run(self):
        self.load_tables()
        self.check_registry()
        self.scan_code()
        if not hasattr(self, 'codes'): return self
        self.check_keys()
        self.check_formats()
        self.check_texts()
        self.check_web()
        return self


def upper(s, code):
    if code == 'tr': s = s.replace('i', 'İ').replace('ı', 'I')
    return s.upper()


def show(k): return repr(k.replace('\x04', ' / '))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--root', default=pathlib.Path(__file__).resolve().parent.parent)
    ap.add_argument('-v', '--verbose', action='store_true')
    a = ap.parse_args()
    c = Check(a.root, a.verbose).run()
    for e in c.errors: print('error: ' + e)
    for w in c.warnings if a.verbose else []: print('warning: ' + w)
    if c.verbose:
        print(f'T(variable) at {len(c.dynamic)} places (their texts are N_("...")): ' + ', '.join(c.dynamic))
    counts = ', '.join(f'{n}: {len(l["entries"])} entries' for n, l in c.langs.items())
    print(f'i18n: {len(getattr(c, "used", {}))} keys in the code; {counts}; {len(c.errors)} error(s), '
          f'{len(c.warnings)} warning(s){"" if a.verbose else " (-v shows them)" if c.warnings else ""}')
    return 1 if c.errors else 0


if __name__ == '__main__':
    sys.exit(main())
