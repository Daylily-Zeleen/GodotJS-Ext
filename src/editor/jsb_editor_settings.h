/************************************************************************/
/*  jsb_editor_settings.h                                               */
/************************************************************************/
/*  This file is part of:                                               */
/*                                GodotJS-Ext                           */
/*              https://github.com/Daylily-Zeleen/GodotJS-Ext           */
/*                                                                      */
/*  Copyright (c) 2026-present 忘忧の (Daylily-Zeleen)                  */
/*                 - Contact: daylily-zeleen@foxmail.com                */
/*  Copyright (c) Contributors of GodotJS                               */
/*                 - <https://github.com/godotjs/GodotJS>               */
/*                                                                      */
/*  This library is free software; you can redistribute it and/or       */
/*  modify it under the terms of the GNU Lesser General Public          */
/*  License as published by the Free Software Foundation; either        */
/*  version 2.1 of the License, or (at your option) any later version.  */
/*                                                                      */
/*  This library is distributed in the hope that it will be useful,     */
/*  but WITHOUT ANY WARRANTY; without even the implied warranty of      */
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU   */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#pragma once

// Editor-side settings registration (editor extension only).
// Ownership split:
//   - the runtime extension registers godotjs_ext/runtime/** project settings
//   - the editor extension registers godotjs_ext/editor/** and
//     godotjs_ext/codegen/** project settings plus EditorSettings defaults

namespace jsb::internal {

/// Register editor-owned PROJECT settings (packaging + codegen/** keys).
/// No EditorSettings dependency; idempotent.
void init_editor_project_settings();

/// Register EditorSettings defaults (autogen paths etc.). Requires the
/// EditorSettings singleton to exist -- call once editor plugins are being
/// instantiated. Idempotent; retried later if it was not ready yet.
void init_editor_settings_defaults();

} //namespace jsb::internal
