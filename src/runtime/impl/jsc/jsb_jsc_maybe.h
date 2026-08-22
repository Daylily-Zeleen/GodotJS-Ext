/************************************************************************/
/*  jsb_jsc_maybe.h                                                     */
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
namespace v8 {
template <typename T>
class Maybe {
public:
	_FORCE_INLINE_ bool IsNothing() const { return !has_value_; }
	_FORCE_INLINE_ bool IsJust() const { return has_value_; }

	void Check() const {
		jsb_check(IsJust());
	}

	Maybe() = default;
	Maybe(T value) : has_value_(true), value_(value) {}

	T ToChecked() const {
		jsb_check(has_value_);
		return value_;
	}

	bool To(T *out) const {
		if (has_value_) {
			*out = value_;
			return true;
		}
		return false;
	}

	T FromMaybe(const T &default_value) const {
		return has_value_ ? value_ : default_value;
	}

private:
	bool has_value_ = false;
	T value_;
};

template <class T>
inline Maybe<T> Just(const T &t) {
	return Maybe<T>(t);
}

template <class T>
inline Maybe<T> Nothing() {
	return Maybe<T>();
}
} //namespace v8
