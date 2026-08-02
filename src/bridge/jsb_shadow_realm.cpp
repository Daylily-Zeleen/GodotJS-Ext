// TODO: 优化 mutex 的使用
#include "jsb_shadow_realm.h"

#include "../internal/jsb_sarray.h"
#include "jsb_buffer.h"
#include "jsb_class_info.h"
#include "jsb_environment.h"
#include "jsb_object_handle.h"
#include "jsb_ref.h"
#include "jsb_type_convert.h"

#define JSB_SHADOW_REALM_LOG(Severity, Format, ...) JSB_LOG_IMPL(ShadowRealm, Severity, Format, ##__VA_ARGS__)
#define JSB_SHADOW_REALM_MODULE_NAME "godot.shadowRealm"

#include <mutex>
#define MUTEX_LOCK_GUARD(lock) std::lock_guard _guard_##__LINE__(lock)

namespace jsb {
enum class FinalizationType : uint8_t;

using ShadowRealmID = internal::Index32;
using ShadowRealmLock = std::recursive_mutex;
class Environment;
class TransferableShadowRealm;

class TransferableShadowRealm;
struct ShadowRealmCreateParams;

void _placeholder(const v8::FunctionCallbackInfo<v8::Value> &info) {}

#pragma region CrossWrapper
static inline v8::Local<v8::Value> wrap_cross_env_value(Environment *p_host_env, v8::Isolate *p_isolate, const v8::Local<v8::Value> &p_function);

/** NOTE: 将在 p_to_isolate 的作用域下创建返回值 */
static v8::Local<v8::String> _transfer_string(v8::Isolate *p_from_isolate, const v8::Local<v8::String> &p_from_str, v8::Isolate *p_to_isolate) {
	const v8::Isolate::Scope isolate_scope1(p_from_isolate);

	v8::Local<v8::String> to_str;

	// QuickJS 没有UTF16相关的公开转换接口，统一使用 UTF8
	const size_t max_utf8_length = p_from_str->Length() * 3 + 1; // QuickJS 又没有UTF8长度的获取结构，真尼玛拧巴
	if (max_utf8_length <= 256) {
		char buffer[257];
		int len = p_from_str->WriteUtf8(p_from_isolate, buffer, 257);

		const v8::Isolate::Scope isolate_scope1(p_to_isolate);
		to_str = v8::String::NewFromUtf8(p_to_isolate, buffer, v8::NewStringType::kNormal, len).ToLocalChecked();
	} else {
		const int buffer_len = max_utf8_length + 1;
		char *buffer = memnew_arr(char, buffer_len);
		memset(buffer, 0, buffer_len);
		int len = p_from_str->WriteUtf8(p_from_isolate, buffer, buffer_len);

		const v8::Isolate::Scope isolate_scope1(p_from_isolate);
		to_str = v8::String::NewFromUtf8(p_to_isolate, buffer, v8::NewStringType::kNormal, len).ToLocalChecked();

		memdelete_arr(buffer);
	}
	return to_str;
}

class SymbolCrossUtils {
private:
	using SymbolGlobal = TWeakRef<v8::Symbol>;
	using SymbolGlobalHash = decltype(((SymbolGlobal *)nullptr)->hash()); // v8::internal::Address *;

	class Container {
		struct Pair {
			v8::Isolate *isolate;
			SymbolGlobal symbol;

			Pair(v8::Isolate *p_isolate, SymbolGlobal &&p_symbol) : isolate(p_isolate), symbol(std::move(p_symbol)) {}
		};
		using Map = std::unordered_map<v8::Isolate *, SymbolGlobal>;
		Map *map{ nullptr };
		Pair *pair{ nullptr };

	public:
		Container() = default;
		~Container() {
			if (map) {
				memdelete(map);
			}
			if (pair) {
				memdelete(pair);
			}
		}

		bool empty() const { return !map && !pair; }

		void emplace(v8::Isolate *p_isolate, SymbolGlobal &&p_symbol) {
			if (pair == nullptr && map == nullptr) {
				pair = memnew(Pair(p_isolate, std::move(p_symbol)));
				return;
			}

			if (pair) {
				if (pair->isolate == p_isolate) {
					return;
				}
				map = memnew(Map);
				map->try_emplace(pair->isolate, std::move(pair->symbol));
				map->try_emplace(p_isolate, std::move(p_symbol));
				memdelete(pair);
				pair = nullptr;
				return;
			}

			map->try_emplace(p_isolate, std::move(p_symbol));
		}

		bool has(v8::Isolate *p_isolate) const {
			if (pair) {
				return pair->isolate == p_isolate;
			}
			if (map) {
				return map->find(p_isolate) != map->end();
			}
			return false;
		}

		SymbolGlobal &get_raw(v8::Isolate *p_isolate) const {
			jsb_check(has(p_isolate));
			if (pair) {
				return pair->symbol;
			}
			return map->at(p_isolate);
		}

		v8::Local<v8::Symbol> get(v8::Isolate *p_isolate) const {
			jsb_check(has(p_isolate));
			if (pair) {
				return pair->symbol.object_.Get(p_isolate);
			}
			return map->at(p_isolate).object_.Get(p_isolate);
		}

		SymbolGlobalHash get_mapped_hash(v8::Isolate *p_isolate) const {
			if (pair) {
				return pair->symbol.hash();
			} else {
				const auto it = map->find(p_isolate);
				if (it != map->end()) {
					return it->second.hash();
				}
			}
			return -1;
		}

		bool erase(v8::Isolate *p_isolate) {
			if (pair && pair->isolate == p_isolate) {
				memdelete(pair);
				pair = nullptr;
				return true;
			} else if (map) {
				map->erase(p_isolate);
				if (map->empty()) {
					memdelete(map);
					map = nullptr;
					return true;
				}
			}
			return false;
		}

		bool erase_by_hash(SymbolGlobalHash p_hash, bool &r_empty) {
			if (pair && pair->symbol.hash() == p_hash) {
				memdelete(pair);
				pair = nullptr;
				r_empty = true;
				return true;
			} else if (map) {
				for (auto it = map->begin(); it != map->end(); ++it) {
					if (it->second.hash() == p_hash) {
						map->erase(it);
						if (map->empty()) {
							memdelete(map);
							map = nullptr;
							r_empty = true;
						}
						break;
					}
				}
				return true;
			}
			return false;
		}
	};

	using CacheType = std::unordered_map<SymbolGlobalHash, Container>; // , SymbolGlobal::hasher, SymbolGlobal::equaler>;

private:
	static CacheType cache_;

	static void clear_cache(const v8::WeakCallbackInfo<SymbolGlobalHash> &data) {
		SymbolGlobalHash hash = *data.GetParameter();
		cache_.erase(hash);
	}

public:
	static void clean() {
		cache_.clear();
		key_for_funcs.clear();
	}

	static void clear_isolate(v8::Isolate *p_isolate) {
		key_for_funcs.erase(p_isolate);

		std::vector<SymbolGlobalHash> to_remove;
		for (auto it = cache_.begin(); it != cache_.end(); it++) {
			Container &container = it->second;
			if (container.has(p_isolate)) {
				to_remove.push_back(container.get_raw(p_isolate).hash());
				if (container.erase(p_isolate)) {
					to_remove.push_back(it->first);
				}
			}
		}

		for (const SymbolGlobalHash hash : to_remove) {
			cache_.erase(hash);
		}
	}

private:
	_FORCE_INLINE_ static void add(
			v8::Isolate *p_from_isolate, const v8::Local<v8::Symbol> &p_from_symbol,
			v8::Isolate *p_to_isolate, const v8::Local<v8::Symbol> &p_to_symbol) {
		SymbolGlobal &&from_symbol = SymbolGlobal(p_from_isolate, p_from_symbol);
		SymbolGlobal &&to_symbol = SymbolGlobal(p_to_isolate, p_to_symbol);

		cache_[from_symbol.hash()].emplace(p_to_isolate, std::forward<SymbolGlobal &&>(to_symbol));
		cache_[to_symbol.hash()].emplace(p_from_isolate, std::forward<SymbolGlobal &&>(from_symbol));
	}

