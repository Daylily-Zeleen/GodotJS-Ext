/*
 *  jsb.ts.ast.cts
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
 *                                                                      *
 *  You should have received a copy of the GNU Lesser General Public    *
 *  License along with this library; if not,                            *
 *  see <https://www.gnu.org/licenses/>.                                *
 */

/**
 * `typescript` AST 的公共只读原语。
 *
 * 三个提取器（`jsb.signature.extract` / `jsb.doc.extract` / `jsb.global.class.extract`）
 * 共用同一套"怎么建源文件、怎么找类、怎么认装饰器"的动作，此前各自抄了一份。
 *
 * 本模块刻意**只收语义一致的部分**：`parseSource` / `decoratorsOf` / `decoratorName` /
 * `localClassesOf` / `importedFromOf` / `extendsClauseText` 等，两侧用法逐字相同。
 *
 * **`default` 导出的三种形态不在此合并**：各提取器接受的形态本来不同
 * （见 `findDefaultClassByModifier` 等三个函数各自的说明），合并成"全能版"会单方面放宽
 * 判据，改变哪些文件产出结果。故这里提供三个**正交**的形态查找函数，由调用方自行组合。
 */

import * as ts from "typescript";

/** 单个类的声明或表达式（`export default class X {}` 与 `… = class X {}` 都要处理）。 */
export type AnyClass = ts.ClassDeclaration | ts.ClassExpression;

/**
 * 后缀 → ScriptKind。
 *
 * `.js` / `.cjs` / `.mjs` 是 JavaScript 源码（`exports.default = …` 形态、无类型标注）；
 * 其余（`.ts` 等）走 TypeScript。**不要**按文件里有没有 `:` 之类猜。
 */
export function scriptKindOfPath(p_fileName: string): ts.ScriptKind {
	const isJavaScript = p_fileName.endsWith(".js") || p_fileName.endsWith(".cjs") || p_fileName.endsWith(".mjs");
	return isJavaScript ? ts.ScriptKind.JS : ts.ScriptKind.TS;
}

/** 解析源码文本。`p_fileName` 只用于诊断信息与 ScriptKind 推断。 */
export function parseSource(p_fileName: string, p_source: string): ts.SourceFile {
	return ts.createSourceFile(p_fileName, p_source, ts.ScriptTarget.ES2022, true, scriptKindOfPath(p_fileName));
}

/** 节点上挂的装饰器；不支持装饰器的节点返回空数组。 */
export function decoratorsOf(p_node: ts.Node): readonly ts.Decorator[] {
	return ts.canHaveDecorators(p_node) ? (ts.getDecorators(p_node) ?? []) : [];
}

/** 修饰符（`static` / `abstract` / `default` / …）；没有则空数组。 */
export function modifiersOf(p_node: ts.Node): readonly ts.ModifierLike[] {
	return (p_node as ts.HasModifiers).modifiers ?? [];
}

/** 节点是否带 `abstract` 修饰符。 */
export function isAbstract(p_node: ts.Node): boolean {
	return (ts.getCombinedModifierFlags(p_node as ts.Declaration) & ts.ModifierFlags.Abstract) !== 0;
}

/**
 * 装饰器表达式的被调用者：`@icon("x")` / `@bind.icon("x")` / `@icon` 都归一成标识符或属性名。
 * 不是标识符/属性访问（例如 `@f()()`）返回 undefined。
 */
function decoratorCallee(p_decorator: ts.Decorator): ts.Identifier | ts.PropertyAccessExpression | undefined {
	const expression = p_decorator.expression;
	const callee = ts.isCallExpression(expression) ? expression.expression : expression;
	if (ts.isIdentifier(callee) || ts.isPropertyAccessExpression(callee)) {
		return callee;
	}
	return undefined;
}

/** 装饰器短名（`@bind.icon` → `icon`，`@icon` → `icon`）；`@f()()` 等形态返回 undefined。 */
export function decoratorName(p_decorator: ts.Decorator): string | undefined {
	const callee = decoratorCallee(p_decorator);
	if (callee === undefined) {
		return undefined;
	}
	return ts.isIdentifier(callee) ? callee.text : callee.name.text;
}

/** 装饰器调用的第一个字符串字面量实参；不是字面量（变量/表达式）则返回 `""`。 */
export function firstStringLiteralArg(p_decorator: ts.Decorator): string {
	const expression = p_decorator.expression;
	if (!ts.isCallExpression(expression) || expression.arguments.length === 0) {
		return "";
	}
	const first = expression.arguments[0];
	return ts.isStringLiteral(first) ? first.text : "";
}

