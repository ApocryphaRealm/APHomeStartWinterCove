#!/usr/bin/env python3
r"""Write the eleven translation files for AP Home Start - Winter Cove.

Every string the AP menu shows lives here, English first and each of the other
ten languages beside it. Output: dist\Interface\Translations\APHomeStartWinterCove_<language>.txt, UTF-16LE with
BOM, one "$key<TAB>text" per line - the SKSE/SkyUI shape the game reads.
A key missing from a language falls back to English at runtime (utils/Strings.h), and this script says
so at build time so a half-finished table never ships silently.

Usage:  python build-translations.py
"""
import io
import os

E = {
    'AHS_Card': 'Winter Cove',
    'AHS_OwnCard': 'Winter Cove',
    'AHS_CardDescr': 'Winter Cove is already yours - the key is in your pack.',
}

T = {
    'german': {
        'AHS_Card': 'Winter Cove',
        'AHS_OwnCard': 'Winter Cove',
        'AHS_CardDescr': 'Winter Cove gehört bereits dir - der Schlüssel ist in deinem Gepäck.',
    },
    'french': {
        'AHS_Card': 'Winter Cove',
        'AHS_OwnCard': 'Winter Cove',
        'AHS_CardDescr': 'Winter Cove vous appartient déjà : la clé est dans votre sac.',
    },
    'spanish': {
        'AHS_Card': 'Winter Cove',
        'AHS_OwnCard': 'Winter Cove',
        'AHS_CardDescr': 'Winter Cove ya te pertenece: la llave está en tu mochila.',
    },
    'italian': {
        'AHS_Card': 'Winter Cove',
        'AHS_OwnCard': 'Winter Cove',
        'AHS_CardDescr': 'Winter Cove ti appartiene già: la chiave è nello zaino.',
    },
    'polish': {
        'AHS_Card': 'Winter Cove',
        'AHS_OwnCard': 'Winter Cove',
        'AHS_CardDescr': 'Winter Cove już należy do ciebie - klucz jest w plecaku.',
    },
    'czech': {
        'AHS_Card': 'Winter Cove',
        'AHS_OwnCard': 'Winter Cove',
        'AHS_CardDescr': 'Winter Cove už patří tobě - klíč máš v batohu.',
    },
    'russian': {
        'AHS_Card': 'Winter Cove',
        'AHS_OwnCard': 'Winter Cove',
        'AHS_CardDescr': 'Winter Cove уже принадлежит вам: ключ у вас в сумке.',
    },
    'japanese': {
        'AHS_Card': 'Winter Cove',
        'AHS_OwnCard': 'Winter Cove',
        'AHS_CardDescr': 'Winter Coveはすでにあなたのものだ。鍵は荷物の中にある。',
    },
    'korean': {
        'AHS_Card': 'Winter Cove',
        'AHS_OwnCard': 'Winter Cove',
        'AHS_CardDescr': 'Winter Cove은(는) 이미 당신의 것입니다. 열쇠는 가방 안에 있습니다.',
    },
    'chinese': {
        'AHS_Card': 'Winter Cove',
        'AHS_OwnCard': 'Winter Cove',
        'AHS_CardDescr': 'Winter Cove已经属于你了——钥匙就在行囊中。',
    },
}


def main() -> int:
    here = os.path.dirname(os.path.abspath(__file__))
    out = os.path.normpath(os.path.join(here, "..", "dist", "Interface", "Translations"))
    os.makedirs(out, exist_ok=True)

    def write(lang, table):
        lines = ["$%s\t%s" % (k, table.get(k, E[k])) for k in E]
        with io.open(os.path.join(out, "APHomeStartWinterCove_%s.txt" % lang), "w", encoding="utf-16-le", newline="\r\n") as f:
            f.write("﻿" + "\n".join(lines) + "\n")

    write("english", E)
    bad = 0
    for lang, table in T.items():
        missing = [k for k in E if k not in table]
        extra = [k for k in table if k not in E]
        if missing or extra:
            print("%s: missing %s extra %s" % (lang, missing, extra))
            bad += 1
        write(lang, table)
    print("wrote %d files, %d keys each, to %s" % (1 + len(T), len(E), out))
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