	static std::unordered_map<v8::Isolate *, TStrongRef<v8::Function>> key_for_funcs;
	/**
	 * NOTE: 工具函数不创建句柄作用域
	 * @brief 获取全局注册 Symbol 的键名，若未注册则返回空字符串
	 */
	_FORCE_INLINE_ static v8::MaybeLocal<v8::String> get_symbol_key_for(v8::Isolate *isolate, v8::Local<v8::Symbol> symbol) {
		v8::Local<v8::Context> context = Environment::wrap(isolate)->get_context();

		// 方法1: 使用预编译的 JS 函数（推荐，性能好）
		v8::Local<v8::Function> key_for_func;
		const auto it = key_for_funcs.find(isolate);
		if (it == key_for_funcs.end()) {
			// 获取全局对象上的 Symbol.keyFor 函数
			v8::Local<v8::Object> global = context->Global();
			v8::Local<v8::Value> symbol_ctor_val;
			if (!global->Get(context, v8::String::NewFromUtf8Literal(isolate, "Symbol")).ToLocal(&symbol_ctor_val)) {
				return v8::MaybeLocal<v8::String>();
			}
			v8::Local<v8::Object> symbol_ctor = symbol_ctor_val.As<v8::Object>();
			v8::Local<v8::Value> key_for_val;
			if (!symbol_ctor->Get(context, v8::String::NewFromUtf8Literal(isolate, "keyFor")).ToLocal(&key_for_val) || !key_for_val->IsFunction()) {
				return v8::MaybeLocal<v8::String>();
			}

			key_for_func = key_for_val.As<v8::Function>();
		} else {
			key_for_func = it->second.object_.Get(isolate);
		}

		v8::Local<v8::Value> argv[] = { symbol };
		v8::MaybeLocal<v8::Value> result = key_for_func->Call(context, context->Global(), 1, argv);

		v8::Local<v8::Value> val;
		if (result.ToLocal(&val) && val->IsString() && val.As<v8::String>()->Length() > 0) {
			return val.As<v8::String>();
		}
		return v8::MaybeLocal<v8::String>();
	}

public:
	using WellKnownSymbolGetter = decltype(v8::Symbol::GetAsyncIterator);
	_FORCE_INLINE_ static WellKnownSymbolGetter *get_well_known_symbol_getter(v8::Isolate *p_isolate, const v8::Local<v8::Symbol> &p_symbol) {
		static const std::unordered_map<int, WellKnownSymbolGetter *> well_known_symbol_getters = [p_isolate] {
			return std::unordered_map<int, WellKnownSymbolGetter *>{
				{ v8::Symbol::GetAsyncIterator(p_isolate)->GetIdentityHash(), v8::Symbol::GetAsyncIterator },
				{ v8::Symbol::GetHasInstance(p_isolate)->GetIdentityHash(), v8::Symbol::GetHasInstance },
				{ v8::Symbol::GetIsConcatSpreadable(p_isolate)->GetIdentityHash(), v8::Symbol::GetIsConcatSpreadable },
				{ v8::Symbol::GetIterator(p_isolate)->GetIdentityHash(), v8::Symbol::GetIterator },
				{ v8::Symbol::GetMatch(p_isolate)->GetIdentityHash(), v8::Symbol::GetMatch },
				{ v8::Symbol::GetReplace(p_isolate)->GetIdentityHash(), v8::Symbol::GetReplace },
				{ v8::Symbol::GetSearch(p_isolate)->GetIdentityHash(), v8::Symbol::GetSearch },
				{ v8::Symbol::GetSplit(p_isolate)->GetIdentityHash(), v8::Symbol::GetSplit },
				{ v8::Symbol::GetToPrimitive(p_isolate)->GetIdentityHash(), v8::Symbol::GetToPrimitive },
				{ v8::Symbol::GetToStringTag(p_isolate)->GetIdentityHash(), v8::Symbol::GetToStringTag },
				{ v8::Symbol::GetUnscopables(p_isolate)->GetIdentityHash(), v8::Symbol::GetUnscopables },
			};
		}();

		const auto it = well_known_symbol_getters.find(p_symbol->GetIdentityHash());
		if (it == well_known_symbol_getters.end()) {
			return nullptr;
		}
		return it->second;
	}

	_FORCE_INLINE_ static bool is_normal_symbol(v8::Isolate *p_isolate, const v8::Local<v8::Symbol> &p_symbol) {
		WellKnownSymbolGetter *getter = get_well_known_symbol_getter(p_isolate, p_symbol);
		if (getter != nullptr) {
			return false;
		}

		v8::HandleScope handle_scope(p_isolate);
		const v8::Isolate::Scope isolate_scope(p_isolate);
		if (!get_symbol_key_for(p_isolate, p_symbol).IsEmpty() || v8::Symbol::GetAsyncIterator(p_isolate) == p_symbol || v8::Symbol::GetHasInstance(p_isolate) == p_symbol || v8::Symbol::GetIsConcatSpreadable(p_isolate) == p_symbol || v8::Symbol::GetIterator(p_isolate) == p_symbol || v8::Symbol::GetMatch(p_isolate) == p_symbol || v8::Symbol::GetReplace(p_isolate) == p_symbol || v8::Symbol::GetSearch(p_isolate) == p_symbol || v8::Symbol::GetSplit(p_isolate) == p_symbol || v8::Symbol::GetToPrimitive(p_isolate) == p_symbol || v8::Symbol::GetToStringTag(p_isolate) == p_symbol || v8::Symbol::GetUnscopables(p_isolate) == p_symbol) {
			return false;
		}
		return true;
	}

	/**
	 * NOTE: 工具函数不创建句柄作用域, 将在 p_to_isolate 中创建Symbol
	 */
	static v8::Local<v8::Symbol> get_symbol(v8::Isolate *p_from_isolate, const v8::Local<v8::Symbol> &p_from_symbol, v8::Isolate *p_to_isolate) {
		// Well-Known
		WellKnownSymbolGetter *getter = get_well_known_symbol_getter(p_from_isolate, p_from_symbol);
		if (getter != nullptr) {
			const v8::Isolate::Scope isolate_scope2(p_to_isolate);
			const v8::Local<v8::Symbol> to_symbol = (*getter)(p_to_isolate);
			return to_symbol;
		}

		// Cache
		SymbolGlobal from_symbol(p_from_isolate, p_from_symbol);
		if (const auto it = cache_.find(from_symbol.hash()); it != cache_.end()) {
			if (it->second.has(p_to_isolate)) {
				return it->second.get(p_to_isolate);
			}
		}

		const v8::HandleScope handle_scope1(p_from_isolate);
		const v8::Isolate::Scope isolate_scope1(p_from_isolate);

		// Create new cache
		const bool is_normal = get_symbol_key_for(p_from_isolate, p_from_symbol).IsEmpty();

		const v8::Isolate::Scope isolate_scope2(p_to_isolate);

		v8::Local<v8::Value> desc_value = p_from_symbol->Description(p_from_isolate);

		v8::Local<v8::String> transferred_desc;
		if (desc_value->IsString()) {
			transferred_desc = _transfer_string(p_from_isolate, desc_value.As<v8::String>(), p_to_isolate);
		} else {
			transferred_desc = v8::String::Empty(p_to_isolate);
		}

		v8::Local<v8::Symbol> to_symbol;
		if (is_normal) {
			to_symbol = v8::Symbol::New(p_to_isolate, transferred_desc);
		} else {
			to_symbol = v8::Symbol::For(p_to_isolate, transferred_desc);
		}

		add(p_from_isolate, p_from_symbol, p_to_isolate, to_symbol);
		return to_symbol;
	}
};

SymbolCrossUtils::CacheType SymbolCrossUtils::cache_{};
std::unordered_map<v8::Isolate *, TStrongRef<v8::Function>> SymbolCrossUtils::key_for_funcs;

struct WrapperIdentity {
	uintptr_t guest_isolate;
	int32_t guest_value;

	WrapperIdentity(v8::Isolate *p_guest_isolate, const v8::Local<v8::Object> &p_guest_value) {
		guest_isolate = reinterpret_cast<uintptr_t>(p_guest_isolate);
		guest_value = p_guest_value->GetIdentityHash();
	}

	size_t hash() const {
		size_t ret{};
		ret |= guest_value;
		ret <<= sizeof(guest_value) * 8;
		ret ^= guest_isolate;
		return ret;
	}

	bool operator==(const WrapperIdentity &p_other) const {
		return guest_value == p_other.guest_value && guest_isolate == p_other.guest_isolate;
	}
};

class CrossWrapper : public CustomNativeBase {
protected:
	v8::Isolate *isolate_;
	TStrongRef<v8::Object> value_;

	CrossWrapper(v8::Isolate *p_isolate, const v8::Local<v8::Object> &p_value) : isolate_(p_isolate), value_(p_isolate, p_value) {
	}

private:
	struct WrapperIdentityHash {
		size_t operator()(const WrapperIdentity &k) const {
			return k.hash();
		}
	};

	struct WrapperIdentityEqual {
		bool operator()(const WrapperIdentity &lhs, const WrapperIdentity &rhs) const {
			return lhs == rhs;
		}
	};

private:
	static std::unordered_multimap<WrapperIdentity, TWeakRef<v8::Object>, WrapperIdentityHash, WrapperIdentityEqual> wrapper_cache_;
	static std::recursive_mutex lock_;
	static std::unordered_map<v8::Isolate *, TStrongRef<v8::Name>> flag_symbols_;

public:
	static void add_cache(v8::Isolate *p_isolate, const v8::Local<v8::Object> &p_value, v8::Isolate *p_host_isolate, const v8::Local<v8::Object> &p_wrapper) {
		MUTEX_LOCK_GUARD(lock_);
		wrapper_cache_.emplace(
				WrapperIdentity{ p_isolate, p_value },
				TWeakRef<v8::Object>{ p_host_isolate, p_wrapper });
	}
	static v8::MaybeLocal<v8::Object> try_get_cache(v8::Isolate *p_isolate, const v8::Local<v8::Object> &p_value, v8::Isolate *p_host_isolate) {
		MUTEX_LOCK_GUARD(lock_);
		auto it = wrapper_cache_.find(WrapperIdentity{ p_isolate, p_value });
		if (it == wrapper_cache_.end()) {
			return {};
		}
		return it->second.object_.Get(p_host_isolate);
	}
	static void remove_cache(v8::Isolate *p_isolate, const v8::Local<v8::Object> &p_value) {
		MUTEX_LOCK_GUARD(lock_);
		wrapper_cache_.erase(WrapperIdentity{ p_isolate, p_value });
	}

public:
	v8::Isolate *get_isolate() const { return isolate_; }
	v8::Local<v8::Object> get_raw_value() const { return value_.object_.Get(isolate_); }
	static const TStrongRef<v8::Name> &get_flag_symbol(v8::Isolate *p_isolate) {
		MUTEX_LOCK_GUARD(lock_);

		v8::Local<v8::Name> symbol;
		auto it = flag_symbols_.find(p_isolate);
		if (it == flag_symbols_.end()) {
			const v8::Isolate::Scope isolate_scope(p_isolate);
			v8::HandleScope handle_scope(p_isolate);
			v8::Local<v8::Symbol> symbol = v8::Symbol::New(p_isolate, v8::String::NewFromUtf8Literal(p_isolate, "DontTouchMe!"));
			it = flag_symbols_.emplace(p_isolate, TStrongRef<v8::Name>{ p_isolate, symbol }).first;
		}
		return it->second;
	}

