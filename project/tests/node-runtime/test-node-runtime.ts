// uid://dit5h0tvym0yw This line is generated, don't modify or remove it.
import { Node, ProjectSettings } from "godot";
import { beginAsyncTest, endAsyncTest, reportTestFailure } from "../test-status";

// node 腿集成测试：fork 探针子进程，断言其 process.execPath 是随包发布的 helper 可执行文件。
//
// 这条腿需要自己的测试：内嵌 libnode 会把 *Godot* 可执行文件报成 process.execPath，
// 于是朴素的 fork() 会重新拉起 Godot 自己。jsb_node_runtime.cpp 把 fork() 重定向到随包的
// `godotjs-ext` helper；在此之前没有任何测试覆盖它。
//
// NOTE: node 内建模块必须经 globalThis.__godotjs_node_require 加载（由 jsb_node_runtime.cpp
// 暴露）。这里的全局 `require` 是 GodotJS 的桥接 require
// （Builtins::_require -> Environment::_load_module），只认 GodotJS 模块，遇到未知 id 会崩。

interface ForkProbeMessage {
	type?: string;
	execPath?: string;
}

interface ForkChild {
	on(event: "message", listener: (message: ForkProbeMessage) => void): void;
	on(event: "error", listener: (error: Error) => void): void;
	on(event: "exit", listener: (code: number | null) => void): void;
	send(message: { type: string }): boolean;
	kill(): void;
	stdout: { on(event: "data", listener: (chunk: unknown) => void): void } | null;
	stderr: { on(event: "data", listener: (chunk: unknown) => void): void } | null;
}

interface ChildProcessModule {
	fork(modulePath: string, args: string[], options: { stdio: string[]; env?: Record<string, string | undefined> }): ForkChild;
}

// native-addon-probe.cjs 写入 `.native-addon-probe.json` 的内容。
interface NativeAddonProbeResult {
	ok: boolean;
	addonType?: string;
	error?: string;
	addonPath?: string;
}

// bootstrap 只暴露调用形式：`globalThis.__godotjs_node_require = function(id){ return require(id) }`
// （jsb_node_runtime.cpp）。它**没有** `resolve`，所以不要声明 —— 声明了但运行时不存在会报
// "require_.resolve is not a function"。
interface NodeRequireBridge {
	(id: string): unknown;
}

interface NodeBridgeGlobal {
	__godotjs_node_require?: NodeRequireBridge;
}

declare const process: {
	platform: string;
	nextTick: (cb: () => void) => void;
	execPath: string;
	env: Record<string, string | undefined>;
};

const FORK_PROBE_TIMEOUT_MS = 15000;

type TimeoutHandle = ReturnType<typeof setTimeout>;

// The bundled helper the fork redirect must select (jsb_node_runtime.cpp looks
// for `godotjs-ext` next to the module; jsb_node_bridge.cpp resolves it).
const HELPER_BASENAME_PATTERN = /godotjs-ext(\.exe)?$/i;

function fail(message: string): void {
	console.error("[node-runtime] " + message);
	reportTestFailure("[node-runtime] " + message);
}

function isNodeLeg(): boolean {
	return typeof process !== "undefined" && typeof process.nextTick === "function";
}

function nodeRequire(): NodeRequireBridge {
	const bridgeRequire = (globalThis as unknown as NodeBridgeGlobal).__godotjs_node_require;
	if (typeof bridgeRequire === "function") {
		return bridgeRequire;
	}
	throw new Error("the node require bridge (__godotjs_node_require) is missing");
}

function asChildProcess(value: unknown): ChildProcessModule | null {
	if (value && typeof value === "object" && typeof (value as ChildProcessModule).fork === "function") {
		return value as ChildProcessModule;
	}
	return null;
}

