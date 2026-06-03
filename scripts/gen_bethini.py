#!/usr/bin/env python3
"""Generate Solero's bethini-skyrimse.json mirroring BethINI Pie's UI structure.

Walks Bethini.json displayTabs (tab -> group -> labeled row) and joins per-INI-key
preset values from settings.json. Output preserves BethINI's tab/group/row layout
so Solero's BethINI window can replicate the real app."""
import json, sys

defs = json.load(open('/tmp/bethini-defs.json'))
sett = json.load(open('/tmp/bethini-settings.json'))

# Index iniValues by name.lower -> entry
ini_values = {}
for e in sett['iniValues']:
    ini_values.setdefault(e['name'].lower(), e)

PRESETS = ["Bethini Poor", "Bethini Low", "Bethini Medium", "Bethini High", "Bethini Ultra"]
GAME_INIS = ("Skyrim.ini", "SkyrimPrefs.ini", "SkyrimCustom.ini")
SKIP_TABS = {"Setup"}                 # BethINI's own game-path setup; Solero handles this
SKIP_GROUPS = {"Presets", "Adjustments"}  # preset selector + scaling functions (we provide our own)

def preset_values_for(name):
    iv = ini_values.get(name.lower())
    if not iv: return {}
    val = iv.get('value', {})
    return {p: val[p] for p in PRESETS if p in val}

def make_row(label, w):
    inis = w.get('targetINIs', [])
    secs = w.get('targetSections', [])
    names = w.get('settings', [])
    if not names or not inis or not secs:
        return None
    if any(i not in GAME_INIS for i in inis):
        return None

    typ = w.get('type', 'Entry')
    row = {"label": label, "type": typ, "tooltip": w.get('tooltip', '')}

    iniKeys = []
    for idx, nm in enumerate(names):
        ini = inis[idx] if idx < len(inis) else inis[0]
        sec = secs[idx] if idx < len(secs) else secs[0]
        iniKeys.append({
            "key": nm, "section": sec, "file": ini,
            "presets": preset_values_for(nm),
        })
    row["iniKeys"] = iniKeys

    if typ in ("Dropdown", "Combobox"):
        choices = w.get('choices')
        if isinstance(choices, list):
            clean = [str(c) for c in choices if isinstance(c, str) and not c.startswith('FUNC')]
            if clean:
                row["choices"] = clean
        sc = w.get('settingChoices')
        if isinstance(sc, dict):
            row["settingChoices"] = sc

    if typ in ("Slider", "Spinbox"):
        if 'from' in w and 'to' in w:
            try:
                lo = float(w['from']); hi = float(w['to'])
                if lo > hi: lo, hi = hi, lo
                row["min"] = lo; row["max"] = hi
            except (ValueError, TypeError):
                pass
        if 'increment' in w:
            try: row["step"] = float(w['increment'])
            except (ValueError, TypeError): pass
        try: row["decimals"] = int(w.get('decimal places', '0'))
        except (ValueError, TypeError): row["decimals"] = 0

    return row

def walk_group(group_dict):
    rows = []
    settings = group_dict.get('Settings')
    if not isinstance(settings, dict):
        return rows
    for label, widget in settings.items():
        if not isinstance(widget, dict) or 'settings' not in widget:
            continue
        r = make_row(label, widget)
        if r:
            rows.append(r)
    return rows

tabs_out = []
for tab_name, tab in defs['displayTabs'].items():
    if tab_name in SKIP_TABS or not isinstance(tab, dict):
        continue
    groups_out = []
    for group_name, group in tab.items():
        if group_name in SKIP_GROUPS or not isinstance(group, dict):
            continue
        rows = walk_group(group)
        if rows:
            groups_out.append({"name": group_name, "rows": rows})
    if groups_out:
        tabs_out.append({"name": tab_name, "groups": groups_out})

out = {
    "game": "Skyrim Special Edition",
    "source": "Derived from BethINI Pie (DoubleYouC/Bethini-Pie-Skyrim-Special-Edition-Plugin)",
    "presets": PRESETS,
    "tabs": tabs_out,
}

nrows = sum(len(g['rows']) for t in tabs_out for g in t['groups'])
print('tabs:', [t['name'] for t in tabs_out], file=sys.stderr)
print('total groups:', sum(len(t['groups']) for t in tabs_out), file=sys.stderr)
print('total rows:', nrows, file=sys.stderr)
import collections
types = collections.Counter(r['type'] for t in tabs_out for g in t['groups'] for r in g['rows'])
print('row types:', dict(types), file=sys.stderr)

json.dump(out, open('/var/home/eamon/dev/solero/resources/bethini-skyrimse.json', 'w'), indent=1)
