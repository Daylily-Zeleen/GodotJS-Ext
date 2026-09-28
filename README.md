<p align="center">
<picture>
  <source media="(min-width: 720px)" srcset="GodotJS/docs/header.svg">
  <img src="GodotJS/docs/header-mobile.svg" width="900" height="330" alt="GodotJS Logo">
</picture>
</p>

# **GodotJS-Ext**

<p align="center">
  TypeScript/JavaScript Support for Godot 4.x by leveraging the high-performance capabilities of V8 to bring the delightful development experience of TypeScript into Godot.
</p>

<p align="center">
    <a href="https://github.com/godotjs/GodotJS/actions"><img src="https://github.com/godotjs/GodotJS/actions/workflows/runner.yml/badge.svg?branch=main" alt="Build Status"></a>
    <a href="https://github.com/Daylily-Zeleen/GodotJS-Ext/blob/main/LICENSE"><img src="https://img.shields.io/badge/License-LGPL%202.1-green.svg" alt="LGPL 2.1 License"></a>
</p>

## Documentation

For full documentation, visit [daylily-zeleen.github.io/godotjs-ext.github.io](https://daylily-zeleen.github.io/godotjs-ext.github.io/).

---

See [Breaking Changes](https://godotjs.github.io/misc/breaking-changes/) if upgrading from old versions.

> [!IMPORTANT]
> 本项目要求 **Godot 4.7 及以上版本**（godot-cpp 绑定按 `API_VERSION = "4.7"` 生成；更旧的引擎因 godot-cpp ABI 不匹配无法加载本扩展）。

> [!NOTE]
> The core functionality is implemented and essentially usable but still under testing.

![typescript_intellisence](https://godotjs.github.io/images/typescript_intellisence.png)

## Features

- [x] Godot ScriptLanguage integration
- [x] Debug with Chrome/VSCode (with v8) and Safari (with JavaScriptCore)
- [x] REPL in Editor
- [x] Hot-reloading
- [x] Support for multiple javascript engines ([v8](https://github.com/v8/v8), [quickjs](https://github.com/bellard/quickjs), [quickjs-ng](https://github.com/quickjs-ng/quickjs), [JavaScriptCore](https://developer.apple.com/documentation/javascriptcore), the host Browser JS)
- [x] [Worker threads](https://daylily-zeleen.github.io/godotjs-ext.github.io/en/runtime/engines) (limited support) (**experimental**)
- [x] Asynchronously loaded modules (limited support) (_temporarily only available in v8.impl, quickjs.impl_)

## Difference from upstream GodotJS

GodotJS-Ext started from [godotjs/GodotJS](https://github.com/godotjs/GodotJS) and keeps its runtime
model, module system and annotation surface. Everything below is what this repository adds or
replaces on top of that; the short version is that upstream is an **engine module** you compile into
Godot, while this is a **GDExtension** you drop into an existing editor.

### Distribution and build

| | Upstream GodotJS | GodotJS-Ext |
|---|---|---|
| Form | Godot engine module (`SCsub`/`config.py` inside a Godot source tree) | GDExtension (`SConstruct`, `jsb_gdextension_init`) |
| Engine version | Follows the engine source you build against | **Godot 4.7+**; the godot-cpp bindings are generated against `API_VERSION = "4.7"` and the manifest declares `compatibility_minimum = 4.7` |
| What you download | A Godot editor build with GodotJS baked in | A per-engine `addons/` archive unpacked into your project; **no engine binaries are published** |
| Products | One engine module; `weaver-editor/**` is added to it only when the engine is built with the editor (`SCsub`, `if env.editor_build`) | **One GDExtension library, two products**: `target=editor` compiles the runtime *and* the editor sources into `godotjs-ext.<platform>.editor.<arch>`; `target=template_*` compiles the runtime only. Both share one entry symbol. |

> This repository previously shipped two GDExtensions talking over a C-ABI bridge; that was merged
> into one library (and the bridge deleted) because the editor's dependency on the runtime could not
> be removed in the first place. Upstream never had two.

### JS engines

Both support V8, QuickJS, QuickJS-NG, JavaScriptCore and the browser's host JS. This repository adds:

- **Node.js** (`use_node=yes`, libnode-embedded V8) as a first-class engine, on all three desktop legs.
- **Web as its own release axis**: `platform=web` with no engine flag builds the *browser host JS*
  (`JSB_WITH_WEB`), and threaded / non-threaded variants are distinguished.
- **Engine selection is a CI-matrix decision.** The build matrix in `.github/workflows/ci.yml` is the
  single derivation point; `misc/release/package.py` and the `verify-release-artifacts` gate both read
  it, so a release can no longer ship a platform the gate never saw.

### Bindings and codegen

- **Selectable binding mode** (`binding_mode=static|shared|dynamic`, default `shared`) — upstream's
  `JSB_WITH_STATIC_BINDINGS` is hardcoded to `0`. `static` emits per-method thunks, `shared` emits
  shared thunks plus per-method callback data, `dynamic` goes through runtime reflection. The active
  mode is readable at runtime as `BINDING_MODE` from `godot-jsb`.
- **Static-binding dispatch tables** generated at build time by `misc/build/static_binding_codegen.py`
  (`src/static_binding/gen/*`), plus a self-contained runtime side (`src/static_binding/dispatch.h`,
  `thunks/**`).
- **`api_tool`: a lazy binary store of engine class information** (`src/api_tool/`), where upstream has
  no such component. Method records are split into a hot layer read by every ptrcall (48–72 B, with a
  2-byte argument descriptor) and a cold `ApiMethodDetail` layer the editor's codegen loads on first
  access — the class-method table shrank from 3,475,812 B to 1,110,366 B, and layouts are pinned by
  `static_assert` so they cannot drift back.

### Numeric and 64-bit handling

- **64-bit integers survive the round trip.** Upstream already emits `BigInt` above 2^53-1; this
  repository's `JSB_WITH_BIGINT` governs both directions and adds the rest of the contract: a
  separate **unsigned exit path** for `uint64` (upstream converts `uint32` explicitly and has no
  `uint64` type at all), `bigint` accepted as an argument, a constructor operand and an operator
  operand, and a two-sided magnitude test (`v > MAX || v < -MAX`) — upstream's per-engine helpers used
  a one-sided test, which silently rounded every *negative* value beyond the safe range through
  `(double)` and broke `ObjectID` round-tripping. The active position is readable at runtime as
  `BIGINT_FOR_64BIT` from `godot-jsb`.
- **Narrow integer slots truncate like the engine does** instead of rejecting, so the static and
  dynamic legs agree (the engine's `binder_common.h` never checks width; the static leg previously did).
  Out-of-range values only warn in debug builds — zero cost in release.

### JS / TypeScript API

- **`@bind.exposed.const()` / `@bind.exposed.shared()`** — expose a `static` member to Godot as a
  read-only constant or as a shared static variable that GDScript can read *and write* with one value
  per process. Upstream's `Script::get_constants()`/`get_members()` overrides are empty stubs.
- **`godot.shadowRealm`** — a ShadowRealm module (`JSShadowRealm`, `TransferableJSShadowRealm`) built
  on upstream's shadow-environment plumbing but exposed to scripts as a module, which upstream does not
  do. It is disabled on the pure-web build.
- **`godot.worker`** — `JSWorker` / `JSWorkerParent` with a single transfer contract: the transferable
  list is an argument of `postMessage`. Upstream's declarations still carry the deprecated
  `worker.ontransfer` / `JSWorkerParent.transfer()` shapes alongside it; this repository exposes only
  the parameter form, which the editor autocompletes and the type checker enforces.
- **Newer engine introspection**: `BIGINT_FOR_64BIT` and `BINDING_MODE` are exported from `godot-jsb`.

### Editor integration

- **Source comments become script documentation.** A resident Node tool process
  (`scripts/jsb.tools/`) parses `.ts`/`.js` comments and feeds class/member docs to the editor, with
  `@bind.help()` taking precedence. Upstream fills `get_documentation()` from annotations only.
- **Signature sidecars** — a `.sig` file next to each compiled script carries function/signal
  signatures, so method and signal info survives without re-reading the source; it is packaged on export.
- **Config Enabled TS Classes** — a dialog (`Config Enabled Classes Bindings`) to choose which native
  classes get bindings generated, with preset import/export.
- **Tool menu**: Generate API Data, Install Project Files, Generate Types, Config Enabled TS Classes,
  Generate All Scene Nodes Types, Generate All Resource Types, Cleanup Invalid Files. The bottom dock
  is `GodotJS-Ext` (REPL + Statistics).
- **Per-source codegen** — typed surfaces for the project's own content in addition to the class API:
  every scene yields a `<Scene>.nodes.gen.ts` (node path → type) and a `<Scene>.tscn.gen.ts`
  (`PackedScene<T>` / `ResourceLoader.load()` return type), and a script resource yields a
  `<res>.gen.ts` (`ResourceTypes` entry). Upstream has the class/documentation codegen only. This is
  additive to, not a replacement for, the generator above.

### Release, testing and CI

- **Per-engine release archives** with a `.gdextension` generated for exactly the files each archive
  contains, a 1:1 self-check in `assemble`, and `[icons]` packaged alongside the binaries. Upstream
  uploads one asset per OS/target/engine combination with a fixed layout.
- **`[information]`** — a project-private metadata block (name/version/author/support link) whose
  `version` is rewritten from the release tag.
- **doctest C++ suite** driven by one `--jsb-run-tests` flag (editor cases compile into the same
  registry on `target=editor`), an integration matrix with 16 scenario groups under `project/tests/`
  (benchmark, cross-environment, default-args, extend, gen_dts_test, indexed-props, int64, numeric,
  operators, os-executor, papaparse, paths_test, resource, singleton, static-members and a CJK-path
  case), a codegen baseline verifier (`misc/verify_codegen.py`), and a **static-vs-dynamic
  benchmark** with a cross-leg consistency gate (`misc/bench_matrix.py`).
- **Changesets release chain**: version PR → CI → `release.yml` → `misc_release.yml`, with the
  packaging plan shared between the release and the gate.

### Not a capability difference

- Path/hash helpers on the `String` prototype are **not a difference**: upstream has no such extension,
  and this repository's own copy of it has been removed. Scripts that used those helpers need their own
  implementation.
- Legacy `@Export` / `@ExportSignal` decorators still work but are deprecated in favour of
  `createClassBinder()` — same as upstream.

## Examples

For more information on how to use `GodotJS` in a project, check out [GodotJSExample](https://github.com/ialex32x/GodotJSExample.git) for examples written in typescript.  
**And, don't forget to run `npm install` and `npx tsc` before opening the example project.**

[![Example: Snake](https://godotjs.github.io/images/snake_01.gif)](https://github.com/ialex32x/GodotJSExample.git)
[![Example: Jummpy Bird](https://godotjs.github.io/images/jumpybird.gif)](https://github.com/ialex32x/GodotJSExample.git)

## Building

### Prerequisites

- Godot 4.x editor
- Python 3.8+
- SCons
- pnpm (for JavaScript runtime)

### Build Steps

```bash
# Initialize submodules
git submodule update --init

# Build the GDExtension
scons platform=windows target=editor compiledb=yes debug_symbols=yes dev_build=yes -j10
```

### JavaScript Engines

GodotJS-Ext supports multiple JavaScript engines:

```bash
# V8 (default, prebuilt)
scons platform=windows target=editor -j10

# QuickJS (built from source)
scons platform=windows target=editor use_quickjs=yes -j10

# QuickJS-NG (built from source)
scons platform=windows target=editor use_quickjs_ng=yes -j10
```

### Testing

#### C++ Unit Tests

C++ unit tests use [doctest](https://github.com/doctest/doctest) and are run by passing the `--jsb-run-tests` flag when loading the GDExtension:

```bash
# Build with tests enabled
scons platform=windows target=editor dev_build=yes tests=yes -j10

# Run tests (requires Godot editor binary)
godot --path project --jsb-run-tests
```

#### TypeScript Integration Tests

TypeScript integration tests run in a Godot project using the test project in `project`:

```bash
# 1. Build the GDExtension
scons platform=windows target=editor compiledb=yes debug_symbols=yes dev_build=yes -j10


# 2. Install JS dependencies, generate typings, compile TypeScript
cd project
pnpm install
pnpm gen:types       # requires GODOT env var pointing at the engine binary
npx tsc              # no --noCheck: the test project must type-check cleanly

# 3. Run the test project headlessly
& godot --audio-driver Dummy --headless --path . --verbose --debug
```

The test suite runs 14 scenes in a fixed order (Resource, Singleton, Extend, Papaparse, OSExecutor,
CrossEnvironment, SourceMap, DefaultArgs, Operators, StaticMembers, StaticMembersGd, Numeric, Int64,
IndexedProps); the count is derived from the list, so adding a scene cannot desync the diagnostic.
Tests report completion via console output sentinels (`GODOTJS_TEST_PROJECT_COMPLETED` /
`GODOTJS_TEST_PROJECT_FAILED:`).

### Generating the API Tool Data (Command Line)

The editor plugin keeps engine class information in a binary store under
`.godot/.api_dumping`, which is required before TypeScript code can resolve
Godot types. In a fresh checkout (or CI) this store does not exist yet and
must be bootstrapped in two steps:

```bash
# 1. Dump the extension API together with docs. On some Godot versions the
#    editor aborts during shutdown, but extension_api.json is already
#    written by then.
godot --headless --editor --path project --dump-extension-api-with-docs

# 2. Convert extension_api.json into the binary store. When running
#    headless, the editor exits automatically once finished.
godot --headless --editor --path project --godotjs-api-generate extension_api.json
```

`--godotjs-api-generate <extension_api.json>` accepts a path relative to
the project directory (or an absolute path). It is also the receiving end
of the reboot chain spawned by the editor menu command *Generate API Data*,
which saves the scenes, quits and relaunches the editor with this argument.


## Release

This repository uses the `scripts` directory as the pnpm workspace root for versioning and release management. Changesets updates the workspace version and changelog, while GitHub Actions creates the GitHub Release and uploads the platform binaries.

### Create a Changeset

When a change should be included in the next release, run the following commands from the repository root:

```bash
cd scripts
pnpm install
pnpm changeset
```

Select `@godot-js/editor` and choose the version increment:

- `patch`: bug fixes and small maintenance changes;
- `minor`: backward-compatible features;
- `major`: breaking changes.

The command creates a Markdown file under `scripts/.changeset/`. Commit that file with your code and open a pull request. Changesets runs only for pushes to `main`, so feature branches and pull requests run the normal build and test jobs without creating releases.

You can check the pending release plan locally with:

```bash
cd scripts
pnpm changeset status
```

### Release Flow

After a pull request containing a Changeset is merged into `main`:

1. The **Changesets** job creates or updates the `changeset-release/main` version pull request.
2. Merging that version pull request updates `scripts/package.json`, `scripts/CHANGELOG.md`, and `src/jsb_version.h`.
3. The next `main` CI workflow completes the build and test jobs. A separate `workflow_run` workflow then detects the generated changelog entry and creates the GitHub Release.
4. After the Release is created, the same workflow packages the artifacts from the successful source CI run **once per JS engine** and attaches all five packages.

### Release Packages

Each GitHub Release carries one archive **per JS engine**, and each archive contains only the platforms that engine is actually built for. The contents (and the `.gdextension` files inside) are derived from the build matrix in `.github/workflows/ci.yml` by `misc/release/package.py` — the same script the `Verify Release Artifact Names` CI gate runs — so the assets and the gate can never drift apart.

| Engine | Asset | Platforms inside |
|---|---|---|
| V8 | `godotjs-ext-v8-windows-linux-macos-android-ios.zip` | Windows, Linux (x86_64 + arm64), macOS (arm64), Android (arm64 + x86_64), iOS (arm64) |
| QuickJS-NG | `godotjs-ext-qjs-ng-windows-linux-macos-android-ios-web.zip` | the same plus Web (wasm32, threaded and not) |
| JavaScriptCore | `godotjs-ext-jsc-macos-ios.zip` | macOS (arm64), iOS (arm64) |
| Node.js | `godotjs-ext-node-windows-linux-macos.zip` | Windows, Linux (x86_64), macOS (arm64) |
| Browser | `godotjs-ext-web.zip` | Web (wasm32, threaded and not) |

The original QuickJS engine is superseded by QuickJS-NG and is not packaged; `use_quickjs=yes` still builds locally.

Every archive is a drop-in `addons/godotjs-ext.daylily-zeleen/` directory: it carries the per-platform binaries under `bin/<platform>/` plus the single `godotjs-ext.gdextension` whose `[libraries]` entries match exactly the files in the archive. Desktop engines ship the editor target only (edit-in-editor use), matching the previous releases.

> [!NOTE]
> `godotjs-ext-web.zip` contains the browser engine alone. The editor still needs a desktop engine to load the extension and to run a Web export, so unpack this archive **over** the desktop engine package you use (the directory layout is identical, so it merges cleanly) — or simply use the QuickJS-NG package, which already includes a QuickJS-based web build.

The build and publish stages are intentionally separate. If a matrix build, especially Windows V8 dependency download, needs to be rerun, the successful CI run can finish without relying on downstream jobs that were already skipped in the original run. The upload workflow uses the source run ID directly because Releases created with the Actions `GITHUB_TOKEN` do not trigger another workflow through `release: published`.

The release workflow does not publish an npm package. The versioned package is private and is used to coordinate the extension version and release notes for the Godot binaries.

### Repository Permissions

The repository must allow GitHub Actions to write repository contents and pull requests. In GitHub repository settings, enable the workflow permission that allows Actions to create and approve pull requests; otherwise the Changesets action cannot create or update the version pull request.

### Troubleshooting

- **`Some packages have been changed but no changesets were found`**: create a Changeset with `pnpm changeset`, or use an empty Changeset only for changes that do not require a release.
- **`Release` is skipped**: confirm that the Version PR was merged into `main` and that `scripts/CHANGELOG.md` contains the new version heading.
- **`Upload Assets` is skipped**: inspect the `Publish Release` workflow, which creates the Release and uploads assets from the source CI run. For an existing Release, use the `Upload Release Assets` manual workflow with the Release tag and a successful CI run ID.
- **Windows V8 download fails**: the CI caches `third/v8`, and SCons retries interrupted V8 downloads automatically. If the first run still fails before the cache is populated, rerun the failed Windows V8 job and inspect the download/retry messages.
- **Release notes are missing**: verify that `scripts/CHANGELOG.md` contains a `## <version>` heading and that the release job runs from the `scripts` workspace.

## Project Structure

```
.
├── src/                        # C++ source code
│   ├── runtime/                # Script/ScriptLanguage, module loading, Environment,
│   │   │                       # engine impls (v8/quickjs/node/jsc/web)
│   │   ├── bridge/             # godot bridge + module loaders
│   │   ├── impl/               # Per-engine layers (v8 / quickjs / node / jsc / web)
│   │   ├── internal/           # Runtime-internal utilities (shared statics, settings, logger)
│   │   └── tests/              # doctest suite (--jsb-run-tests; editor cases included
│   │                           #   in target=editor builds)
│   ├── editor/                 # Editor sources: EditorPlugin, REPL, export plugin,
│   │   ├── codegen/            #   C++ code generator (api_tool -> gen/ + typings/)
│   │   ├── weaver-editor/      #   Editor plugin / dock / REPL UI
│   │   └── tests/              #   Editor doctest cases (same registry / flag)
│   ├── api_tool/               # API data tooling (parse extension_api.json -> binary store)
│   ├── compat/                 # Compatibility layer
│   ├── internal/               # Shared internal utilities
│   └── tests/                  # Test runner shared by all suites
├── scripts/                    # JavaScript/TypeScript toolchain
│   ├── jsb.runtime/            # Runtime TypeScript package
│   ├── jsb.editor/             # Editor TypeScript package
│   ├── out/                     # Built JS output (gitignored)
│   └── typings/                # TypeScript type definitions
├── third/                      # Third-party dependencies
│   ├── godot-cpp/              # godot-cpp binding library
│   ├── v8/                     # V8 engine (prebuilt)
│   ├── quickjs/                # QuickJS engine
│   ├── quickjs-ng/             # QuickJS-NG engine
│   └── doctest/                # C++ test framework
├── project/                    # Godot test project (integration tests)
├── GodotJS/                    # Original GodotJS module (submodule)
└── SConstruct                  # SCons build script
```

### One library, two products

The project ships **one GDExtension** (`godotjs-ext.gdextension`) built by a
single `SConstruct` target, whose product differs per build target:

- **`target=editor`** compiles the runtime sources *and* the editor sources
  (`src/editor/**`, `src/api_tool/editor/**`) into
  `godotjs-ext.<platform>.editor.<arch>.<ext>`: the script language, module
  loading, `EditorPlugin`, REPL, export plugin and the C++ code generator.
- **`target=template_release` / `template_debug`** compile the runtime sources
  only into `godotjs-ext.<platform>.template_<flavor>.<arch>.<ext>`. The editor
  sources are never compiled in (they need the `TOOLS_ENABLED` godot-cpp
  headers, which template builds do not have), so no editor code exists in an
  exported game.

Both products expose the same entry symbol (`jsb_gdextension_init`); the
`JSB_WITH_EDITOR` macro mirrors the target split inside shared translation
units.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for development setup and guidelines.

## Updating Trellis Files

This repository uses [Trellis](https://github.com/mindfoldhq/trellis) for task/workflow management. Before running an update, know how it treats your local edits:

- **Managed files are overwritten, not merged.** Everything listed in `.trellis/.template-hashes.json` (`.trellis/workflow.md`, `.trellis/config.yaml`, `.omp/agents/*.md`, `.omp/extensions/trellis/index.ts`, `.omp/skills/**`, `.trellis/scripts/**`, …) is replaced by the upstream template on update. Your customizations to them do **not** survive unless you skip them.
- **Free files are never touched.** Files outside the hash table — `.trellis/spec/**`, `.trellis/workspace/**`, `.trellis/tasks/**`, `.omp/hooks/**`, and the content of `AGENTS.md` outside its managed block — are preserved by design.

Recommended update flow:

```bash
# 1. Preview what the update will change
trellis update --dry-run

# 2. Apply, but skip every file you have customized
trellis update -s        # skip all modified-by-you files
# NEVER use -f here: it force-overwrites managed files and drops local edits
```

After updating:

1. Re-check `git status` for the files you customized (`.trellis/workflow.md`, `.trellis/config.yaml`, `.omp/agents/*.md`, `.omp/extensions/trellis/index.ts`, …). If `trellis update` was run with `-s`, they are untouched; if you had to let one be overwritten, re-apply your edits.
2. `AGENTS.md` is the exception: update performs a **block-level merge** between `<!-- TRELLIS:START -->` and `<!-- TRELLIS:END -->`. Content outside that block is always preserved, so your project rules there are safe.
3. `.trellis/spec/**`, `.trellis/tasks/**`, `.trellis/workspace/**` are protected (user data) and are never modified by update.

Where to keep durable rules:

- Put rules that must survive updates in **free files**: `.trellis/spec/guides/` (e.g. `workflow-rules.md`) or `.omp/hooks/`. They are never overwritten.
- If a rule must live in a managed file (e.g. the per-turn breadcrumb in `.trellis/workflow.md`, because the injector parses it there), keep a copy of the rule text in a free file and re-apply it after an update. The repository keeps this guidance in `.trellis/spec/guides/workflow-rules.md` under "工作流面包屑的维护规则".

`trellis update` also writes its own backup under `.trellis/.backup-*/` (gitignored) before changing anything, so a botched run can be inspected there.

## License

GNU LGPL 2.1 - see [LICENSE](LICENSE) for details.

Third-party components under `third/` keep their own upstream licenses (for example `third/GodotJS/LICENSE`
is MIT, copyright the GodotJS authors).
