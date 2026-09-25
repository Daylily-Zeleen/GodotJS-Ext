import { GArray, GDictionary, Node, ResourceLoader, Script } from "godot";
import { JSWorker } from "godot.worker";
import { beginAsyncTest, endAsyncTest, reportTestFailure } from "../test-status";
import StaticMembersDerived from "./static-members-derived";
import StaticMembersNamespaced from "./static-members-namespaced";
import StaticMembersTarget from "./static-members-target";

// Coverage for `@bind.exposed.const()` / `@bind.exposed.shared()`:
//  - constants are parsed off the annotated static members, with the R2.3 Variant whitelist
//    rejecting the shapes that merely *convert* (Vector2 / Packed* / Object / plain JS literals)
//  - enum normalization produces a Dictionary the GDScript side can read as a constant map
//  - container constants are read-only, recursively, so a nested container is frozen too
//  - a derived script merges its base's constants, with the derived value shadowing
//  - a shared static resolves through one process-wide store, so a worker sees the main
//    environment's write and vice versa
//  - both declaration forms (member form, and the class form naming members declared in a merged
//    `namespace`) reach the same constant map, and a name annotated as both a constant and a
//    shared static is resolved as a constant rather than declared writable
//
// `checkGodotSide` reads what Godot itself observes (`Script::get_script_constant_map`), which is
// the surface GDScript uses - asserting it here rather than through a one-off probe keeps the
// Godot-facing half of the contract covered.
const WORKER_READY_TIMEOUT_MS = 95000;
const ROUND_TRIP_TIMEOUT_MS = 5000;

function check(context: string, actual: unknown, expected: unknown): void {
	if (actual !== expected) {
		reportTestFailure(context, new Error(`expected ${String(expected)}, got ${String(actual)}`));
	}
}

function checkTrue(context: string, value: boolean): void {
	if (!value) {
		reportTestFailure(context, new Error("expected true"));
	}
}

// The member names Godot exposes for a script. `GDScriptAnalyzer` resolves a member of a *foreign*
// script exclusively through this list, so a member missing here is a member the editor cannot see.
function propertyNames(script: Script): string[] {
	return script
		.get_script_property_list()
		.to_array()
		.map((info) => (info as GDictionary).get("name") as string);
}

export default class TestStaticMembers extends Node {
	public completeCallback: (() => unknown) | null = null;

	async _ready() {
		beginAsyncTest();
		try {
			this.checkConstants();
			this.checkGodotSide();
			this.checkNamespacedForm();
			this.checkSharedStaticLocal();
			await this.checkCrossEnvironment();
		} catch (error) {
			reportTestFailure("static-members", error);
		} finally {
			endAsyncTest();
			this.completeCallback?.();
		}
	}

	private checkConstants(): void {
		check("const int", StaticMembersTarget.N, 42);
		check("const float", StaticMembersTarget.F, 1.5);
		check("const string", StaticMembersTarget.S, "hello");
		check("const bool", StaticMembersTarget.B, true);
		check("const null", StaticMembersTarget.NUL, null);
		// the JS property keeps the raw BigInt; only the Godot-side snapshot converts to INT
		check("const bigint", StaticMembersTarget.BIG, 123n);

		// the enum stays usable from JS as the original enum object
		check("enum member", StaticMembersTarget.E.Blue, 2);

		// containers keep their contents, and are frozen on the shared storage
		checkTrue("container const frozen", StaticMembersTarget.ARR.is_read_only());
		const nested = StaticMembersTarget.NESTED;
		checkTrue("nested container frozen", nested.is_read_only());
		checkTrue("nested container frozen recursively", (nested.get(0) as GArray).is_read_only());

		// an unannotated static member is never collected as a constant
		check("unannotated stays a plain member", StaticMembersTarget.PLAIN, 7);
	}

