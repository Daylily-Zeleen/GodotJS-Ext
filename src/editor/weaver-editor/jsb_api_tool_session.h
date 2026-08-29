/************************************************************************/
/*  jsb_api_tool_session.h                                              */
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

#include "api_tool/api_tool.h"

namespace jsb::editor {

// Scoped guarantee that the api_tool store is available for editor-side work
// (codegen, doc queries, descriptor generation).
//
// Single-library era: initialize() is idempotent and cheap when already loaded;
// the destructor intentionally does NOT finalize because the runtime side keeps
// reading the same store until engine shutdown.
//
// Two-library era (S6): each extension will hold its own store copy; the
// destructor will then release THIS side's copy. The call sites are already
// scoped so that switch is a one-line change here.
class ApiToolSession {
public:
	explicit ApiToolSession(bool p_require_data = true) {
		if (p_require_data && !api_tool::has_generated_data()) {
			valid_ = false;
			return;
		}
		// Idempotent; no-op when already initialized.
		api_tool::initialize();
		valid_ = true;
	}

	~ApiToolSession() {
		// Release this extension's own store copy. The runtime holds a separate
		// copy for the whole process lifetime, so finalizing here is safe.
		api_tool::finalize();
	}

	bool is_valid() const { return valid_; }

private:
	bool valid_ = false;
};

} //namespace jsb::editor
