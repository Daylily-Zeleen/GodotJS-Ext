#include "jsb_resource_saver.h"
#include "jsb_script.h"
#include "jsb_script_language.h"

#include <godot_cpp/classes/resource_uid.hpp>
#include <godot_cpp/classes/resource_saver.hpp>
#include <godot_cpp/classes/resource_loader.hpp>

#define UID_COMMENT_PREFIX "// uid://"
#define UID_COMMENT_SUFFIX "This line is generated, don't modify or remove it."
static int64_t extract_uid_from_line(const String &p_line) {
	PackedStringArray splits = p_line.strip_edges().substr(3).split(" ", false, 1);
	if (splits.is_empty()) {
		return -1;
	}
	return ResourceUID::get_singleton()->text_to_id(splits[0]);
}

bool ResourceFormatSaverGodotJSScript::add_uid_to_source(String &p_r_source, const String &p_path, int64_t p_uid) const {
	bool need_update = false;
	PackedStringArray lines = p_r_source.split("\n");
	bool uid_comment_valid = false;
	for (int32_t i = 0; i < lines.size(); i++) {
		const String &line = lines[i].strip_edges();
		if (line.begins_with(UID_COMMENT_PREFIX)) {
			int64_t uid = extract_uid_from_line(line);
			if (uid == ResourceUID::INVALID_ID || p_uid != uid) {
				if (p_uid == ResourceUID::INVALID_ID) {
					p_uid = ResourceSaver::get_singleton()->get_resource_id_for_path(p_path, true);
				}

				if (uid != p_uid) {
					lines.set(i, vformat("// %s %s", ResourceUID::get_singleton()->id_to_text(uid), UID_COMMENT_SUFFIX));
					if (ResourceUID::get_singleton()->has_id(uid)) {
						ResourceUID::get_singleton()->set_id(uid, p_path);
					} else {
						ResourceUID::get_singleton()->add_id(uid, p_path);
					}
					need_update = true;
				}
			}

			uid_comment_valid = true;
			break;
		} else if (!line.strip_edges().is_empty()) {
			break;
		}
	}

	if (!uid_comment_valid) {
		int64_t uid = p_uid == ResourceUID::INVALID_ID ? ResourceSaver::get_singleton()->get_resource_id_for_path(p_path, true) : p_uid;
		lines.insert(0, vformat("// %s %s", ResourceUID::get_singleton()->id_to_text(uid), UID_COMMENT_SUFFIX));
		if (ResourceUID::get_singleton()->has_id(uid)) {
			ResourceUID::get_singleton()->set_id(uid, p_path);
		} else {
			ResourceUID::get_singleton()->add_id(uid, p_path);
		}
		need_update = true;
	}

	if (need_update) {
		p_r_source = String("\n").join(lines);
		return true;
	} else {
		return false;
	}
}

// @seealso: gdscript.cpp ResourceFormatSaverGDScript::save
Error ResourceFormatSaverGodotJSScript::_save(const Ref<Resource>& p_resource, const String& p_path, uint32_t p_flags)
{
    const Ref<GodotJSScript> sqscr = p_resource;
    ERR_FAIL_COND_V(sqscr.is_null(), ERR_INVALID_PARAMETER);

    const Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE);
    if (file.is_null())
    {
        Error err = FileAccess::get_open_error();
        JSB_LOG(Error, "Cannot save %s file '%s'.", jsb_typename(GodotJSScript), p_path);
        return err;
    }

    String source = sqscr->get_source_code();
    bool source_changed = false;
    if (!FileAccess::file_exists(p_path + String(".uid"))) {
		source_changed = add_uid_to_source(source, p_path);
    }

    file->store_string(sqscr->get_source_code());
    if (file->get_error() != OK && file->get_error() != ERR_FILE_EOF)
    {
        return ERR_CANT_CREATE;
    }
    file->close();

    if (source_changed) {
        sqscr->_set_source_code(source);
        sqscr->_reload(true);
        sqscr->emit_changed();
    }

    // TODO: Reload scripts on save (equivalent to ScriptServer::is_reload_scripts_on_save_enabled())
    {
        // WTF??
        GodotJSScriptLanguage::get_singleton()->_reload_tool_script(p_resource, true);
    }

    return OK;
}

PackedStringArray ResourceFormatSaverGodotJSScript::_get_recognized_extensions(const Ref<Resource>& p_resource) const
{
    if (Object::cast_to<GodotJSScript>(*p_resource))
    {
        PackedStringArray exts;
#if JSB_USE_TYPESCRIPT
        exts.push_back(JSB_TYPESCRIPT_EXT);
#endif
        exts.push_back(JSB_JAVASCRIPT_EXT);
        exts.push_back(JSB_COMMONJS_EXT);
        exts.push_back(JSB_MODULE_EXT);
        return exts;
    }
    return PackedStringArray();
}

bool ResourceFormatSaverGodotJSScript::_recognize(const Ref<Resource>& p_resource) const
{
    return Object::cast_to<GodotJSScript>(*p_resource) != nullptr;
}

Error ResourceFormatSaverGodotJSScript::_set_uid(const String &p_path, int64_t p_uid)
{
    if (FileAccess::file_exists(p_path + String(".uid"))) {
		Ref<FileAccess> f = FileAccess::open(p_path + String(".uid"), FileAccess::WRITE);
		if (f.is_valid()) {
			f->store_line(ResourceUID::get_singleton()->id_to_text(p_uid));
			return OK;
		} else {
			return FileAccess::get_open_error();
		}
	} else if (p_path.get_extension().to_lower() == "ts") {
		String source = FileAccess::get_file_as_string(p_path);
		Error err = FileAccess::get_open_error();
		if (err != OK) {
			return err;
		}

		const bool source_changed = add_uid_to_source(source, p_path, p_uid);
		if (source_changed) {
			Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::WRITE);
			if (f.is_valid()) {
				f->store_string(source);
				f->close();

				// Update source code if it's loaded.
				Ref<Resource> loaded_res = ResourceLoader::get_singleton()->load(p_path, "", ResourceLoader::CACHE_MODE_REUSE);
				Ref<GodotJSScript> loaded_script = loaded_res;
				if (loaded_script.is_valid()) {
					loaded_script->_set_source_code(source);
					loaded_script->_reload(true);
					loaded_script->emit_changed();
				}

				err = OK;
			} else {
				err = FileAccess::get_open_error();
			}
		}

		return err;
	}

	return ERR_FILE_UNRECOGNIZED;
}