	static void remove_flag_symbol(v8::Isolate *p_isolate) {
		MUTEX_LOCK_GUARD(lock_);
		flag_symbols_.erase(p_isolate);
	}
};

std::unordered_map<v8::Isolate *, TStrongRef<v8::Name>> CrossWrapper::flag_symbols_;
std::unordered_multimap<WrapperIdentity, TWeakRef<v8::Object>, CrossWrapper::WrapperIdentityHash, CrossWrapper::WrapperIdentityEqual> CrossWrapper::wrapper_cache_{};
std::recursive_mutex CrossWrapper::lock_;

class FunctionCrossWrapper : public CrossWrapper {
private:
	FunctionCrossWrapper(v8::Isolate *p_isolate, const v8::Local<v8::Function> &p_function) : CrossWrapper(p_isolate, p_function) {}

public:
	v8::Local<v8::Function> get_function() const {
		return value_.object_.Get(isolate_).As<v8::Function>();
	}

public:
	/** NOTE: 不创建句柄作用域，将在 p_host_env 中创建对象 */
	static v8::Local<v8::Object> create(Environment *p_host_env, v8::Isolate *p_guest_isolate, const v8::Local<v8::Function> &p_function) {
		/** NOTE: 操作中 p_guest_isolate 未涉及 V8 API  */
		v8::MaybeLocal<v8::Object> maybe_cache = try_get_cache(p_guest_isolate, p_function, p_host_env->get_isolate());
		v8::Local<v8::Object> cache;
		if (maybe_cache.ToLocal(&cache)) {
			JSB_LOG(Info, "Get cache: %s", impl::Helper::to_string_without_side_effect(p_guest_isolate, cache));
			return cache;
		}

		v8::Isolate *isolate = p_host_env->get_isolate();
		jsb_check(isolate != p_guest_isolate);

		const v8::Isolate::Scope isolate_scope(isolate);

		NativeClassID class_id;
		const StringName &class_name = jsb_string_name(FunctionCrossWrapper);
		p_host_env->find_native_class(class_name, &class_id);
		const NativeClassInfoPtr class_info = p_host_env->find_native_class(class_name, &class_id);

		const v8::Local<v8::Context> context = p_host_env->get_context();
		const v8::Context::Scope context_scope(context);
		const v8::Local<v8::Object> data = class_info->clazz.NewInstance(context);

		FunctionCrossWrapper *ptr = memnew(FunctionCrossWrapper(p_guest_isolate, p_function));
		const NativeObjectID handle = p_host_env->bind_js_owned_pointer(class_id, NativeClassType::Custom, ptr, data);
		jsb_check(handle);

		const v8::Local<v8::Function> wrapper = v8::Function::New(context, &FunctionCrossWrapper::call, data).ToLocalChecked();

		const TStrongRef<v8::Name> &symbol = get_flag_symbol(isolate);
		wrapper.As<v8::Object>()->Set(context, symbol.object_.Get(isolate), data).Check();
		// TODO: Freeze 或 proxy, 防止被篡改

		add_cache(p_guest_isolate, p_function, isolate, wrapper);
		return wrapper;
	}

	static void call(const v8::FunctionCallbackInfo<v8::Value> &info) {
		v8::Isolate *host_isolate = info.GetIsolate();
		Environment *host_env = Environment::wrap(host_isolate);
		const v8::HandleScope handle_scope(host_isolate);
		const v8::Isolate::Scope isolate_scope(host_isolate);

		const FunctionCrossWrapper *wrapper = (const FunctionCrossWrapper *)(info.Data().As<v8::Object>()->GetAlignedPointerFromInternalField(IF_Pointer));

		v8::Isolate *guest_isolate = wrapper->get_isolate();
		const v8::HandleScope handle_scope1(guest_isolate);
		const v8::Isolate::Scope isolate_scope1(guest_isolate);
		Environment *guest_env = Environment::wrap(guest_isolate);

		// TODO: jsb_stackalloc
		LocalVector<v8::Local<v8::Value>> args;
		args.reserve(info.Length());
		for (int i = 0; i < info.Length(); i++) {
			const v8::Local<v8::Value> arg = info[i];
			v8::Local<v8::Value> warpped_arg = wrap_cross_env_value(guest_env, host_isolate, arg); /** NOTE: 将在 guest_env(guest_isolate) 中创建对象 */
			args.push_back(warpped_arg);
		}

		const v8::Local<v8::Function> function = wrapper->get_function();
		const v8::Local<v8::Context> guest_context = guest_env->get_context();
		const v8::Context::Scope context_scope(guest_context);

		v8::Local<v8::Value> result = function->Call(guest_context, v8::Undefined(guest_isolate), args.size(), args.ptr()).ToLocalChecked();
		const v8::Local<v8::Value> wrapped_result = wrap_cross_env_value(host_env, guest_isolate, result); /** NOTE: 将在 host_env(host_isolate) 中创建对象 */
		info.GetReturnValue().Set(wrapped_result);
	}

public:
	static void register_class(Environment *p_env) {
		v8::Isolate *isolate = p_env->get_isolate();
		const v8::HandleScope handle_scope(isolate);
		const v8::Isolate::Scope isolate_scope(isolate);

		{
			const StringName &class_name = jsb_string_name(FunctionCrossWrapper);
			const NativeClassID class_id = p_env->add_native_class(NativeClassType::Custom, class_name);
			impl::ClassBuilder class_builder = impl::ClassBuilder::New<IF_ObjectFieldCount>(isolate, class_name, &_placeholder, *class_id);

			const NativeClassInfoPtr class_info = p_env->get_native_class(class_id);
			class_info->finalizer = &FunctionCrossWrapper::finalizer;
			class_info->clazz = class_builder.Build();
			jsb_check(!class_info->clazz.IsEmpty());
			jsb_check(class_info->name == class_name);
		}
	}

	static void finalizer(Environment *, void *pointer, FinalizationType /* p_finalize */) {
		FunctionCrossWrapper *self = (FunctionCrossWrapper *)pointer;

		v8::Isolate *isolate = self->get_isolate();
		const v8::HandleScope handle_scope(isolate);
		const v8::Isolate::Scope isolate_scope(isolate);
		remove_cache(isolate, self->get_raw_value());

		JSB_SHADOW_REALM_LOG(VeryVerbose, "deleting FunctionCrossWrapper.");
		memdelete(self);
	}
};

class ObjectCrossWrapper : public CrossWrapper {
private:
	ObjectCrossWrapper(v8::Isolate *p_isolate, const v8::Local<v8::Object> &p_obj) : CrossWrapper(p_isolate, p_obj) {}

public:
	v8::Local<v8::Object> get_object() const { return value_.object_.Get(isolate_).As<v8::Object>(); }

private:
	/** NOTE: 工具函数不创建句柄作用域，将在 p_to_isolate 中创建对象 */
	_FORCE_INLINE_ static v8::Local<v8::Name> transfer_key(v8::Isolate *p_from_isolate, const v8::Local<v8::Name> &p_from_key, v8::Isolate *p_to_isolate) {
		if (p_from_key->IsSymbol()) {
			if (SymbolCrossUtils::is_normal_symbol(p_from_isolate, p_from_key.As<v8::Symbol>())) {
				jsb_throw(p_from_isolate, "Can't access regular symbol property from another realm.");
				return v8::Local<v8::Name>();
			} else {
				return SymbolCrossUtils::get_symbol(p_from_isolate, p_from_key.As<v8::Symbol>(), p_to_isolate);
			}
		}
		return _transfer_string(p_from_isolate, p_from_key.As<v8::String>(), p_to_isolate);
	}

public:
	/** NOTE: 不创建句柄作用域，将在 p_host_env 中创建对象 */
	static v8::Local<v8::Object> create(Environment *p_host_env, v8::Isolate *p_guest_isolate, const v8::Local<v8::Object> &p_guest_obj) {
		/** NOTE: 在操作中 p_guest_isolate 未涉及 V8 API */
		v8::MaybeLocal<v8::Object> maybe_cache = try_get_cache(p_guest_isolate, p_guest_obj, p_host_env->get_isolate());
		v8::Local<v8::Object> cache;
		if (maybe_cache.ToLocal(&cache)) {
			JSB_LOG(Info, "Get cache: %s", impl::Helper::to_string_without_side_effect(p_guest_isolate, cache));
			return cache;
		}

		v8::Isolate *isolate = p_host_env->get_isolate();
		jsb_check(isolate != p_guest_isolate);
		const v8::Isolate::Scope isolate_scope(isolate);

		const v8::Local<v8::Context> context = p_host_env->get_context();
		const v8::Context::Scope context_scope(context);

		NativeClassID class_id;
		const StringName &class_name = jsb_string_name(ObjectCrossWrapper);
		const NativeClassInfoPtr class_info = p_host_env->find_native_class(class_name, &class_id);
		const v8::Local<v8::Object> wrapper = class_info->clazz.NewInstance(context);

		ObjectCrossWrapper *ptr = memnew(ObjectCrossWrapper(p_guest_isolate, p_guest_obj));
		const NativeObjectID handle = p_host_env->bind_js_owned_pointer(class_id, NativeClassType::Custom, ptr, wrapper);
		jsb_check(handle);

		// Proxy Handler
		v8::Local<v8::Object> handler = v8::Object::New(isolate);
		handler->Set(context,
					   jsb_name(p_host_env, has),
					   v8::Function::New(context, &ObjectCrossWrapper::proxy_has).ToLocalChecked())
				.Check();
		handler->Set(context,
					   jsb_name(p_host_env, get),
					   v8::Function::New(context, &ObjectCrossWrapper::proxy_get).ToLocalChecked())
				.Check();
		handler->Set(context,
					   jsb_name(p_host_env, set),
					   v8::Function::New(context, &ObjectCrossWrapper::proxy_set).ToLocalChecked())
				.Check();
		handler->Set(context,
					   jsb_name(p_host_env, getPrototypeOf),
					   v8::Function::New(context, &ObjectCrossWrapper::proxy_getPrototypeOf).ToLocalChecked())
				.Check();

		v8::Local<v8::Proxy> proxy = v8::Proxy::New(context, wrapper, handler).ToLocalChecked();

		add_cache(p_guest_isolate, p_guest_obj, isolate, proxy);
		return proxy;
	}

