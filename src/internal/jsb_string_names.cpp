#include "jsb_string_names.h"
namespace jsb::internal {
StringNames *StringNames::singleton_ = nullptr;

StringNames::StringNames() {
#pragma push_macro("DEF")
#undef DEF
#define DEF(KeyName) sn_##KeyName = StringName(#KeyName);
#include "jsb_string_names.def.h"
#pragma pop_macro("DEF")
	sn_godot_typeloader = StringName("godot.typeloader");
	sn_godot_postbind = StringName("_post_bind_");

	ignored_.insert(sn_name);

	add_replacement("Dictionary", "GDictionary");
	add_replacement("Array", "GArray");
}

} //namespace jsb::internal
