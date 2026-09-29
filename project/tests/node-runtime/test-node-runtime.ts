import { Node } from "godot";
import { reportTestFailure } from "../test-status";

// Node-leg integration test — modelled on Gode's `test/fixtures/npm_native_llama`
// (see .agent_tmp/gode): it forks a probe child and asserts the child's
// process.execPath IS the bundled helper executable, then forks a second probe
// from inside node_modules to prove npm-resolved paths work too.
//
// Why this leg needs its own test: an embedded libnode reports the *Godot*
// executable as process.execPath, so a naive fork() re-spawns Godot itself.
// jsb_node_runtime.cpp redirects fork() to the bundled `godotjs-ext` helper;
// nothing covered that before (the generic C++/TS suites resolve no node
// builtins and never fork).
//
// NOTE: node builtins must be loaded through globalThis.__godotjs_node_require
// (jsb_node_runtime.cpp exposes it). The global `require` here is the GodotJS
// bridge require (Builtins::_require -> Environment::_load_module), which only
// resolves GodotJS modules and crashes on ids it does not know.

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

interface GodeRequire {
	(id: string): unknown;
	resolve(id: string): string;
}

interface NodeBridgeGlobal {
	__godotjs_node_require?: GodeRequire;
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

function nodeRequire(): GodeRequire {
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
// Mirrors Gode's runForkProbe: any of timeout / error / early exit / an execPath
// that is not the bundled helper is a hard failure.
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
			try {
				child.send({ type: "exit" });
			} catch (e) {
				/* the child may already be gone; the execPath is what matters */
			}
			finish(() => resolve(execPath));
		});
		child.on("error", (error) => finish(() => reject(error)));
		child.on("exit", (code) =>
			finish(() => reject(new Error("fork probe exited before reporting execPath: " + String(code) + (stderrText ? "\n" + stderrText : "")))));
	});
}

export default class NodeRuntimeTest extends Node {
	async _ready(): Promise<void> {
		try {
			if (!isNodeLeg()) {
				console.log("[node-runtime] skipped: not a node build");
				return;
			}

			await this._testNextTick();
			this._testNodeBuiltins();
			await this._testForkHelper();
			await this._testNodeModulesFork();
		} catch (error) {
			fail(error instanceof Error ? error.stack || error.message : String(error));
		} finally {
			const root = this.get_tree().root;
			if (root) {
				root.call_deferred("remove_child", this);
			}
			this.queue_free();
		}
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

	// Gode's first probe: the child must be the bundled helper, not Godot.
	private async _testForkHelper(): Promise<void> {
		const cp = asChildProcess(nodeRequire()("child_process"));
		if (!cp) {
			fail("child_process.fork is not available");
			return;
		}
		const execPath = await forkExecPath(cp, "res://tests/node-runtime/fork-probe-child.cjs");
		console.log("[node-runtime] fork helper OK: " + execPath);
	}

	// Gode's second probe: a fork target resolved out of node_modules must work too
	// (it exercises res:// -> OS path translation for paths outside res://).
	private async _testNodeModulesFork(): Promise<void> {
		const require_ = nodeRequire();
		const cp = asChildProcess(require_("child_process"));
		if (!cp) {
			fail("child_process.fork is not available");
			return;
		}
		const packageMain = require_.resolve("typescript");
		// packageMain is the package entry; fork the directory's package.json-relative
		// probe the same way Gode forks testBindingBinary.js inside node_modules.
		const probePath = packageMain.replace(/\\/g, "/").replace(/\/lib\/typescript\.js$/, "/package.json");
		const execPath = await forkExecPath(cp, probePath, { GODE_NPM_PROBE: "1" });
		console.log("[node-runtime] node_modules fork OK: " + execPath);
	}
}