	static void proxy_has(const v8::FunctionCallbackInfo<v8::Value> &info) {
		v8::Isolate *isolate = info.GetIsolate();
		const v8::HandleScope handle_scope(isolate);
		const v8::Isolate::Scope isolate_scope(isolate);

		jsb_check(info.Length() == 2);
		v8::Local<v8::Object> target = info[0].As<v8::Object>();
		const v8::Local<v8::Name> key = info[1].As<v8::Name>();

		const TStrongRef<v8::Name> &symbol = get_flag_symbol(isolate);
		const Environment *env = Environment::wrap(isolate);
		const v8::Local<v8::Context> context = env->get_context();
		const v8::Context::Scope context_scope(context);
		if (symbol.object_.Get(isolate)->Equals(context, key).ToChecked()) {
			info.GetReturnValue().Set(v8::Boolean::New(isolate, true));
		} else {
			const ObjectCrossWrapper *wrapper = (const ObjectCrossWrapper *)(target->GetAlignedPointerFromInternalField(IF_Pointer));
			v8::Isolate *guest_isolate = wrapper->get_isolate();

			const v8::Isolate::Scope isolate_scope1(guest_isolate);
			const v8::HandleScope handle_scope1(guest_isolate); // 为 guest_isolate 创建句柄作用域，后续的工具函数调用将在其中创建对象
			const v8::Local<v8::Name> transferred_key = transfer_key(isolate, key, guest_isolate);
			if (transferred_key.IsEmpty()) {
				info.GetReturnValue().Set(v8::Boolean::New(isolate, true));
				return;
			}

			const v8::Local<v8::Context> guest_context = Environment::wrap(guest_isolate)->get_context();
			const v8::Context::Scope context_scope1(guest_context);

			const v8::Local<v8::Object> guest_object = wrapper->get_object();
			const bool result = guest_object->HasOwnProperty(guest_context, transferred_key).ToChecked();

			info.GetReturnValue().Set(v8::Boolean::New(isolate, result));
		}
	}
	static void proxy_get(const v8::FunctionCallbackInfo<v8::Value> &info) {
		v8::Isolate *isolate = info.GetIsolate();
		const v8::HandleScope handle_scope(isolate);
		const v8::Isolate::Scope isolate_scope(isolate);

		jsb_check(info.Length() == 3);
		const v8::Local<v8::Object> target = info[0].As<v8::Object>();
		const v8::Local<v8::Name> key = info[1].As<v8::Name>();

		const TStrongRef<v8::Name> &symbol = get_flag_symbol(isolate);
		const Environment *env = Environment::wrap(isolate);
		const v8::Local<v8::Context> context = env->get_context();
		const v8::Context::Scope context_scope(context);
		if (symbol.object_.Get(isolate)->Equals(context, key).ToChecked()) {
			info.GetReturnValue().Set(v8::Boolean::New(isolate, true));
		} else {
			const ObjectCrossWrapper *wrapper = (const ObjectCrossWrapper *)(target->GetAlignedPointerFromInternalField(IF_Pointer));
			v8::Isolate *guest_isolate = wrapper->get_isolate();

			const v8::Isolate::Scope isolate_scope1(guest_isolate);
			const v8::HandleScope handle_scope1(guest_isolate); // 为 guest_isolate 创建句柄作用域，后续的工具函数调用将在其中创建对象
			const v8::Local<v8::Name> transferred_key = transfer_key(isolate, key, guest_isolate);
			if (transferred_key.IsEmpty()) {
				info.GetReturnValue().Set(v8::Undefined(isolate));
				return;
			}

			const v8::Local<v8::Context> guest_context = Environment::wrap(guest_isolate)->get_context();
			const v8::Local<v8::Object> guest_object = wrapper->get_object();

			const v8::Local<v8::Value> value = guest_object->Get(guest_context, transferred_key).ToLocalChecked();
			const v8::Local<v8::Value> result = wrap_cross_env_value(Environment::wrap(isolate), guest_isolate, value); /** NOTE: 将在 isolate 中创建对象 */

			info.GetReturnValue().Set(result);
		}
	}
	static void proxy_set(const v8::FunctionCallbackInfo<v8::Value> &info) {
		v8::Isolate *isolate = info.GetIsolate();
		const v8::HandleScope handle_scope(isolate);
		const v8::Isolate::Scope isolate_scope(isolate);

		jsb_check(info.Length() == 4);
		const v8::Local<v8::Object> target = info[0].As<v8::Object>();
		const v8::Local<v8::Name> key = info[1].As<v8::Name>();
		const v8::Local<v8::Value> value = info[2].As<v8::Value>();

		const TStrongRef<v8::Name> &symbol = get_flag_symbol(isolate);
		const v8::Local<v8::Context> context = Environment::wrap(isolate)->get_context();
		const v8::Context::Scope context_scope(context);
		if (symbol.object_.Get(isolate)->Equals(context, key).ToChecked()) {
			return;
		} else {
			const ObjectCrossWrapper *wrapper = (const ObjectCrossWrapper *)(target->GetAlignedPointerFromInternalField(IF_Pointer));
			v8::Isolate *guest_isolate = wrapper->get_isolate();

			const v8::Isolate::Scope isolate_scope1(guest_isolate);
			const v8::HandleScope handle_scope1(guest_isolate); // 为 guest_isolate 创建句柄作用域，后续的工具函数调用将在其中创建对象
			const v8::Local<v8::Name> transferred_key = transfer_key(isolate, key, guest_isolate);
			if (transferred_key.IsEmpty()) {
				return;
			}

			Environment *guest_env = Environment::wrap(guest_isolate);
			const v8::Local<v8::Context> guest_context = guest_env->get_context();
			const v8::Context::Scope context_scope1(guest_context);

			const v8::Local<v8::Object> guest_object = wrapper->get_object();
			v8::Local<v8::Value> wrapped = wrap_cross_env_value(guest_env, isolate, value); /** NOTE: 将在 guest_env(guest_isolate) 中创建对象 */
			guest_object->Set(guest_context, transferred_key, wrapped).Check();
		}
	}
	static void proxy_getPrototypeOf(const v8::FunctionCallbackInfo<v8::Value> &info) { info.GetReturnValue().SetUndefined(); }

public:
	static void register_class(Environment *p_env) {
		v8::Isolate *isolate = p_env->get_isolate();
		const v8::Isolate::Scope isolate_scope(isolate);
		const v8::HandleScope handle_scope(isolate);

		{
			const StringName &class_name = jsb_string_name(ObjectCrossWrapper);
			const NativeClassID class_id = p_env->add_native_class(NativeClassType::Custom, class_name);
			impl::ClassBuilder class_builder = impl::ClassBuilder::New<IF_ObjectFieldCount>(isolate, class_name, &_placeholder, *class_id);

			const NativeClassInfoPtr class_info = p_env->get_native_class(class_id);
			class_info->finalizer = &ObjectCrossWrapper::finalizer;
			class_info->clazz = class_builder.Build();

			jsb_check(!class_info->clazz.IsEmpty());
			jsb_check(class_info->name == class_name);
		}
	}

	static void finalizer(Environment *, void *pointer, FinalizationType /* p_finalize */) {
		ObjectCrossWrapper *self = (ObjectCrossWrapper *)pointer;
		v8::Isolate *isolate = self->get_isolate();
		const v8::Isolate::Scope isolate_scope(isolate);
		const v8::HandleScope handle_scope(isolate);
		remove_cache(isolate, self->get_raw_value());

		JSB_SHADOW_REALM_LOG(VeryVerbose, "deleting ObjectCrossWrapper.");
		memdelete(self);
	}
};

/** NOTE: 将在 p_host_env 中创建对象，注意提前创建 HandleScope */
static _FORCE_INLINE_ v8::Local<v8::Value> wrap_cross_env_value(Environment *p_host_env, v8::Isolate *p_guest_isolate, const v8::Local<v8::Value> &p_guest_value) {
	v8::Isolate *host_isolate = p_host_env->get_isolate();

	const v8::Isolate::Scope guest_isolate_scope(p_guest_isolate);
	const v8::HandleScope guest_handle_scope(p_guest_isolate);
	if (host_isolate == p_guest_isolate) {
		// 同环境，直接返回
		return p_guest_value;
	}

	const v8::Isolate::Scope isolate_scope(host_isolate);

	const v8::Local<v8::Context> guest_context = Environment::wrap(p_guest_isolate)->get_context();
	const v8::Context::Scope context_scope(guest_context);
	if (p_guest_value->IsFunction()) {
		const v8::Local<v8::Function> func = p_guest_value.As<v8::Function>();
		const v8::Local<v8::Name> symbol = CrossWrapper::get_flag_symbol(p_guest_isolate).object_.Get(p_guest_isolate);
		const v8::Maybe<bool> has_meta = func->HasRealNamedProperty(guest_context, symbol);
		if (has_meta.IsJust() && has_meta.ToChecked()) {
			const v8::Local<v8::Value> meta = func->Get(guest_context, symbol).ToLocalChecked();
			jsb_check(meta->IsObject());
			const v8::Local<v8::Object> obj = meta.As<v8::Object>();
			jsb_check(obj->InternalFieldCount() == IF_ObjectFieldCount);
			const NativeClassType::Type type = (NativeClassType::Type)(uintptr_t)obj->GetAlignedPointerFromInternalField(IF_ClassType);
			jsb_check(type == NativeClassType::Custom);
			const CrossWrapper *wrapper = static_cast<CrossWrapper *>(obj->GetAlignedPointerFromInternalField(IF_Pointer));
			if (wrapper->get_isolate() == host_isolate) {
				return wrapper->get_raw_value(); // 返回到原始环境
			} else {
				return wrap_cross_env_value(p_host_env, wrapper->get_isolate(), wrapper->get_raw_value()); // 传送到其他环境？
			}
		}

		return FunctionCrossWrapper::create(p_host_env, p_guest_isolate, p_guest_value.As<v8::Function>());
	} else if (p_guest_value->IsObject()) {
		const v8::Local<v8::Object> obj = p_guest_value.As<v8::Object>();
		const v8::Local<v8::Name> symbol = CrossWrapper::get_flag_symbol(p_guest_isolate).object_.Get(p_guest_isolate);
		const v8::Maybe<bool> has_meta = obj->HasOwnProperty(guest_context, symbol);
		if (has_meta.IsJust() && has_meta.ToChecked()) {
			const v8::Local<v8::Proxy> proxy = obj.As<v8::Proxy>();
			const v8::Local<v8::Object> target = proxy->GetTarget().As<v8::Object>();
			jsb_check(target->InternalFieldCount() == IF_ObjectFieldCount);
			const NativeClassType::Type type = (NativeClassType::Type)(uintptr_t)target->GetAlignedPointerFromInternalField(IF_ClassType);
			jsb_check(type == NativeClassType::Custom);
			const CrossWrapper *wrapper = static_cast<CrossWrapper *>(target->GetAlignedPointerFromInternalField(IF_Pointer));
			if (wrapper->get_isolate() == host_isolate) {
				return wrapper->get_raw_value(); // 返回到原始环境
			} else {
				return wrap_cross_env_value(p_host_env, wrapper->get_isolate(), wrapper->get_raw_value()); // 传送到其他环境？
			}
		}

		return ObjectCrossWrapper::create(p_host_env, p_guest_isolate, p_guest_value.As<v8::Object>());
	} else if (p_guest_value->IsSymbol()) {
		const auto ret = SymbolCrossUtils::get_symbol(p_guest_isolate, p_guest_value.As<v8::Symbol>(), host_isolate);
		return ret;
	} else {
		v8::Local<v8::Value> out;
		const bool isPrimitive = p_guest_value->ToPrimitive(guest_context).ToLocal(&out);
		if (isPrimitive) {
			// 序列化
			v8::ValueSerializer serializer(p_guest_isolate);
			serializer.WriteHeader();
			impl::TryCatch try_catch(p_guest_isolate);
			serializer.WriteValue(guest_context, out).Check();
			std::pair<uint8_t *, size_t> data = serializer.Release();
			ERR_FAIL_COND_V_MSG(try_catch.has_caught(), {}, BridgeHelper::get_exception(try_catch));

			const v8::Local<v8::Context> host_context = p_host_env->get_context();
			const v8::Context::Scope host_context_scope(host_context);

			// 反序列化到目标 Isolate
			v8::ValueDeserializer deserializer(host_isolate, data.first, data.second);
			impl::TryCatch try_catch1(p_guest_isolate);
			deserializer.ReadHeader(host_context).Check();
			v8::Local<v8::Value> result = deserializer.ReadValue(host_context).ToLocalChecked();
			delete[] data.first;

			ERR_FAIL_COND_V_MSG(try_catch1.has_caught(), {}, BridgeHelper::get_exception(try_catch1));

			return result;
		} else {
			jsb_checkf(isPrimitive, "Wrapper failed.");
			return v8::Undefined(host_isolate);
		}
	}
}

struct ShadowRealmCreateParams {
	String startup_script = "";
	bool allow_import_any_module = false;
};
#pragma endregion CrossWrapper

#pragma region ShadownRealm

class ShadowRealmImpl {
	ShadowRealmID id_{};
	internal::Index32 id_in_master_{};

