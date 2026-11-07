# GF Amiga Poedit

Poedit, the translation editor for gettext PO files, for AmigaOS 3 (68040, FPU used when present), on the GFOpenWX Toolkit (wxWidgets on GFOpenLayout).

Poedit and the libraries it uses are fetched at build time at the versions `poedit.lock` pins; nothing third-party is kept in this repository.

Designed, tested and delivered by The AmiCradle Team.
Copyright (c) 2026 AmiCradle Team

## ARexx

Poedit has an ARexx port, `POEDIT.1` (`POEDIT.2` for a second copy), from the GFOpenWX Toolkit's `wxARexxPort` on arexx.class (`src/amiga/rexx.cpp`, patch 0007).

| Command | Does |
| --- | --- |
| `OPEN FILE/A` | open a translation (`.po`, `.xliff`, `.json` ...) in a window |
| `SAVE` | save the front window's translation (and its `.mo`) |
| `STATS` | RESULT: the front window's entries: all, finished, fuzzy, untranslated, with errors (`120 100 12 8 0`) |
| `QUIT`, `ACTIVATE`, `DEACTIVATE`, `VERSION`, `HELP`, `FAULT` | the toolkit's standard commands |

The front window is the active one, else the first one; RC 10 when no window has a translation.

```
/* Poedit: how far is the German translation? */
OPTIONS RESULTS
ADDRESS 'POEDIT.1'
'OPEN Work:Translations/de.po'
'STATS'
PARSE VAR result all done fuzzy todo bad
SAY done 'of' all 'finished,' fuzzy 'fuzzy,' todo 'to do'
```

`Rexx/Stats.rexx` (in the Rexx drawer beside the program) is an example.
