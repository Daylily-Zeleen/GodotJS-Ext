/*
 *  test-global-class-extract.mts
 *
 *  This file is part of:
 *                                GodotJS-Ext
 *              https://github.com/Daylily-Zeleen/GodotJS-Ext
 *
 *  Copyright (c) 2026-present 忘忧の (Daylily-Zeleen)                  *
 *                 - Contact: daylily-zeleen@foxmail.com                *
 *
 *  This library is free software; you can redistribute it and/or       *
 *  modify it under the terms of the GNU Lesser General Public          *
 *  License as published by the Free Software Foundation; either        *
 *  version 2.1 of the License, or (at your option) any later version.  *
 *
 *  This library is distributed in the hope that it will be useful,     *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of      *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU    *
 *  Lesser General Public License for more details.                     *
 *
 *  You should have received a copy of the GNU Lesser General Public    *
 *  License along with this library; if not,                            *
 *  see <https://www.gnu.org/licenses/>.                                *
 */

/**
 * `collectGlobalClassFromText` 的回归测试。
 *
 * 覆盖 C++ 侧正则原先支持的三种形态，外加正则做不到的几处（跨行 extends、
 * 字符串/注释里的假文本、装饰器字面量 icon、abstract）。
 *
 * 跑法：`cd scripts/jsb.tools && node --experimental-strip-types test/test-global-class-extract.mts`
 * （需先 `pnpm build` 出 `scripts/out/jsb.global.class.extract.cjs`）。
 */

import extract from "../../out/jsb.global.class.extract.cjs";

const { collectGlobalClassFromText } = extract as {
	collectGlobalClassFromText: (p_fileName: string, p_source: string) => {
		class: string;
		base_type: string;
		icon_path: string;
		is_tool: boolean;
		is_abstract: boolean;
	} | undefined;
};

let checks = 0;
let failures = 0;

function check(p_condition: boolean, p_message: string): void {
	checks++;
	if (!p_condition) {
		failures++;
		console.error("[FAIL] " + p_message);
	}
}

interface Expectation {
	name: string;
	fileName: string;
	source: string;
	/** undefined = 期望解析不出（没有 default 导出 / 没有 extends）。 */
	expected?: { class: string; base_type: string; icon_path: string; is_tool: boolean; is_abstract: boolean };
}

const expectations: Expectation[] = [
	{
		name: "ts: export default class extends",
		fileName: "C.ts",
		source: `import { Node } from "godot";
export default class C extends Node {}
`,
		expected: { name: "C", base_type: "Node", icon_path: "", is_tool: false, is_abstract: false },
	},
	{
		name: "ts: cross-line extends (regex could not)",
		fileName: "C.ts",
		source: `import { Node } from "godot";
export default class C
	extends Node {}
`,
		expected: { name: "C", base_type: "Node", icon_path: "", is_tool: false, is_abstract: false },
	},
	{
		name: "ts: icon + tool decorators",
		fileName: "C.ts",
		source: `import { Node } from "godot";
@bind.icon("res://icon/affiliate.svg")
@bind.tool()
export default class C extends Node {}
`,
		expected: { name: "C", base_type: "Node", icon_path: "res://icon/affiliate.svg", is_tool: true, is_abstract: false },
	},
	{
		name: "ts: abstract class",
		fileName: "C.ts",
		source: `import { Node } from "godot";
export default abstract class C extends Node {}
`,
		expected: { name: "C", base_type: "Node", icon_path: "", is_tool: false, is_abstract: true },
	},
	{
		name: "ts: icon with a variable argument yields no icon_path",
		fileName: "C.ts",
		source: `import { Node } from "godot";
const p = "res://x.svg";
@bind.icon(p)
export default class C extends Node {}
`,
		expected: { name: "C", base_type: "Node", icon_path: "", is_tool: false, is_abstract: false },
	},
	{
		name: "ts: class text inside a string is not picked up",
		fileName: "C.ts",
		source: `import { Node } from "godot";
const decoy = "export default class Fake extends Other {}";
export default class C extends Node {}
`,
		expected: { name: "C", base_type: "Node", icon_path: "", is_tool: false, is_abstract: false },
	},
	{
		name: "ts: not default-exported => nothing",
		fileName: "C.ts",
		source: `import { Node } from "godot";
class C extends Node {}
`,
		expected: undefined,
	},
	{
		name: "ts: no extends => nothing",
		fileName: "C.ts",
		source: `export default class C {}
`,
		expected: undefined,
	},
	{
		name: "js: exports.default = class X extends Y",
		fileName: "C.js",
		source: `const gd = require("godot");
exports.default = class C extends gd.Node {};
`,
		expected: { name: "C", base_type: "gd.Node", icon_path: "", is_tool: false, is_abstract: false },
	},
	{
		name: "js: class X + exports.default = X",
		fileName: "C.js",
		source: `const gd = require("godot");
class C extends gd.Node {}
exports.default = C;
`,
		expected: { name: "C", base_type: "gd.Node", icon_path: "", is_tool: false, is_abstract: false },
	},
	{
		name: "js: no exports.default => nothing",
		fileName: "C.js",
		source: `const gd = require("godot");
class C extends gd.Node {}
`,
		expected: undefined,
	},
];

for (const expectation of expectations) {
	const actual = collectGlobalClassFromText(expectation.fileName, expectation.source);
	if (expectation.expected === undefined) {
		check(actual === undefined, expectation.name + ": expected no result, got " + JSON.stringify(actual));
		continue;
	}
	if (actual === undefined) {
		check(false, expectation.name + ": expected a result, got undefined");
		continue;
	}
	check(actual.name === expectation.expected.name, expectation.name + ": name = " + actual.name);
	check(actual.base_type === expectation.expected.base_type, expectation.name + ": base_type = " + actual.base_type);
	check(actual.icon_path === expectation.expected.icon_path, expectation.name + ": icon_path = " + actual.icon_path);
	check(actual.is_tool === expectation.expected.is_tool, expectation.name + ": is_tool = " + String(actual.is_tool));
	check(actual.is_abstract === expectation.expected.is_abstract, expectation.name + ": is_abstract = " + String(actual.is_abstract));
}

console.log("[global-class-extract] checks=" + checks + " failures=" + failures);
process.exit(failures === 0 ? 0 : 1);