	private checkGodotSide(): void {
		// Read through the engine, not the JS class: this is the surface GDScript and the editor use.
		// The values are parse-time snapshots, which is also why the JS-side write above does not
		// reach here.
		const script = ResourceLoader.load("res://tests/static-members/static-members-target.ts") as Script;
		checkTrue("target script loads", script instanceof Script);

		const map = script.get_script_constant_map();
		check("godot-side int", map.get("N"), 42);
		check("godot-side float", map.get("F"), 1.5);
		check("godot-side string", map.get("S"), "hello");
		check("godot-side bool", map.get("B"), true);
		// JS renders both NIL and a missing entry as nullish, so presence and nullishness are
		// asserted together rather than pinning one of the two representations.
		checkTrue("godot-side null constant present", map.has("NUL"));
		checkTrue("godot-side null constant is nil", map.get("NUL") == null);

		// the enum arrives as Dictionary{name: int}, the shape a GDScript enum has
		const enum_dict = map.get("E") as GDictionary;
		checkTrue("godot-side enum is a dictionary", enum_dict instanceof GDictionary);
		check("godot-side enum member", enum_dict.get("Blue"), 2);

		// container constants are frozen on the Godot side, recursively
		const arr = map.get("ARR") as GArray;
		checkTrue("godot-side container frozen", arr.is_read_only());
		check("godot-side container contents", arr.get(1), 2);
		checkTrue("godot-side nested frozen recursively", ((map.get("NESTED") as GArray).get(0) as GArray).is_read_only());

		// R2.3 whitelist as seen from the Godot side: the rejected shapes must be absent
		for (const rejected of ["VEC", "LIT", "FN", "PLAIN"]) {
			checkTrue(`godot-side rejects ${rejected}`, !map.has(rejected));
		}
		// a shared static is not a constant and must not leak into the constant map
		checkTrue("godot-side excludes shared static", !map.has("score"));

		// The member list the GDScript analyzer reads. A shared static has to appear here (a foreign
		// script has no `STATIC_VARIABLE` channel of its own), and the name must appear exactly once -
		// `HashSet::insert` returns an Iterator, so the dedup only works when guarded by `has`.
		const names = propertyNames(script);
		checkTrue("godot-side member list exposes the shared static", names.includes("score"));
		checkTrue("godot-side member list has no duplicates", new Set(names).size === names.length);

		// Own constants only, like GDScript: `get_script_constant_map()` must NOT merge the base
		// chain. The derived `N` is present, the inherited `F` is not. The inherited value stays
		// reachable at runtime through `_get` (which walks `base`), which the GDScript-side check
		// (`static-members-gdcheck.gd`) covers.
		const derived = ResourceLoader.load("res://tests/static-members/static-members-derived.ts") as Script;
		checkTrue("derived script loads", derived instanceof Script);
		const derived_map = derived.get_script_constant_map();
		check("derived constant map keeps its own constant", derived_map.get("N"), 22);
		checkTrue("derived constant map excludes the inherited constant", !derived_map.has("F"));
		const derived_names = propertyNames(derived);
		checkTrue("derived member list exposes the shared static", derived_names.includes("score"));
		// `score` is declared by both the base and the derived class. This list is
		// `Script::get_script_property_list()` (the member set the GDScript analyzer reads), and it
		// carries the name once per declaring script.
		//NOTE `tag` / `baseOnly` are *instance* properties, and this list does not dedupe them across
		//     the base chain (pre-existing behaviour, unrelated to the static-member work). Measured
		//     on the engine: the target script yields ["tag", "baseOnly", "score"] and the derived one
		//     ["tag", "tag", "baseOnly", "score"] - `tag` once per declaring script. So they are not
		//     asserted here. `_get_members()` - the list the remote debugger reads - is
		//     own-members-only and is covered by the C++ suite.
		check("derived member list declares the shared static once", derived_names.filter((name) => name === "score").length, 1);
	}

	private checkSharedStaticLocal(): void {
		check("shared static initial value", StaticMembersTarget.score, 0);
		StaticMembersTarget.score = 11;
		check("shared static write", StaticMembersTarget.score, 11);
	}

