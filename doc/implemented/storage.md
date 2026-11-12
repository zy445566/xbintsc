# Storage (localStorage / sessionStorage / indexedDB)

> Part of [Implemented syntax and features](../implemented.md) · [简体中文](../zh-CN/implemented/storage.md)

The three browser storage globals — `localStorage`, `sessionStorage` and
`indexedDB` — are always available (they are core globals), backed by a
vendored SQLite (`runtime/xt_storage/sqlite3`). No extension flag is needed.

## Backend selection

The backend is chosen at build time from the `storage` object in
`xbintsc.config.json`; it defaults to an **in-memory** SQLite database:

```json
{ "storage": { "backend": "memory" } }
```

```json
{ "storage": { "backend": "file", "path": "data/app.db" } }
```

`storage.path` resolves against the config file's directory (absolute paths are
used as-is). `sessionStorage` is **always** in-memory regardless of the backend,
because a session never outlives the process; `localStorage` and `indexedDB`
follow the configured backend. The driver embeds the resolved backend and path
as a small object the runtime reads at first use (`src/driver/storage.ts`).

## It is a link feature

SQLite is heavy, so the whole implementation is a **link feature** named
`storage`, declared by the always-on core extension: the driver scans the
bundled program and links it only when the code may reach one of the three
globals. A program that never touches storage pays nothing for SQLite.
`--feature storage` forces the backend on (for storage reached through names
the scan cannot see) and `--no-feature storage` keeps it out.

## Surface

| Global | Notes |
| --- | --- |
| `localStorage` | `getItem` / `setItem` / `removeItem` / `clear` / `key(i)` / `length`, backed by the configured backend. |
| `sessionStorage` | the same surface, always in-memory. |
| `indexedDB` | `open(name, version?)`, `deleteDatabase(name)`; databases with `createObjectStore` / `deleteObjectStore` / `transaction`; object stores with `put` / `add` / `get` / `getAll` / `getKey` / `delete` / `clear` / `count`; requests with `onsuccess` / `onerror` / `onupgradeneeded` / `result` / `error`. |

IndexedDB requests settle from a microtask, so the `onsuccess` / `onerror` /
`onupgradeneeded` handlers fire asynchronously like the browser. Values are
serialised as JSON; keys are canonicalised so a number key `5` and a string key
`"5"` stay distinct. `keyPath` and `autoIncrement` are supported on
`createObjectStore`.

## Limits

- The indexedDB subset is focused: no `index`, `IDBKeyRange`, cursors,
  `createIndex`, or `count(key)`/`getAll(query, count)` filtering; `getAll`
  returns the whole store and `count` the whole store.
- Transactions are not atomic — each object-store operation is committed
  immediately — and `abort` is not implemented.
- JSON serialisation means non-JSON values (`undefined`, functions, `Date`
  identity) round-trip as `null` or are dropped.
- The key returned by `key(i)` / `objectStoreNames` uses insertion order
  (`rowid`); re-setting a Web Storage key keeps its original position.
- The `gui` extension keeps `window.localStorage` as its engine's own in-memory
  store; the bare `localStorage` global (and `sessionStorage` / `indexedDB`)
  are the SQLite-backed globals described here.
