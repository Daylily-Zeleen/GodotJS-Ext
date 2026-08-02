#pragma once

#define VERSION_COMPARE(Current, MinExpected, ComparisonChain) (((Current) > (MinExpected)) || ((Current) == (MinExpected) && (ComparisonChain)))

#include <godot_cpp/core/version.hpp>
#define GODOT_VERSION_NEWER_THAN(major, minor, patch) GODOT_VERSION_COMPARE(GODOT_VERSION_MAJOR, major, GODOT_VERSION_COMPARE(GODOT_VERSION_MINOR, minor, GODOT_VERSION_COMPARE(GODOT_VERSION_PATCH, patch, false)))

// NOTE: 以 Godot 4.7 为基准版本
#define GODOT_4_7_OR_NEWER GODOT_VERSION_NEWER_THAN(4, 7, -1)
