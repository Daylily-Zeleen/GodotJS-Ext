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

from misc.methods import print_error, print_warning
from misc.copyright import read_copyright_text, generate_copyright_header_cpp

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
opts.Add(BoolVariable("use_node", "Build with Node.js (libnode) support. libnode embeds V8, implies use_v8 and disables v8_monolith linking", False))
opts.Add(BoolVariable("use_typescript", "Build with typescript support", True))
opts.Add(BoolVariable("skip_js_runtime", "Skip building GodotJS JavaScript runtime files", False))
opts.Add(BoolVariable("tests", "Build and run C++ unit tests", False))
opts.Add(BoolVariable("embedded_natvis", "Embed natvis files into the PDB via /NATVIS (MSVC/clang-cl linkers only). If no, merge all natvis files into a single root-level godotjs-ext.natvis instead.", True))
opts.Update(localEnv)

Help(opts.GenerateHelpText(localEnv))

env = localEnv.Clone()
env["build_profile"] = "./build_profile.json";
env.Replace(**{v: ARGUMENTS[v] for v in ("CC", "CXX") if v in ARGUMENTS})

# # Enable SCons cache to speed up builds
# scons_cache_path = os.path.join(os.getcwd(), ".scons_cache")
# if not os.path.exists(scons_cache_path):
#     os.makedirs(scons_cache_path)
# os.environ["SCONS_CACHE"] = scons_cache_path

if not (os.path.isdir("third/godot-cpp") and os.listdir("third/godot-cpp")):
    print_error("godot-cpp is not available. Initialize git submodules first.")
    sys.exit(1)

# =============================================================================
# GodotJS Build Integration
# =============================================================================

third_folder_name = "third"
third_dir = third_folder_name

# godot-cpp >= PR #2034 (require-api-version) mandates an explicit api_version.
# The bindings are generated from gdextension/extension_api-4.7.json.
env = SConscript("third/godot-cpp/SConstruct", {"env": env, "customs": customs, "api_version": "4.7"})

# godot-cpp sets SHLIBPREFIX="" only on Windows; on Linux/macOS the default is
# "lib" which produces libgodotjs-ext.*.so — but our .gdextension file expects
# the name without the lib prefix. Override to keep all platforms consistent.
env['SHLIBPREFIX'] = ''

# Source root directory
src_dir = "src"
runtime_dir = os.path.join(src_dir, "runtime")
common_dir = os.path.join(src_dir, "common")

jsb_platform = "linux" if env["platform"] == "linuxbsd" else env["platform"]
jsb_arch = env["arch"]

# Dependencies
deps_release_tag = "1.1"
deps_v8_version = "12.4.254.21"
deps_lws_version = "4.3"
deps_node_version = "24.18.0"
deps_url = "https://github.com/godotjs/GodotJS-Dependencies/releases"
deps_node_url = "https://github.com/moluopro/libnode/releases/download"

class LibraryDetails:
    def __init__(self, platform, arch, libname, delimiter, custom_platform_base = None):
        self.platform = platform
        self.arch = arch
        self.libname = libname
        self.delimiter = delimiter
        self.custom_platform_base = custom_platform_base
    def platform_base(self):
        if self.custom_platform_base is None:
            return self.platform + self.delimiter + self.arch + self.delimiter + "release"
        else:
            return self.custom_platform_base(self)

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
# libnode: Node.js as a static library (embeds V8). Prebuilt archives must be placed at
# third/libnode/<platform>_<arch>_release/libnode.{lib,a} with headers at third/libnode/include.
def _libnode_platform_base(details: LibraryDetails):
    arch_map :dict = {
        "x86_64": "x64",
        "arm64": "arm64",
    }
    return f"{details.platform}{details.delimiter}{arch_map[details.arch]}"
