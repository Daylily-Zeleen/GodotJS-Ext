#!/usr/bin/env python
import os
import sys
import io
import json
import re
import shutil
import subprocess
import time
import urllib.request
import zipfile
import zlib

from methods import print_error, print_warning

os.system("chcp 65001")

libname = "godotjs-ext"
projectdir = "project"
addon_dir = os.path.join(projectdir, "addons", "godotjs-ext.daylily-zeleen")

localEnv = Environment(tools=["default"], PLATFORM="")

customs = ["custom.py"]
customs = [os.path.abspath(path) for path in customs]

opts = Variables(customs, ARGUMENTS)
opts.Add(BoolVariable("use_quickjs", "Prefer to use QuickJS rather than the default VM", False))
opts.Add(BoolVariable("use_quickjs_ng", "Prefer to use QuickJS-NG rather than the default VM", False))
opts.Add(BoolVariable("use_jsc", "Prefer to use JavaScriptCore (macos/ios only)", False))
opts.Add(BoolVariable("use_typescript", "Build with typescript support", True))
opts.Add(BoolVariable("skip_js_runtime", "Skip building GodotJS JavaScript runtime files", False))
opts.Update(localEnv)

Help(opts.GenerateHelpText(localEnv))

env = localEnv.Clone()
env["build_profile"] = "./build_profile.json"

if not (os.path.isdir("godot-cpp") and os.listdir("godot-cpp")):
    print_error("godot-cpp is not available. Initialize git submodules first.")
    sys.exit(1)

# =============================================================================
# GodotJS Build Integration
# =============================================================================

third_folder_name = "third"
third_dir = f"GodotJS/{third_folder_name}"

env = SConscript("godot-cpp/SConstruct", {"env": env, "customs": customs})

# Change to GodotJS directory for dependency resolution
original_cwd = os.getcwd()
os.chdir("GodotJS")

jsb_platform = "linux" if env["platform"] == "linuxbsd" else env["platform"]
jsb_arch = env["arch"]

# Dependencies
deps_release_tag = "1.1"
deps_v8_version = "12.4.254.21"
deps_lws_version = "4.3"
deps_url = "https://github.com/godotjs/GodotJS-Dependencies/releases"

class LibraryDetails:
    def __init__(self, platform, arch, libname, delimiter):
        self.platform = platform
        self.arch = arch
        self.libname = libname
        self.delimiter = delimiter
    def platform_base(self):
        return self.platform + self.delimiter + self.arch + self.delimiter + "release"

class LibraryDescriptor:
    def __init__(self, name, details):
        self.name = name
        self.details = details

class ThirdPartyDetails:
    def __init__(self, path, sources, validator=None):
        self.path = path
        self.sources = sources
        self.validator = validator if validator is not None else lambda: True

class ThirdPartyDescriptor:
    def __init__(self, name, details):
        self.name = name
        self.details = details

v8_prebuilt_libs = LibraryDescriptor("v8", [
    LibraryDetails("windows", "x86_64", "v8_monolith.lib", "_"),
    LibraryDetails("linux", "x86_64", "libv8_monolith.a", "."),
    LibraryDetails("linux", "arm64", "libv8_monolith.a", "."),
    LibraryDetails("macos", "x86_64", "libv8_monolith.a", "."),
    LibraryDetails("macos", "arm64", "libv8_monolith.a", "."),
    LibraryDetails("ios", "arm64", "libv8_monolith.a", "."),
    LibraryDetails("android", "x86_64", "libv8_monolith.a", "."),
    LibraryDetails("android", "arm64", "libv8_monolith.a", "."),
])
lws_prebuilt_libs = LibraryDescriptor("lws", [
    LibraryDetails("windows", "x86_64", "websockets_static.lib", "_"),
    LibraryDetails("linux", "x86_64", "libwebsockets.a", "_"),
    LibraryDetails("linux", "arm64", "libwebsockets.a", "_"),
    LibraryDetails("macos", "x86_64", "libwebsockets.a", "_"),
    LibraryDetails("macos", "arm64", "libwebsockets.a", "_"),
])
quickjs_src_descs = ThirdPartyDescriptor("quickjs", [
    ThirdPartyDetails(f"{third_folder_name}/quickjs-ng", ["dtoa.c", "libregexp.c", "libunicode.c", "quickjs.c"]),
    ThirdPartyDetails(f"{third_folder_name}/quickjs", ["cutils.c", "libbf.c", "libregexp.c", "libunicode.c", "quickjs.c"]),
])

class CompileDefines:
    def __init__(self, name, value, help=None):
        self.name = name
        self.value = value
        self.help = help

def check(condition, text):
    if not condition:
        print_error(text)
        Exit(2)

