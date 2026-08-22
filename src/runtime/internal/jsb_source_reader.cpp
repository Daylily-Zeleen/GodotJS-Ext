/************************************************************************/
/*  jsb_source_reader.cpp                                               */
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

#include "jsb_source_reader.h"
#include "jsb_logger.h"
#include "jsb_macros.h"

namespace jsb::internal {
FileAccessSourceReader::FileAccessSourceReader(const String &p_file_name) {
	file_ = FileAccess::open(p_file_name, FileAccess::READ);
	if (file_.is_valid()) {
		cached_length_ = file_->get_length();
		source_url = file_->get_path_absolute();
	} else {
		// 偶发性的 ERR_CAN_NOT_OPEN.
		JSB_LOG(Error, "Failed to open file: %s - %s", p_file_name, UtilityFunctions::error_string(FileAccess::get_open_error()));
		cached_length_ = 0;
		source_url = p_file_name;
	}
}

StringSourceReader::StringSourceReader(const String &p_path, const String &p_absolute_path, const String &p_source)
		: path_(p_path)
		, absolute_path_(p_absolute_path)
		, buffer_(p_source.to_utf8_buffer()) {
}

uint64_t StringSourceReader::get_buffer(uint8_t *p_dst, uint64_t p_length) const {
	const uint64_t len = std::min(p_length, (uint64_t)buffer_.size());
	memcpy(p_dst, buffer_.ptr(), len);
	return len;
}

} //namespace jsb::internal
