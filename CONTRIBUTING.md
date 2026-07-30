# Contributing to GodotJS-Ext

## Development Setup

### Prerequisites

- Git
- Python 3.8+
- SCons
- Node.js (LTS recommended)
- pnpm
- Godot 4.x editor

### Initial Setup

```bash
# Clone the repository
git clone git@github.com:godotjs/GodotJS-Ext.git
cd GodotJS-Ext

# Initialize submodules (godot-cpp, dependencies)
git submodule update --init

# Install JS dependencies
cd scripts
pnpm install
```

### Building

```bash
# Build the GDExtension (Windows)
scons platform=windows target=editor compiledb=yes debug_symbols=yes dev_build=yes -j10

# Build for Linux
scons platform=linux target=editor compiledb=yes debug_symbols=yes dev_build=yes -j10

# Build for macOS
scons platform=macos target=editor compiledb=yes debug_symbols=yes dev_build=yes -j10
```

### JavaScript Runtime

The JavaScript runtime is built separately using TypeScript:

```bash
cd scripts
pnpm install
pnpm build
```

This outputs to `scripts/out/` and is automatically embedded into the GDExtension during the scons build.

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

## Testing

### C++ Unit Tests

C++ unit tests use [doctest](https://github.com/doctest/doctest) and run automatically when the GDExtension is loaded in debug mode:

```bash
# Build with tests enabled
scons platform=windows target=editor dev_build=yes tests=yes -j10
```

### TypeScript Integration Tests

TypeScript integration tests run in a Godot project:

```bash
# Build JS runtime
cd scripts
pnpm install
pnpm build

# Run tests
pnpm test
```

## IDE Configuration

### Visual Studio Code

1. Install the C/C++ extension
2. Generate `compile_commands.json`:
   ```bash
   scons compiledb=yes platform=windows target=editor
   ```
3. Open the folder in VS Code - it should automatically detect `compile_commands.json`

### CLion

1. Generate `compile_commands.json` as above
2. Open the folder in CLion - it should automatically detect the compilation database

## Code Style

- Follow the existing code style in the project
- Use the `.clang-format` configuration for C++ formatting
- Use the `.prettierrerrc` configuration for TypeScript formatting

## Pull Request Process

1. Fork the repository
2. Create a feature branch from `main`
3. Make your changes
4. Add tests if applicable
5. Ensure all tests pass
6. Submit a pull request

## License

By contributing, you agree that your contributions will be licensed under the MIT License.
