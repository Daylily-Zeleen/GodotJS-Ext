# 编辑器图标（`.gdextension` 的 `[icons]`）

> 适用范围：`project/addons/godotjs-ext.daylily-zeleen/icons/*.svg`、
> `.gdextension` 的 `[icons]` 段、`misc/release/package.py` 的图标搬运。
> 发布侧的搬运与校验见 [release-packaging.md](./release-packaging.md)。

## 引擎侧机制

`[icons]` 由引擎读取（`core/extension/gdextension_library_loader.cpp:419-429`）：每个键写入
`class_icon_paths`，值 = 图标路径**相对 `.gdextension` 解析**（`p_path.get_base_dir().path_join`）。
`GodotJSScript = "icons/GodotJSScript.svg"` 就是脚本类图标的来源。

因为路径是相对的且指向 `bin/` 之外，**打包时必须把图标文件一起带上**——否则包内声明的路径
指向不存在的文件（这一点已纳入 `assemble` 的 1:1 校验）。

## 图标必须是纯路径（踩过，2026-09-29）

**Godot 用 ThorVG 光栅化 SVG，`<text>` 元素不会被渲染。** 实测：带 `<text>` 的候选图标经
`Image.load_svg_from_string()` 渲染后**只剩外框，字全没了**——而 Godot 自带的 `TextFile.svg`
之所以有字母，是因为它的字母是**路径**（`<path>`），不是文本节点。

所以：**只用 `<path>` / `<rect>` / `<circle>` 等几何元素**；要画字形就自己画成路径
（矩形拼像素字最稳）。ThorVG 的 `<text>` 支持不完整，别赌。

## 尺寸与颜色

- **16×16** 是标准（Godot 自己的 `editor/icons/*.svg` 全是 16×16；引擎里
  `class_icon_size = 16 * EDSCALE`）。Godot 会用矢量重采样到编辑器缩放，所以只需保证
  **16px 下清晰**，18px（1.125× 缩放）会自动受益。
- **颜色只用 Godot 约定色**：`#e0e0e0`（`editor/themes/editor_color_map.cpp` 里注释为
  "Common icon color"，浅色主题下映射为 `#5a5a5a`）。**不要**用品牌色（如 TS 的 `#3178c6`）：
  一是与其它类图标不一致，二是导入选项 `Convert Colors with Editor Theme` 只能映射约定色，
  非约定色在浅色主题下可能对比度不足。

## 可辨识性：靠实测，不靠眼睛

16px 下的成败必须**真的渲染出来看**，不能凭 SVG 源码想象。做法（本仓已验证可行）：

```gdscript
# 用 Godot 自己的光栅化器，而不是第三方渲染器（差异会骗人）
var img := Image.new()
img.load_svg_from_string(FileAccess.get_file_as_string("res://icon.svg"), 1.0)
for y in img.get_height():
    for x in img.get_width():
        print_rich(...)  # 把 alpha 打到字符画里
```

注意：

- `Image.load_svg_from_string` 读的是文件，**`res://` 下的文件要先让 Godot 见过**；
  测试脚本可直接用**绝对路径**（`DirAccess` / `FileAccess` 都接受），省掉导入这一步。
- **不要用 Chromium 的 SVG 渲染当依据**：它对 `<text>`、字距、抗锯齿的处理与 ThorVG 不同，
  会让「有字」的图标看起来没问题，实际在编辑器里是空的。

## 本仓的 `GodotJSScript.svg`

圆角外框 + 像素字 `JS`（4×7 字形，整数对齐，`stroke-width` 0.25 补粗）。选这个形态的原因：

- 与 Godot 的脚本类图标同族（`Script.svg` 是方框、`TextFile.svg` 是齿轮+字母、`GDScript.svg`
  是齿轮+圆点），加个框能一眼归入「脚本/文件」类，而不像纯字母那样容易被误认成文字标签。
- 字母放**框内**、不做齿轮：齿轮的镂空区只有 ~5px，塞两个字母必然糊成一团（实测过）。
- 纯路径、无 `<text>`；颜色只有 `#e0e0e0`。

改这个图标时**必须重新渲染核对** 16px 与 18px：字母是整数对齐的，`x` 偏移差 0.5px 就会让
某一列落到像素边界上、整行变淡。
