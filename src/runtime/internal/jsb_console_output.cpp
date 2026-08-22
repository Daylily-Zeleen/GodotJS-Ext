/************************************************************************/
/*  jsb_console_output.cpp                                              */
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
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU   */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#include "jsb_console_output.h"

#include <runtime/compat/rw_lock.h>
#include <godot_cpp/templates/vector.hpp>

namespace jsb::internal {
namespace {
RWLock lock_;
Vector<IConsoleOutput *> outputs_; // TODO: LocalVector?
} //namespace

IConsoleOutput::IConsoleOutput() {
	RWLockWrite lock(lock_);
	outputs_.append(this);
}

IConsoleOutput::~IConsoleOutput() {
	remove_from_output_list();
}

void IConsoleOutput::remove_from_output_list() {
	RWLockWrite lock(lock_);
	outputs_.erase(this);
}

void IConsoleOutput::internal_write(ELogSeverity::Type p_severity, const String &p_text) {
	RWLockRead lock(lock_);
	for (IConsoleOutput *output : outputs_) {
		output->write(p_severity, p_text);
	}
}

} //namespace jsb::internal
