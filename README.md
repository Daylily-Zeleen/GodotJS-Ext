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
    <a href="https://github.com/godotjs/GodotJS/blob/main/LICENSE"><img src="https://img.shields.io/badge/License-MIT-yellow.svg" alt="MIT License"></a>
</p>

## Documentation

For full documentation, visit [godotjs.github.io](https://godotjs.github.io/documentation/getting-started/).

---

See [Breaking Changes](https://godotjs.github.io/misc/breaking-changes/) if upgrading from old versions.

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
Godot_v4.7.1-stable_win64.exe --path project --jsb-run-tests
```

#### TypeScript Integration Tests

TypeScript integration tests run in a Godot project using the test project in `project`:

```bash
# 1. Build the GDExtension
scons platform=windows target=editor compiledb=yes debug_symbols=yes dev_build=yes -j10


# 2. Install JS dependencies and compile TypeScript
cd project
pnpm install
npx tsc --noCheck

# 3. Run the test project headlessly
& "godot.windows.editor.x86_64.exe" --audio-driver Dummy --headless --path . --verbose --debug
```

The test suite includes 6 test scenes: Resource, Singleton, Extend, Papaparse, OSExecutor, and Worker. Tests report completion via console output sentinels (`GODOTJS_TEST_PROJECT_COMPLETED` / `GODOTJS_TEST_PROJECT_FAILED:`).

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
4. After the Release is created, the same workflow downloads artifacts from the successful source CI run and attaches the V8 and QuickJS-NG packages.

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
├── src/                    # C++ source code
│   ├── bridge/             # JavaScript bridge implementation
│   ├── compat/             # Compatibility layer
│   ├── internal/           # Internal utilities
│   ├── tests/              # C++ unit tests (doctest)
│   ├── weaver/             # Script language implementation
│   ├── weaver-editor/      # Editor plugins
│   ├── register_types.cpp  # GDExtension entry point
│   └── jsb.config.h        # Configuration header
├── scripts/                # JavaScript/TypeScript toolchain
│   ├── jsb.runtime/        # Runtime TypeScript package
│   ├── jsb.editor/         # Editor TypeScript package
│   ├── out/                 # Built JS output (gitignored)
│   └── typings/            # TypeScript type definitions
├── third/                  # Third-party dependencies
│   ├── godot-cpp/          # godot-cpp binding library
│   ├── v8/                 # V8 engine (prebuilt)
│   ├── quickjs/            # QuickJS engine
│   ├── quickjs-ng/         # QuickJS-NG engine
│   └── doctest/            # C++ test framework
├── project/                # Godot test project (integration tests)
├── GodotJS/                # Original GodotJS module (submodule)
├── SConstruct              # SCons build script
```

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for development setup and guidelines.

## License

MIT License - see [LICENSE](GodotJS/LICENSE) for details.
