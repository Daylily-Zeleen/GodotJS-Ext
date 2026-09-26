// uid://v3m5ylct6oaf This line is generated, don't modify or remove it.
import {
    AStar2D,
    CodeEdit,
    Curve2D,
    FileAccess,
    GArray,
    GDictionary,
    ImageTexture,
    Node,
    PackedByteArray,
    TabBar,
    Vector2,
    Vector2i,
} from "godot";
import { hasTestFailure, reportTestFailure } from "../test-status";

/**
 * Missing-argument (default-value) regression coverage, shared by both binding
 * legs (static / dynamic). Covers:
 *  - builtin EncodeT default slots (float / bool / Variant / String)
 *  - class engine-side defaults (String / StringName / int / float / bool /
 *    Vector2 / Variant / Object*), exercised through observable readbacks; Color/Array/
 *    Dictionary class defaults have no cheap readback (void draw_* sinks) and
 *    are covered by the .d.ts rendering + codegen baseline diff instead
 *  - String-default decoding (str_to_var fix): the two cases below returned
 *    quote-char garbage / "" before the fix on one or both legs
 *  - cross-type value constructor (new Vector2(Vector2i)): guards the
 *    reflect-ctor path where an over-strict dev assertion crashed the
 *    dynamic leg (jsb_static_binding_util.h)
 *
 *
 * All failures go through reportTestFailure: a raw throw inside _ready does
 * not propagate to start.ts (deferred add_child stack) and the suite would
 * falsely report COMPLETED.
 */
/**
 * A 64-bit alias is `number | bigint`: the runtime writer picks by magnitude,
 * so the declaration says both and the value may be either. The values asserted
 * here are indices and counts that fit a `number`, so normalise before
 * comparing.
 */
function asNumber(value: unknown): unknown {
    if (typeof value === "bigint" && value >= BigInt(Number.MIN_SAFE_INTEGER) && value <= BigInt(Number.MAX_SAFE_INTEGER)) {
        return Number(value);
    }
    return value;
}

/** `JSON.stringify` throws on `bigint`; render it tagged so failures stay readable. */
function stringify(value: unknown): string {
    return JSON.stringify(value, (_key, item: unknown) => (typeof item === "bigint" ? `${String(item)}n` : item));
}

function check(context: string, actual: unknown, expected: unknown): void {
    if (asNumber(actual) !== asNumber(expected)) {
        reportTestFailure(context, `expected ${stringify(expected)}, got ${stringify(actual)}`);
    }
}

function expectThrows(context: string, fn: () => void): void {
    try {
        fn();
        reportTestFailure(context, "expected an error, but the call succeeded");
    } catch {
        // expected
    }
}

/**
 * Invoke a member with an argument list the declared signature rejects.
 *
 * The arity boundary is exactly what these assertions probe, so the call
 * cannot be typed. `Reflect.get`/`Reflect.apply` express that without an
 * `as any` suppression.
 */
function callWithArity(target: object, name: string, args: unknown[]): void {
    const fn: unknown = Reflect.get(target, name);
    if (typeof fn !== "function") {
        throw new Error(`member '${name}' is not a function`);
    }
    Reflect.apply(fn, target, args);
}

function expectPasses(context: string, fn: () => void): void {
    try {
        fn();
    } catch (e) {
        reportTestFailure(context, `expected success, got error: ${String(e)}`);
    }
}

function section(name: string, fn: () => void): void {
    try {
        fn();
    } catch (e) {
        reportTestFailure(name, e);
    }
}

