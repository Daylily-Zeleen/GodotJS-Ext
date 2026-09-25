extends Node

# The GDScript half of the static-member contract (A1/A2/A3, R5.1).
#
# This file's *parse* is itself the assertion: `S.score` and `S.N` are resolved by
# `GDScriptAnalyzer::reduce_identifier_from_base`, which for a non-GDScript script reads only
# `Script::get_script_property_list` and `get_constants`, and probes `get_method_info()` for every
# identifier *without* a `has_method` guard. If `_get_method_info()` ever returns a non-empty
# MethodInfo for an arbitrary name again, every access below fails to resolve and the scene will not
# load - which surfaces as a script error in the run. The value checks then pin the runtime
# semantics.
#
# A constant's analysed type is a metatype, so `S.ARR.method()` is a parse error; values are copied
# into typed locals first.
#
# `score` is a *shared* static: the JS-side test writes it too, so nothing here asserts an absolute
# value - the read/write round trip is the contract, and the value is restored at the end so this
# scene is independent of where it sits in the run order.
const S := preload("res://tests/static-members/static-members-target.ts")
const D := preload("res://tests/static-members/static-members-derived.ts")
const NSP := preload("res://tests/static-members/static-members-namespaced.ts")

const FAILURE_SENTINEL := "GODOTJS_TEST_PROJECT_FAILED:"

var _failures: int = 0


func _check(label: String, actual: Variant, expected: Variant) -> void:
	if actual != expected:
		_failures += 1
		push_error("%s static-members(gd) %s: expected %s, got %s" % [FAILURE_SENTINEL, label, str(expected), str(actual)])


func _ready() -> void:
	# A1: primitives, values matching the JS side
	var n: int = S.N
	var f: float = S.F
	var s: String = S.S
	var b: bool = S.B
	var big: int = S.BIG
	_check("int", n, 42)
	_check("float", f, 1.5)
	_check("string", s, "hello")
	_check("bool", b, true)
	_check("null", S.NUL, null)
	_check("bigint", big, 123)

	# A2: the enum is a Dictionary{name: int}; members must be read by index - dotted access is a
	# compile error on a foreign script's constant (see the annotation docs).
	var e: Dictionary = S.E
	_check("enum size", e.size(), 3)
	_check("enum member", e["Blue"], 2)

	# A1: containers, and they arrive frozen (recursively)
	var arr: Array = S.ARR
	_check("array contents", arr, [1, 2, 3])
	_check("array frozen", arr.is_read_only(), true)
	var nested: Array = S.NESTED
	_check("nested frozen", nested.is_read_only(), true)
	var inner: Array = nested[0]
	_check("nested frozen recursively", inner.is_read_only(), true)
	var dict: Dictionary = S.D
	_check("dict contents", dict["a"], 1)
	_check("dict frozen", dict.is_read_only(), true)

	# A2/A13: the engine-side constant map holds exactly the accepted shapes
	var scr := load("res://tests/static-members/static-members-target.ts") as Script
	var map: Dictionary = scr.get_script_constant_map()
	_check("map has N", map.has("N"), true)
	_check("map rejects VEC", map.has("VEC"), false)
	_check("map rejects LIT", map.has("LIT"), false)
	_check("map rejects FN", map.has("FN"), false)
	_check("map excludes shared static", map.has("score"), false)

	# A3: the shared static is readable and writable from GDScript, round-tripping through the
	# process-wide store that the JS side reads.
	S.score = 77
	var after_write: int = S.score
	_check("shared static round trip", after_write, 77)
	S.score = 0

	# A7: `_get_constants()` reports own constants only (GDScript parity), so the derived script's
	# own `N` resolves while the inherited `F` is deliberately absent from its constant map. A
	# foreign script has no `class_type` chain for the analyzer to walk, so with a `const` base the
	# inherited name does not resolve at analysis time at all - the whole file fails to parse with
	# `Cannot find member "F" in base "…static-members-derived.ts"` (measured). The inherited
	# *value* stays reachable at runtime through `_get`, which walks `base`; that is what the
	# non-const reference below pins. `score` here is the derived module's own slot.
	var derived_n: int = D.N
	_check("derived shadows constant", derived_n, 22)
	var derived_map: Dictionary = (load("res://tests/static-members/static-members-derived.ts") as Script).get_script_constant_map()
	_check("derived constant map keeps the own constant", derived_map.has("N"), true)
	_check("derived constant map excludes the inherited constant", derived_map.has("F"), false)
	var derived_dyn := preload("res://tests/static-members/static-members-derived.ts")
	var inherited_f: float = derived_dyn.F
	_check("inherited constant reachable at runtime via _get", inherited_f, 1.5)
	var derived_score: int = D.score
	_check("derived shared static", derived_score, 2)

	# `_has_method()` / `_get_method_info()` report own methods only (GDScript parity), so the
	# base-chain walk lives in the instance layer (`GodotJSScriptInstance::has_method`, aligned with
	# `GDScriptInstance::has_method`). `greet` is declared by the base alone; this pins that it is
	# still reachable through an instance of the derived script.
	var derived_instance := D.new()
	_check("inherited method reachable on the instance", derived_instance.has_method("greet"), true)
	_check("unknown method is not reported", derived_instance.has_method("__nope__"), false)
	derived_instance.free()

	# The class form: the members are declared in a namespace merged with the class and named at
	# the class level. GDScript must resolve them exactly like the member form.
	var ns_n: int = NSP.NS_N
	var ns_inner: int = NSP.NS_INNER
	_check("class form int", ns_n, 7)
	_check("member form alongside class form", ns_inner, 5)
	var ns_e: Dictionary = NSP.NS_E
	_check("class form enum member", ns_e["Magenta"], 1)
	var ns_arr: Array = NSP.NS_ARR
	_check("class form container contents", ns_arr, [9, 8, 7])
	_check("class form container frozen", ns_arr.is_read_only(), true)
	var ns_map: Dictionary = (load("res://tests/static-members/static-members-namespaced.ts") as Script).get_script_constant_map()
	_check("class form map has NS_N", ns_map.has("NS_N"), true)
	_check("class form map ignores a missing name", ns_map.has("NS_MISSING"), false)
	# a member annotated as both a constant and a shared static resolves as a constant, so it must
	# not be declared writable anywhere
	_check("conflicting annotation resolves as a constant", ns_map.has("NS_CONFLICT"), true)
	NSP.nsScore = 88
	var ns_score: int = NSP.nsScore
	_check("class form shared static round trip", ns_score, 88)
	NSP.nsScore = 3

	if _failures == 0:
		print("STATIC-MEMBERS-GD-OK")
