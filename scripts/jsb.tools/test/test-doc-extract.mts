/*
 *  test-doc-extract.mts
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
 * 注释关联规则的回归测试（用户 2026-09-28 拍板，见 design.md §6）。
 *
 * 这些规则**只能**在源码文本上验证：注释写在装饰器之后时 `node.jsDoc` 为空，
 * 因此必须直接调 `collectScriptDocFromText`，而不是走引擎。
 *
 * 跑法：`cd scripts/jsb.tools && node --experimental-strip-types test/test-doc-extract.mts`
 * （需先 `pnpm build` 出 `scripts/out/jsb.doc.extract.cjs`）。
 */

// 产物是 CommonJS：具名导出拿不到（`MemberDoc` 只是类型，值导出用默认导入再解构）。
import docExtract from "../../out/jsb.doc.extract.cjs";
import type { MemberDoc } from "../../out/jsb.doc.extract.cjs";

const { collectScriptDocFromText } = docExtract as {
	collectScriptDocFromText: (p_fileName: string, p_source: string) => { class: { brief: string; description: string }; members: MemberDoc[] } | undefined;
};

interface Expectation {
	name: string;
	source: string;
	/** 期望的类级 brief（`undefined` = 必须没有类文档）。 */
	classBrief?: string;
	/** 期望的成员文档，键为成员名。 */
	members: Record<string, { kind: string; brief: string }>;
	/** 必须**不存在**的成员名。 */
	absent?: string[];
}

const expectations: Expectation[] = [
	{
		name: "doc above the decorator",
		source: `import { Node } from "godot";
/** Class brief. */
@bind()
export default class C extends Node {
	/** Above decorator. */
	@bind.exposed.const()
	static readonly A = 1;
}
`,
		classBrief: "Class brief.",
		members: { A: { kind: "constant", brief: "Above decorator." } },
	},
	{
		name: "doc between the decorator and the member",
		source: `import { Node } from "godot";
export default class C extends Node {
	@bind.exposed.const()
	/** Between decorator and member. */
	static readonly B = 2;
}
`,
		members: { B: { kind: "constant", brief: "Between decorator and member." } },
	},
	{
		name: "a blank line detaches the comment",
		source: `import { Node } from "godot";
export default class C extends Node {
	/** Detached by a blank line. */

	plain(): void {}
}
`,
		members: {},
		absent: ["plain"],
	},
	{
		name: "an orphan comment above another comment is not attached",
		source: `import { Node } from "godot";
export default class C extends Node {
	/** Orphan. */

	/** Real doc. */
	kept(): void {}
}
`,
		members: { kept: { kind: "method", brief: "Real doc." } },
	},
	{
		name: "member kinds: method, getter, signal and property",
		source: `import { Node } from "godot";
export default class C extends Node {
	/** M. */
	method(): void {}

	/** G. */
	get prop(): number { return 1; }

	/** S. */
	@bind.signal()
	accessor changed!: Signal<() => void>;

	/** P. */
	@bind.export(Variant.Type.TYPE_INT)
	accessor value: number = 0;
}
`,
		members: {
			method: { kind: "method", brief: "M." },
			prop: { kind: "method", brief: "G." },
			changed: { kind: "signal", brief: "S." },
			value: { kind: "property", brief: "P." },
		},
	},
	{
		name: "first line is the brief, full text is the description",
		source: `import { Node } from "godot";
/**
 * Brief line.
 *
 * Body paragraph.
 */
export default class C extends Node {}
`,
		classBrief: "Brief line.",
		members: {},
	},
];

let failures = 0;
let checks = 0;

function check(condition: boolean, message: string): void {
	++checks;
	if (!condition) {
		++failures;
		console.error("FAIL: " + message);
	}
}

for (const expectation of expectations) {
	const doc = collectScriptDocFromText("fixture.ts", expectation.source);
	check(doc !== undefined, expectation.name + ": no script doc was produced");
	if (doc === undefined) {
		continue;
	}
	if (expectation.classBrief !== undefined) {
		check(doc.class.brief === expectation.classBrief,
			expectation.name + ": class brief = " + JSON.stringify(doc.class.brief) + ", expected " + JSON.stringify(expectation.classBrief));
	}

	const byName = new Map<string, MemberDoc>();
	for (const member of doc.members) {
		byName.set(member.name, member);
	}
	for (const [name, expected] of Object.entries(expectation.members)) {
		const member = byName.get(name);
		check(member !== undefined, expectation.name + ": member '" + name + "' is missing");
		if (member === undefined) {
			continue;
		}
		check(member.kind === expected.kind, expectation.name + ": member '" + name + "' kind = " + member.kind + ", expected " + expected.kind);
		check(member.brief === expected.brief, expectation.name + ": member '" + name + "' brief = " + JSON.stringify(member.brief) + ", expected " + JSON.stringify(expected.brief));
	}
	for (const name of expectation.absent ?? []) {
		check(!byName.has(name), expectation.name + ": member '" + name + "' must NOT carry a doc");
	}
}

// 类文档的分段：首行 = brief，全文 = description（含空行保留）。
{
	const doc = collectScriptDocFromText("fixture.ts", `import { Node } from "godot";
/**
 * Brief line.
 *
 * Body paragraph.
 */
export default class C extends Node {}
`);
	check(doc !== undefined && doc.class.brief === "Brief line.", "splitBrief: brief must be the first line");
	check(doc !== undefined && doc.class.description.includes("Body paragraph."), "splitBrief: description must keep the body");
	check(doc !== undefined && doc.class.description.includes("\n\n"), "splitBrief: description must keep the blank line");
}

console.log("[doc-extract] checks=" + checks + " failures=" + failures);
process.exit(failures === 0 ? 0 : 1);
