"""Metin katalogu anahtari ekler (tr-TR + en-US + string_ids.h) — elle tutarsizligi onler.

Kullanim: python -I scripts/add_strings.py <keys.json>
keys.json: {"diag.on_battery": ["Türkçe", "English"], ...}
"""
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def ident(key):
    return ''.join(p.capitalize() for p in key.replace('.', '_').split('_'))


def main(path):
    add = json.load(open(path, encoding='utf-8'))
    for idx, loc in ((0, 'tr-TR'), (1, 'en-US')):
        p = os.path.join(ROOT, 'design', 'strings', f'{loc}.json')
        d = json.load(open(p, encoding='utf-8'))
        for k, v in add.items():
            d['strings'][k] = v[idx]
        with open(p, 'w', encoding='utf-8', newline='\n') as f:
            f.write(json.dumps(d, ensure_ascii=False, indent=2) + '\n')
    h = os.path.join(ROOT, 'src', 'core', 'i18n', 'string_ids.h')
    lines = open(h, encoding='utf-8').read().rstrip('\n').split('\n')
    existing = {l.split('"')[1] for l in lines if l.strip().startswith('X(')}
    bs = chr(92)
    new = [k for k in add if k not in existing]
    if new:
        lines[-1] = lines[-1] + ' ' + bs
        for k in new:
            lines.append(f'    X({ident(k)}, "{k}") ' + bs)
        lines[-1] = lines[-1][:-2]
    with open(h, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    print(f'{len(new)} yeni anahtar')


if __name__ == '__main__':
    main(sys.argv[1])
