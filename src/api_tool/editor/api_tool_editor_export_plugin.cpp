#include "api_tool_editor_export_plugin.h"
#include "../api_tool_types.h"
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

// api_tool_editor_export_plugin.cpp
// 此文件仅提供文档说明，实际使用时直接继承 godot::EditorExportPlugin
//
// 示例用法：
//
// class MyExportPlugin : public godot::EditorExportPlugin {
//     GDCLASS(MyExportPlugin, godot::EditorExportPlugin);
//
// protected:
//     static void _bind_methods() {}
//
// public:
//     bool _export_file(const String &p_path, const String &p_type, const PackedStringArray &p_features) override {
//         // 跳过 documents 目录
//         if (p_path.contains("/" + String(DIR_DOCUMENTS) + "/")) {
//             return false;
//         }
//         return true;
//     }
// };
//
// 在编辑器初始化时注册：
//     auto plugin = memnew(MyExportPlugin);
//     add_export_plugin(plugin);

namespace api_tool {

} // namespace api_tool