node_prebuilt_libs = LibraryDescriptor("libnode", [
    LibraryDetails("windows", "x86_64", "libnode.lib", "/", _libnode_platform_base),
    LibraryDetails("linux", "x86_64", "libnode.a", "/", _libnode_platform_base),
    LibraryDetails("linux", "arm64", "libnode.a", "/", _libnode_platform_base),
    LibraryDetails("macos", "x86_64", "libnode.a", "/", _libnode_platform_base),
    LibraryDetails("macos", "arm64", "libnode.a", "/", _libnode_platform_base),
    LibraryDetails("ios", "arm64", "libnode.a", "/", _libnode_platform_base),
    LibraryDetails("android", "x86_64", "libnode.a", "/", _libnode_platform_base),
    LibraryDetails("android", "arm64", "libnode.a", "/", _libnode_platform_base),
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
    with open(os.path.join(runtime_dir, "jsb.config.h"), "rt", encoding="utf-8") as f:
        regex = rf"^#define\s+{name}\s+(\d+)$"
        for line in f:
            matches = re.finditer(regex, line)
            for _, match in enumerate(matches, start=1):
                return match.group(1)
    if def_val is not None:
        return def_val
    raise ValueError(f"no {name} defined in jsb.config.h")

def dependency_is_ready(name, target_dir):
    if not os.path.isdir(target_dir):
        return False
    if name == "v8":
        for details in v8_prebuilt_libs.details:
            if details.platform == jsb_platform and details.arch == jsb_arch:
                return (
                    os.path.isfile(os.path.join(target_dir, "include", "v8.h"))
                    and os.path.isfile(
                        os.path.join(target_dir, details.platform_base(), details.libname)
                    )
                )
        return False
    if name == "node":
        for details in node_prebuilt_libs.details:
            if details.platform == jsb_platform and details.arch == jsb_arch:
                return (
                    os.path.isfile(os.path.join(target_dir, "include", "node.h"))
                    and os.path.isfile(
                        os.path.join(target_dir, details.platform_base(), details.libname)
                    )
                )
        return False
    if name == "lws":
        for details in lws_prebuilt_libs.details:
            if details.platform == jsb_platform and details.arch == jsb_arch:
                return (
                    os.path.isdir(
                        os.path.join(target_dir, details.platform_base(), "include")
                    )
                    and os.path.isfile(
                        os.path.join(target_dir, details.platform_base(), details.libname)
                    )
                )
        return False
    return bool(os.listdir(target_dir))

def remove_dependency_path(path):
    if os.path.isdir(path):
        shutil.rmtree(path, ignore_errors=True)
    elif os.path.exists(path):
        os.remove(path)

def download_dependency(name, version, target_dir, url_override=None, archive_root=None):
    if dependency_is_ready(name, target_dir):
        return
    if os.path.exists(target_dir):
        remove_dependency_path(target_dir)
    filename = f"{name}_{version}.zip"
    temporary_filename = f"{filename}.part"
    # Use override URL for libnode (different source than other dependencies)
    if url_override:
        url = url_override
    else:
        url = f"{deps_url}/download/{deps_release_tag}/{filename}"
    print(f"Dependency '{name}' not found at '{target_dir}'.")
    print(f"Downloading {filename} from {url} ...")
    try:
        download_attempts = 4
        for attempt in range(1, download_attempts + 1):
            try:
                if os.path.exists(temporary_filename):
                    os.remove(temporary_filename)
                request = urllib.request.Request(
                    url,
                    headers={"User-Agent": "GodotJS-Ext-SCons"},
                )
                with urllib.request.urlopen(request, timeout=120) as response:
                    with open(temporary_filename, "wb") as output:
                        shutil.copyfileobj(response, output)
                # Validate the archive before making it visible as the final file.
                with zipfile.ZipFile(temporary_filename, "r") as zip_ref:
                    bad_file = zip_ref.testzip()
                    if bad_file is not None:
                        raise RuntimeError(f"corrupt archive entry: {bad_file}")
                os.replace(temporary_filename, filename)
                break
            except Exception as download_error:
                if os.path.exists(temporary_filename):
                    os.remove(temporary_filename)
                if attempt == download_attempts:
                    raise
                delay = 2 ** (attempt - 1)
                print(
                    f"Download attempt {attempt}/{download_attempts} failed: "
                    f"{download_error}. Retrying in {delay}s ..."
                )
                time.sleep(delay)

        print(f"Extracting {filename} ...")
        parent_dir = os.path.dirname(target_dir)
        temporary_target_dir = f"{target_dir}.part"
        if os.path.exists(temporary_target_dir):
            shutil.rmtree(temporary_target_dir)
        os.makedirs(temporary_target_dir, exist_ok=True)
        with zipfile.ZipFile(filename, 'r') as zip_ref:
            # The archive contains a top-level directory named after the dependency
            # (e.g. "v8"), so extract into a temporary parent before installing it.
            zip_ref.extractall(temporary_target_dir)
        # Use archive_root if specified (for libnode which has "libnode/" instead of "node/")
        expected_root = archive_root if archive_root else os.path.basename(target_dir)
        extracted_target_dir = os.path.join(temporary_target_dir, expected_root)
        if not os.path.exists(extracted_target_dir):
            raise RuntimeError(f"Extraction failed: Directory '{expected_root}' was not found in archive.")
        os.makedirs(parent_dir, exist_ok=True)
        os.replace(extracted_target_dir, target_dir)
        shutil.rmtree(temporary_target_dir, ignore_errors=True)
        if os.path.exists(filename):
            os.remove(filename)
        print(f"Successfully installed {name}.")
    except Exception as e:
        if os.path.exists(temporary_filename):
            os.remove(temporary_filename)
        if os.path.exists(filename):
            os.remove(filename)
        temporary_target_dir = f"{target_dir}.part"
        if os.path.exists(temporary_target_dir):
            shutil.rmtree(temporary_target_dir, ignore_errors=True)
        check(False, f"Failed to download or extract {name}: {str(e)}")

# Node.js (libnode) support. When enabled, libnode provides the embedded V8,
# so quickjs/jsc are disabled and the standalone v8_monolith is NOT linked.
node_support = None
if env.get("use_node", False):
    if not is_library_supported(node_prebuilt_libs):
        check(False, "libnode prebuilt is not supported for this platform/arch. See plan (Phase 1) for supported targets.")
    node_url = f"{deps_node_url}/{deps_node_version}/libnode.zip"
    download_dependency("node", deps_node_version, f"{third_folder_name}/libnode", url_override=node_url)
    node_support = validate_library_support(node_prebuilt_libs)
    check(node_support is not None, "libnode prebuild lib is not found.")
    if is_library_supported(lws_prebuilt_libs) and jsb_platform != "linux":
        download_dependency("lws", deps_lws_version, f"{third_folder_name}/lws")

use_quickjs = None if node_support is not None else (f"{third_folder_name}/quickjs" if env.get("use_quickjs", False) else (f"{third_folder_name}/quickjs-ng" if env.get("use_quickjs_ng", False) else None))
jsc_support = None if node_support is not None else ("jsc" if env.get("use_jsc", False) and use_quickjs is None else None)
quickjs_support = get_thirdparty_support(quickjs_src_descs, use_quickjs)

if node_support is None and quickjs_support is None and jsc_support is None and is_library_supported(v8_prebuilt_libs):
    download_dependency("v8", deps_v8_version, f"{third_folder_name}/v8")
    # TEMPORARY (see TODO.md): linux prebuilt lws is not PIC and cannot be linked into a
    # shared library. Disable lws on linux until a PIC build is available.
    if is_library_supported(lws_prebuilt_libs) and jsb_platform != "linux":
        download_dependency("lws", deps_lws_version, f"{third_folder_name}/lws")

if node_support is not None:
    # libnode embeds V8: keep JSB_WITH_V8=1 so the bridge code compiles against the V8 API,
    # but v8_monolith is NOT linked (its symbols come from libnode).
    v8_support = v8_prebuilt_libs if is_library_supported(v8_prebuilt_libs) else None
else:
    v8_support = validate_library_support(v8_prebuilt_libs) if quickjs_support is None and jsc_support is None else None

# The prebuilt v8_monolith for iOS is built against the iphoneos SDK only; the
# simulator SDK refuses to link it ("object file built for 'iOS'"). Until a
# simulator variant of the prebuilt is published, reject this combination
# explicitly instead of failing with a cryptic linker error.
if v8_support is not None and jsb_platform == "ios" and env.get('ios_simulator', False):
    check(False, "v8 prebuilt does not support iOS Simulator (only device builds are available). "
                 "Use 'ios_simulator=no', or pick quickjs-ng/jsc for simulator builds.")

# Same class of problem on macOS: the v8 prebuilt ships per-architecture
# archives only (macos.arm64.release / macos.x86_64.release), no universal
# variant. A universal build would either silently fail to match any prebuilt
# (v8_support becomes None -> build degrades to "no engine") or fail at link
# time with missing symbols for the other architecture. Reject it explicitly.
if v8_support is not None and jsb_platform == "macos" and jsb_arch == "universal":
    check(False, "v8 prebuilt does not support macOS universal builds (only per-arch arm64/x86_64 are available). "
                 "Use arch=arm64 or arch=x86_64, or pick quickjs-ng/jsc for universal builds.")
# TEMPORARY (see TODO.md): disable lws on linux (prebuilt lib is not PIC).
# In node mode lws is disabled too (avoids cross-linking v8 symbols against libnode).
lws_support = validate_library_support(lws_prebuilt_libs) if v8_support is not None and node_support is None and jsb_platform != "linux" else None

jsb_defines = [
    CompileDefines("JSB_USE_TYPESCRIPT", 1 if env.get("use_typescript", True) else 0),
    CompileDefines("JSB_WITH_QUICKJS", 1 if quickjs_support is not None else 0),
    CompileDefines("JSB_PREFER_QUICKJS_NG", 1 if quickjs_support is not None and quickjs_support[1].path == f"{third_folder_name}/quickjs-ng" else 0),
    CompileDefines("JSB_WITH_V8", 1 if v8_support is not None else 0),
    CompileDefines("JSB_WITH_NODE", 1 if node_support is not None else 0),
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

module_name = libname.replace("-", "_")

print(f"compiling: {module_name}")
print(f"javascript engine: {'node' if is_defined('JSB_WITH_NODE') else 'v8' if is_defined('JSB_WITH_V8') else 'web' if is_defined('JSB_WITH_WEB') else use_quickjs if is_defined('JSB_WITH_QUICKJS') else 'JavaScriptCore' if is_defined('JSB_WITH_JAVASCRIPTCORE') else 'none'}")
print(f"websocket lib: {'lws' if is_defined('JSB_WITH_LWS') and is_defined('JSB_WITH_V8') else 'none'}")
print(f"platform: {jsb_platform}")
print(f"arch: {jsb_arch}")
print(f"zero_terminated: {zero_terminated}")

# =============================================================================
# Build JS runtime
# =============================================================================

if not env.get("skip_js_runtime", False) and os.path.exists(os.path.join("scripts", "package.json")):
    pnpm_command = shutil.which("pnpm")
    if pnpm_command:
        print("Installing JS dependencies...")
        subprocess.run([pnpm_command, "install"], cwd="scripts", check=True)
        print("Building JS runtime...")
        subprocess.run([pnpm_command, "build"], cwd="scripts", check=True)
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
    output.write("\n")
    output.write(generate_copyright_header_cpp("jsb.gen.h", read_copyright_text()))
    output.write("#pragma once\n")
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
    write_file(os.path.join(runtime_dir, "jsb.gen.h"), output)

# =============================================================================
# Generate jsb_project_preset.gen.cpp (embedded JS bundles)
# =============================================================================

class PresetTransformer:
    def transform(self, data):
        return data

class AMDSourceTransformer(PresetTransformer):
    def transform(self, data):
        return b"(function(define){" + data + b"\n})"

class ZeroTerminatedTransformer(PresetTransformer):
    def transform(self, data):
        return data + b"\0"

class PresetDefine:
    def __init__(self, sourcename, targetname, zero_terminated=False, transformer=None):
        self.sourcename = sourcename
        self.targetname = targetname
        self.transformers = []
        self.zero_terminated = zero_terminated
        if transformer is not None:
            self.transformers.append(transformer)
        if zero_terminated:
            self.transformers.append(ZeroTerminatedTransformer())

    def transform(self, transformer):
        self.transformers.append(transformer)
        return self

    def read_source(self):
        with open(self.sourcename, "rb") as input:
            data = input.read()
            for transformer in self.transformers:
                data = transformer.transform(data)
            return data

def remove_file(filename):
    if os.path.exists(filename):
        print(f"deleting deprecated file {filename}")
        os.remove(filename)

def try_compress(bytes):
    result = bytes
    if len(bytes) > 512:
        result = zlib.compress(bytes, zlib.Z_BEST_SPEED)
    return result if len(result) < len(bytes) else bytes

def generate_method_code(output, methodname, indent, preset_defines):
    output.write(f"jsb::internal::PresetSource GodotJSProjectPreset::{methodname}(const String& p_filename)\n")
    output.write("{\n")
    output.write(indent + "static const unsigned char data[] = {\n")
    generated_sources = {}
    cursor = 0
    for preset_define in preset_defines:
        sourcename = preset_define.sourcename
        targetname = preset_define.targetname
        if len(targetname) == 0:
            targetname = os.path.basename(sourcename)
        newline = 0
        mtime = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(os.path.getmtime(sourcename)))

        bytes = preset_define.read_source()
        original_length = len(bytes)
        compressed_bytes = try_compress(bytes)
        if compressed_bytes != bytes:
            bytes = compressed_bytes
            output.write(indent + indent + f"// target: {targetname} length: {len(bytes)} ({original_length}) modified: {mtime}\n")
            output.write(indent + indent)
            generated_sources[targetname] = (cursor, len(bytes), original_length, preset_define.zero_terminated)
        else:
            output.write(indent + indent + f"// target: {targetname} length: {len(bytes)} modified: {mtime}\n")
            output.write(indent + indent)
            generated_sources[targetname] = (cursor, len(bytes), 0, preset_define.zero_terminated)

        cursor += len(bytes)
        for byte in bytes:
            output.write(f"0x{byte:02x}, ")
            newline += 1
            if newline >= 64:
                newline = 0
                output.write("\n")
                output.write(indent + indent)

        if newline != 0:
            newline = 0
            output.write("\n")
    output.write("\n")
    output.write(indent + "};\n")
    for targetname in generated_sources:
        start = generated_sources[targetname][0]
        size = generated_sources[targetname][1]
        osize = generated_sources[targetname][2]
        zero_terminated = "true" if generated_sources[targetname][3] else "false"
        output.write(indent + f'if (p_filename == "{targetname}") {{ return jsb::internal::PresetSource(p_filename, (const char *) data+{start}, {size}, {osize}, {zero_terminated}); }}\n')
    output.write(indent + "return jsb::internal::PresetSource();\n")
    output.write("}\n")

def generate_code(rt_preset_defines, ed_preset_defines):
    indent = "    "

    # delete obsolete files
    remove_file(os.path.join(runtime_dir, "weaver-editor", "jsb_project_preset.cpp"))
    remove_file(os.path.join(runtime_dir, "jsb_project_preset.cpp"))

    JSB_BUNDLE_VERSION = read_macro_value("JSB_BUNDLE_VERSION")
    version_assert = f"static_assert({JSB_BUNDLE_VERSION} == JSB_BUNDLE_VERSION, \"obsolete preset data found, please regenerate project sources with scons\");\n"

    # runtime side: embedded runtime bundles only
    rt_output = io.StringIO()
    rt_output.write("// AUTO-GENERATED\n")
    rt_output.write("\n")
    rt_output.write(generate_copyright_header_cpp("jsb_project_preset.gen.cpp", read_copyright_text()))
    rt_output.write("\n")
    rt_output.write("#include <common/jsb_project_preset.h>\n")
    rt_output.write("#include \"jsb.config.h\"\n")
    rt_output.write(version_assert)
    generate_method_code(rt_output, "get_source_rt", indent, rt_preset_defines)
    write_file(os.path.join(runtime_dir, "jsb_project_preset.gen.cpp"), rt_output)

    # editor side: embedded editor bundles + project scaffolding templates.
    # Consumed exclusively by the editor extension (which always builds with TOOLS_ENABLED).
    ed_output = io.StringIO()
    ed_output.write("// AUTO-GENERATED\n")
    ed_output.write("\n")
    ed_output.write(generate_copyright_header_cpp("jsb_editor_preset.gen.cpp", read_copyright_text()))
    ed_output.write("\n")
    ed_output.write("#include <common/jsb_project_preset.h>\n")
    ed_output.write("#include \"jsb.config.h\"\n")
    ed_output.write(version_assert)
    ed_output.write("#ifdef TOOLS_ENABLED\n")
    generate_method_code(ed_output, "get_source_ed", indent, ed_preset_defines)
    ed_output.write("#endif\n")
    write_file(os.path.join(src_dir, "editor", "weaver-editor", "jsb_editor_preset.gen.cpp"), ed_output)

generate_code([
    PresetDefine("scripts/out/jsb.runtime.bundle.js", "", zero_terminated, AMDSourceTransformer()),
], [
    PresetDefine("scripts/out/jsb.editor.bundle.js", "", zero_terminated, AMDSourceTransformer()),
    PresetDefine("scripts/typings/godot.minimal.d.ts", ""),
    PresetDefine("scripts/typings/godot.mix.d.ts", ""),
    PresetDefine("scripts/typings/godot.worker.d.ts", ""),
    PresetDefine("scripts/typings/godot.shadowRealm.d.ts", ""),
    PresetDefine("scripts/typings/type.extension.d.ts", ""),
    PresetDefine("scripts/out/jsb.runtime.bundle.d.ts", ""),
    PresetDefine("scripts/out/jsb.runtime.bundle.js.map", ""),
    PresetDefine("scripts/out/jsb.editor.bundle.d.ts", ""),
    PresetDefine("scripts/out/jsb.editor.bundle.js.map", ""),
    PresetDefine("scripts/presets/package.json.txt", "package.json"),
    PresetDefine("scripts/presets/tsconfig.json.txt", "tsconfig.json"),
    PresetDefine("scripts/presets/jsconfig.json.txt", "jsconfig.json"),
    PresetDefine("scripts/presets/gdignore.txt", ".gdignore"),
])

# =============================================================================
# Generate templates.gen.h
# =============================================================================

templates_script = os.path.join("misc", "build", "generate_templates_header.py")
templates_output = os.path.join(src_dir, "editor", "weaver-editor", "templates", "templates.gen.h")
if os.path.exists(templates_script):
    subprocess.run([sys.executable, templates_script, os.path.join(src_dir, "editor", "weaver-editor", "templates"), templates_output], check=True)

generate_jsb_gen_header()

# Use absolute paths to ensure compile_commands.json has correct include paths
root_dir = Dir('#').abspath

# Enable C++20 (cross-compiler support)
# Detect MSVC vs GCC/Clang: check if use_mingw or use_llvm is set
cxx_compiler = str(env.subst('$CXX'))
cxx_flags :list = env["CXXFLAGS"]
cxx_compiler_base = os.path.basename(cxx_compiler).lower()
if cxx_compiler_base in ("cl", "cl.exe", "clang-cl") and not env.get('use_mingw', False):
    if '/std:c++17' in cxx_flags:
        cxx_flags.remove('/std:c++17')
    cxx_flags.append('/std:c++20')
else:
    cxx_flags :list = env["CXXFLAGS"]
    if '/-std=c++17' in cxx_flags:
        cxx_flags.remove('/-std=c++17')
    cxx_flags.append('-std=c++20')
env["CXXFLAGS"] = cxx_flags

# =============================================================================
# Natvis handling
#
# 1. MSVC / clang-cl (link.exe / lld-link) support the /NATVIS: option which
#    embeds a natvis file into the PDB. This is the preferred path (default:
#    embedded_natvis=yes) because debuggers then pick up the visualizers
#    automatically, with zero launch.json configuration.
# 2. All other toolchains have no such linker option, and passing /NATVIS:
#    would fail the build. For those (or when embedded_natvis=no is given),
#    merge all natvis files into a single root-level godotjs-ext.natvis via
#    misc/build/merge_natvis.py. Debuggers can load it through the
#    visualizerFile attribute or the _NT_NATVIS_FILE environment variable.
# =============================================================================

natvis_sources = [
    os.path.join(root_dir, "third", "godot-cpp", "natvis", "godot-cpp.natvis"),
    os.path.join(root_dir, "src", "runtime", "jsb.natvis"),
    os.path.join(root_dir, "src", "runtime", "impl", "quickjs", "jsb.quickjs.natvis"),
]
merge_script = os.path.join(root_dir, "misc", "build", "merge_natvis.py")
merged_natvis = os.path.join(root_dir, "godotjs-ext.natvis")

is_msvc_linker = env.get("is_msvc", False)
if env.get("embedded_natvis", True) and is_msvc_linker:
    # MSVC / clang-cl: embed each natvis file into the PDB.
    for natvis_path in natvis_sources:
        env.Append(LINKFLAGS=[f"/NATVIS:{natvis_path}"])
    print("natvis: embedding into PDB via /NATVIS:")
    for natvis_path in natvis_sources:
        print(f"  {natvis_path}")
else:
    # embedded_natvis=no, or a linker without /NATVIS support: merge to a
    # single root-level file for debugger-side loading.
    reason = "embedded_natvis=no" if not env.get("embedded_natvis", True) else "linker without /NATVIS support"
    print(f"natvis: {reason}, merging into {merged_natvis}")
    subprocess.run([sys.executable, merge_script, merged_natvis, *natvis_sources], check=True)

env.Append(CPPPATH=[
    os.path.join(root_dir, src_dir),
    os.path.join(root_dir, common_dir),
    os.path.join(root_dir, runtime_dir),
    os.path.join(root_dir, common_dir, "compat"),
    os.path.join(root_dir, common_dir, "impl", "shared"),
    os.path.join(root_dir, runtime_dir, "compat"),
    os.path.join(root_dir, runtime_dir, "internal"),
    os.path.join(root_dir, common_dir, "internal"),
    os.path.join(root_dir, runtime_dir, "weaver"),
    os.path.join(root_dir, runtime_dir, "bridge"),
    os.path.join(root_dir, runtime_dir, "js_type_extension"),
    os.path.join(root_dir, third_dir),
])

# Add editor include path for editor target
if env["target"] == "editor":
    editor_dir = os.path.join(src_dir, "editor")
    env.Append(CPPPATH=[os.path.join(root_dir, editor_dir)])

# Add v8 include/library path
# (skipped in node mode: libnode bundles the v8 headers and provides the v8 symbols)
if v8_support is not None and node_support is None:
    v8_basename = v8_support[1].platform_base()
    env.Append(CPPPATH=[os.path.join(root_dir, third_dir, "v8", "include")])
    if jsb_platform == "windows":
        env.Append(LIBS=[File(os.path.join(third_dir, "v8", v8_basename, "v8_monolith.lib"))])
        env.Append(LINKFLAGS=["winmm.lib", "Dbghelp.lib", "advapi32.lib"])
    elif jsb_platform in ("linux", "macos", "ios"):
        env.Append(LIBS=[File(os.path.join(third_dir, "v8", v8_basename, "libv8_monolith.a"))])
    env.Append(CPPDEFINES=["V8_COMPRESS_POINTERS"])

# Add node (libnode) include/library path
# libnode is a static archive which must be fully linked (--whole-archive / -force_load / WHOLEARCHIVE)
# so that the N-API symbols it defines are preserved and exported by the main DLL.
if node_support is not None:
    node_basename = node_support[1].platform_base()
    node_lib_path = os.path.join(root_dir, third_dir, "libnode", node_basename, "libnode.lib" if jsb_platform == "windows" else "libnode.a")
    env.Append(CPPPATH=[os.path.join(root_dir, third_dir, "libnode", "include")])
    # NOTE: do NOT define V8_COMPRESS_POINTERS here. The moluopro/libnode prebuilt
    # (Node 24.x) is built WITHOUT pointer compression, and V8::Initialize() aborts
    # with "Embedder-vs-V8 build configuration mismatch" when the embedder side
    # compiles with V8_COMPRESS_POINTERS defined but the V8 library has it disabled.
    if jsb_platform == "windows":
        env.Append(LIBS=[File(node_lib_path)])
        env.Append(LINKFLAGS=[f"/WHOLEARCHIVE:{os.path.abspath(node_lib_path)}"])
        env.Append(LINKFLAGS=["Dbghelp.lib", "Psapi.lib", "Winmm.lib", "Ws2_32.lib", "Advapi32.lib", "Crypt32.lib", "Ole32.lib", "Iphlpapi.lib", "Shell32.lib", "User32.lib", "Userenv.lib", "Uuid.lib"])
    elif jsb_platform == "linux":
        env.Append(LIBS=[File(node_lib_path)])
        env.Append(LINKFLAGS=["-Wl,--whole-archive", os.path.abspath(node_lib_path), "-Wl,--no-whole-archive"])
        env.Append(LIBS=["dl", "pthread", "rt"])
    elif jsb_platform == "macos":
        env.Append(LIBS=[File(node_lib_path)])
        env.Append(LINKFLAGS=["-Wl,-force_load", os.path.abspath(node_lib_path)])
        env.Append(LINKFLAGS=["-framework", "CoreFoundation", "-framework", "Security"])
    elif jsb_platform == "android":
        env.Append(LIBS=[File(node_lib_path)])
        env.Append(LINKFLAGS=["-Wl,--whole-archive", os.path.abspath(node_lib_path), "-Wl,--no-whole-archive"])
        env.Append(LIBS=["log", "dl"])
    elif jsb_platform == "ios":
        env.Append(LIBS=[File(node_lib_path)])
        env.Append(LINKFLAGS=["-Wl,-force_load", os.path.abspath(node_lib_path)])
        env.Append(LINKFLAGS=["-framework", "Foundation"])

# JavaScriptCore is a system framework on macOS and iOS; link it for JSC builds
if jsc_support is not None and jsb_platform in ("macos", "ios"):
    env.Append(LINKFLAGS=["-framework", "JavaScriptCore"])

# Add lws include/library path
if lws_support is not None:
    lws_basename = lws_support[1].platform_base()
    env.Append(CPPPATH=[os.path.join(root_dir, f"{third_dir}/lws/{lws_basename}/include")])
    if jsb_platform == "windows":
        env.Append(LIBS=[File(os.path.join(third_dir, "lws", lws_basename, "websockets_static.lib"))])
        env.Append(LIBS=["ws2_32.lib"])
    elif jsb_platform == "linux":
        # NOTE: lws is disabled on linux (see TODO.md) because the prebuilt lib is not PIC.
        pass
    elif jsb_platform == "macos":
        env.Append(LIBS=[File(f"{third_dir}/lws/{lws_basename}/libwebsockets.a")])

# Add GodotJS source files, split into the runtime and editor extension targets.
#
# Ownership rules (TASK_STATUS.md ch.14):
#   runtime target: src/runtime/** + api_tool core (store/loader/payload/types)
#   editor target:  src/editor/** + api_tool/editor orchestration
# Shared headers (compat/, internal/ pure tools, impl/shared data structs,
# testing/) are compiled into BOTH targets.

runtime_sources = []
runtime_sources += Glob(os.path.join(runtime_dir, "*.cpp"))
runtime_sources += Glob(os.path.join(common_dir, "compat", "*.cpp"))
runtime_sources += Glob(os.path.join(common_dir, "internal", "*.cpp"))
runtime_sources += Glob(os.path.join(runtime_dir, "internal", "*.cpp"))
runtime_sources += Glob(os.path.join(runtime_dir, "bridge", "*.cpp"))
runtime_sources += Glob(os.path.join(runtime_dir, "weaver", "*.cpp"))
runtime_sources += Glob(os.path.join(runtime_dir, "js_type_extension", "*.cpp"))
# api_tool module: core store/loader/payload/types
runtime_sources += Glob(os.path.join(src_dir, "api_tool", "*.cpp"))
runtime_sources += Glob(os.path.join(src_dir, "api_tool", "core", "*.cpp"))

editor_env = env.Clone()
editor_sources = []
editor_dir = os.path.join(src_dir, "editor")
editor_sources += Glob(os.path.join(editor_dir, "*.cpp"))
editor_sources += Glob(os.path.join(editor_dir, "weaver-editor", "*.cpp"))
# api_tool editor-side orchestration (parser/writer/generator/docs queries)
editor_sources += Glob(os.path.join(src_dir, "api_tool", "editor", "*.cpp"))
editor_sources += Glob(os.path.join(editor_dir, "codegen", "*.cpp"))

# Shared runtime utilities referenced by the editor side: stateless tools,
# data structs, api_tool core (each side holds its own read-only store copy),
# and string names. Compiled into BOTH targets.
shared_utility_sources = [
    os.path.join(common_dir, "internal", "jsb_settings.cpp"),
    os.path.join(common_dir, "internal", "jsb_class_visibility.cpp"),
    os.path.join(common_dir, "internal", "jsb_console_output.cpp"),
    os.path.join(common_dir, "internal", "jsb_logger.cpp"),
    os.path.join(common_dir, "internal", "jsb_naming_util.cpp"),
    os.path.join(common_dir, "internal", "jsb_path_util.cpp"),
    os.path.join(common_dir, "internal", "jsb_process.cpp"),
    os.path.join(common_dir, "internal", "jsb_thread_util.cpp"),
    os.path.join(common_dir, "internal", "jsb_string_names.cpp"),
    os.path.join(common_dir, "internal", "jsb_variant_util.cpp"),
    # api_tool core: both sides hold their own read-only store copy
    os.path.join(src_dir, "api_tool", "api_tool.cpp"),
    os.path.join(src_dir, "api_tool", "api_tool_types.cpp"),
    os.path.join(src_dir, "api_tool", "core", "api_tool_loader.cpp"),
    os.path.join(src_dir, "api_tool", "core", "api_tool_store.cpp"),
]


# plugin.get_preset_source consults both rt and ed preset sets; keep behavior by
# compiling the runtime preset data into the editor target as well.
# compat implementations used by the editor side (_GLOBAL_DEF/_EDITOR_DEF wrappers)
for f in Glob(os.path.join(common_dir, "compat", "*.cpp")):
    editor_sources.append(editor_env.SharedObject(
        target=os.path.join("editor_shared", os.path.basename(str(f)).replace(".cpp", ".obj")),
        source=f))


# Engine-specific impl sources (NOTE: node mode implies JSB_WITH_V8 (libnode
# embeds V8), so the node branch MUST be checked before the v8 branch).
impl_sources = []
if is_defined("JSB_WITH_NODE"):
    impl_sources += Glob(os.path.join(runtime_dir, "impl", "node", "*.cpp"))
elif is_defined("JSB_WITH_V8"):
    impl_sources += Glob(os.path.join(runtime_dir, "impl", "v8", "*.cpp"))
elif is_defined("JSB_WITH_QUICKJS"):
    impl_sources += Glob(os.path.join(runtime_dir, "impl", "quickjs", "*.cpp"))
elif is_defined("JSB_WITH_WEB"):
    impl_sources += Glob(os.path.join(runtime_dir, "impl", "web", "*.cpp"))
elif is_defined("JSB_WITH_JAVASCRIPTCORE"):
    impl_sources += Glob(os.path.join(runtime_dir, "impl", "jsc", "*.cpp"))
runtime_sources += impl_sources

if env.get("tests", False):
    env.Append(CPPDEFINES=["JSB_TESTS_ENABLED"])
    runtime_sources += Glob(os.path.join(runtime_dir, "tests", "*.cpp"))
    editor_sources += Glob(os.path.join(editor_dir, "tests", "*.cpp"))

godotjs_sources = runtime_sources + editor_sources

# Add quickjs/quickjs-ng source files (C files) with C11 flags
quickjs_obj = []
if quickjs_support is not None:
    quickjs_dir = quickjs_support[1].path
    # Create a clone environment for C compilation with C11 flags
    env_c = env.Clone()
    cc_compiler_base = os.path.basename(str(env.subst('$CC'))).lower()
    if cc_compiler_base in ("cl", "cl.exe", "clang-cl") and not env.get('use_mingw', False):
        env_c.Append(CCFLAGS=["/std:c11"])
        if "third/quickjs-ng" in quickjs_dir:
            env_c.Append(CCFLAGS=["/experimental:c11atomics"])
    else:
        env_c.Append(CCFLAGS=["-std=gnu11"])
    # NOTE: use SharedObject (not Object) so the C objects are marked as PIC and
    # can be linked into the shared library. SCons rejects plain static .o files
    # when linking a shared library on linux/macos ("is static and is not
    # compatible with shared target").
    for src in quickjs_support[1].sources:
        quickjs_obj.append(env_c.SharedObject(File(os.path.join(quickjs_dir, src))))

if jsb_platform == "windows":
    # /FS: parallel CL.EXE instances share the target PDB
    env.Append(CCFLAGS=["/FS"])

# Combine all sources for compilation
all_sources = godotjs_sources + quickjs_obj

# .dev doesn't inhibit compatibility
# Preserve .universal suffix for macOS/iOS to distinguish architectures
# Add .simulator suffix for iOS simulator builds
if env['platform'] in ['macos', 'ios']:
    # macOS/iOS: preserve .universal suffix
    suffix = env['suffix'].replace(".dev", "")
    # iOS simulator: add .simulator suffix
    if env['platform'] == 'ios' and env.get('ios_simulator', False):
        suffix = suffix.replace(".universal", ".universal.simulator")
else:
    # Other platforms: remove .universal suffix
    suffix = env['suffix'].replace(".dev", "").replace(".universal", "")
lib_filename = "{}{}{}{}".format(env.subst('$SHLIBPREFIX'), libname, suffix, env.subst('$SHLIBSUFFIX'))

def make_target_env(base_env, pdb_name):
    target_env = base_env.Clone()
    if jsb_platform == "windows":
        # godot-cpp sets LINKFLAGS=/WX; a missing PDB would trip LNK4099 ->
        # LNK1218. Give every intermediate a real PDB (unique per target so the
        # parallel CL.EXE instances never contend) and keep incremental off.
        target_env.Append(CCFLAGS=["/Zi", "/FS", f"/Fd{pdb_name}.pdb"],
                          LINKFLAGS=[f"/PDB:{pdb_name}.pdb", "/DEBUG:FULL", "/INCREMENTAL:NO", "/IGNORE:4099"])
    return target_env

library = make_target_env(env, "bin/windows/godotjs-ext").SharedLibrary(
    "bin/{}/{}".format(env['platform'], lib_filename),
    source=runtime_sources + quickjs_obj,
)

# Shared utilities are compiled with the EDITOR env into distinct objects
# (same sources as some runtime-target objects; separate object paths avoid
# SCons target conflicts between the two environments).
editor_env = env.Clone()

shared_objs = []
for f in shared_utility_sources:
    # compile under a dedicated variant dir to avoid clashing with the runtime
    # target's objects of the same basename
    obj = editor_env.SharedObject(target=os.path.join("editor_shared", os.path.basename(f).replace(".cpp", ".obj")), source=File(f))
    shared_objs.append(obj)

editor_libname = "{}{}-editor{}{}".format(env.subst('$SHLIBPREFIX'), libname, suffix, env.subst('$SHLIBSUFFIX'))
preset_gen_obj = editor_env.SharedObject(
    target=os.path.join("editor_shared", "jsb_project_preset.gen"),
    source=File(os.path.join(runtime_dir, "jsb_project_preset.gen.cpp")))

editor_libname = "{}{}-editor{}{}".format(env.subst('$SHLIBPREFIX'), libname, suffix, env.subst('$SHLIBSUFFIX'))
editor_library = make_target_env(env, "bin/windows/godotjs-ext-editor").SharedLibrary(
    "bin/{}/{}".format(env['platform'], editor_libname),
    source=editor_sources + shared_objs + [preset_gen_obj],
)

copy = env.Install("{}/bin/{}/".format(addon_dir, env["platform"]), library)
editor_copy = env.Install("{}/bin/{}/".format(addon_dir, env["platform"]), editor_library)

default_args = [library, copy, editor_library, editor_copy]

# iOS: generate xcframework from device and simulator builds
if jsb_platform == "ios" and env.get('ios_simulator', False):
    # The xcframework is generated as a post-build step of the SIMULATOR build
    # ('ios_simulator=yes'). At this point the simulator dylib is part of the
    # current build graph, so it can be safely declared as a source. The device
    # dylib however is produced by a SEPARATE scons invocation
    # ('ios_simulator=no'), so it must NOT be declared as a source (SCons
    # requires all sources to exist at parse time); the action checks for it at
    # execution time instead.
    #
    # AlwaysBuild() forces regeneration on every 'ios_simulator=yes' run, so a
    # freshly rebuilt device dylib is always picked up without manually
    # deleting the xcframework.
    target = env['target']

    xcframework_name = "{}.ios.{}.xcframework".format(libname, target)
    xcframework_path = os.path.join("bin", "ios", xcframework_name)

    # Note: the dylib suffix embeds the arch part following godot-cpp's naming:
    # ".universal" for arch=universal, ".{arch}" otherwise; simulator builds append ".simulator".
    # The SHLIBPREFIX ("lib" on macOS/iOS) must be included to match the actual output filename.
    shlibprefix = env.subst('$SHLIBPREFIX')
    device_lib_path = os.path.join("bin", "ios", "{}{}.ios.{}{}.dylib".format(shlibprefix, libname, target, ".universal" if env["arch"] == "universal" else "." + env["arch"]))
    simulator_lib_path = os.path.join("bin", "ios", "{}{}.ios.{}{}.simulator.dylib".format(shlibprefix, libname, target, ".universal" if env["arch"] == "universal" else "." + env["arch"]))

    # NOTE: SCons invokes the action with target=/source=/env= keyword
    # arguments, so the parameter names must match exactly.
    def _generate_xcframework(target, source, env):
        if not os.path.exists(device_lib_path):
            print_warning(f"Skip generating xcframework: device library not found at '{device_lib_path}'. "
                          f"Run 'scons platform=ios ... ios_simulator=no' first.")
            return None

        cmd = [
            "xcodebuild", "-create-xcframework",
            "-library", device_lib_path,
            "-library", simulator_lib_path,
            "-output", xcframework_path
        ]
        print("Generating xcframework:", " ".join(cmd))
        subprocess.check_call(cmd)
        return None

    xcframework_cmd = env.Command(
        xcframework_path,
        # declare the simulator dylib node as source so SCons links it before
        # running the xcframework action (the action itself uses the plain
        # path strings above)
        [library],
        _generate_xcframework
    )
    env.AlwaysBuild(xcframework_cmd)

    # Copy xcframework to addon directory
    xcframework_copy = env.Install("{}/bin/ios/".format(addon_dir), xcframework_cmd)
    default_args += [xcframework_cmd, xcframework_copy]

# Windows: generate a node.dll Node-API forwarder so native .node addons can resolve
# napi_* symbols (they import them from a module literally named 'node.dll').
# The forwarder forwards every napi_*/node_api_*/node_module_register export to the
# main DLL, which exports them because libnode's object files carry dllexport marks.
if node_support is not None and jsb_platform == "windows":
    node_dll_path = os.path.join(root_dir, "bin", jsb_platform, "node.dll")
    node_def_path = os.path.join(root_dir, "bin", jsb_platform, "node.def")
    node_shim_script = os.path.join(root_dir, "misc", "build", "generate_node_shim.py")
    node_include_dir = os.path.join(root_dir, third_dir, "libnode", "include")
    shim_arch = "ARM64" if jsb_arch == "arm64" else "X64"

    def _build_node_shim(target, source, env):
        # 1) extract N-API symbols from libnode headers and write node.def
        subprocess.check_call([
            sys.executable, node_shim_script,
            node_include_dir, node_def_path, lib_filename])
        # 2) link the forwarding node.dll (no implementation, just exports).
        # Resolve `link` through the SCons-configured toolchain PATH: calling the
        # bare name via CreateProcess can fail with ERROR_FILE_NOT_FOUND even when
        # the directory is on PATH, so we locate the exe explicitly first.
        link_env = os.environ.copy()
        link_env["PATH"] = str(env["ENV"]["PATH"])
        link_cmd = str(env.subst("$LINK"))
        link_exe = shutil.which(link_cmd, path=link_env["PATH"])
        if not link_exe:
            raise RuntimeError(f"[node-shim] cannot locate '{link_cmd}' in the MSVC toolchain PATH")
        subprocess.check_call([
            link_exe, "/nologo", "/dll", "/noentry", "/ignore:4001",
            f"/machine:{shim_arch}", f"/def:{node_def_path}",
            f"/out:{node_dll_path}"], env=link_env)
        return None

    node_shim = env.Command(node_dll_path, library, _build_node_shim)
    node_shim_copy = env.Install("{}/bin/{}/".format(addon_dir, env["platform"]), node_shim)
    default_args += [node_shim, node_shim_copy]

# Build the standalone node helper executable used as the execPath for
# child_process.fork() probes (native addon host detection). The embedded
# runtime reports Godot as process.execPath, so forking a probe must start
# this helper instead. The helper forwards to `godotjs_node_probe_main`
# exported by the main DLL (which statically links libnode), so it only
# links against the main DLL and keeps its own size tiny.
if node_support is not None and jsb_platform in ("windows", "linux", "macos"):
    helper_main = os.path.join(runtime_dir, "node_helper", "jsb_node_host_main.cpp")
    helper_name = "godotjs-ext" + (".exe" if jsb_platform == "windows" else "")
    helper_dir = os.path.join("bin", env["platform"])
    # windows: link the main DLL's import library; posix: link the shared
    # library directly (the helper resolves it at runtime through rpath).
    helper_link_target = lib_filename if jsb_platform != "windows" else os.path.splitext(lib_filename)[0] + ".lib"
    helper_env = env.Clone()
    # The helper must NOT inherit the main environment's static libs and link
    # flags: it would statically link the whole libnode (via /WHOLEARCHIVE) and
    # produce a ~116MB exe instead of a tiny dynamic forwarder. Reset LIBS to
    # only the main DLL's import library, and strip whole-archive/force_load.
    helper_env['LIBS'] = [File(os.path.join(helper_dir, helper_link_target))]
    helper_env['LINKFLAGS'] = [
        flag for flag in helper_env['LINKFLAGS']
        if '/WHOLEARCHIVE' not in str(flag).upper()
        and '--whole-archive' not in str(flag)
        and '-Wl,--whole-archive' not in str(flag)
        and '-force_load' not in str(flag)
        and '-Wl,-force_load' not in str(flag)
    ]
    if jsb_platform == "linux":
        # $ORIGIN must be escaped for scons ($ -> $$). --no-as-needed keeps the
        # dependency recorded even though the helper only calls one symbol.
        helper_env.Append(LINKFLAGS=["-Wl,-rpath,$$ORIGIN", "-Wl,--no-as-needed"])
    elif jsb_platform == "macos":
        helper_env.Append(LINKFLAGS=["-Wl,-rpath,@loader_path"])
    helper = helper_env.Program(os.path.join(helper_dir, helper_name), [helper_main])
    helper_copy = env.Install("{}/bin/{}/".format(addon_dir, env["platform"]), helper)
    if jsb_platform in ("linux", "macos"):
        def _make_helper_executable(target, source, env):
            import stat as _stat
            for t in target:
                os.chmod(str(t), os.stat(str(t)).st_mode | _stat.S_IXUSR | _stat.S_IXGRP | _stat.S_IXOTH)
            return None
        env.AddPostAction(helper_copy, _make_helper_executable)
    default_args += [helper, helper_copy]

Default(*default_args)
