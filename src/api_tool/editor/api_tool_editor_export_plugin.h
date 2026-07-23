
#pragma once

// api_tool_editor_export_plugin.h
// 导出插件基类。
// 由语言绑定继承并注册，用于控制 .api_dumping/ 文件的导出行为。
// - documents/ 默认不导出
// - classes/ 根据裁剪预设导出
// - utility_functions/builtin_classes/constants 可通过编译标志控制
//
// 注意：此文件仅提供文档说明，实际实现应直接继承 godot::EditorExportPlugin

#include <godot_cpp/classes/editor_export_plugin.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

// TODO: 你妈的臭傻逼

namespace api_tool {

// ApiToolEditorExportPlugin 是一个文档占位符，实际使用时直接继承 godot::EditorExportPlugin
// 并实现 should_export_class、should_export_builtin_class、should_export_utility_function 方法

} // namespace api_tool
