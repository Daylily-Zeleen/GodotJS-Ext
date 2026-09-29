# 评估：`settings::project` 的 `get_*_wildcards()` 是否应改为返回 `Vector<String>`

结论：**应当改**，但理由不是「`Vector<String>` 更快」这么笼统——真正的理由是
**这些 getter 在逐节点递归里被反复调用**，每次都要付一次 `GLOBAL_GET` + 一次
Variant→`PackedStringArray` 转换。修法是**把取配置提到循环外**，而不是改返回类型。

## 事实

### 1. 定义（`src/editor/jsb_editor_settings.cpp:198-212`）

```cpp
PackedStringArray get_resource_dts_include_path_wildcards() {
	return (PackedStringArray)GLOBAL_GET(kEdResourceDTSIncludePathWildcards);
}
```

四个同形：`get_{resource,scene}_dts_{include,exclude}_path_wildcards()`。

`GLOBAL_GET(m_var)` = `ProjectSettings::get_singleton()->get_setting_with_override(m_var)`
（`src/compat/project_settings.h:33`），godot-cpp 侧返回 **`Variant`**
（`third/godot-cpp/gen/include/godot_cpp/classes/project_settings.hpp:62`）。
所以每次调用 = 一次引擎方法调用（取 Variant）+ 一次 `(PackedStringArray)` 转换，
后者是 `from_variant_constructor` 的一次函数指针调用
（`third/godot-cpp/gen/src/variant/packed_string_array.cpp:151-153`）+ 一次整块
字符串数组的拷贝。

### 2. 调用点（全仓 2 处）

| 位置 | 上下文 | 频率 |
|---|---|---|
| `src/editor/codegen/jsb_codegen_scene_descriptors.cpp:167-168` | `_build_node_type_descriptor()` 内部 | **逐节点递归**（`:131` 自递归；入口 `:318`/`:394`）|
| `src/editor/weaver-editor/jsb_editor_plugin.cpp:1151-1152`、`:1198-1199` | `generate_scene_nodes_types` / `generate_resource_types` 函数体，只调一次 | 每次生成 1 次 |

即：**第一处是问题所在**。对一棵有 N 个节点的场景，`get_scene_dts_*_wildcards()`
被调用 **2N 次**，每次返回一个**内容完全相同**的数组——全部是浪费。

### 3. 返回类型本身不是瓶颈

`PackedStringArray` 与 `Vector<String>` 的差别在这里**不是主要成本**。这两个 getter
一次返回整块，调用方拿到后只做「遍历 + `String::match`」（`is_path_matchn`），
**没有逐元素 API 调用**——所以即使保持 `PackedStringArray`，把调用提到循环外，
2N 次就降到 2 次，收益远大于改类型的收益。

（对比上一轮 G3 回退的那批：那里是**逐元素 push/filter**，`PackedStringArray` 的逐元素
API 每个元素一次函数指针调用，所以类型本身是瓶颈；这里不是。）

### 4. 改成 `Vector<String>` 的代价

`is_path_matchn` 现在收 `const PackedStringArray &`（两处重载：
`jsb_editor_plugin.cpp:895`、`jsb_codegen_scene_descriptors.cpp:407`）。若 getter 改返回
`Vector<String>`：
- 调用点本来就要「遍历」，`Vector<String>` 遍历是纯 C++，**没有**转换成本；
- 但 `get_*_wildcards()` 内部仍要做一次 Variant→容器转换（`ProjectSettings` 里存的就是
  PackedStringArray），所以**转换次数不变**，只省掉「转换后那一次容器拷贝」中的一层
  （`Vector<String>` 也仍需从 Variant 拿内容——godot-cpp 没有 `Variant→Vector<String>`
  的直接转换，得先转 `PackedStringArray` 再逐元素拷进 `Vector<String>`）⇒
  **反而多一次逐元素拷贝**。

所以：**改返回类型是净负收益**（多一次逐元素拷贝），真正该做的是**提循环外**。

## 建议的改法（未实施，待确认）

在 `_build_node_type_descriptor` 里把两行移到函数**入口之前**——该函数是递归的，
提到函数内顶部没用（每次递归仍会执行），要提到**递归入口之上**，即
`get_resource_type_descriptor`（`:290`）与 `get_scene_nodes`（`:366`）里，
再用参数传进去；或用一个惰性缓存（`static` + 失效点）。

考虑到这两个 wildcards 是「编辑器设置、生成期间不会变」，最省事的等价写法是
**在 `jsb_codegen_scene_descriptors.cpp` 里加一个文件级惰性缓存**，或在生成器
（`SceneTSDGenerator`）构造时取一次、存成员、递归时读成员。后者更干净——
与 `SceneTSDGenerator` 已有的 `scene_paths_` 形态一致（`jsb_codegen_generator.h:93`）。

**待确认**：wildcards 是否需要支持「设置改了不重启引擎也生效」。若需要，
文件级 `static` 缓存要在设置变更时清（`jsb_editor_settings.cpp` 有设置变更的处理点吗？待查）；
若不需要（生成期间不变即可），构造时取一次最合适。
