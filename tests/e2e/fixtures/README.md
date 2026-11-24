# Test fixtures

Binary fixtures are stored as base64 so the repository stays text-only; a suite
decodes one into its temporary work directory when it needs the bytes.

| File | Used by | What it is |
| --- | --- | --- |
| `tiny-ogg.b64` | `gui-audio*.test.ts` | a 20 ms 440 Hz Ogg/Vorbis tone |
| `webfont-fixture.ttf.b64` | `gui-font-face.test.ts` | a 1 KiB hand-built TrueType face |
| `webfont-fixture.woff.b64` | `gui-font-face.test.ts` | the same face as WOFF 1.0 |
| `webfont-fixture.woff2.b64` | `gui-font-face.test.ts` | the same face as WOFF 2.0 (transformed `glyf`/`loca`/`hmtx`, one composite glyph) |

## webfont fixture provenance

The webfont fixtures are one face written in three containers, so the test can
require the *same* metrics from a plain sfnt, a WOFF and a WOFF2 source. The
face is generated from scratch (simple rectangular outlines, no third-party
font and no font licence): family `Xt Webfont Fixture`, glyphs `.notdef`,
`space`, `m` (advance 1000/1000 em) and `i` (a composite glyph referencing `m`).

They were produced once with fontTools (4.67) and Brotli, and are checked in
rather than rebuilt per test run because CI has no Python font toolchain:

```python
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib import TTFont

# Build the face with FontBuilder (setupGlyphOrder, setupCharacterMap,
# setupGlyf, setupHorizontalMetrics, setupHorizontalHeader, setupNameTable,
# setupOS2, setupPost), save it as the .ttf, then re-save the same TTFont with
# `font.flavor = "woff"` and `font.flavor = "woff2"`, and base64 the three files.
```

A stale fixture is still valid input — the suites only compare the three
containers against each other — so it never has to be regenerated.