export default class TestDefaultArgs extends Node {
    _ready(): void {
        section("builtin scalar slots", () => {
            // float slot: Vector2.limit_length() defaults length to 1.0
            const limited = new Vector2(3, 0).limit_length();
            check("Vector2.limit_length().x", limited.x, 1);
            check("Vector2.limit_length().y", limited.y, 0);

            // bool slot: GArray.bsearch(x) defaults before to true
            const arr = new GArray();
            arr.append(1);
            arr.append(2);
            arr.append(3);
            check("GArray.bsearch(2)", arr.bsearch(2), 1);

            // Variant slot: GDictionary.get() defaults to null
            check("GDictionary.get('missing')", new GDictionary().get("missing"), null);
        });

        section("cross-type value constructor", () => {
            // reflect ctor path: an over-strict dev assertion crashed
            // `new Vector2(Vector2i)` on the dynamic leg
            // (jsb_static_binding_util.h); guards both legs' ctor paths.
            const crossCtor = new Vector2(new Vector2i(3, 4));
            check("new Vector2(Vector2i).x", crossCtor.x, 3);
            check("new Vector2(Vector2i).y", crossCtor.y, 4);
        });

        section("builtin String slot (str_to_var fix)", () => {
            // String slot: PackedByteArray.get_string_from_multibyte_char()
            // defaults encoding to "" (engine decodes via the active code page).
            // Pre-fix the default was the two-quote-chars string '""' and the
            // call returned "" after an os_windows encoding lookup error.
            const src = new GArray();
            src.append(0x68); // 'h'
            src.append(0x69); // 'i'
            const bytes = new PackedByteArray(src);
            check(
                "PackedByteArray.get_string_from_multibyte_char()",
                bytes.get_string_from_multibyte_char(),
                "hi",
            );
        });

        section("class defaults", () => {
            // class String default "": TabBar.add_tab(title, icon)
            const tabBar = new TabBar();
            tabBar.add_tab();
            check("TabBar.tab_count", tabBar.tab_count, 1);
            check("TabBar.get_tab_title(0)", tabBar.get_tab_title(0), "");
            check("TabBar.get_tab_icon(0)", tabBar.get_tab_icon(0), null);

            // class String defaults "region"/"endregion": CodeEdit.set_code_region_tags
            // (dynamic-leg str_to_var fix observable: pre-fix the parser stored the
            // quote-wrapped strings, so the readback returned '"region"')
            const codeEdit = new CodeEdit();
            codeEdit.set_code_region_tags();
            check("CodeEdit.get_code_region_start_tag()", codeEdit.get_code_region_start_tag(), "region");
            check("CodeEdit.get_code_region_end_tag()", codeEdit.get_code_region_end_tag(), "endregion");

            // class Vector2 defaults in_ / out_ = (0, 0): Curve2D.add_point
            const curve = new Curve2D();
            curve.add_point(new Vector2(10, 20));
            check("Curve2D.get_point_position(0).x", curve.get_point_position(0).x, 10);
            check("Curve2D.get_point_position(0).y", curve.get_point_position(0).y, 20);
            check("Curve2D.get_point_in(0).x", curve.get_point_in(0).x, 0);
            check("Curve2D.get_point_in(0).y", curve.get_point_in(0).y, 0);

            // class int default -1: Curve2D.add_point index — the append
            // semantics are the observable: a second omitted-index point must
            // land at index 1, not insert at 0.
            curve.add_point(new Vector2(30, 40));
            check("Curve2D.point_count", curve.point_count, 2);
            check("Curve2D.get_point_position(1).x", curve.get_point_position(1).x, 30);
            check("Curve2D.get_point_position(1).y", curve.get_point_position(1).y, 40);

            // class float default 1.0: AStar2D.add_point weight_scale
            const astar = new AStar2D();
            astar.add_point(1, new Vector2(5, 6));
            check("AStar2D.get_point_weight_scale(1)", astar.get_point_weight_scale(1), 1.0);

            // class String default delim ",": FileAccess.get_csv_line
            const file = FileAccess.open("res://config/test.notcsv", FileAccess.ModeFlags.READ);
            if (file === null) {
                reportTestFailure("FileAccess.open(test.notcsv)", "failed to open fixture");
            } else {
                const row = file.get_csv_line();
                check("FileAccess.get_csv_line().size()", row.size(), 2);
                check("FileAccess.get_csv_line().get(0)", row.get(0), "keys");
                check("FileAccess.get_csv_line().get(1)", row.get(1), "en");
                file.close();
            }

            // class Variant default null: Object.get_meta
            check("Node.get_meta('missing', null)", this.get_meta("missing", null), null);
            // class String type "" + bool recursive/owned: Node.find_children
            check("Node.find_children(nomatch).size()", this.find_children("zzz_nomatch*").size(), 0);
            // class StringName context "": Object.tr
            check("Node.tr('hello')", this.tr("hello"), "hello");
        });


        section("explicit undefined takes the position's default", () => {
            // JS default-parameter semantics: an explicit `undefined` at a
            // DEFAULTED position means "use THIS position's default" -- and it
            // must not shift the arguments around it, so
            // `set_code_region_tags(undefined, "END")` is legal and keeps
            // `end = "END"` while `start` falls back to "region".
            //
            // Both legs implement that, by different means: the dynamic path
            // substitutes the position before conversion, the static builtin
            // thunks divert the position to their shared default slot, and the
            // static class thunks (which carry no default literals) hand the
            // call to the dynamic callback. This section therefore asserts the
            // same behavior on both legs.

            // builtin float slot, whole optional tail:
            // Vector2.limit_length(length = 1.0)
            const limited = new Vector2(3, 0).limit_length(undefined);
            check("Vector2.limit_length(undefined).x", limited.x, 1);
            check("Vector2.limit_length(undefined).y", limited.y, 0);

            // builtin bool slot: GArray.bsearch(value, before = true)
            const arr = new GArray();
            arr.append(1);
            arr.append(2);
            arr.append(3);
            check("GArray.bsearch(2, undefined)", arr.bsearch(2, undefined), 1);

            // builtin Variant slot: GDictionary.get(key, default = null)
            check("GDictionary.get('missing', undefined)", new GDictionary().get("missing", undefined), null);

            // builtin, undefined BETWEEN supplied positions -- the previous
            // argument order must survive:
            // GArray.slice(begin, end = 2147483647, step = 1, deep = false)
            const seq = new GArray();
            seq.append(1);
            seq.append(2);
            seq.append(3);
            seq.append(4);
            seq.append(5);
            const stepped = seq.slice(0, undefined, 2);
            check("GArray.slice(0, undefined, 2).size()", stepped.size(), 3);
            check("GArray.slice(0, undefined, 2).get(0)", stepped.get(0), 1);
            check("GArray.slice(0, undefined, 2).get(1)", stepped.get(1), 3);
            check("GArray.slice(0, undefined, 2).get(2)", stepped.get(2), 5);

            // class String + Object slots, whole optional tail:
            // TabBar.add_tab(title = "", icon = null)
            const tabBar = new TabBar();
            tabBar.add_tab(undefined, undefined);
            check("TabBar.tab_count [add_tab(undefined, undefined)]", tabBar.tab_count, 1);
            check("TabBar.get_tab_title(0)", tabBar.get_tab_title(0), "");

            // class, undefined FIRST with the later position still supplied:
            // the defaulted `title` must fall back without disturbing `icon`.
            // (`icon` is a Texture2D, not a nullable one: pass a real texture,
            // and the non-null icon readback below is what proves the value in
            // that position survived while `title` fell back.)
            const iconTexture = new ImageTexture();
            tabBar.add_tab(undefined, iconTexture);
            check("TabBar.tab_count [add_tab(undefined, icon)]", tabBar.tab_count, 2);
            check("TabBar.get_tab_title(1)", tabBar.get_tab_title(1), "");
            check("TabBar.get_tab_icon(1)", tabBar.get_tab_icon(1), iconTexture);

            // class String slots, undefined trailed by a real value -- this is
            // the case the old arity-trimming semantics got wrong (it threw
            // "bad argument: 0" because only a contiguous tail was trimmed):
            // CodeEdit.set_code_region_tags(start = "region", end = "endregion")
            const codeEdit = new CodeEdit();
            codeEdit.set_code_region_tags(undefined, "END");
            check("CodeEdit.get_code_region_start_tag() [undefined, 'END']", codeEdit.get_code_region_start_tag(), "region");
            check("CodeEdit.get_code_region_end_tag() [undefined, 'END']", codeEdit.get_code_region_end_tag(), "END");

            // class String + Object slots, whole optional tail omitted entirely
            // (engine-side fill, must keep working alongside substitution):
            const codeEdit2 = new CodeEdit();
            codeEdit2.set_code_region_tags();
            check("CodeEdit.get_code_region_start_tag() [no args]", codeEdit2.get_code_region_start_tag(), "region");
            check("CodeEdit.get_code_region_end_tag() [no args]", codeEdit2.get_code_region_end_tag(), "endregion");

            // class Vector2 + int slots: undefined on the trailing `index` must
            // still resolve to -1, i.e. append, not insert at 0.
            const curve = new Curve2D();
            curve.add_point(new Vector2(10, 20));
            curve.add_point(new Vector2(30, 40), undefined, undefined, undefined);
            check("Curve2D.point_count", curve.point_count, 2);
            check("Curve2D.get_point_position(1).x", curve.get_point_position(1).x, 30);
            check("Curve2D.get_point_in(1).x", curve.get_point_in(1).x, 0);

            // class undefined inside the defaulted run while later positions are
            // left out: substitution and engine-side filling in one call.
            // Curve2D.add_point(position, in = (0,0), out = (0,0), index = -1)
            curve.add_point(new Vector2(50, 60), undefined);
            check("Curve2D.point_count [add_point(p, undefined)]", curve.point_count, 3);
            check("Curve2D.get_point_position(2).x", curve.get_point_position(2).x, 50);
            check("Curve2D.get_point_out(2).x", curve.get_point_out(2).x, 0);

            // class String type "" + bool recursive/owned: Node.find_children
            check(
                "Node.find_children('zzz_nomatch*', undefined).size()",
                this.find_children("zzz_nomatch*", undefined).size(),
                0,
            );

            // boundary: undefined over a REQUIRED position is still a value.
            // Substitution only applies at defaulted positions, so a
            // non-convertible one keeps failing instead of silently becoming a
            // default. These probes are deliberately invalid calls that the
            // generated typings reject, so they need one unchecked cast:
            // reason = arity/type violations are exactly what is under test.
            const callable = (target: object): Record<string, (...args: unknown[]) => unknown> =>
                target as unknown as Record<string, (...args: unknown[]) => unknown>;
            expectThrows("Curve2D.get_point_position(undefined) [required int]", () => callable(curve).get_point_position(undefined));
            // boundary: arity is still checked on what the caller literally
            // passed, so an N+1 call stays rejected.
            expectThrows("Vector2.limit_length(undefined, undefined) [N+1 throws]", () => callable(limited).limit_length(undefined, undefined));
            // boundary: an undefined on a required position of a class method
            // with defaults elsewhere is still not a defaulted position.
            // CodeEdit.set_code_region_tags is all-optional, so use a method
            // with a required prefix: Curve2D.add_point(position, ...).
            expectThrows("Curve2D.add_point(undefined) [required Vector2]", () => callable(curve).add_point(undefined));
        });

        section("arity boundaries", () => {
            const arr = new GArray();
            arr.append(1);
            arr.append(2);
            arr.append(3);
            // GArray.bsearch: M = 1, N = 2
            expectThrows("GArray.bsearch() [M-1 throws]", () => callWithArity(arr, "bsearch", []));
            expectPasses("GArray.bsearch(2) [M passes]", () => arr.bsearch(2));
            expectPasses("GArray.bsearch(2, true) [N passes]", () => arr.bsearch(2, true));
            expectThrows("GArray.bsearch(2, true, 0) [N+1 throws]", () => callWithArity(arr, "bsearch", [2, true, 0]));

            const curve = new Curve2D();
            const p = new Vector2(1, 2);
            // Curve2D.add_point: M = 1, N = 4
            expectThrows("Curve2D.add_point() [M-1 throws]", () => callWithArity(curve, "add_point", []));
            expectPasses("Curve2D.add_point(p) [M passes]", () => curve.add_point(p));
            expectPasses("Curve2D.add_point 4 args [N passes]", () =>
                curve.add_point(p, p, p, -1),
            );
            expectThrows("Curve2D.add_point 5 args [N+1 throws]", () =>
                callWithArity(curve, "add_point", [p, p, p, -1, 0]),
            );
        });

        console.warn(
            "DefaultArgs: default-argument checks finished, fail=" + String(hasTestFailure()),
        );
    }
}
