# Vendored test262 cases

The files under `cases/` are **unmodified** copies from
[test262](https://github.com/tc39/test262), the ECMAScript conformance suite
maintained by TC39.

- Upstream commit: `d53ebc8b1931d9f1541e43d59650776e4b766248` (2026-10-13)
- Copyright and licence: see [`LICENSE`](./LICENSE) (BSD-3-Clause); every copied
  file keeps its original copyright header and frontmatter.
- Selection: one or two cases per area in [`manifest.ts`](./manifest.ts), chosen
  because xbintsc compiles **and runs** them today. The suite fails if a case
  regresses, so the support matrix cannot claim more than the compiler does.

## What is adapted

Only the harness is adapted, never the test source:

- `harness/assert.js` provides the test262 assertion API
  (`assert`, `assert.sameValue`, `assert.throws`, `assert.compareArray`, ...)
  on `Function.prototype`. test262's own harness attaches those helpers to the
  `assert` function object, but xbintsc currently creates a fresh closure for
  every reference to a function declaration, so expando properties on `assert`
  are not observable. The prototype placement keeps `assert.sameValue(...)`
  resolving without changing a single test file.
- `includes:` harness files are not copied: `harness/assert.js` already provides
  the helpers the selected cases use.
- `flags: [async]` cases must print `Test262:AsyncTestComplete`; the runner
  checks that marker, so a continuation that never runs cannot pass.

## Adding a case

1. Copy the file from a test262 checkout, preserving its path under `cases/` and
   its copyright header.
2. Add an entry to `SUPPORTED_CASES` in `manifest.ts` with an area and a
   bilingual claim.
3. Run `npm test -- tests/conformance` and `npm run docs:matrix`.

If a supported case regresses, fix the compiler or move the entry to
`UNSUPPORTED_CASES` with the diagnostic xbintsc produces — do not edit the test.