def is_library_supported(support):
    for details in support.details:
        if jsb_platform == details.platform and jsb_arch == details.arch:
            return True
    return False

def validate_library_support(support):
    for details in support.details:
        if jsb_platform != details.platform or jsb_arch != details.arch:
            continue
        libpath = os.path.join(third_folder_name, support.name, details.platform_base(), details.libname)
        if os.path.exists(libpath):
            return (support, details)
    return None

def get_thirdparty_support(support, path):
    for details in support.details:
        if details.path == path and os.path.exists(details.path):
            return (support, details)
    return None

def read_macro_value(name, def_val=None):
    with open("jsb.config.h", "rt", encoding="utf-8") as f:
        regex = rf"^#define\s+{name}\s+(\d+)$"
        for line in f:
            matches = re.finditer(regex, line)
            for _, match in enumerate(matches, start=1):
                return match.group(1)
    if def_val is not None:
        return def_val
    raise ValueError(f"no {name} defined in jsb.config.h")

def download_dependency(name, version, target_dir):
    if os.path.exists(target_dir):
        return
    filename = f"{name}_{version}.zip"
    url = f"{deps_url}/download/{deps_release_tag}/{filename}"
    print(f"Dependency '{name}' not found at '{target_dir}'.")
    print(f"Downloading {filename} from {url} ...")
    try:
        urllib.request.urlretrieve(url, filename)
        print(f"Extracting {filename} ...")
        with zipfile.ZipFile(filename, 'r') as zip_ref:
            zip_ref.extractall(".")
        if os.path.exists(filename):
            os.remove(filename)
        if not os.path.exists(target_dir):
            check(False, f"Extraction failed: Directory '{target_dir}' was not created.")
        print(f"Successfully installed {name}.")
    except Exception as e:
        check(False, f"Failed to download or extract {name}: {str(e)}")

use_quickjs = f"{third_folder_name}/quickjs" if env.get("use_quickjs", False) else (f"{third_folder_name}/quickjs-ng" if env.get("use_quickjs_ng", False) else None)
jsc_support = "jsc" if env.get("use_jsc", False) and use_quickjs is None else None
quickjs_support = get_thirdparty_support(quickjs_src_descs, use_quickjs)

if quickjs_support is None and jsc_support is None and is_library_supported(v8_prebuilt_libs):
    download_dependency("v8", deps_v8_version, f"{third_folder_name}/v8")
    if is_library_supported(lws_prebuilt_libs):
        download_dependency("lws", deps_lws_version, f"{third_folder_name}/lws")

v8_support = validate_library_support(v8_prebuilt_libs) if quickjs_support is None and jsc_support is None else None
lws_support = validate_library_support(lws_prebuilt_libs) if v8_support is not None else None

jsb_defines = [
    CompileDefines("JSB_USE_TYPESCRIPT", 1 if env.get("use_typescript", True) else 0),
    CompileDefines("JSB_WITH_QUICKJS", 1 if quickjs_support is not None else 0),
    CompileDefines("JSB_PREFER_QUICKJS_NG", 1 if quickjs_support is not None and quickjs_support[1].path == f"{third_folder_name}/quickjs-ng" else 0),
    CompileDefines("JSB_WITH_V8", 1 if v8_support is not None else 0),
    CompileDefines("JSB_WITH_WEB", 1 if jsb_platform == "web" and quickjs_support is None else 0),
    CompileDefines("JSB_WITH_JAVASCRIPTCORE", 1 if jsc_support is not None else 0),
    CompileDefines("JSB_WITH_EDITOR_UTILITY_FUNCS", 1 if jsb_platform != "web" and env["target"] in ["editor", "template_debug"] else 0),
    CompileDefines("JSB_WITH_LWS", 1 if lws_support is not None else 0),
]

def is_defined(name):
    for t in jsb_defines:
        if t.name == name:
            return t.value == 1
    return False

zero_terminated = is_defined("JSB_WITH_QUICKJS") or is_defined("JSB_WITH_WEB") or is_defined("JSB_WITH_JAVASCRIPTCORE")

module_name = os.path.basename(os.path.dirname(os.path.abspath("jsb.h")))

print(f"compiling: {module_name}")
print(f"javascript engine: {'v8' if is_defined('JSB_WITH_V8') else 'web' if is_defined('JSB_WITH_WEB') else use_quickjs if is_defined('JSB_WITH_QUICKJS') else 'JavaScriptCore' if is_defined('JSB_WITH_JAVASCRIPTCORE') else 'none'}")
print(f"websocket lib: {'lws' if is_defined('JSB_WITH_LWS') and is_defined('JSB_WITH_V8') else 'none'}")
print(f"platform: {jsb_platform}")
print(f"arch: {jsb_arch}")
print(f"zero_terminated: {zero_terminated}")

