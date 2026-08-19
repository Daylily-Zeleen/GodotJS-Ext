/************************************************************************/
/*  jsb_benchmark.h                                                     */
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

#pragma once
#include "jsb_internal_pch.h"

#if JSB_BENCHMARK
#	include <godot_cpp/classes/time.hpp>
#	define JSB_BENCHMARK_SCOPE(RegionName, DetailName)                                      \
		static const char *__String__##RegionName##DetailName = #RegionName "." #DetailName; \
		::jsb::internal::Benchmark __Benchmark__##RegionName##DetailName(__String__##RegionName##DetailName, __FILE__, __LINE__)
#else
#	define JSB_BENCHMARK_SCOPE(RegionName, DetailName) (void)0
#endif

namespace jsb::internal {
// simple implementation of benchmark
struct Benchmark {
	Benchmark(const char *p_name, const char *p_file, int p_line) : name_(p_name), file_(p_file), line_(p_line) {
		start_ = Time::get_singleton()->get_ticks_usec();
		// OS::get_singleton()->benchmark_begin_measure(name_);
	}

	~Benchmark() {
		const uint64_t total = Time::get_singleton()->get_ticks_usec() - start_;
		// ignore if finished in a jiffy
		if (total > 20000) {
			const double total_f = (double)total / 1000000.0;
			JSB_LOG(Debug, "slow process %s: %f s (%s:%d)", name_, total_f, file_, line_);
		}
		// OS::get_singleton()->benchmark_end_measure(name_);
	}

private:
	const char *name_;
	const char *file_;
	int line_;
	uint64_t start_;
};
} //namespace jsb::internal
