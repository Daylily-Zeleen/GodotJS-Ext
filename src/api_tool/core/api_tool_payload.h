#pragma once

#include <godot_cpp/classes/file_access.hpp>
#include <memory>

namespace api_tool::internal {

template <bool ReadMode>
class ApiToolPayload {
private:
	godot::Ref<godot::FileAccess> file;

	template <typename T>
	static void custom_serializer(ApiToolPayload &payload, const T &data) {}
	template <typename T>
	static void custom_deserializer(ApiToolPayload &payload, T &data) {}

public:
	ApiToolPayload(const godot::Ref<godot::FileAccess> &p_file) : file(p_file) {}

	static std::unique_ptr<ApiToolPayload> open(const godot::String &p_path, godot::Error &r_error) {
		using namespace godot;
		Ref<FileAccess> file;
		if constexpr (ReadMode) {
			file = FileAccess::open_compressed(p_path, FileAccess::READ, FileAccess::COMPRESSION_DEFLATE);
		} else {
			file = FileAccess::open_compressed(p_path, FileAccess::WRITE, FileAccess::COMPRESSION_DEFLATE);
		}
		r_error = FileAccess::get_open_error();

		ERR_FAIL_COND_V_MSG(file.is_null(), nullptr, vformat("Open paly load file failed(%s): %s", UtilityFunctions::error_string(r_error), p_path));
		return std::move(std::make_unique<ApiToolPayload>(file));
	}

	template <typename T>
	using CustomSerializer = decltype(&ApiToolPayload::custom_serializer<T>);
	template <typename T>
	using CustomDeserializer = decltype(&ApiToolPayload::custom_deserializer<T>);

	template <typename T>
	void read(T &data, CustomDeserializer<T> deserializer) {
		static_assert(ReadMode, "ReadMode only.");
		deserializer(*this, data);
	}

	template <typename T>
	void write(const T &data, CustomSerializer<T> serializer) {
		static_assert(!ReadMode, "WriteMode only.");
		serializer(*this, data);
	}

	template <typename T>
	void read(godot::LocalVector<T> &data) {
		static_assert(ReadMode, "ReadMode only.");
		uint32_t size = file->get_32();
		data.reserve(size);
		for (uint32_t i = 0; i < size; i++) {
			T elem{};
			read<T>(elem);
			data.push_back(elem);
		}
	}

	template <typename T>
	void write(const godot::LocalVector<T> &data) {
		static_assert(!ReadMode, "WriteMode only.");
		uint32_t size = static_cast<uint32_t>(data.size());
		file->store_32(size);
		for (uint32_t i = 0; i < size; i++) {
			write(data[i]);
		}
	}

	template <typename T>
	void read(godot::LocalVector<T> &data, CustomDeserializer<T> deserializer) {
		static_assert(ReadMode, "ReadMode only.");
		uint32_t size = file->get_32();
		data.reserve(size);
		for (uint32_t i = 0; i < size; i++) {
			T d{};
			read<T>(d, deserializer);
			data.push_back(d);
		}
	}

	template <typename T>
	void write(const godot::LocalVector<T> &data, CustomSerializer<T> serializer) {
		static_assert(!ReadMode, "WriteMode only.");
		uint32_t size = static_cast<uint32_t>(data.size());
		file->store_32(size);
		for (uint32_t i = 0; i < size; i++) {
			write((data[i]), serializer);
		}
	}

	template <typename T>
	void read(T &data) {
		static_assert(ReadMode, "ReadMode only.");
		op<T, T &>(data);
	}

	template <typename T>
	void write(const T &data) {
		static_assert(!ReadMode, "WriteMode only.");
		op<T, const T &>(data);
	}

private:
	// =========
	template <typename T, typename ArgT>
		requires(!std::is_const_v<T> && !std::is_const_v<T> && !std::is_reference_v<T>) && (!ReadMode || (!std::is_const_v<ArgT> && std::is_reference_v<ArgT>))
	void op(ArgT data) {
		using namespace godot;
		if constexpr (std::is_enum_v<T>) {
			using UnderlyingType = std::underlying_type<T>::type;
			if constexpr (ReadMode) {
				UnderlyingType u{ 0 };
				op<UnderlyingType, UnderlyingType &>(u);
				data = static_cast<T>(u);
			} else {
				UnderlyingType u = static_cast<UnderlyingType>(data);
				op<UnderlyingType, UnderlyingType>(u);
			}
		} else if constexpr (std::is_same_v<T, bool>) {
			if constexpr (ReadMode)
				data = file->get_8() != 0;
			else
				file->store_8(data ? 1 : 0);
		} else if constexpr (std::is_same_v<T, String> || std::is_same_v<T, StringName>) {
			if constexpr (ReadMode)
				data = T(file->get_pascal_string());
			else
				file->store_pascal_string(data);
		} else if constexpr (std::is_same_v<T, Variant>) {
			if constexpr (ReadMode)
				data = file->get_var();
			else
				file->store_var(data);
		} else if constexpr (std::is_integral_v<T>) {
			if constexpr (sizeof(T) == 1) {
				if constexpr (ReadMode)
					*reinterpret_cast<uint8_t *>(&data) = file->get_8();
				else
					file->store_8(*reinterpret_cast<const uint8_t *>(&data));
			} else if constexpr (sizeof(T) == 2) {
				if constexpr (ReadMode)
					*reinterpret_cast<uint16_t *>(&data) = file->get_16();
				else
					file->store_16(*reinterpret_cast<const uint16_t *>(&data));
			} else if constexpr (sizeof(T) == 4) {
				if constexpr (ReadMode)
					*reinterpret_cast<uint32_t *>(&data) = file->get_32();
				else
					file->store_32(*reinterpret_cast<const uint32_t *>(&data));
			} else if constexpr (sizeof(T) == 8) {
				if constexpr (ReadMode)
					*reinterpret_cast<uint64_t *>(&data) = file->get_64();
				else
					file->store_64(*reinterpret_cast<const uint64_t *>(&data));
			}
		} else if constexpr (std::is_same_v<T, float>) {
			if constexpr (ReadMode)
				data = file->get_float();
			else
				file->store_float(data);
		} else if constexpr (std::is_same_v<T, double>) {
			if constexpr (ReadMode)
				data = file->get_double();
			else
				file->store_double(data);
		};
	}
};

} //namespace api_tool