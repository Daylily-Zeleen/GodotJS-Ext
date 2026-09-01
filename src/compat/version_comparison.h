/************************************************************************/
/*  version_comparison.h                                                */
/************************************************************************/
/*  This file is part of:                                               */
/*                                GodotJS-Ext                           */
/*              https://github.com/Daylily-Zeleen/GodotJS-Ext           */
/*                                                                      */
/*  Copyright (c) 2026-present 忘忧の (Daylily-Zeleen)                  */
/*                 - Contact: daylily-zeleen@foxmail.com                */
/*                                                                      */
/*  This library is free software; you can redistribute it and/or       */
/*  modify it under the terms of the GNU Lesser General Public          */
/*  License as published by the Free Software Foundation; either        */
/*  version 2.1 of the License, or (at your option) any later version.  */
/*                                                                      */
/*  This library is distributed in the hope that it will be useful,     */
/*  but WITHOUT ANY WARRANTY; without even the implied warranty of      */
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU   */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#pragma once

#define VERSION_COMPARE(Current, MinExpected, ComparisonChain) (((Current) > (MinExpected)) || ((Current) == (MinExpected) && (ComparisonChain)))

#include <godot_cpp/core/version.hpp>
#define GODOT_VERSION_NEWER_THAN(major, minor, patch) GODOT_VERSION_COMPARE(GODOT_VERSION_MAJOR, major, GODOT_VERSION_COMPARE(GODOT_VERSION_MINOR, minor, GODOT_VERSION_COMPARE(GODOT_VERSION_PATCH, patch, false)))

// NOTE: 以 Godot 4.7 为基准版本
#define GODOT_4_7_OR_NEWER GODOT_VERSION_NEWER_THAN(4, 7, -1)