	// object id of this shadow_realm object in the master environment
	NativeObjectID handle_;

	void *token_ = nullptr;
	jsb::DefaultModuleResolver *module_resolver_{ nullptr };

protected:
	std::shared_ptr<Environment> env_{ nullptr };

	// friend class TransferableShadowRealmImpl;

protected:
	static internal::SArray<ShadowRealmImpl *, ShadowRealmID> &get_shadow_realm_list();
	static std::recursive_mutex lock_;

protected:
	template<typename ShadowRealmType>
	requires std::is_convertible_v<ShadowRealmType*, ShadowRealmImpl *>
	static void constructor(const v8::FunctionCallbackInfo<v8::Value> &info) {
		v8::Isolate *isolate = info.GetIsolate();
		Environment *env = Environment::wrap(isolate);
		const v8::HandleScope handle_scope(isolate);
		const v8::Isolate::Scope isolate_scope(isolate);
		const v8::Local<v8::Context> context = env->get_context();
		const v8::Context::Scope context_scope(context);

		const v8::Local<v8::Object> self = info.This();
		const internal::Index32 class_id(info.Data().As<v8::Uint32>()->Value());

		ShadowRealmCreateParams params;
		v8::Local<v8::Value> arg = info[0];
		if (info[0]->IsObject()) {
			v8::Local<v8::Object> obj = arg.As<v8::Object>();
			v8::MaybeLocal<v8::Value> startup_script = obj->Get(context, jsb_name(env, startupScript));
			v8::MaybeLocal<v8::Value> allow_import_any_module = obj->Get(context, jsb_name(env,allowImportAnyModule));
			if (startup_script.IsEmpty() || allow_import_any_module.IsEmpty()) {
				jsb_throw(isolate, "bad param");
			}
			params.startup_script = impl::Helper::to_string(isolate, startup_script.ToLocalChecked());
			params.allow_import_any_module = allow_import_any_module.ToLocalChecked()->BooleanValue(isolate);
		} else if (!arg->IsNullOrUndefined()) {
			jsb_throw(isolate, "bad param");
		}

		MUTEX_LOCK_GUARD(lock_);

		ShadowRealmType *realm = memnew(ShadowRealmType(env));
		const ShadowRealmID id = get_shadow_realm_list().add(realm);
		if (realm->init(id, env, params)) {
			const NativeObjectID handle = env->bind_js_owned_pointer(class_id, NativeClassType::Shadow, realm, self);
			jsb_check(handle);
			realm->handle_ = handle;
		} else {
			get_shadow_realm_list().remove_at(id);
			realm->id_ = ShadowRealmID::none();
		}
	}

	virtual void init_environment() {}

public:
	ShadowRealmImpl(Environment *p_master) : token_(p_master) {}
	virtual ~ShadowRealmImpl() {
		JSB_SHADOW_REALM_LOG(VeryVerbose, "ShadowRealmImpl destroyed: %d", id_);
	}

	bool init(ShadowRealmID id, Environment *p_master, const ShadowRealmCreateParams &p_create_params) {
		jsb_check(!id_);
		jsb_check(id);
		id_ = id;

		Environment::CreateParams params;
		params.initial_class_slots = JSB_SHADOW_REALM_INITIAL_CLASS_SLOTS;
		params.initial_object_slots = JSB_SHADOW_REALM_INITIAL_OBJECT_SLOTS;
		params.initial_script_slots = JSB_SHADOW_REALM_INITIAL_SCRIPT_SLOTS;
		params.thread_id = ThreadEx::get_caller_id();
		params.type = Environment::Type::Worker; // HACK, TODO: 专属标志？

		env_ = std::make_shared<Environment>(params);
		if (p_create_params.allow_import_any_module) {
			env_->init();
		} else {
			module_resolver_ = &env_->add_module_resolver<jsb::DefaultModuleResolver>();
		}

		{
			v8::Isolate *isolate = env_->get_isolate();
			impl::Helper::set_as_interruptible(isolate);

			init_environment();
		}

		if (!p_create_params.startup_script.is_empty()) {
			if (env_->load(p_create_params.startup_script) != OK) {
				finish();
				jsb_throw(p_master->get_isolate(), "Create ShadowRealm failed: failed to load startup script.");
				return false;
			}
		}

		id_in_master_ = p_master->add_shadow_env(env_);
		return true;
	}

// protected:
	_FORCE_INLINE_ ShadowRealmID get_id() const { return id_; }

	_FORCE_INLINE_ NativeObjectID get_handle() const { return handle_; }

	_FORCE_INLINE_ void *get_token() const { return token_; }


protected:
	void finish() {
		if (!ShadowRealmImpl::is_valid(id_)) {
			return;
		}

		v8::Isolate *isolate = env_->get_isolate();
		CrossWrapper::remove_flag_symbol(isolate);
		SymbolCrossUtils::clear_isolate(isolate);

		module_resolver_ = nullptr;
		if (*id_in_master_ != *internal::Index32::none()) {
			const std::shared_ptr<Environment> owner_env = Environment::_access(token_);
			if (owner_env) {
				owner_env->remove_shadow_env(id_in_master_);
			}
			id_in_master_ = internal::Index32::none();
		}

		if (env_.get()) {
			v8::Isolate *isolate = env_->get_isolate();
			{
				const v8::Isolate::Scope isolate_scope(isolate);
				const v8::HandleScope handle_scope(isolate);
				if (isolate->IsExecutionTerminating()) {
					JSB_SHADOW_REALM_LOG(Log, "shadow_realm is terminating %d", id_);
					return;
				}
				isolate->TerminateExecution();
			}

			env_->dispose();
			env_.reset();

			JSB_SHADOW_REALM_LOG(VeryVerbose, "ShadowRealm exited: %d", id_);
		}

		id_ = ShadowRealmID::none();
	}

	static bool _terminate(ShadowRealmID p_shadow_id) {
		MUTEX_LOCK_GUARD(lock_);

		ShadowRealmImpl *impl;
		if (get_shadow_realm_list().try_get_value(p_shadow_id, impl)) {
			impl->finish();

			get_shadow_realm_list().remove_at(p_shadow_id);
			jsb_check(!get_shadow_realm_list().is_valid_index(p_shadow_id));

			impl->id_ = ShadowRealmID::none();
			return true;
		}
		return false;
	}

