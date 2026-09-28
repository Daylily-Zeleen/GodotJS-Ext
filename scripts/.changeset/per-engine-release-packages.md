---
"@godot-js/editor": patch
---

Release one archive per JS engine (v8, qjs-ng, jsc, node, web) instead of a single
v8/qjs-ng pair, and ship a `.gdextension` whose `[libraries]`/`[dependencies]`
entries match the files actually present in the archive.

The engine list, the legs each archive contains and the packaged
`.gdextension` are all derived from the CI build matrix by
`misc/release/package.py` - the same script the `verify-release-artifacts` gate
runs - so jsc and node can no longer be silently left out of a release, and the
browser (pure-web) wasm can no longer end up inside the v8 archive.