	// The class form: members declared in a namespace merged with the class and named at the class
	// level. It must reach the same surfaces the member form does, and a member carrying both
	// annotations must resolve as a constant rather than be declared writable.
	private checkNamespacedForm(): void {
		const script = ResourceLoader.load("res://tests/static-members/static-members-namespaced.ts") as Script;
		checkTrue("namespaced script loads", script instanceof Script);
		const map = script.get_script_constant_map();

		// class-form constants, resolved off the class object at parse time
		check("class form int", map.get("NS_N"), 7);
		const enum_dict = map.get("NS_E") as GDictionary;
		checkTrue("class form enum is a dictionary", enum_dict instanceof GDictionary);
		check("class form enum member", enum_dict.get("Magenta"), 1);
		const arr = map.get("NS_ARR") as GArray;
		checkTrue("class form container frozen", arr.is_read_only());
		check("class form container contents", arr.get(0), 9);
		check("class form dict contents", (map.get("NS_DICT") as GDictionary).get("z"), 26);

		// member form in the same class: both forms coexist in one script
		check("member form alongside class form", map.get("NS_INNER"), 5);

		// a named member that does not exist is ignored with a warning, never invented
		checkTrue("class form ignores a missing name", !map.has("NS_MISSING"));

		// the class form reads the member off the class object, so the JS side sees the same value
		check("class form constant visible from JS", StaticMembersNamespaced.NS_N, 7);
		check("member form constant visible from JS", StaticMembersNamespaced.NS_INNER, 5);

		// a member annotated as both a constant and a shared static: the constant wins, and the
		// shared-static annotation is dropped - so it is readable as a constant but declared
		// nowhere as writable (no `STATIC_VARIABLE`-style entry in the property list).
		check("conflicting annotation resolves as a constant", map.get("NS_CONFLICT"), 6);
		const names = propertyNames(script);
		checkTrue("conflicting annotation is not declared writable", !names.includes("NS_CONFLICT"));

		// class-form shared static: still a real static variable
		checkTrue("class form shared static is declared writable", names.includes("nsScore"));
		check("class form shared static initial value", StaticMembersNamespaced.nsScore, 3);
		StaticMembersNamespaced.nsScore = 44;
		check("class form shared static write", StaticMembersNamespaced.nsScore, 44);
		StaticMembersNamespaced.nsScore = 3;
	}

	private async checkCrossEnvironment(): Promise<void> {
		const worker = new JSWorker("tests/static-members/static-members-peer");
		try {
			await this.waitForWorkerReady(worker);

			// The worker imports the same module in its own isolate; the value it reads must be the
			// one this environment just wrote, not a per-isolate copy of the initializer.
			check("worker reads the main environment value", await this.roundTrip(worker, "read"), 11);

			// ...and a write from the worker must be visible back here.
			check("worker write acknowledged", await this.roundTrip(worker, "write"), 2000);
			check("main environment reads the worker value", StaticMembersTarget.score, 2000);
		} finally {
			worker.terminate();
		}
	}

	private roundTrip(worker: JSWorker, action: string): Promise<number> {
		return new Promise<number>((resolve, reject) => {
			const timeout = setTimeout(() => reject(new Error(`worker ${action} timed out`)), ROUND_TRIP_TIMEOUT_MS);
			worker.onmessage = (message: { action?: unknown; value?: unknown }) => {
				clearTimeout(timeout);
				if (message?.action !== action || typeof message.value !== "number") {
					reject(new Error(`unexpected worker response: ${JSON.stringify(message)}`));
					return;
				}
				resolve(message.value);
			};
			worker.postMessage(action);
		});
	}

	private waitForWorkerReady(worker: JSWorker): Promise<void> {
		return new Promise<void>((resolve, reject) => {
			const timeout = setTimeout(() => {
				reject(new Error(`worker ready timed out after ${String(WORKER_READY_TIMEOUT_MS)}ms`));
			}, WORKER_READY_TIMEOUT_MS);
			worker.onready = () => {
				clearTimeout(timeout);
				resolve();
			};
		});
	}
}
