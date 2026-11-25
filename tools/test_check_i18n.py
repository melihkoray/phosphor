#!/usr/bin/env python3
"""Tests of tools/check_i18n.py: the real tree passes, and each kind of mistake, made in a copy of the tree, fails.

  python3 tools/test_check_i18n.py
"""
import pathlib, shutil, sys, tempfile, unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import check_i18n

ROOT = pathlib.Path(__file__).resolve().parent.parent
REL = 'firmware/src/boards/touch349'
G = REL + '/gadget/'


class CheckI18n(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.base = pathlib.Path(cls.tmp.name) / 'base'
        shutil.copytree(ROOT / REL, cls.base / REL)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def setUp(self):
        self.root = pathlib.Path(self.tmp.name) / self.id().split('.')[-1]
        shutil.copytree(self.base, self.root)

    def tearDown(self):
        shutil.rmtree(self.root)

    def edit(self, rel, old, new, count=1):
        p = self.root / rel
        s = p.read_text(encoding='utf-8')
        self.assertEqual(s.count(old), count, f'{rel}: {old!r}')
        p.write_text(s.replace(old, new), encoding='utf-8')

    def errors(self):
        return check_i18n.Check(self.root).run().errors

    def fails(self, *parts):
        errs = self.errors()
        self.assertTrue(any(all(x in e for x in parts) for e in errs), f'no error with {parts}; got {errs}')

    def test_real_tree_passes(self):
        self.assertEqual(check_i18n.Check(ROOT).run().errors, [])

    def test_copy_passes(self):
        self.assertEqual(self.errors(), [])

    # ---- calls in the code

    def test_macro_typo(self):  # the bug the first version of the migration had: TC("readout", "LEVEL")
        self.edit(G + 'ToolApps.cpp', 'TCTX("breathe", "PAUSED")', 'TC("breathe", "PAUSED")', count=2)
        self.fails('ToolApps.cpp', 'TC("...") is not T, TCTX or N_')

    def test_old_api(self):
        self.edit(G + 'GameBlackjack.cpp', 'T("AGAIN")', 'L("AGAIN", "TEKRAR")')
        self.fails('GameBlackjack.cpp', 'L("...")')

    def test_old_settings_tr(self):
        self.edit(G + 'GameBlackjack.cpp', 'T("AGAIN")', '(settings::tr() ? "TEKRAR" : "AGAIN")')
        self.fails('settings::tr()')

    def test_concatenated_literal(self):
        self.edit(G + 'GameBlackjack.cpp', 'T("AGAIN")', 'T("AG" "AIN")')
        self.fails('GameBlackjack.cpp', 'one string literal')

    def test_context_not_lower_case(self):
        self.edit(G + 'ToolApps.cpp', 'TCTX("breathe", "PAUSED")', 'TCTX("Breathe", "PAUSED")', count=2)
        self.fails('lower case letters only')

    def test_context_inside_t(self):  # works in Turkish, shows "count\004RESET" in English
        self.edit(G + 'GameBlackjack.cpp', 'T("AGAIN")', 'T("AGAIN"), T("count\\004RESET")')
        self.fails('GameBlackjack.cpp', 'a context is TCTX')

    def test_comment_is_no_key(self):  # a key only in a comment is not used: its entry is dead
        self.edit(G + 'GameBlackjack.cpp', 'T("AGAIN")', '/* T("AGAIN") */ "AGAIN"')
        self.fails('LangTr.cpp', "'AGAIN' is used nowhere")

    # ---- keys and tables

    def test_key_missing_in_one_language(self):
        self.edit(G + 'LangPl.cpp', '  { "WAKE UP!", "POBUDKA!" },\n', '')
        self.fails('LangPl.cpp', "no entry for 'WAKE UP!'")

    def test_struct_key_missing(self):  # a text in a struct, translated with T(variable), marked with N_()
        self.edit(G + 'LangTr.cpp', '  { "WI-FI", "WI-FI" },\n', '')
        self.fails('LangTr.cpp', "no entry for 'WI-FI'", 'SettingsUi.cpp')

    def test_new_key_without_entries(self):
        self.edit(G + 'Study.cpp', 'T("BREAK")', 'T("SHORT BREAK")')
        self.fails('LangTr.cpp', "no entry for 'SHORT BREAK'")
        self.fails('LangPl.cpp', "no entry for 'SHORT BREAK'")

    def test_unused_entry(self):
        self.edit(G + 'LangTr.cpp', '  { "YOU", "SEN" },\n', '  { "YOU", "SEN" },\n  { "YOUR", "SENİN" },\n')
        self.fails('LangTr.cpp', "'YOUR' is used nowhere")

    def test_not_sorted(self):
        self.edit(G + 'LangTr.cpp', '  { "YOU", "SEN" },\n  { "YOU WIN", "KAZANDIN" },\n',
                  '  { "YOU WIN", "KAZANDIN" },\n  { "YOU", "SEN" },\n')
        self.fails('LangTr.cpp', 'is not after', 'strcmp order')

    def test_key_twice(self):
        self.edit(G + 'LangTr.cpp', '  { "YOU", "SEN" },\n', '  { "YOU", "SEN" },\n  { "YOU", "SİZ" },\n')
        self.fails('LangTr.cpp', "'YOU' twice")

    def test_unparsed_line(self):
        self.edit(G + 'LangTr.cpp', '  { "YOU", "SEN" },\n', '  { "YOU", "SEN" }, { "X", "Y" },\n')
        self.fails('LangTr.cpp', 'not a table line')

    def test_bad_context_key(self):
        self.edit(G + 'LangTr.cpp', '"breathe\\004PAUSED"', '"\\004PAUSED"')
        self.fails('bad context key')

    def test_date_words(self):
        self.edit(G + 'LangPl.cpp', '"PIĄ", "SOB" }', '"PIĄ" }')
        self.fails('LangPl.cpp', '6 words, not 7')

    # ---- printf formats

    def test_format_conversion_changed(self):
        self.edit(G + 'LangPl.cpp', '"%u / %u KB free", "%u / %u KB wolne"', '"%u / %u KB free", "%u / %s KB wolne"')
        self.fails('LangPl.cpp', "'%u / %u KB free'", "['%u', '%s']")

    def test_format_conversion_dropped(self):
        self.edit(G + 'LangTr.cpp', '"SCREEN TO %d%%", "EKRAN %%%d"', '"SCREEN TO %d%%", "EKRAN %%"')
        self.fails('LangTr.cpp', 'SCREEN TO')

    def test_format_lone_percent(self):
        self.edit(G + 'LangTr.cpp', '"SCREEN TO %d%%", "EKRAN %%%d"', '"SCREEN TO %d%%", "EKRAN %%d %"')
        self.fails('LangTr.cpp', "'%?'")

    def test_format_length_modifier(self):  # %lu is not %u: on the ESP32 they take different types
        self.edit(G + 'LangPl.cpp', '"%lu min", "%lu min"', '"%lu min", "%u min"')
        self.fails('LangPl.cpp', "'%lu min'")

    # ---- characters

    def test_letter_without_glyph(self):
        self.edit(G + 'LangPl.cpp', '"WAKE UP!", "POBUDKA!"', '"WAKE UP!", "ПОБУДКА!"')
        self.fails('LangPl.cpp', 'no glyph in VFD, c64, amiga')

    def test_letter_without_machine_glyph(self):  # ñ: the VFD draws it as N, the C64 and Amiga fonts have no ñ
        self.edit(G + 'LangPl.cpp', '"WAKE UP!", "POBUDKA!"', '"WAKE UP!", "MAÑANA!"')
        self.fails('LangPl.cpp', "'Ñ'", 'c64, amiga')

    def test_control_character(self):
        self.edit(G + 'LangPl.cpp', '"WAKE UP!", "POBUDKA!"', '"WAKE UP!", "POBUDKA!\\n"')
        self.fails('LangPl.cpp', 'control character U+000A')

    def test_invalid_utf8(self):
        p = self.root / G / 'LangPl.cpp'
        p.write_bytes(p.read_bytes().replace(b'"POBUDKA!"', b'"POBUDKA\xc5!"'))
        self.fails('LangPl.cpp', 'not valid UTF-8')

    # ---- the registry and the web page

    def test_language_not_registered(self):
        self.edit(G + 'Lang.cpp', '{ &DATA_EN, &DATA_TR, &DATA_PL }', '{ &DATA_EN, &DATA_TR }')
        self.fails('ALL[]')

    def test_registry_order(self):
        self.edit(G + 'Lang.cpp', '{ &DATA_EN, &DATA_TR, &DATA_PL }', '{ &DATA_EN, &DATA_PL, &DATA_TR }')
        self.fails('ALL[]')

    def test_code_twice(self):
        self.edit(G + 'LangPl.cpp', '"pl", "POLSKI"', '"tr", "POLSKI"')
        self.fails('codes twice')

    def test_web_button_missing(self):
        self.edit(G + 'SettingsPage.cpp', '<button data-v="pl">Polski</button>', '')
        self.fails('SettingsPage.cpp', 'language buttons')

    def test_web_page_names_column_missing(self):
        self.edit(G + 'SettingsPage.cpp', "['snow', 'Snow', 'Kar', 'Śnieg']", "['snow', 'Snow', 'Kar']")
        self.fails('SettingsPage.cpp', "PAGES row ['snow']")


if __name__ == '__main__':
    unittest.main(verbosity=1)