	static v8::Local<v8::Value> _importValue(
			Environment *env, const ShadowRealmImpl *realm, const v8::Local<v8::String> &specifier, const v8::Local<v8::String> &value_name,
			String &r_error_msg) {
		// MUTEX_LOCK_GUARD(lock_);

		v8::Isolate *isolate = env->get_isolate();
		const v8::Local<v8::Context> context = env->get_context();

		const v8::HandleScope handle_scope(isolate);
		const v8::Isolate::Scope isolate_scope(isolate);
		const v8::Context::Scope context_scope(context);

		const String module_id = impl::Helper::to_string(isolate, specifier);

		v8::Isolate *guest_isolate = realm->env_->get_isolate();
		const v8::HandleScope handle_scope1(guest_isolate);
		const v8::Isolate::Scope isolate_scope1(guest_isolate);

		impl::TryCatch try_catch(guest_isolate);

		JavaScriptModule *module{ nullptr };
		if (realm->env_->load(module_id, &module) == OK) {
			const v8::Local<v8::Value> exports = module->exports.Get(guest_isolate);
			if (value_name->IsUndefined()) {
				return wrap_cross_env_value(env, guest_isolate, exports);
			} else {
				if (exports->IsObject()) {
					const v8::Local<v8::Context> guest_context = realm->env_->get_context();
					const v8::Context::Scope context_scope1(guest_context);

					const v8::Local<v8::Object> exports_obj = exports.As<v8::Object>();
					const v8::Local<v8::String> value_name_str = _transfer_string(guest_isolate, value_name.As<v8::String>(), isolate);

					v8::Local<v8::Value> result;
					if (exports_obj->Get(guest_context, value_name_str).ToLocal(&result)) {
						const v8::Local<v8::Value> wrapped_result = wrap_cross_env_value(env, guest_isolate, result);
						return wrapped_result;
					} else {
						r_error_msg = vformat("The exports object has not key: %s", impl::Helper::to_string(isolate, value_name));
					}
				} else {
					r_error_msg = "The variable `exports` is not an object.";
				}
			}
		} else {
			if (try_catch.has_caught()) {
				r_error_msg = BridgeHelper::get_exception(try_catch);

			} else {
				r_error_msg = vformat("load module failed: %s", module_id);
			}
		}
		return {};
	}

public:
	static void finalizer(Environment *p_env, void *pointer, FinalizationType /* p_finalize */) {
		ShadowRealmImpl *self = (ShadowRealmImpl *)pointer;
		if (ShadowRealmImpl::is_valid(self->id_)) {
			JSB_SHADOW_REALM_LOG(Debug, "shadow_realm is not explicitly terminated before garbage collected, will be terminated automatically.");
			self->finish();
		}
		JSB_SHADOW_REALM_LOG(VeryVerbose, "deleting ShadowRealm %d", self->id_);
		memdelete(self);
	}

	// -------

	static bool is_valid(ShadowRealmID p_id) {
		MUTEX_LOCK_GUARD(lock_);
		return get_shadow_realm_list().is_valid_index(p_id);
	}

	static void addAllowedModuleSearchPath(const v8::FunctionCallbackInfo<v8::Value> &info) {
		v8::Isolate *isolate = info.GetIsolate();

		const v8::Local<v8::Object> self = info.This();
		if (!TypeConvert::is_object(self, NativeClassType::Shadow)) {
			jsb_throw(isolate, "bad this: postMessage must be called on a TransferableShadowRealm instance");
			return;
		}

		const ShadowRealmImpl *realm = (ShadowRealmImpl *)self->GetAlignedPointerFromInternalField(IF_Pointer);
		if (realm == nullptr) {
			jsb_throw(isolate, "Call on an invalid shadow realm");
			return;
		}

		const v8::HandleScope handle_scope(isolate);
		const v8::Isolate::Scope isolate_scope(isolate);

		if (info.Length() <= 0 || !info[0]->IsString()) {
			jsb_throw(isolate, "bad argument: require a string.");
			return;
		}

		v8::Local<v8::String> source = info[0].As<v8::String>();
		if (source->Length() <= 0) {
			jsb_throw(isolate, "bad argument: empty string.");
			return;
		}

		if (realm->module_resolver_) {
			realm->module_resolver_->add_search_path(impl::Helper::to_string(isolate, source));
		} else {
			JSB_SHADOW_REALM_LOG(Error, "Execute `add_allowed_module_search_path()` failed: the realm is allow any module or the realm is invalid.");
		}
	}

	static void evaluate(const v8::FunctionCallbackInfo<v8::Value> &info) {
		// MUTEX_LOCK_GUARD(lock_);
		v8::Isolate *isolate = info.GetIsolate();

		const v8::Local<v8::Object> self = info.This();
		if (!TypeConvert::is_object(self, NativeClassType::Shadow)) {
			jsb_throw(isolate, "bad this: postMessage must be called on a TransferableShadowRealm instance");
			return;
		}

		const ShadowRealmImpl *realm = (ShadowRealmImpl *)self->GetAlignedPointerFromInternalField(IF_Pointer);
		if (realm == nullptr) {
			jsb_throw(isolate, "Call on an invalid shadow realm");
			return;
		}

		String wrapped_source;
		v8::Isolate *host_isolate = info.GetIsolate();
		{
			if (info.Length() <= 0 || !info[0]->IsString()) {
				jsb_throw(host_isolate, "bad argument: require a string.");
				return;
			}
			v8::Local<v8::String> source_text = info[0].As<v8::String>();

			const String source_str = impl::Helper::to_string(host_isolate, source_text);
			wrapped_source = jsb_format("(function() { return (%s); })()", source_str);
		}

		{
			v8::Isolate *guest_isolate = realm->env_->get_isolate();
			const v8::HandleScope handle_scope1(guest_isolate);
			const v8::Isolate::Scope isolate_scope1(guest_isolate);

			const v8::Local<v8::Context> guest_context = realm->env_->get_context();
			const v8::Context::Scope context_scope(guest_context);

			impl::TryCatch try_catch(guest_isolate);

			v8::Local<v8::String> func_source = impl::Helper::new_string(guest_isolate, wrapped_source);
			v8::Local<v8::Script> script = v8::Script::Compile(guest_context, func_source).ToLocalChecked();
			v8::Local<v8::Value> result = script->Run(guest_context).ToLocalChecked();

			if (try_catch.has_caught()) {
				jsb_throw(guest_isolate, BridgeHelper::get_exception(try_catch));
				return;
			}

			const v8::HandleScope handle_scope(host_isolate);
			const v8::Isolate::Scope isolate_scope(host_isolate); /** NOTE: 将在 host_env 中创建对象 */
			Environment *host_env = Environment::wrap(host_isolate);
			v8::Local<v8::Value> wrapped_result = wrap_cross_env_value(host_env, guest_isolate, result);

			info.GetReturnValue().Set(wrapped_result);
		}
	}

	static void importValue(const v8::FunctionCallbackInfo<v8::Value> &info) {
		// MUTEX_LOCK_GUARD(lock_);
		v8::Isolate *isolate = info.GetIsolate();

		const v8::Local<v8::Object> self = info.This();
		if (!TypeConvert::is_object(self, NativeClassType::Shadow)) {
			jsb_throw(isolate, "bad this: postMessage must be called on a TransferableShadowRealm instance");
			return;
		}

		const ShadowRealmImpl *realm = (ShadowRealmImpl *)self->GetAlignedPointerFromInternalField(IF_Pointer);
		if (realm == nullptr) {
			jsb_throw(isolate, "Call on an invalid shadow realm");
			return;
		}

		Environment *env = Environment::wrap(isolate);
		const v8::Local<v8::Context> context = env->get_context();

		const v8::HandleScope handle_scope(isolate);
		const v8::Isolate::Scope isolate_scope(isolate);
		const v8::Context::Scope context_scope(context);

		// 创建 Promise resolver
		v8::Local<v8::Promise::Resolver> resolver = v8::Promise::Resolver::New(context).ToLocalChecked();
		v8::Local<v8::Promise> promise = resolver->GetPromise();
		info.GetReturnValue().Set(promise);

		if (info.Length() <= 0 || !info[0]->IsString() || info[0].As<v8::String>()->Length() <= 0) {
			jsb_throw(isolate, "bad argument of index 1: require a not empty string.");
			resolver->Reject(context, v8::String::NewFromUtf8Literal(isolate, "bad argument of index 1: require a not empty string.")).Check();
			return;
		}
		v8::Local<v8::Value> value_name = v8::Undefined(isolate);
		if (info.Length() > 1) {
			if (!info[1]->IsString() || info[1].As<v8::String>()->Length() <= 0) {
				jsb_throw(isolate, "bad argument of index 2: require a not empty string.");
				resolver->Reject(context, v8::String::NewFromUtf8Literal(isolate, "bad argument of index 2: require a not empty string.")).Check();
				return;
			}
			value_name = info[1];
		}

		v8::Local<v8::String> specifier = info[0].As<v8::String>(); // ModuleID
		String err_msg;
		v8::Local<v8::Value> result = _importValue(env, realm, specifier, value_name.As<v8::String>(), err_msg);
		if (result.IsEmpty()) {
			v8::Isolate *guest_isolate = realm->env_->get_isolate();
			jsb_throw(guest_isolate, err_msg);
			resolver->Reject(context, impl::Helper::new_string(isolate, err_msg)).Check();
		} else {
			resolver->Resolve(context, result).Check();
		}
	}

	static void importValueSync(const v8::FunctionCallbackInfo<v8::Value> &info) {
		// MUTEX_LOCK_GUARD(lock_);
		v8::Isolate *isolate = info.GetIsolate();

		const v8::Local<v8::Object> self = info.This();
		if (!TypeConvert::is_object(self, NativeClassType::Shadow)) {
			jsb_throw(isolate, "bad this: postMessage must be called on a TransferableShadowRealm instance");
			return;
		}

		const ShadowRealmImpl *realm = (ShadowRealmImpl *)self->GetAlignedPointerFromInternalField(IF_Pointer);
		if (realm == nullptr) {
			jsb_throw(isolate, "Call on an invalid shadow realm");
			return;
		}

		Environment *env = Environment::wrap(isolate);
		const v8::Local<v8::Context> context = env->get_context();

		const v8::HandleScope handle_scope(isolate);
		const v8::Isolate::Scope isolate_scope(isolate);
		const v8::Context::Scope context_scope(context);

		if (info.Length() <= 0 || !info[0]->IsString() || info[0].As<v8::String>()->Length() <= 0) {
			jsb_throw(isolate, "bad argument of index 1: require a not empty string.");
			return;
		}
		v8::Local<v8::Value> value_name = v8::Undefined(isolate);
		if (info.Length() > 1) {
			if (!info[1]->IsString() || info[1].As<v8::String>()->Length() <= 0) {
				jsb_throw(isolate, "bad argument of index 2: require a not empty string.");
				return;
			}
			value_name = info[1];
		}

		v8::Local<v8::String> specifier = info[0].As<v8::String>(); // ModuleID
		String err_msg;
		v8::Local<v8::Value> result = _importValue(env, realm, specifier, value_name.As<v8::String>(), err_msg);
		if (result.IsEmpty()) {
			v8::Isolate *guest_isolate = realm->env_->get_isolate();
			jsb_throw(guest_isolate, err_msg);
		} else {
			info.GetReturnValue().Set(result);
		}
	}