/** 形态一：类声明自带 `default` 修饰符 —— `export default class X extends Y`。 */
export function findDefaultClassByModifier(p_sourceFile: ts.SourceFile): ts.ClassDeclaration | undefined {
	for (const statement of p_sourceFile.statements) {
		if (ts.isClassDeclaration(statement) && statement.name
			&& (ts.getCombinedModifierFlags(statement) & ts.ModifierFlags.Default) !== 0) {
			return statement;
		}
	}
	return undefined;
}

/** 形态二：`export default <标识符>` 的标识符名（声明在不在本文件里由调用方自行判定）。 */
export function findExportAssignmentName(p_sourceFile: ts.SourceFile): string | undefined {
	for (const statement of p_sourceFile.statements) {
		if (ts.isExportAssignment(statement) && !statement.isExportEquals && ts.isIdentifier(statement.expression)) {
			return statement.expression.text;
		}
	}
	return undefined;
}

/**
 * 形态三：CommonJS 的 `exports.default = …`（`<x>.default = <右侧>`，左侧必须是 `.default`
 * 属性访问）。右侧是类表达式时直接返回它；是标识符时记下名字，随后在本文件里找同名类声明。
 */
export function findCommonJsDefaultClass(p_sourceFile: ts.SourceFile): AnyClass | undefined {
	const defaultNames = new Set<string>();
	let inlineClass: ts.ClassExpression | undefined;
	for (const statement of p_sourceFile.statements) {
		if (!ts.isExpressionStatement(statement)) {
			continue;
		}
		const expression = statement.expression;
		if (!ts.isBinaryExpression(expression) || expression.operatorToken.kind !== ts.SyntaxKind.EqualsToken) {
			continue;
		}
		const left = expression.left;
		if (!ts.isPropertyAccessExpression(left) || left.name.text !== "default") {
			continue;
		}
		if (ts.isClassExpression(expression.right)) {
			inlineClass = expression.right;
		} else if (ts.isIdentifier(expression.right)) {
			defaultNames.add(expression.right.text);
		}
	}
	if (inlineClass && inlineClass.name) {
		return inlineClass;
	}
	for (const statement of p_sourceFile.statements) {
		if (ts.isClassDeclaration(statement) && statement.name && defaultNames.has(statement.name.text)) {
			return statement;
		}
	}
	return undefined;
}

/**
 * `extends` 之后的**标识符**名。
 *
 * `extends Mixin(Base)` 这类表达式返回 undefined —— 调用方据此判"未知"而不是"不是"。
 * 需要任意表达式文本（如写给人看的 `base_type`）时用 `extendsClauseText`。
 */
export function findBaseIdentifierName(p_class: AnyClass): string | undefined {
	const clause = p_class.heritageClauses?.find((c) => c.token === ts.SyntaxKind.ExtendsKeyword);
	const type = clause?.types[0];
	if (!type) {
		return undefined;
	}
	return ts.isIdentifier(type.expression) ? type.expression.text : undefined;
}

/** `extends` 后面那段的源码文本；没有 `extends` 时返回 `""`。 */
export function extendsClauseText(p_class: AnyClass): string {
	for (const clause of p_class.heritageClauses ?? []) {
		if (clause.token === ts.SyntaxKind.ExtendsKeyword && clause.types.length > 0) {
			return clause.types[0].expression.getText();
		}
	}
	return "";
}

/** 本文件内的类声明表（按名索引），供跨"标识符 → 声明"的判定使用。 */
export function localClassesOf(p_sourceFile: ts.SourceFile): Map<string, ts.ClassDeclaration> {
	const classes = new Map<string, ts.ClassDeclaration>();
	for (const statement of p_sourceFile.statements) {
		if (ts.isClassDeclaration(statement) && statement.name) {
			classes.set(statement.name.text, statement);
		}
	}
	return classes;
}

/** 标识符 → 它从哪个模块导入（`import { Node } from "godot"` ⇒ `Node` → `godot`）。 */
export function importedFromOf(p_sourceFile: ts.SourceFile): Map<string, string> {
	const imported = new Map<string, string>();
	for (const statement of p_sourceFile.statements) {
		if (!ts.isImportDeclaration(statement) || !statement.importClause
			|| !ts.isStringLiteral(statement.moduleSpecifier)) {
			continue;
		}
		const moduleName = statement.moduleSpecifier.text;
		const clause = statement.importClause;
		if (clause.name) {
			imported.set(clause.name.text, moduleName);
		}
		const bindings = clause.namedBindings;
		if (bindings && ts.isNamedImports(bindings)) {
			for (const element of bindings.elements) {
				imported.set(element.name.text, moduleName);
			}
		}
	}
	return imported;
}
