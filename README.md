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
    <a href="https://github.com/godotjs/GodotJS/blob/main/LICENSE"><img src="https://img.shields.io/badge/License-LGPL%203.1-green.svg" alt="LGPL 2.1 License"></a>
</p>

## Documentation

For full documentation, visit [godotjs.github.io](https://godotjs.github.io/documentation/getting-started/).

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
- [x] [Worker threads](https://godotjs.github.io/documentation/experimental/worker/) (limited support) (**experimental**)
- [x] Asynchronously loaded modules (limited support) (_temporarily only available in v8.impl, quickjs.impl_)

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

The test suite includes 6 test scenes: Resource, Singleton, Extend, Papaparse, OSExecutor, and Worker. Tests report completion via console output sentinels (`GODOTJS_TEST_PROJECT_COMPLETED` / `GODOTJS_TEST_PROJECT_FAILED:`).

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

MIT License - see [LICENSE](GodotJS/LICENSE) for details.
