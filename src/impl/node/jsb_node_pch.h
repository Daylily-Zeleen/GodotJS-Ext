#pragma once

// Node.js (libnode) engine layer precompiled header.
// libnode bundles the V8 headers, so `node.h` transitively pulls in <v8.h>.
//
// NOTE: <uv.h> is deliberately NOT included here. On Windows uv/win.h pulls in
// <windows.h> whose winnt.h defines SEVERITY_WARNING/SEVERITY_ERROR macros that
// clash with godot-cpp's generated enums (e.g. EditorToaster::Severity) whenever
// a translation unit includes both the node headers and godot-cpp headers.
// Translation units that need libuv APIs include <uv.h> themselves (their own
// macro namespace pollution is harmless). node.h already forward-declares
// `struct uv_loop_s` for the types used in headers.
//
// V8 13's v8.h no longer includes v8-forward.h before v8-array-buffer.h, and the
// unqualified `friend class TypedArray;` inside v8::ArrayBuffer would then bind to
// `godot::TypedArray` (declared by godot-cpp, a class template) when godot-cpp
// headers were included first, causing error C2990. Pre-including v8-forward.h
// makes v8::TypedArray visible so the friend declaration binds to it. The V8
// headers have include guards, so this is harmless for the other translation units.
#include <v8-forward.h>

#include <node.h>
#include <node_api.h>

// override from Windows SDK, clashes with Object enum
#ifdef WINDOWS_ENABLED
// libuv (uv.h) defines CONNECT_DEFERRED on Windows which collides with
// Godot's socket constants (kept here in case a TU includes <uv.h>).
#	undef CONNECT_DEFERRED
#endif

// JSB_ISOLATE_SCOPE: expands to v8::Locker + Isolate::Scope in node mode
// (multi-threaded node::MultiIsolatePlatform requires the locker on every V8 entry).
#include "../shared/jsb_isolate_scope.h"

#include "../../internal/jsb_logger.h"
#include "../../internal/jsb_macros.h"

#include "../shared/jsb_custom_field.h"
