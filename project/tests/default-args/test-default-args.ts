import {
    AStar2D,
    CodeEdit,
    Curve2D,
    FileAccess,
    GArray,
    GDictionary,
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
function check(context: string, actual: unknown, expected: unknown): void {
    if (actual !== expected) {
        reportTestFailure(context, `expected ${JSON.stringify(expected)}, got ${JSON.stringify(actual)}`);
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
