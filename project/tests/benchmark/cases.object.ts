/**
 * Handwritten object-layer benchmark cases: high-frequency Object-derived
 * classes, singletons, vararg calls and operator methods. Only APIs present
 * in BOTH binding configurations are benchmarked (the dynamic leg has no
 * IN/NOT/POSITIVE/MODULE registrations, so those are deliberately absent).
 */
import {
	ClassDB,
	GArray,
	Color,
	Engine,
	Image,
	Input,
	Node,
	Node2D,
	ProjectSettings,
	ResourceLoader,
	Vector2,
} from "godot";
import { BuiltinCase } from "./bench";

export const OBJECT_CASES: BuiltinCase[] = [
	{
		group: "Node",
		makeTarget: () => {
			const n = new Node();
			const child = new Node();
			n.add_child(child);
			(n as any)._bench_child = child;
			return n;
		},
		cases: [
			{ name: "get_name(0)", fn: (t: any) => t.get_name() },
			{ name: "get_child_count(0)", fn: (t: any) => t.get_child_count() },
			{ name: "is_inside_tree(0)", fn: (t: any) => t.is_inside_tree() },
			{ name: "call(vararg)", fn: (t: any) => t.call("get_name") },
			{ name: "call(1prefix+1tail)", fn: (t: any) => t.call("has_node", "missing/path") },
			{ name: "has_node(1+1default)", fn: (t: any) => t.has_node("missing/path") },
			{ name: "is_in_group(1)", fn: (t: any) => t.is_in_group("bench_group") },
			{ name: "find_child(1+2defaults)", fn: (t: any) => t.find_child("missing") },
			{ name: "find_children(1+3defaults)", fn: (t: any) => t.find_children("missing") },
			{ name: "find_children(4)", fn: (t: any) => t.find_children("missing", "", true, true) },
			{ name: "move_child(2)", fn: (t: any) => t.move_child((t as any)._bench_child, 0) },
			{ name: "get_instance_id(0)", fn: (t: any) => t.get_instance_id() },
		],
	},
	{
		group: "Node2D",
		makeTarget: () => new Node2D(),
		cases: [
			{ name: "get position", fn: (t: any) => t.position },
			{ name: "set position", fn: (t: any) => { t.position = new Vector2(3, 4); } },
			{ name: "get rotation", fn: (t: any) => t.rotation },
			{ name: "set rotation", fn: (t: any) => { t.rotation = 0.5; } },
		],
	},
	{
		group: "ResourceLoader",
		makeTarget: () => null,
		cases: [
			{ name: "load(defaults)", fn: () => ResourceLoader.load("res://project.godot") },
			{ name: "exists(1)", fn: () => ResourceLoader.exists("res://project.godot") },
		],
	},
	{
		group: "ProjectSettings",
		makeTarget: () => null,
		cases: [
			{ name: "get_setting(1+default)", fn: () => ProjectSettings.get_setting("application/config/name", "x") },
			{ name: "has_setting(1)", fn: () => ProjectSettings.has_setting("application/config/name") },
		],
	},
	{
		group: "Engine",
		makeTarget: () => null,
		cases: [
			{ name: "get_frames_drawn(0)", fn: () => Engine.get_frames_drawn() },
			{ name: "get_process_frames(0)", fn: () => Engine.get_process_frames() },
		],
	},
	{
		group: "Input",
		makeTarget: () => null,
		cases: [
			{ name: "is_action_pressed(1)", fn: () => Input.is_action_pressed("ui_accept") },
		],
	},
	{
		group: "Operators",
		makeTarget: () => ({ a: new Vector2(1, 2), b: new Vector2(3, 4) }),
		cases: [
			{ name: "Vector2.ADD", fn: (t: any) => t.a.constructor.ADD(t.a, t.b) },
			{ name: "Vector2.SUBTRACT", fn: (t: any) => t.a.constructor.SUBTRACT(t.a, t.b) },
			{ name: "Vector2.EQUAL", fn: (t: any) => t.a.constructor.EQUAL(t.a, t.b) },
		],
	},
	{
		group: "Constructors",
		makeTarget: () => null,
		cases: [
			{ name: "new Vector2(x, y)", fn: () => new Vector2(1, 2) },
			{ name: "new Color(r, g, b, a)", fn: () => new Color(0.1, 0.2, 0.3, 0.4) },
			{ name: "new GArray()", fn: () => new GArray() },
		],
	},

	{
		group: "Image",
		makeTarget: () => Image.create(4, 4, false, 5),
		cases: [
			{ name: "set_pixel(2)", fn: (t: any) => t.set_pixel(1, 1, new Color(0.5, 0.5, 0.5)) },
			{ name: "get_pixel(2)", fn: (t: any) => t.get_pixel(1, 1) },
		],
	},
	{
		group: "ClassDB",
		makeTarget: () => null,
		cases: [
			{ name: "class_has_method(2+1default)", fn: () => ClassDB.class_has_method("Node", "get_name", true) },
			{ name: "class_has_method(2)", fn: () => ClassDB.class_has_method("Node", "get_name") },
		],
	},
]