# =============================================================================
# Build JS runtime
# =============================================================================

if not env.get("skip_js_runtime", False) and os.path.exists("package.json"):
    pnpm_command = shutil.which("pnpm")
    if pnpm_command:
        print("Installing JS dependencies...")
        subprocess.run([pnpm_command, "install"], check=True)
        print("Building JS runtime...")
        subprocess.run([pnpm_command, "build"], check=True)
    else:
        print_warning("pnpm not found, skipping JS runtime build")

# =============================================================================
# Utility functions
# =============================================================================

def write_file(filename, ostream):
    ostream.seek(0)
    content = ostream.read()
    if os.path.exists(filename):
        with open(filename, "rt", encoding="utf-8") as input:
            if input.read() == content:
                print(f"generate {filename}: no diff")
                return
    with open(filename, "wt", encoding="utf-8") as output:
        print(f"generating {filename}")
        output.write(content)

# =============================================================================
# Generate jsb.gen.h
# =============================================================================

def generate_jsb_gen_header():
    output = io.StringIO()
    output.write("// AUTO-GENERATED\n")
    output.write("#ifndef GODOTJS_GEN_H\n")
    output.write("#define GODOTJS_GEN_H\n")
    output.write("\n")
    output.write(f"#define JSB_MODULE_NAME {module_name}\n")
    output.write(f"#define jsb_initialize_module initialize_{module_name}_module\n")
    output.write(f"#define jsb_uninitialize_module uninitialize_{module_name}_module\n")
    output.write("\n")
    for t in jsb_defines:
        if t.help:
            if isinstance(t.help, list):
                for line in t.help:
                    output.write(f"// {line}\n")
            else:
                output.write(f"// {t.help}\n")
        output.write(f"#define {t.name} {t.value}\n")
    output.write("\n")
    output.write("#endif\n")
    write_file("jsb.gen.h", output)

# =============================================================================
# Generate API extensions (utility_functions_ext, core_constants, variant_builtin_ext)
# =============================================================================

def generate_api_extensions():
    """Generate API extension files from extension_api.json using external script"""
    # Use the original_cwd (project root) since cwd was changed to GodotJS/
    project_root = os.path.dirname(os.path.abspath(os.path.join(original_cwd, "SConstruct")))

    # Resolve extension_api.json path using godot-cpp's env.Dir()
    extension_dir = env.get("gdextension_dir")
    if extension_dir is None:
        extension_dir = env.Dir("godot-cpp/gdextension").srcnode().abspath

    api_version = env.get("api_version", None)

    if api_version is None:
        api_path = os.path.join(extension_dir, "extension_api.json")
    else:
        filename = "extension_api-%s.json" % api_version.replace(".", "-")
        api_path = os.path.join(extension_dir, filename)
        if not os.path.exists(api_path):
            print_warning(f"Cannot find {filename} file for api_version {api_version}")
            return

    if not os.path.exists(api_path):
        print_warning(f"extension_api.json not found at {api_path}, skipping API extensions generation")
        return

    print(f"Generating API extensions from {api_path}...")

    # Use external script to generate all API extension files
    script_path = os.path.join(project_root, "scripts", "build", "generate_api_extensions.py")
    if os.path.exists(script_path):
        import subprocess
        result = subprocess.run([sys.executable, script_path], env={**os.environ, "GODOT_EXTENSION_API_PATH": api_path})
        if result.returncode != 0:
            print_error("Failed to generate API extensions")
            Exit(1)
    else:
        print_warning(f"Generate script not found at {script_path}, skipping API extensions generation")
        return
# Generate API extensions before building
generate_api_extensions()

generate_jsb_gen_header()

# =============================================================================
# Restore cwd and configure build
# =============================================================================

os.chdir(original_cwd)

# Use absolute paths to ensure compile_commands.json has correct include paths
root_dir = Dir('#').abspath

# Enable C++20 (cross-compiler support)
# Detect MSVC vs GCC/Clang: check if use_mingw or use_llvm is set
cxx_compiler = str(env.subst('$CXX'))
cxx_flags :list = env["CXXFLAGS"]
if 'cl' in os.path.basename(cxx_compiler).lower() and not env.get('use_mingw', False):
    if '/std:c++17' in cxx_flags:
        cxx_flags.remove('/std:c++17')
    cxx_flags.append('/std:c++20')
else:
    cxx_flags :list = env["CXXFLAGS"]
    if '/-std=c++17' in cxx_flags:
        cxx_flags.remove('/-std=c++17')
    cxx_flags.append('-std=c++20')
env["CXXFLAGS"] = cxx_flags

