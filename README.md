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
"F:\godot\godot\bin\Godot_v4.7-stable_win64.exe" --path project --jsb-run-tests
```

#### TypeScript Integration Tests

TypeScript integration tests run in a Godot project using the test project in `test/project`:

```bash
# 1. Build the GDExtension
scons platform=windows target=editor compiledb=yes debug_symbols=yes dev_build=yes -j10

# 2. Copy the built DLL to the test project addon directory
Copy-Item bin\windows\godotjs-ext.windows.editor.x86_64.dll test\project\addons\godotjs-ext.daylily-zeleen\bin\windows\

# 3. Install JS dependencies and compile TypeScript
cd test\project
pnpm install
npx tsc --noCheck

# 4. Run the test project headlessly
& "F:\godot\godot\bin\Godot_v4.7-stable_win64.exe" --audio-driver Dummy --headless --path .
```

The test suite includes 6 test scenes: Resource, Singleton, Extend, Papaparse, OSExecutor, and Worker. Tests report completion via console output sentinels (`GODOTJS_TEST_PROJECT_COMPLETED` / `GODOTJS_TEST_PROJECT_FAILED:`).

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
├── project/                # Godot test project
├── GodotJS/                # Original GodotJS module (submodule)
├── SConstruct              # SCons build script
└── test/                   # Test projects and scripts
```

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for development setup and guidelines.

## License

MIT License - see [LICENSE](GodotJS/LICENSE) for details.