	static void terminate(const v8::FunctionCallbackInfo<v8::Value> &info) {
		v8::Isolate *isolate = info.GetIsolate();
		const v8::Local<v8::Object> self = info.This();
		if (!TypeConvert::is_object(self, NativeClassType::Shadow)) {
			jsb_throw(isolate, "bad this: postMessage must be called on a TransferableShadowRealm instance");
			return;
		}

		const ShadowRealmImpl *realm = (ShadowRealmImpl *)self->GetAlignedPointerFromInternalField(IF_Pointer);
		if (realm == nullptr) {
			jsb_throw(isolate, "Can not terminate an invalid shadow realm");
			return;
		}
		const ShadowRealmID shadow_realm_id = realm->get_id();
		_terminate(shadow_realm_id);
	}

public:
	static void finish_all() {
		while (true) {
			MUTEX_LOCK_GUARD(lock_);

			const ShadowRealmID id = get_shadow_realm_list().get_first_index();
			if (!id) {
				break;
			}
			jsb_check(get_shadow_realm_list().is_valid_index(id));
			jsb_check(!get_shadow_realm_list().is_empty());
			ShadowRealmImpl *impl;
			get_shadow_realm_list().try_get_value(id, impl);

			if (impl) {
				impl->finish();
			}
		}
	}

public:
	static NativeClassInfoPtr register_class(Environment *p_env, v8::Isolate *p_isolate) {
		const StringName &class_name = jsb_string_name(ShadowRealm);
		const NativeClassID class_id = p_env->add_native_class(NativeClassType::Shadow, class_name);
		impl::ClassBuilder class_builder = impl::ClassBuilder::New<IF_ObjectFieldCount>(
			p_isolate, class_name, &constructor<ShadowRealmImpl>, *class_id);

		class_builder.Instance().Method("evaluate", &ShadowRealmImpl::evaluate);
		class_builder.Instance().Method("addAllowedModuleSearchPath", &ShadowRealmImpl::addAllowedModuleSearchPath);
		class_builder.Instance().Method("importValue", &ShadowRealmImpl::importValue);
		class_builder.Instance().Method("importValueSync", &ShadowRealmImpl::importValueSync);
		class_builder.Instance().Method("terminate", &ShadowRealmImpl::terminate);

		const NativeClassInfoPtr class_info = p_env->get_native_class(class_id);
		class_info->finalizer = &ShadowRealmImpl::finalizer;
		class_info->clazz = class_builder.Build();
		jsb_check(!class_info->clazz.IsEmpty());
		jsb_check(class_info->name == class_name);
		return class_info;
	}
};

internal::SArray<ShadowRealmImpl *, ShadowRealmID> &ShadowRealmImpl::get_shadow_realm_list() {
	static internal::SArray<ShadowRealmImpl *, ShadowRealmID> list;
	return list;
}
std::recursive_mutex ShadowRealmImpl::lock_;
#pragma endregion ShadownRealm

#pragma region ShadowRealmMessage
// A message from the master envrionment to a shadow realm.
// Contains the serialized V8 data and a side-channel list of Godot variants/objects being transferred.
struct ShadowRealmMessage {
public:
	ShadowRealmMessage() = delete;

	ShadowRealmMessage(const ShadowRealmMessage &) = delete;
	ShadowRealmMessage &operator=(const ShadowRealmMessage &) = delete;

	ShadowRealmMessage(ShadowRealmMessage &&) noexcept = default;
	ShadowRealmMessage &operator=(ShadowRealmMessage &&) noexcept = default;

	ShadowRealmMessage(Buffer &&p_data, std::vector<TransferData> &&p_transfers) :
			data(std::move(p_data)), transfers(std::move(p_transfers)) {
	}

	const Buffer &get_data() const { return data; }
	const std::vector<TransferData> &get_transfers() const { return transfers; }

private:
	Buffer data;
	std::vector<TransferData> transfers;
};
#pragma endregion ShadowRealmMessage

#pragma region TransferableShadowRealm
class TransferableShadowRealmImpl : public ShadowRealmImpl {
	v8::Global<v8::Object> context_obj_handle_;

protected:
	virtual void init_environment() override {
		v8::Isolate *isolate = env_->get_isolate();
		const v8::Isolate::Scope isolate_scope(isolate);
		const v8::HandleScope handle_scope(isolate);
		const v8::Local<v8::Context> context = env_->get_context();
		const v8::Context::Scope context_scope(context);

		ShadowRealmID id = get_id();

		// setup 'postMessage, onmessage etc.' for ShadowRealmParent
		JavaScriptModule *module = nullptr;
		jsb_ensuref(env_->load(JSB_SHADOW_REALM_MODULE_NAME, &module) == OK,
				"failed to load '%s' module in shadowRealm thread %d", JSB_SHADOW_REALM_MODULE_NAME, id);

		const v8::Local<v8::Object> context_obj = v8::Object::New(isolate);
		context_obj_handle_.Reset(isolate, context_obj);

		const v8::Local<v8::Value> exports_val = module->exports.Get(isolate);
		jsb_check(!exports_val.IsEmpty() && exports_val->IsObject() && !exports_val->IsNullOrUndefined());
		const v8::Local<v8::Object> exports = exports_val.As<v8::Object>();
		exports->Set(context, impl::Helper::new_string(isolate, "ShadowRealmParent"), context_obj).Check();

		context_obj->Set(context,
							jsb_name(env_, postMessage),
							v8::Function::New(context, &post_message_to_host, v8::Uint32::NewFromUnsigned(isolate, *id)).ToLocalChecked())
				.Check();
		context_obj->Set(context,
							jsb_name(env_, close),
							v8::Function::New(context, &close_from_shadow_realm, v8::Uint32::NewFromUnsigned(isolate, *id)).ToLocalChecked())
				.Check();
		context_obj->Set(context,
							jsb_name(env_, onmessage),
							v8::Null(isolate))
				.Check();
	}

public:
	TransferableShadowRealmImpl(Environment *p_master) : ShadowRealmImpl(p_master) {}
	~TransferableShadowRealmImpl() {
		JSB_SHADOW_REALM_LOG(VeryVerbose, "TransferableShadowRealm destroyed: %d", get_id());
	}

private:
	static bool try_get_shadow_env(ShadowRealmID p_id, NativeObjectID &r_handle, void *&r_token_ptr) {
		MUTEX_LOCK_GUARD(lock_);

		ShadowRealmImpl *impl;
		if (get_shadow_realm_list().try_get_value(p_id, impl)) {
			r_handle = impl->get_handle();
			r_token_ptr = impl->get_token();
		} else {
			r_handle = {};
			r_token_ptr = nullptr;
		}

		return (bool)r_handle;
	}

	static std::pair<uint8_t *, size_t> handle_post_message(const v8::FunctionCallbackInfo<v8::Value> &info, internal::ReferentialVariantMap<TransferData> &transfers) {
		v8::Isolate *isolate = info.GetIsolate();
		const v8::Isolate::Scope isolate_scope(isolate);
		const v8::HandleScope handle_scope(isolate);

		const v8::Local<v8::Context> context = isolate->GetCurrentContext();
		const v8::Context::Scope context_scope(context);

		Environment *from_env = Environment::wrap(isolate);
		from_env->check_internal_state();

		if (info.Length() == 0) {
			jsb_throw(isolate, "postMessage requires at least 1 argument");
			return { nullptr, 0 };
		}

		if (info.Length() > 1 && !info[1]->IsUndefined()) {
			v8::Local<v8::Value> transfer_arg = info[1];

			if (!transfer_arg->IsArray() && !transfer_arg->IsObject()) {
				jsb_throw(isolate, "transfer list must be an array");
				return { nullptr, 0 };
			}

			if (transfer_arg->IsArray()) {
				v8::Local<v8::Array> transfer_array = transfer_arg.As<v8::Array>();

				for (uint32_t i = 0, len = transfer_array->Length(); i < len; i++) {
					v8::Local<v8::Value> item = transfer_array->Get(context, i).ToLocalChecked();

					if (!item->IsObject()) {
						// JS primitive, no underling Variant exists to transfer. Since JS primitives are automatically
						// coerced to variants, it's more consistent if we permit (but ignore) them.
						continue;
					}

					Variant variant;

					if (!TypeConvert::js_to_gd_var(isolate, context, item.As<v8::Object>(), variant)) {
						jsb_throw(isolate, "transfer list must contain Godot object/variant types only");
						return { nullptr, 0 };
					}

					TransferData transfer_data;
					from_env->prepare_transfer_out(NativeObjectID::none(), transfers.size(), variant, transfer_data);
					transfers.insert(variant, transfer_data);
				}
			} else {
				Variant transfer_var;

				if (!TypeConvert::js_to_gd_var(isolate, context, transfer_arg.As<v8::Object>(), Variant::Type::ARRAY, transfer_var)) {
					jsb_throw(isolate, "transfer list must be an array");
					return std::pair<uint8_t *, size_t>();
				}

				if (transfer_var.get_type() != Variant::ARRAY) {
					jsb_throw(isolate, "transfer list must be an array");
					return std::pair<uint8_t *, size_t>();
				}

				Array transfer_arr = transfer_var;

				for (int i = 0, size = transfer_arr.size(); i < size; i++) {
					Variant &variant = transfer_arr[i];
					TransferData transfer_data;
					from_env->prepare_transfer_out(NativeObjectID::none(), i, variant, transfer_data);
					transfers.insert(variant, transfer_data);
				}
			}
		}

		Vector<TransferData> transferred;

		// TODO: Transfer support non-V8.
#if JSB_WITH_V8
		Serialization::VariantSerializerDelegate delegate(from_env, transfers);
		v8::ValueSerializer serializer(isolate, &delegate);
		delegate.SetSerializer(&serializer);
#else
		v8::ValueSerializer serializer(isolate);
#endif

		serializer.WriteHeader();
		v8::Maybe<bool> write_result = serializer.WriteValue(context, info[0]);

		if (write_result.IsNothing()) {
			return { nullptr, 0 };
		}

		for (const auto &entry : transfers) {
			from_env->finalize_transfer_out(entry.value);
		}

		return serializer.Release();
	}