env.Append(CPPPATH=[
    os.path.join(root_dir, "GodotJS"),
    os.path.join(root_dir, "GodotJS/compat"),
    os.path.join(root_dir, "GodotJS/internal"),
    os.path.join(root_dir, "GodotJS/weaver"),
    os.path.join(root_dir, "GodotJS/bridge"),
    os.path.join(root_dir, third_dir),
])

# Add v8 include/library path
if v8_support is not None:
    v8_basename = v8_support[1].platform_base()
    env.Append(CPPPATH=[os.path.join(root_dir, f"{third_dir}/v8/include")])
    if jsb_platform == "windows":
        env.Append(LIBS=[File(f"{third_dir}/v8/{v8_basename}/v8_monolith.lib")])
        env.Append(LINKFLAGS=["winmm.lib", "Dbghelp.lib", "advapi32.lib"])
    elif jsb_platform == "linux":
        env.Append(LIBS=[File(f"{third_dir}/v8/{v8_basename}/libv8_monolith.a")])
    elif jsb_platform == "macos":
        env.Append(LIBS=[File(f"{third_dir}/v8/{v8_basename}/libv8_monolith.a")])
    env.Append(CPPDEFINES=["V8_COMPRESS_POINTERS"])

# Add lws include/library path
if lws_support is not None:
    lws_basename = lws_support[1].platform_base()
    env.Append(CPPPATH=[os.path.join(root_dir, f"{third_dir}/lws/{lws_basename}/include")])
    if jsb_platform == "windows":
        env.Append(LIBS=[File(f"{third_dir}/lws/{lws_basename}/websockets_static.lib")])
        env.Append(LIBS=["ws2_32.lib"])
    elif jsb_platform == "linux":
        env.Append(LIBS=[File(f"{third_dir}/lws/{lws_basename}/libwebsockets.a")])

# Add all GodotJS source files
godotjs_sources = []
godotjs_sources += Glob("GodotJS/*.cpp")
godotjs_sources += Glob("GodotJS/gen/*.cpp")
godotjs_sources += Glob("GodotJS/internal/*.cpp")
godotjs_sources += Glob("GodotJS/bridge/*.cpp")
godotjs_sources += Glob("GodotJS/weaver/*.cpp")
# api_tool module: core (runtime)
godotjs_sources += Glob("GodotJS/api_tool/*.cpp")
godotjs_sources += Glob("GodotJS/api_tool/core/*.cpp")
if env["target"] in ["editor", "template_debug"]:
    godotjs_sources += Glob("GodotJS/weaver-editor/*.cpp")
    # api_tool module: editor-only (parser, generator, export plugin)
    godotjs_sources += Glob("GodotJS/api_tool/editor/*.cpp")

# Add engine-specific impl sources
if is_defined("JSB_WITH_V8"):
    godotjs_sources += Glob("GodotJS/impl/v8/*.cpp")
elif is_defined("JSB_WITH_QUICKJS"):
    godotjs_sources += Glob("GodotJS/impl/quickjs/*.cpp")
elif is_defined("JSB_WITH_WEB"):
    godotjs_sources += Glob("GodotJS/impl/web/*.cpp")
elif is_defined("JSB_WITH_JAVASCRIPTCORE"):
    godotjs_sources += Glob("GodotJS/impl/jsc/*.cpp")

# Add quickjs/quickjs-ng source files (C files) with C11 flags
quickjs_obj = []
if quickjs_support is not None:
    quickjs_dir = quickjs_support[1].path
    # Create a clone environment for C compilation with C11 flags
    env_c = env.Clone()
    if 'cl' in os.path.basename(str(env.subst('$CC'))).lower() and not env.get('use_mingw', False):
        env_c.Append(CCFLAGS=["/std:c11"])
        if "third/quickjs-ng" in quickjs_dir:
            env_c.Append(CCFLAGS=["/experimental:c11atomics"])
    else:
        env_c.Append(CCFLAGS=["-std=c11"])
    for src in quickjs_support[1].sources:
        quickjs_obj.append(env_c.Object(File(os.path.join("GodotJS", quickjs_dir, src))))

# Source files
sources = Glob("src/*.cpp")

# Combine all sources for compilation
all_sources = sources + godotjs_sources + quickjs_obj

# .dev doesn't inhibit compatibility
suffix = env['suffix'].replace(".dev", "").replace(".universal", "")
lib_filename = "{}{}{}{}".format(env.subst('$SHLIBPREFIX'), libname, suffix, env.subst('$SHLIBSUFFIX'))

library = env.SharedLibrary(
    "bin/{}/{}".format(env['platform'], lib_filename),
    source=all_sources,
)

copy = env.Install("{}/bin/{}/".format(addon_dir, env["platform"]), library)

default_args = [library, copy]
Default(*default_args)