// Fork `modulePath` and resolve with the execPath the child reports over IPC.
// 超时 / 出错 / 提前退出 / execPath 不是随包 helper，任一即判失败。
function forkExecPath(cp: ChildProcessModule, modulePath: string, extraEnv?: Record<string, string | undefined>): Promise<string> {
	return new Promise<string>((resolve, reject) => {
		let settled = false;
		let timer: TimeoutHandle | null = null;
		const finish = (callback: () => void) => {
			if (!settled) {
				settled = true;
				if (timer !== null) {
					clearTimeout(timer);
				}
				callback();
			}
		};
		const child = cp.fork(modulePath, [], {
			stdio: ["ignore", "pipe", "pipe", "ipc"],
			...(extraEnv ? { env: { ...process.env, ...extraEnv } } : {}),
		});
		let stderrText = "";
		child.stderr?.on("data", (chunk) => {
			stderrText += String(chunk);
		});
		timer = setTimeout(() => {
			try {
				child.kill();
			} catch (e) {
				/* already gone */
			}
			finish(() => reject(new Error("fork probe timed out without reporting an execPath")));
		}, FORK_PROBE_TIMEOUT_MS);

		child.on("message", (message) => {
			const execPath = message.execPath;
			if (message.type !== "node-fork-probe" || typeof execPath !== "string") {
				return;
			}
			if (!HELPER_BASENAME_PATTERN.test(execPath.replace(/\\/g, "/"))) {
				finish(() => reject(new Error("fork did not use the bundled godotjs-ext helper: " + execPath)));
				return;
			}
			// 不要回写：探针子进程在 process.send() 后立即退出，回写会打到已关闭的 IPC 通道。
			// Linux 上会抛 EPIPE，默认让整个进程以 SIGPIPE 终止（exit 13）—— 断言都来不及报出来。
			finish(() => resolve(execPath));
		});
		child.on("error", (error) => finish(() => reject(error)));
		child.on("exit", (code) =>
			finish(() => reject(new Error("fork probe exited before reporting execPath: " + String(code) + (stderrText ? "\n" + stderrText : "")))));
	});
}

// fork 原生 addon 探针，读它写出的结果文件。用文件而非 IPC：fork 重定向会改写参数，
// 不保证 IPC 通道可用。
function forkNativeAddonProbe(cp: ChildProcessModule, probePath: string, resultPath: string): Promise<NativeAddonProbeResult> {
	return new Promise<NativeAddonProbeResult>((resolve, reject) => {
		const child = cp.fork(probePath, [], { stdio: ["ignore", "pipe", "pipe", "ipc"] });
		let settled = false;
		let stderr = "";
		let timer: TimeoutHandle | null = null;
		const finish = (callback: () => void) => {
			if (settled) {
				return;
			}
			settled = true;
			if (timer !== null) {
				clearTimeout(timer);
			}
			callback();
		};
		timer = setTimeout(() => {
			child.kill();
			finish(() => reject(new Error("native addon probe timed out")));
		}, FORK_PROBE_TIMEOUT_MS);
		child.stderr?.on("data", (chunk: unknown) => {
			stderr += String(chunk);
		});
		child.on("error", (error: Error) => finish(() => reject(error)));
		child.on("exit", (code: number | null) => {
			finish(() => {
				const raw = readTextFile(resultPath);
				if (raw === null) {
					reject(new Error("probe wrote no result (exit " + String(code) + ")" + (stderr ? "\n" + stderr : "")));
					return;
				}
				try {
					resolve(JSON.parse(raw) as NativeAddonProbeResult);
				} catch (error) {
					reject(new Error("probe result is unreadable: " + String(error)));
				}
			});
		});
	});
}

function readTextFile(path: string): string | null {
	try {
		const fs = nodeRequire()("node:fs");
		if (fs !== null && typeof fs === "object" && "readFileSync" in fs && typeof fs.readFileSync === "function") {
			return String(fs.readFileSync(path, "utf8"));
		}
	} catch (_) {}
	return null;
}

