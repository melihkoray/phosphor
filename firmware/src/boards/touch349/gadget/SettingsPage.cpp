#ifdef BOARD_TOUCH349

#include "Settings.h"

// The settings page, served at /settings. Self-contained: the phone that opens it does the city search (Open-Meteo
// geocoding) and the station search (Radio Browser) itself, the device only stores the result as text.
const char SETTINGS_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Settings</title>
<style>
:root {
  --bg: #07100f; --panel: #0d1a19; --line: #1c302e; --ghost: #2a4643;
  --text: #cfe9e5; --dim: #7fa29d; --lit: #6ff3e0; --hot: #ff3ea5; --hot-dim: #6b2049;
  --r: 10px;
}
* { box-sizing: border-box; }
body { margin: 0; background: var(--bg); color: var(--text); font: 15px/1.45 system-ui, -apple-system, "Segoe UI", sans-serif; }
main { max-width: 640px; margin: 0 auto; padding: 20px 16px 120px; }
h1 { font: 600 13px/1 ui-monospace, Menlo, monospace; letter-spacing: .18em; color: var(--lit); margin: 8px 0 4px; text-transform: uppercase; }
.sub { color: var(--dim); margin: 0 0 24px; font-size: 13px; }
section { border-top: 1px solid var(--line); padding: 22px 0 6px; }
h2 { font: 600 12px/1 ui-monospace, Menlo, monospace; letter-spacing: .14em; text-transform: uppercase; color: var(--dim); margin: 0 0 14px; }
label { display: block; color: var(--dim); font-size: 13px; margin: 0 0 6px; }
input, select, button { font: inherit; color: inherit; }
input[type=text], input[type=number], input[type=url], select {
  width: 100%; background: var(--panel); border: 1px solid var(--line); border-radius: var(--r); padding: 10px 12px; outline: none;
}
input:focus, select:focus { border-color: var(--lit); }
button { background: var(--panel); border: 1px solid var(--ghost); border-radius: var(--r); padding: 9px 14px; cursor: pointer; }
button:hover { border-color: var(--lit); }
button.primary { background: var(--lit); color: #04201c; border-color: var(--lit); font-weight: 600; }
button.icon { padding: 6px 10px; min-width: 38px; }
button.warn:hover { border-color: var(--hot); color: var(--hot); }
.row { display: flex; gap: 10px; align-items: center; }
.row > * { flex: 1; }
.row > .shrink { flex: 0 0 auto; }
.current { font-size: 17px; margin: 0 0 12px; }
.current b { color: var(--lit); font-weight: 600; }
.results { list-style: none; margin: 8px 0 0; padding: 0; }
.results li { padding: 10px 12px; border: 1px solid var(--line); border-radius: var(--r); margin-top: 6px; cursor: pointer; }
.results li:hover { border-color: var(--lit); }
.results small { color: var(--dim); display: block; }
.seg { display: inline-flex; border: 1px solid var(--ghost); border-radius: var(--r); overflow: hidden; }
.seg button { border: 0; border-radius: 0; background: transparent; padding: 9px 16px; }
.seg button.on { background: var(--lit); color: #04201c; font-weight: 600; }
.grid3 { display: grid; grid-template-columns: repeat(3, 1fr); gap: 10px; }
.group { border: 1px solid var(--line); border-radius: var(--r); padding: 12px; margin-bottom: 12px; }
.group-head { display: grid; grid-template-columns: 96px 1fr auto auto auto; gap: 8px; align-items: center; margin-bottom: 8px; }
.group-head input { padding: 8px 10px; }
.station { display: grid; grid-template-columns: 1fr auto auto auto auto; gap: 6px; align-items: center; padding: 6px 0; border-top: 1px solid var(--line); }
.station .name { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.station .name small { display: block; color: var(--dim); font-size: 12px; overflow: hidden; text-overflow: ellipsis; }
.empty { color: var(--dim); font-size: 13px; padding: 6px 0; }
.pages { display: grid; grid-template-columns: repeat(auto-fill, minmax(150px, 1fr)); gap: 8px; }
.pages label { display: flex; gap: 8px; align-items: center; margin: 0; padding: 9px 12px; border: 1px solid var(--line); border-radius: var(--r); color: var(--text); cursor: pointer; }
.pages label.off { color: var(--dim); }
.pages input { accent-color: var(--lit); width: 18px; height: 18px; margin: 0; }
.hint { color: var(--dim); font-size: 13px; margin: 8px 0 0; }
.add { margin-top: 18px; padding: 14px; border: 1px dashed var(--ghost); border-radius: var(--r); }
.tabs { display: flex; gap: 6px; margin-bottom: 12px; }
.bar { position: fixed; left: 0; right: 0; bottom: 0; background: color-mix(in srgb, var(--bg) 92%, transparent); border-top: 1px solid var(--line); backdrop-filter: blur(6px); }
.bar-in { max-width: 640px; margin: 0 auto; padding: 12px 16px; display: flex; gap: 12px; align-items: center; }
#status { flex: 1; color: var(--dim); font-size: 13px; }
#status.ok { color: var(--lit); } #status.err { color: var(--hot); }
.tools { display: flex; gap: 8px; flex-wrap: wrap; margin-top: 12px; }
</style>
</head>
<body>
<main>
  <h1 id="t-title">Settings</h1>
  <p class="sub" id="t-sub">Changes take effect on the device when you press Save.</p>

  <section>
    <h2 id="t-loc">Location</h2>
    <p class="current"><span id="t-now">Weather and time for</span> <b id="city-now"></b></p>
    <div class="row">
      <input id="city-q" type="text" placeholder="Search a city" autocomplete="off">
      <button class="shrink" id="city-go">Search</button>
    </div>
    <ul class="results" id="city-res"></ul>
  </section>

  <section>
    <h2 id="t-disp">Display</h2>
    <div class="row" style="flex-wrap:wrap;gap:18px">
      <div class="shrink"><label id="t-lang">Language</label>
        <div class="seg" id="lang"><button data-v="en">English</button><button data-v="tr">Türkçe</button><button data-v="pl">Polski</button></div></div>
      <div class="shrink"><label id="t-clock">Clock</label>
        <div class="seg" id="h24"><button data-v="1">24 h</button><button data-v="0">12 h</button></div></div>
      <div class="shrink"><label id="t-unit">Temperature</label>
        <div class="seg" id="unit"><button data-v="c">°C</button><button data-v="f">°F</button></div></div>
      <div class="shrink"><label id="t-night">Night dim (screen at 15%)</label>
        <div class="seg" id="night"><button data-v="0">Off</button><button data-v="1">22:00-07:00</button><button data-v="2">23:00-07:00</button><button data-v="3">00:00-08:00</button></div></div>
    </div>
  </section>

  <section>
    <h2 id="t-pages">Pages</h2>
    <p class="hint" id="t-pages-hint" style="margin:0 0 12px">Pages you swipe through on the device. Untick the ones you do not want; the clock and this settings page always stay.</p>
    <div class="pages" id="pages"></div>
  </section>

  <section>
    <h2 id="t-pomo">Focus timer (minutes)</h2>
    <div class="grid3">
      <div><label id="t-focus">Focus</label><input id="focus" type="number" min="1" max="180"></div>
      <div><label id="t-short">Short break</label><input id="short" type="number" min="1" max="60"></div>
      <div><label id="t-long">Long break</label><input id="long" type="number" min="1" max="120"></div>
    </div>
  </section>

  <section>
    <h2 id="t-radio">Radio</h2>
    <p class="hint" id="t-radio-hint" style="margin:0 0 14px">Each group is one button on the radio screen (up to 7). Tapping a button again steps to the next station in it.</p>
    <div id="groups"></div>
    <button id="add-group">+ Group</button>

    <div class="add">
      <div class="tabs"><div class="seg" id="add-mode"><button data-v="search" class="on">Search</button><button data-v="url">Stream URL</button></div></div>
      <div id="add-search">
        <div class="row">
          <input id="st-q" type="text" placeholder="Station name, genre or country" autocomplete="off">
          <button class="shrink" id="st-go">Search</button>
        </div>
        <ul class="results" id="st-res"></ul>
      </div>
      <div id="add-url" hidden>
        <label id="t-st-name">Name (16 letters)</label>
        <input id="st-name" type="text" maxlength="16" style="margin-bottom:10px">
        <label>URL</label>
        <input id="st-url" type="url" placeholder="http://...mp3" style="margin-bottom:10px">
        <div class="row"><button class="shrink" id="st-test">▶ Test</button><button class="shrink" id="st-add">Add</button></div>
      </div>
      <div style="margin-top:12px"><label id="t-into">Add to group</label><select id="st-group"></select></div>
      <p class="hint" id="t-codec">Only MP3 and AAC streams play on the device. Stations marked ? did not report a format: test them first.</p>
    </div>

    <div class="tools">
      <button id="export">Export list</button>
      <button id="import">Import list</button>
      <input id="import-file" type="file" accept=".txt,text/plain" hidden>
    </div>
  </section>
</main>
<div class="bar"><div class="bar-in"><span id="status"></span><button class="primary" id="save">Save</button></div></div>

<script>
const $ = s => document.querySelector(s);
let S = { city: '', lat: 0, lon: 0, lang: 'en', h24: '1', unit: 'c', night: '0', hide: '', focus: 25, short: 5, long: 15, groups: [], stations: [] };

const TR = {
  't-title': 'Ayarlar', 't-sub': 'Kaydet’e basınca cihazda hemen geçerli olur.', 't-loc': 'Konum', 't-now': 'Hava ve saat:',
  't-disp': 'Ekran', 't-lang': 'Dil', 't-clock': 'Saat', 't-unit': 'Sıcaklık', 't-night': 'Gece kısma (ekran %15)', 't-pages': 'Sayfalar', 't-pages-hint': 'Cihazda kaydırarak gezdiğin sayfalar. İstemediklerinin işaretini kaldır; saat ve bu ayar sayfası hep kalır.', 't-pomo': 'Odak sayacı (dakika)', 't-focus': 'Odak',
  't-short': 'Kısa mola', 't-long': 'Uzun mola', 't-radio': 'Radyo',
  't-radio-hint': 'Her grup radyo ekranında bir buton (en çok 7). Aynı butona tekrar dokununca gruptaki sonraki istasyona geçer.',
  't-st-name': 'Ad (16 harf)', 't-into': 'Eklenecek grup', 't-codec': 'Cihaz sadece MP3 ve AAC yayınları çalar. ? işaretli istasyonlar biçimini bildirmiyor: önce deneyin.'
};
const PL = {
  't-title': 'Ustawienia', 't-sub': 'Zmiany działają na urządzeniu od razu po naciśnięciu Zapisz.', 't-loc': 'Lokalizacja', 't-now': 'Pogoda i czas dla:',
  't-disp': 'Ekran', 't-lang': 'Język', 't-clock': 'Zegar', 't-unit': 'Temperatura', 't-night': 'Ściemnianie w nocy (ekran 15%)', 't-pages': 'Strony', 't-pages-hint': 'Strony, które przewijasz na urządzeniu. Odznacz te, których nie chcesz; zegar i ta strona ustawień zostają zawsze.', 't-pomo': 'Licznik skupienia (minuty)', 't-focus': 'Skupienie',
  't-short': 'Krótka przerwa', 't-long': 'Długa przerwa', 't-radio': 'Radio',
  't-radio-hint': 'Każda grupa to jeden przycisk na ekranie radia (maksymalnie 7). Ponowne dotknięcie przycisku przechodzi do następnej stacji w grupie.',
  't-st-name': 'Nazwa (16 liter)', 't-into': 'Dodaj do grupy', 't-codec': 'Urządzenie odtwarza tylko strumienie MP3 i AAC. Stacje oznaczone ? nie podały formatu: najpierw je wypróbuj.'
};
const EN = {};
document.querySelectorAll('[id^="t-"]').forEach(e => EN[e.id] = e.textContent);
const W = {
  en: { search: 'Search', city: 'Search a city', st: 'Station name, genre or country', save: 'Save', saved: 'Saved. The device is updated.',
        add: 'Add', test: '▶ Test', group: '+ Group', del: 'Delete', none: 'No stations yet', found0: 'Nothing found', testing: 'Playing on the device…',
        exp: 'Export list', imp: 'Import list', modeS: 'Search', modeU: 'Stream URL', loading: 'Searching…', unsaved: 'Not saved yet', try: 'test first' },
  tr: { search: 'Ara', city: 'Şehir ara', st: 'İstasyon adı, tür ya da ülke', save: 'Kaydet', saved: 'Kaydedildi. Cihaz güncellendi.',
        add: 'Ekle', test: '▶ Dene', group: '+ Grup', del: 'Sil', none: 'Henüz istasyon yok', found0: 'Bulunamadı', testing: 'Cihazda çalıyor…',
        exp: 'Listeyi dışa aktar', imp: 'Listeyi içe aktar', modeS: 'Ara', modeU: 'Yayın adresi', loading: 'Aranıyor…', unsaved: 'Henüz kaydedilmedi', try: 'önce dene' },
  pl: { search: 'Szukaj', city: 'Szukaj miasta', st: 'Nazwa stacji, gatunek lub kraj', save: 'Zapisz', saved: 'Zapisano. Urządzenie zaktualizowane.',
        add: 'Dodaj', test: '▶ Próba', group: '+ Grupa', del: 'Usuń', none: 'Brak stacji', found0: 'Nic nie znaleziono', testing: 'Gra na urządzeniu…',
        exp: 'Eksportuj listę', imp: 'Importuj listę', modeS: 'Szukaj', modeU: 'Adres strumienia', loading: 'Szukanie…', unsaved: 'Jeszcze niezapisane', try: 'najpierw wypróbuj' }
};
const w = k => W[S.lang][k];

function parse(text) {
  const s = { groups: [], stations: [] };
  for (const line of text.split('\n')) {
    const i = line.indexOf('='); if (i < 1) continue;
    const k = line.slice(0, i), v = line.slice(i + 1);
    if (k === 'group') { const [key, name] = v.split('|'); s.groups.push({ key, name: name || key }); }
    else if (k === 'station') { const p = v.split('|'); s.stations.push({ g: +p[0], name: p[1], url: p.slice(2).join('|') }); }
    else s[k] = v;
  }
  return s;
}
function serialize() {
  let t = `city=${S.city}\nlat=${S.lat}\nlon=${S.lon}\nlang=${S.lang}\nh24=${S.h24}\nunit=${S.unit}\nnight=${S.night || 0}\nhide=${S.hide || ''}\nfocus=${$('#focus').value}\nshort=${$('#short').value}\nlong=${$('#long').value}\n`;
  S.groups.forEach(g => t += `group=${g.key}|${g.name}\n`);
  S.stations.forEach(s => t += `station=${s.g}|${s.name}|${s.url}\n`);
  return t;
}
function status(msg, cls) { const e = $('#status'); e.textContent = msg; e.className = cls || ''; }
function dirty() { status(w('unsaved')); }
const esc = s => String(s).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));

function seg(id, val, onPick) {
  document.querySelectorAll(`#${id} button`).forEach(b => {
    b.classList.toggle('on', b.dataset.v === val);
    b.onclick = () => { onPick(b.dataset.v); };
  });
}
function texts() {
  const T = S.lang === 'tr' ? TR : S.lang === 'pl' ? PL : EN;
  for (const id in T) { const e = document.getElementById(id); if (e) e.textContent = T[id]; }
  document.documentElement.lang = S.lang;
  $('#city-go').textContent = $('#st-go').textContent = w('search');
  $('#city-q').placeholder = w('city'); $('#st-q').placeholder = w('st');
  $('#save').textContent = w('save'); $('#st-add').textContent = w('add'); $('#st-test').textContent = w('test');
  $('#add-group').textContent = w('group'); $('#export').textContent = w('exp'); $('#import').textContent = w('imp');
  const m = document.querySelectorAll('#add-mode button'); m[0].textContent = w('modeS'); m[1].textContent = w('modeU');
}
function render() {
  $('#city-now').textContent = S.city;
  seg('lang', S.lang, v => { S.lang = v; render(); dirty(); });
  seg('h24', String(S.h24), v => { S.h24 = v; render(); dirty(); });
  seg('unit', S.unit || 'c', v => { S.unit = v; render(); dirty(); });
  seg('night', String(S.night || '0'), v => { S.night = v; render(); dirty(); });
  renderPages();
  texts();
  const box = $('#groups'); box.innerHTML = '';
  S.groups.forEach((g, gi) => {
    const el = document.createElement('div'); el.className = 'group';
    el.innerHTML = `<div class="group-head"><input type="text" maxlength="6" value="${esc(g.key)}" title="Button" style="text-transform:uppercase"><input type="text" maxlength="16" value="${esc(g.name)}" title="Name">
      <button class="icon" data-a="up">↑</button><button class="icon" data-a="down">↓</button><button class="icon warn" data-a="del">✕</button></div>`;
    const [ik, inm] = el.querySelectorAll('input');
    ik.oninput = () => { g.key = ik.value.toUpperCase(); dirty(); fillGroupSelect(); };
    inm.oninput = () => { g.name = inm.value; dirty(); fillGroupSelect(); };
    el.querySelector('[data-a=up]').onclick = () => moveGroup(gi, -1);
    el.querySelector('[data-a=down]').onclick = () => moveGroup(gi, 1);
    el.querySelector('[data-a=del]').onclick = () => { if (S.groups.length > 1) { S.stations = S.stations.filter(s => s.g !== gi).map(s => ({ ...s, g: s.g > gi ? s.g - 1 : s.g })); S.groups.splice(gi, 1); render(); dirty(); } };
    const list = S.stations.map((s, i) => [s, i]).filter(([s]) => s.g === gi);
    if (!list.length) el.insertAdjacentHTML('beforeend', `<div class="empty">${w('none')}</div>`);
    list.forEach(([s, i]) => {
      const r = document.createElement('div'); r.className = 'station';
      r.innerHTML = `<div class="name">${esc(s.name)}<small>${esc(s.url)}</small></div>
        <button class="icon" data-a="play" title="Test">▶</button><button class="icon" data-a="up">↑</button><button class="icon" data-a="down">↓</button><button class="icon warn" data-a="del">✕</button>`;
      r.querySelector('[data-a=play]').onclick = () => test(s.url);
      r.querySelector('[data-a=up]').onclick = () => moveStation(i, -1);
      r.querySelector('[data-a=down]').onclick = () => moveStation(i, 1);
      r.querySelector('[data-a=del]').onclick = () => { S.stations.splice(i, 1); render(); dirty(); };
      el.appendChild(r);
    });
    box.appendChild(el);
  });
  $('#add-group').disabled = S.groups.length >= 7;
  fillGroupSelect();
}
const PAGES = [['clock', 'Clock', 'Saat', 'Zegar', 1], ['radio', 'Radio', 'Radyo', 'Radio'], ['weather', 'Weather', 'Hava durumu', 'Pogoda'], ['timer', 'Timer', 'Sayaç', 'Minutnik'],
  ['study', 'Focus', 'Odak', 'Skupienie'], ['hourglass', 'Hourglass', 'Kum saati', 'Klepsydra'], ['aquarium', 'Fish tank', 'Akvaryum', 'Akwarium'], ['snow', 'Snow', 'Kar', 'Śnieg'],
  ['fire', 'Fireplace', 'Şömine', 'Kominek'], ['sea', 'Lighthouse', 'Fener', 'Latarnia'], ['wave', 'Ship', 'Gemi', 'Statek'], ['lava', 'Lava lamp', 'Lav lambası', 'Lampa lawowa'],
  ['tools', 'Tools', 'Araçlar', 'Narzędzia'], ['play', 'Play', 'Oyun', 'Gry'], ['settings', 'Settings', 'Ayarlar', 'Ustawienia', 1]];
function renderPages() {
  const hidden = new Set((S.hide || '').split(',').filter(Boolean)), box = $('#pages');
  box.innerHTML = '';
  PAGES.forEach(([key, en, tr, pl, locked]) => {
    const l = document.createElement('label'), c = document.createElement('input');
    c.type = 'checkbox'; c.checked = locked || !hidden.has(key); c.disabled = !!locked;
    l.className = c.checked ? '' : 'off';
    c.onchange = () => {
      c.checked ? hidden.delete(key) : hidden.add(key);
      S.hide = PAGES.filter(p => hidden.has(p[0])).map(p => p[0]).join(',');
      l.className = c.checked ? '' : 'off'; dirty();
    };
    l.append(c, S.lang === 'tr' ? tr : S.lang === 'pl' ? pl : en);
    box.appendChild(l);
  });
}
function fillGroupSelect() {
  const sel = $('#st-group'), keep = sel.value;
  sel.innerHTML = S.groups.map((g, i) => `<option value="${i}">${esc(g.key)} · ${esc(g.name)}</option>`).join('');
  if (keep && keep < S.groups.length) sel.value = keep;
}
function moveGroup(i, d) {
  const j = i + d; if (j < 0 || j >= S.groups.length) return;
  [S.groups[i], S.groups[j]] = [S.groups[j], S.groups[i]];
  S.stations.forEach(s => { if (s.g === i) s.g = j; else if (s.g === j) s.g = i; });
  render(); dirty();
}
function moveStation(i, d) {
  const g = S.stations[i].g; let j = i + d;
  while (j >= 0 && j < S.stations.length && S.stations[j].g !== g) j += d;
  if (j < 0 || j >= S.stations.length) return;
  [S.stations[i], S.stations[j]] = [S.stations[j], S.stations[i]];
  render(); dirty();
}
function addStation(name, url) {
  name = (name || 'Station').trim().slice(0, 16);
  S.stations.push({ g: +$('#st-group').value || 0, name, url: url.trim() });
  render(); dirty();
}
async function test(url) {
  status(w('testing'));
  const b = new URLSearchParams({ name: 'radiotest', arg: url });
  try { await fetch('/cmd', { method: 'POST', body: b }); } catch (e) {}
}

$('#city-go').onclick = async () => {
  const q = $('#city-q').value.trim(); if (!q) return;
  const ul = $('#city-res'); ul.innerHTML = `<li>${w('loading')}</li>`;
  try {
    const r = await fetch(`https://geocoding-api.open-meteo.com/v1/search?count=6&language=${S.lang}&name=${encodeURIComponent(q)}`);
    const j = await r.json();
    ul.innerHTML = '';
    (j.results || []).forEach(c => {
      const li = document.createElement('li');
      li.innerHTML = `${esc(c.name)}<small>${esc([c.admin1, c.country].filter(Boolean).join(', '))}</small>`;
      li.onclick = () => { S.city = c.admin1 && c.admin1 !== c.name ? `${c.name}, ${c.admin1}` : c.name; S.lat = c.latitude.toFixed(4); S.lon = c.longitude.toFixed(4); ul.innerHTML = ''; $('#city-q').value = ''; render(); dirty(); };
      ul.appendChild(li);
    });
    if (!ul.children.length) ul.innerHTML = `<li>${w('found0')}</li>`;
  } catch (e) { ul.innerHTML = `<li>${esc(e.message)}</li>`; }
};
$('#city-q').onkeydown = e => { if (e.key === 'Enter') $('#city-go').click(); };

$('#st-go').onclick = async () => {
  const q = $('#st-q').value.trim(); if (!q) return;
  const ul = $('#st-res'); ul.innerHTML = `<li>${w('loading')}</li>`;
  const base = 'https://all.api.radio-browser.info/json/stations/search?hidebroken=true&order=votes&reverse=true&limit=40&';
  try {
    const qs = ['name', 'tag', 'country'].map(f => fetch(base + f + '=' + encodeURIComponent(q)).then(r => r.json()).catch(() => []));
    const seen = new Set(), list = [];
    for (const arr of await Promise.all(qs)) for (const s of arr) {
      const codec = (s.codec || '').toUpperCase();
      const unknown = !codec || codec === 'UNKNOWN';
      if (!(unknown || codec === 'MP3' || codec.startsWith('AAC'))) continue;
      const url = s.url_resolved || s.url; if (!url || seen.has(url) || url.includes('.m3u8')) continue;
      seen.add(url); list.push({ name: s.name.trim(), url, meta: [s.country, unknown ? '? ' + w('try') : codec, s.bitrate ? s.bitrate + ' kbps' : ''].filter(Boolean).join(' · ') });
    }
    ul.innerHTML = '';
    list.slice(0, 25).forEach(s => {
      const li = document.createElement('li');
      li.innerHTML = `<div class="row"><div>${esc(s.name)}<small>${esc(s.meta)}</small></div>
        <button class="icon shrink" data-a="play">▶</button><button class="shrink" data-a="add">${w('add')}</button></div>`;
      li.style.cursor = 'default';
      li.querySelector('[data-a=play]').onclick = () => test(s.url);
      li.querySelector('[data-a=add]').onclick = e => { addStation(s.name, s.url); e.target.disabled = true; };
      ul.appendChild(li);
    });
    if (!ul.children.length) ul.innerHTML = `<li>${w('found0')}</li>`;
  } catch (e) { ul.innerHTML = `<li>${esc(e.message)}</li>`; }
};
$('#st-q').onkeydown = e => { if (e.key === 'Enter') $('#st-go').click(); };

seg('add-mode', 'search', function pick(v) {
  $('#add-search').hidden = v !== 'search'; $('#add-url').hidden = v !== 'url'; seg('add-mode', v, pick);
});
$('#st-test').onclick = () => { const u = $('#st-url').value.trim(); if (u) test(u); };
$('#st-add').onclick = () => {
  const u = $('#st-url').value.trim(); if (!/^https?:\/\//.test(u)) { status('URL: http://…', 'err'); return; }
  addStation($('#st-name').value, u); $('#st-url').value = ''; $('#st-name').value = '';
};
$('#add-group').onclick = () => { if (S.groups.length < 7) { S.groups.push({ key: 'NEW', name: 'New' }); render(); dirty(); } };
['focus', 'short', 'long'].forEach(id => $('#' + id).oninput = dirty);

$('#export').onclick = () => {
  const a = document.createElement('a');
  a.href = URL.createObjectURL(new Blob([serialize()], { type: 'text/plain' }));
  a.download = 'desk-settings.txt'; a.click();
};
$('#import').onclick = () => $('#import-file').click();
$('#import-file').onchange = async e => {
  const f = e.target.files[0]; if (!f) return;
  const s = parse(await f.text());
  if (s.groups.length) { S.groups = s.groups; S.stations = s.stations; render(); dirty(); }
};

$('#save').onclick = async () => {
  try {
    const r = await fetch('/api/settings', { method: 'POST', headers: { 'Content-Type': 'text/plain', 'X-Phosphor': '1' }, body: serialize() });
    const t = await r.text();
    r.ok ? status(w('saved'), 'ok') : status(t, 'err');
  } catch (e) { status(e.message, 'err'); }
};

fetch('/api/settings').then(r => r.text()).then(t => {
  const s = parse(t);
  Object.assign(S, s);
  $('#focus').value = S.focus; $('#short').value = S.short; $('#long').value = S.long;
  render(); status('');
});
</script>
</body>
</html>
)HTML";

#endif
