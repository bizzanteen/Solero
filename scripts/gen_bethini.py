#!/usr/bin/env python3
"""Generate Solero's bethini-skyrimse.json from BethINI Pie's data files.
Joins Bethini.json (widget defs: file/section/tooltip/type/range) with
settings.json (iniValues: preset values)."""
import json, sys

defs = json.load(open('/tmp/bethini-defs.json'))
sett = json.load(open('/tmp/bethini-settings.json'))

# Index iniValues by (name.lower) -> value dict. Names are unique enough for the
# UI-exposed subset; collisions are rare and we take the first.
ini_values = {}
for e in sett['iniValues']:
    ini_values.setdefault(e['name'].lower(), e)

# Collect setting widgets from displayTabs
widgets = []
def walk(o):
    if isinstance(o, dict):
        if 'settings' in o and 'targetINIs' in o and 'targetSections' in o:
            widgets.append(o)
        for v in o.values(): walk(v)
    elif isinstance(o, list):
        for v in o: walk(v)
walk(defs['displayTabs'])

PRESET_KEYS = {
    "Low": "Bethini Low",
    "Medium": "Bethini Medium",
    "High": "Bethini High",
    "Ultra": "Bethini Ultra",
    "BethINI": "recommended",  # BethINI's recommended tweak
}

def widget_type(w, ival):
    t = w.get('type')
    if t == 'Checkbutton':
        return 'bool'
    if t in ('Combobox', 'Dropdown'):
        return 'enum'
    if t == 'Slider' or t == 'Spinbox':
        dp = w.get('decimal places', '0')
        return 'float' if str(dp) not in ('0', '', None) else 'int'
    # Entry/Color/other
    if ival and ival.get('type') == 'float':
        return 'float'
    if ival and ival.get('type') == 'boolean':
        return 'bool'
    return 'string'

def coerce(v, typ):
    if v is None: return None
    try:
        if typ == 'bool':  return bool(int(v)) if str(v) in ('0','1') else bool(v)
        if typ == 'int':   return int(float(v))
        if typ == 'float': return float(v)
    except (ValueError, TypeError):
        return v
    return v

settings_out = []
seen = set()
for w in widgets:
    names = w.get('settings', [])
    inis  = w.get('targetINIs', [])
    secs  = w.get('targetSections', [])
    if not names or not inis or not secs:
        continue
    name = names[0]
    ini  = inis[0]
    sec  = secs[0]
    # Skip non-game ini files (Bethini.ini is the editor's own config)
    if ini not in ('Skyrim.ini', 'SkyrimPrefs.ini', 'SkyrimCustom.ini'):
        continue
    key = (sec, name)
    if key in seen:
        continue
    seen.add(key)

    ival = ini_values.get(name.lower())
    typ = widget_type(w, ival)

    entry = {
        "section": sec,
        "key": name,
        "file": ini,
        "label": name,  # BethINI shows raw key; tooltip carries the human description
        "description": w.get('tooltip', ''),
        "type": typ,
    }

    # Range for sliders/spinboxes (some BethINI sliders run high->low; normalize)
    if 'from' in w and 'to' in w:
        lo = coerce(w['from'], typ); hi = coerce(w['to'], typ)
        try:
            if lo is not None and hi is not None and lo > hi: lo, hi = hi, lo
        except TypeError:
            pass
        entry['min'] = lo; entry['max'] = hi
    if 'increment' in w: entry['step'] = coerce(w['increment'], typ)

    # Enum choices (must be a proper list; some widgets use a FUNC string)
    if typ == 'enum' and isinstance(w.get('choices'), list):
        choices = [str(c) for c in w['choices']
                   if isinstance(c, str) and not c.startswith('FUNC')]
        if choices:
            entry['enumValues'] = choices
        else:
            entry['type'] = 'int'  # function-driven choices → treat as numeric
            typ = 'int'

    # Presets
    presets = {}
    if ival:
        val = ival.get('value', {})
        for out_key, src_key in PRESET_KEYS.items():
            if src_key in val:
                presets[out_key] = coerce(val[src_key], typ)
        # Fall back BethINI->default if no recommended
        if 'BethINI' not in presets and 'default' in val:
            presets['BethINI'] = coerce(val['default'], typ)
    if presets:
        entry['presets'] = presets

    settings_out.append(entry)

out = {
    "game": "Skyrim Special Edition",
    "source": "Derived from BethINI Pie (DoubleYouC/Bethini-Pie-Skyrim-Special-Edition-Plugin)",
    "presets": {
        "Low":     {"description": "BethINI Low quality"},
        "Medium":  {"description": "BethINI Medium quality"},
        "High":    {"description": "BethINI High quality"},
        "Ultra":   {"description": "BethINI Ultra quality"},
        "BethINI": {"description": "BethINI recommended tweaks"},
    },
    "settings": settings_out,
}

import collections
print('settings:', len(settings_out), file=sys.stderr)
print('by file:', collections.Counter(s['file'] for s in settings_out), file=sys.stderr)
print('by type:', collections.Counter(s['type'] for s in settings_out), file=sys.stderr)
print('with presets:', sum(1 for s in settings_out if 'presets' in s), file=sys.stderr)

json.dump(out, open('/var/home/eamon/dev/solero/resources/bethini-skyrimse.json','w'), indent=2)
