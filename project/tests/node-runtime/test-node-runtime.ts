import { Node, ProjectSettings } from "godot";
import { reportTestFailure } from "../test-status";

// Node-leg integration test — modelled on Gode's `test/fixtures/npm_native_llama`
// (see .agent_tmp/gode): it forks a probe child and asserts the child's
// process.execPath IS the bundled helper executable.
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

// The bootstrap exposes only the call form: `globalThis.__godotjs_node_require =
// function(id) { return require(id); }` (jsb_node_runtime.cpp). There is NO
// `resolve` on it, so do not declare one -- a declared-but-missing member fails
// at runtime with "require_.resolve is not a function".
interface GodeRequire {
	(id: string): unknown;
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
			// Do NOT reply: the probe child exits right after process.send(), so
			// writing back hits a closed IPC channel. On Linux that raises EPIPE,
			// which by default terminates the whole process with SIGPIPE (exit 13)
			// -- the run died before this assertion could even be reported.
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
		} catch (error) {
			fail(error instanceof Error ? error.stack || error.message : String(error));
		}
		// Do NOT remove/free ourselves: start.ts owns remove_child + queue_free for
		// every scene it loads. Doing it here too double-frees the node and the
		// trailing call_deferred fails with "Object::call_deferred. Bad this".
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
		// Fork an OS absolute path, not a res:// one: the redirect translates
		// res:// through process.cwd(), and under CI the cwd is the repository root
		// while res:// is the project directory -- the translation then points at a
		// file that does not exist and fork exits 127. globalize_path is exact.
		const probePath = ProjectSettings.globalize_path("res://tests/node-runtime/fork-probe-child.cjs");
		const execPath = await forkExecPath(cp, probePath);
		console.log("[node-runtime] fork helper OK: " + execPath);
	}

	// Gode's second probe: a fork target resolved out of node_modules must work too
	// (it exercises res:// -> OS path translation for paths outside res://).
	// Gode's fixture has a second probe that forks a native-module probe script
	// resolved out of node_modules (testBindingBinary.js). We have no equivalent
	// here: this project ships no native addon to probe, and forking some
	// unrelated third-party script would assert nothing about the fork redirect --
	// it cannot report the node-fork-probe message our assertion reads. So this
	// leg is covered by the single probe above rather than a second one invented
	// to mirror Gode's shape.
	// (A second probe becomes meaningful once a real .node dependency exists.)
}