export default class NodeRuntimeTest extends Node {
	// start.ts 只对带 completeCallback 的场景等这个回调，否则 100ms 后就直接 free。
	// fork 探针要等子进程（原生 addon 那条更慢），必然超过 100ms：测试的异步续体会活到
	// 引擎 quit 之后，在那里再碰 ProjectSettings 已经拿不到对象（"Bad this"），于是
	// GODOTJS_TEST_PROJECT_COMPLETED 之后又补一条 FAILED。
	public completeCallback: (() => unknown) | null = null;

	async _ready(): Promise<void> {
		beginAsyncTest();
		try {
			if (!isNodeLeg()) {
				console.log("[node-runtime] skipped: not a node build");
				return;
			}

			await this._testNextTick();
			this._testNodeBuiltins();
			await this._testForkHelper();
			await this._testNativeAddon();
		} catch (error) {
			fail(error instanceof Error ? error.stack || error.message : String(error));
		} finally {
			endAsyncTest();
			this.completeCallback?.();
		}
		// 不要自行 remove/free：start.ts 负责它加载的每个场景的 remove_child + queue_free。
		// 这里再做一次会双重释放该节点，随后的 call_deferred 报 "Object::call_deferred. Bad this"。
	}

	private async _testNextTick(): Promise<void> {
		const order: string[] = [];
		process.nextTick(() => order.push("nextTick"));
		queueMicrotask(() => order.push("microtask"));
		await Promise.resolve();
		await new Promise<void>((resolve) => setTimeout(resolve, 0));
		if (!order.includes("nextTick")) {
			fail("process.nextTick did not run");
		}
		if (!order.includes("microtask")) {
			fail("queueMicrotask did not run");
		}
	}

	private _testNodeBuiltins(): void {
		const pathModule = nodeRequire()("node:path") as { join?: unknown } | null;
		if (!pathModule || typeof pathModule.join !== "function") {
			fail("require('node:path') did not resolve to the node builtin");
		}
	}

	// 子进程必须是随包 helper，而不是 Godot 自己。
	private async _testForkHelper(): Promise<void> {
		const cp = asChildProcess(nodeRequire()("child_process"));
		if (!cp) {
			fail("child_process.fork is not available");
			return;
		}
		// 传 OS 绝对路径而非 res://：重定向按 process.cwd() 翻译 res://，而 CI 下 cwd 是仓库根、
		// res:// 是工程目录，翻译出来指向不存在的文件，fork 会以 127 退出。globalize_path 是精确的。
		const probePath = ProjectSettings.globalize_path("res://tests/node-runtime/fork-probe-child.cjs");
		const execPath = await forkExecPath(cp, probePath);
		console.log("[node-runtime] fork helper OK: " + execPath);
	}

	// 加载 npm 包自带的原生模块（`.node`）：在 forked helper（真 node 进程）里加载
	// node-llama-cpp 的 llama.cpp addon。
	//
	// 这是 node module 通路，其它测试都不覆盖。它也是要求**宿主可执行文件**导出 napi_* 的场景：
	// 该 addon 运行时在宿主上取 N-API 符号（GetProcAddress(GetModuleHandle(NULL), ...)），
	// 所以 helper exe 带导出表（见 SConstruct）。
	private async _testNativeAddon(): Promise<void> {
		const cp = asChildProcess(nodeRequire()("child_process"));
		if (!cp) {
			fail("child_process.fork is not available");
			return;
		}
		const probePath = ProjectSettings.globalize_path("res://tests/node-runtime/native-addon-probe.cjs");
		const resultPath = ProjectSettings.globalize_path("res://.native-addon-probe.json");
		const result = await forkNativeAddonProbe(cp, probePath, resultPath);
		if (!result.ok) {
			fail("the native addon did not load: " + String(result.error));
			return;
		}
		console.log("[node-runtime] native addon OK (" + String(result.addonType) + ")");
	}
}
