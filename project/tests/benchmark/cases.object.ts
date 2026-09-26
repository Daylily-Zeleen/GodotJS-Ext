// uid://dx0rgqdq7lv3c This line is generated, don't modify or remove it.
/**
 * Handwritten Object-derived and singleton benchmarks: methods, properties,
 * varargs and calls with omitted or explicit optional arguments.
 * Targets are shared within each group; earlier cases can change later inputs.
 */
import {
    ClassDB,
    GArray,
    Color,
    Control,
    CPUParticles2D,
    Engine,
    Image,
    Input,
    Node,
    Node2D,
    NodePath,
    ProjectSettings,
    ResourceLoader,
    Vector2,
} from "godot";
import { CaseGroup } from "./benchmark";

export const OBJECT_CASES: CaseGroup[] = [
    {
        group: "Node",
        makeTarget: () => {
            const n = new Node();
            const child = new Node();
            n.add_child(child);
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
            { name: "move_child(2)", fn: (t: any) => t.move_child(t.get_child(0), 0) },
            {
                name: "get_instance_id(0)",
                fn: (t: any) => t.get_instance_id(),
                // ObjectIDs are per-process: the static and dynamic legs cannot agree.
                processDependent: true,
            },
        ],
    },
    {
        group: "Node2D",
        makeTarget: () => new Node2D(),
        cases: [
            { name: "get position", fn: (t: any) => t.position },
            {
                name: "set position",
                fn: (t: any) => {
                    t.position = new Vector2(3, 4);
                },
            },
            { name: "get rotation", fn: (t: any) => t.rotation },
            {
                name: "set rotation",
                fn: (t: any) => {
                    t.rotation = 0.5;
                },
            },
        ],
    },
    {
        group: "ResourceLoader",
        makeTarget: () => null,
        cases: [
            { name: "load(defaults)", fn: () => ResourceLoader.load("res://tests/benchmark/Benchmark.tscn") },
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
        cases: [{ name: "get_frames_drawn(0)", fn: () => Engine.get_frames_drawn() }],
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
    // Indexed properties (`prop.index >= 0`): the accessor forwards a constant
    // index to a backing method shared by several properties, so one method
    // serves `get_param_min(i)` / `get_param_max(i)` / `get_param_curve(i)`.
    // Under static bindings these take the generated
    // `indexed_property_getter/setter_thunk`; otherwise the reflect path.
    {
        group: "IndexedProp",
        makeTarget: () => new CPUParticles2D(),
        cases: [
            // float, index 6 (damping pair) and index 0 (initial_velocity pair)
            { name: "get float(param_max,6)", fn: (t: CPUParticles2D) => t.damping_max },
            { name: "set float(param_max,6)", fn: (t: CPUParticles2D) => { t.damping_max = 1.5; } },
            { name: "get float(param_min,0)", fn: (t: CPUParticles2D) => t.initial_velocity_min },
            { name: "set float(param_min,0)", fn: (t: CPUParticles2D) => { t.initial_velocity_min = -1.5; } },
            // bool, index 0
            { name: "get bool(particle_flag,0)", fn: (t: CPUParticles2D) => t.particle_flag_align_y },
            { name: "set bool(particle_flag,0)", fn: (t: CPUParticles2D) => { t.particle_flag_align_y = true; } },
            // object, index 6 -- the slot whose write crashed before the fix
            { name: "get object(param_curve,6)", fn: (t: CPUParticles2D) => t.damping_curve },
            { name: "set object(param_curve,6)", fn: (t: CPUParticles2D) => { t.damping_curve = null; } },
        ],
    },
    // The same accessor shape on an Object-derived class whose backing methods
    // take a leading ENUM argument (`get_offset(Side)` / `set_offset(Side,float)`,
    // `get_anchor(Side)`, `get/set_focus_neighbor(Side)`). The constant index is
    // what fills that leading argument.
    {
        group: "IndexedPropEnum",
        makeTarget: () => new Control(),
        cases: [
            { name: "get float(get_offset,0)", fn: (t: Control) => t.offset_left },
            { name: "set float(set_offset,0)", fn: (t: Control) => { t.offset_left = 1.5; } },
            { name: "get float(get_offset,3)", fn: (t: Control) => t.offset_bottom },
            { name: "get float(get_anchor,0)", fn: (t: Control) => t.anchor_left },
            { name: "get nodepath(focus,0)", fn: (t: Control) => t.focus_neighbor_left },
            { name: "set nodepath(focus,0)", fn: (t: Control) => { t.focus_neighbor_left = new NodePath("Path/To/Target"); } },
        ],
    },
];