	// handle message from master
	void _on_message(const ShadowRealmMessage &p_message) {
		v8::Isolate *isolate = env_->get_isolate();
		const v8::Isolate::Scope isolate_scope(isolate);
		const v8::HandleScope handle_scope(isolate);
		const v8::Local<v8::Context> context = env_->get_context();
		const v8::Context::Scope context_scope(context);
		const v8::Local<v8::Object> context_obj = context_obj_handle_.Get(isolate);

		v8::Local<v8::Value> callback;
		if (!context_obj->Get(context, jsb_name(env_, onmessage)).ToLocal(&callback) || !callback->IsFunction()) {
			JSB_SHADOW_REALM_LOG(Error, "onmessage is not a function");
			return;
		}

		Environment *env = env_.get();

		if (p_message.get_transfers().size() > 0) {
			const std::shared_ptr<Environment> owner_env = Environment::_access(get_token());

			if (!owner_env) {
				JSB_SHADOW_REALM_LOG(Error, "failed to access shadow_realm owner environment from shadow_realm_env onmessage");
				return;
			}

			for (const auto &transfer : p_message.get_transfers()) {
				v8::HandleScope transfer_bind_scope(isolate);
				env->transfer_in_bind(context, transfer);
			}

			for (const auto &transfer : p_message.get_transfers()) {
				v8::HandleScope transfer_state_scope(isolate);
				env->transfer_in_apply_state(transfer);
			}
		}

#if JSB_WITH_V8
		Serialization::VariantDeserializerDelegate delegate(env, p_message.get_transfers());
		v8::ValueDeserializer deserializer(isolate, p_message.get_data().ptr(), p_message.get_data().size(), &delegate);
		delegate.SetSerializer(&deserializer);
#else
		v8::ValueDeserializer deserializer(isolate, p_message.get_data().ptr(), p_message.get_data().size());
#endif

		bool ok;
		if (!deserializer.ReadHeader(context).To(&ok) || !ok) {
			JSB_SHADOW_REALM_LOG(Error, "failed to parse message header");
			return;
		}
		v8::Local<v8::Value> value;

		{
			Environment::ExecutionDeferredScope defer(env);

			if (!deserializer.ReadValue(context).ToLocal(&value)) {
				JSB_SHADOW_REALM_LOG(Error, "failed to parse message value");
				return;
			}
		}

		const impl::TryCatch try_catch(isolate);
		const v8::Local<v8::Function> call = callback.As<v8::Function>();
		const v8::MaybeLocal<v8::Value> rval = call->Call(context, v8::Undefined(isolate), 1, &value);
		jsb_unused(rval);
		if (try_catch.has_caught()) {
			JSB_SHADOW_REALM_LOG(Error, "%s", BridgeHelper::get_exception(try_catch));
		}
	}

	static void on_receive(ShadowRealmID p_id, ShadowRealmMessage &&p_message) {
		ShadowRealmImpl *impl;
		{
			MUTEX_LOCK_GUARD(lock_);
			if (!get_shadow_realm_list().try_get_value(p_id, impl)) {
				JSB_SHADOW_REALM_LOG(Error, "can't post message to a dead shadowRealm (%d)", p_id);
				return;
			}
		}

		if (!ShadowRealmImpl::is_valid(p_id)) {
			JSB_SHADOW_REALM_LOG(Error, "Can't post message to a dead shadowRealm (%d)", p_id);
			return;
		}

		TransferableShadowRealmImpl *tf_impl = static_cast<TransferableShadowRealmImpl *>(impl);

		tf_impl->_on_message(p_message);
	}

private:
	// transferableShadowRealm.close()
	static void close_from_shadow_realm(const v8::FunctionCallbackInfo<v8::Value> &info) {
		v8::Isolate *isolate = info.GetIsolate();
		const v8::HandleScope handle_scope(isolate);
		const v8::Isolate::Scope isolate_scope(isolate);
		const ShadowRealmID shadow_realm_id = (ShadowRealmID)info.Data().As<v8::Uint32>()->Value();
		ShadowRealmImpl::_terminate(shadow_realm_id);
	}

	// transferableShadowRealm -> master (run in shadowRealm env)
	static void post_message_to_host(const v8::FunctionCallbackInfo<v8::Value> &info) {
		v8::Isolate *isolate = info.GetIsolate();
		const v8::HandleScope handle_scope(isolate);
		const v8::Isolate::Scope isolate_scope(isolate);

		const ShadowRealmID shadow_realm_id = (ShadowRealmID)info.Data().As<v8::Uint32>()->Value();

		NativeObjectID handle;
		void *token_ptr = nullptr;
		if (!TransferableShadowRealmImpl::try_get_shadow_env(shadow_realm_id, handle, token_ptr)) {
			jsb_throw(isolate, "invalid shadowRealm id");
			return;
		}

		jsb_check(handle);
		const std::shared_ptr<Environment> master = Environment::_access(token_ptr);
		if (!master) {
			jsb_throw(isolate, "invalid environment");
			return;
		}

		internal::ReferentialVariantMap<TransferData> transfer_map;
		const std::pair<uint8_t *, size_t> data = TransferableShadowRealmImpl::handle_post_message(info, transfer_map);

		if (data.first) {
			std::vector<TransferData> transfers;
			transfers.reserve(transfer_map.size());

			for (const auto &transfer : transfer_map) {
				transfers.push_back(transfer.value);
			}

			master->handle_message(Message(Message::TYPE_MESSAGE, handle, Buffer::steal(data.first, data.second), std::move(transfers)));
		}
	}

public:
	// master.postMessage
	static void post_message(const v8::FunctionCallbackInfo<v8::Value> &info) {
		v8::Isolate *isolate = info.GetIsolate();

		const v8::Local<v8::Object> self = info.This();
		if (!TypeConvert::is_object(self, NativeClassType::Shadow)) {
			jsb_throw(isolate, "bad this: postMessage must be called on a TransferableShadowRealm instance");
			return;
		}

		const ShadowRealmImpl *realm = (ShadowRealmImpl *)self->GetAlignedPointerFromInternalField(IF_Pointer);
		if (realm == nullptr) {
			jsb_throw(info.GetIsolate(), "Call on an invalid shadow realm");
			return;
		}

		if (!ShadowRealmImpl::is_valid(realm->get_id())) {
			jsb_throw(isolate, "TransferableShadowRealm is not running");
			return;
		}

		const v8::HandleScope handle_scope(isolate);
		const v8::Isolate::Scope isolate_scope(isolate);

		internal::ReferentialVariantMap<TransferData> transfer_map;
		const std::pair<uint8_t *, size_t> data = TransferableShadowRealmImpl::handle_post_message(info, transfer_map);

		if (data.first) {
			std::vector<TransferData> transfers;
			transfers.reserve(transfer_map.size());

			for (const auto &transfer : transfer_map) {
				transfers.push_back(transfer.value);
			}

			TransferableShadowRealmImpl::on_receive(realm->get_id(), ShadowRealmMessage(Buffer::steal(data.first, data.second), std::move(transfers)));
		}
	}

	static NativeClassInfoPtr register_class(Environment *p_env, v8::Isolate *p_isolate, const NativeClassInfoPtr p_base_class_info) {
		const StringName &class_name = jsb_string_name(TransferableShadowRealm);
		const NativeClassID class_id = p_env->add_native_class(NativeClassType::Shadow, class_name);
		impl::ClassBuilder class_builder = impl::ClassBuilder::New<IF_ObjectFieldCount>(
			p_isolate, class_name, &constructor<TransferableShadowRealmImpl>, *class_id);

		class_builder.Instance().Method("postMessage", &TransferableShadowRealmImpl::post_message);
		class_builder.Instance().Method("onerror", &_placeholder);
		class_builder.Instance().Method("onmessage", &_placeholder);

		class_builder.Inherit(p_base_class_info->clazz);

		const NativeClassInfoPtr class_info = p_env->get_native_class(class_id);
		class_info->finalizer = &ShadowRealmImpl::finalizer;
		class_info->clazz = class_builder.Build();
		jsb_check(!class_info->clazz.IsEmpty());
		jsb_check(class_info->name == class_name);
		return class_info;
	}
};
#pragma endregion TransferableShadowRealm

#pragma region TransferableShadowRealm
class ShadowRealmModuleLoader : public IModuleLoader {
public:
	virtual ~ShadowRealmModuleLoader() override = default;

	virtual bool load(Environment *p_env, JavaScriptModule &p_module) override {
		v8::Isolate *isolate = p_env->get_isolate();
		const v8::Isolate::Scope isolate_scope(isolate);
		const v8::HandleScope handle_scope(isolate);

		const v8::Local<v8::Context> context = p_env->get_context();
		const v8::Context::Scope context_scope(context);

		const v8::Local<v8::Object> exports = v8::Object::New(isolate);
		p_module.exports.Reset(isolate, exports);

		//	Can not load `ShadowRealm` and `TransferableShadowRealm`in shadown environment.
		if (!p_env->is_shadow()) {
			const NativeClassInfoPtr shadow_realm_class_info = ShadowRealmImpl::register_class(p_env, isolate);
			exports->Set(context, jsb_name(p_env, ShadowRealm), shadow_realm_class_info->clazz.Get(isolate)).Check();

			const NativeClassInfoPtr transferable_shadow_realm_class_info = TransferableShadowRealmImpl::register_class(p_env, isolate, shadow_realm_class_info);
			exports->Set(context, jsb_name(p_env, TransferableShadowRealm),transferable_shadow_realm_class_info->clazz.Get(isolate)).Check();
		}
		return true;
	}
};
#pragma endregion TransferableShadowRealm

#pragma region ShadowRealm
void ShadowRealm::finish_all() {
	ShadowRealmImpl::finish_all();
	SymbolCrossUtils::clean();
}

void ShadowRealm::register_(const v8::Local<v8::Context> &p_context, const v8::Local<v8::Object> &p_self) {
	// v8::Context::Scope context_scope(p_context);

	Environment *env = Environment::wrap(p_context);
	env->add_module_loader<ShadowRealmModuleLoader>(JSB_SHADOW_REALM_MODULE_NAME);

	FunctionCrossWrapper::register_class(env);
	ObjectCrossWrapper::register_class(env);
}
#pragma endregion ShadowRealm

} //namespace jsb