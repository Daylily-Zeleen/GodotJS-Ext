import { GArray, GDictionary, Node, Variant, Vector2 } from "godot";
import { createClassBinder } from "godot.annotations";

export enum Color2 {
  Red,
  Green,
  Blue,
}

const bind = createClassBinder();

@bind()
export default class StaticMembersTarget extends Node {
  // --- accepted constants ---

  @bind.exposed.const()
  static readonly N: number = 42;

  @bind.exposed.const()
  static readonly F = 1.5;

  @bind.exposed.const()
  static readonly S = "hello";

  @bind.exposed.const()
  static readonly B = true;

  @bind.exposed.const()
  static readonly NUL = null;

  @bind.exposed.const()
  static readonly BIG = 123n;

  @bind.exposed.const()
  static readonly E = Color2;

  @bind.exposed.const()
  static readonly ARR = GArray.create([1, 2, 3]);

  @bind.exposed.const()
  static readonly NESTED = GArray.create([GArray.create([1, 2]), 3]);

  @bind.exposed.const()
  static readonly D = GDictionary.create({ a: 1 });

  // --- rejected: whitelist / typeof prefilter ---

  @bind.exposed.const()
  static readonly VEC = new Vector2(1, 2);

  @bind.exposed.const()
  static readonly LIT = [1, 2, 3];

  @bind.exposed.const()
  static readonly FN = () => 1;

  // --- annotated, but must NOT be collected as a constant ---

  static PLAIN = 7;

  // --- instance member ---
  // `_get_members()` reports *instance* members only (it is what the remote debugger uses to tell
  // script members apart from exported properties), so the fixture needs one to exercise it. The
  // derived script inherits this through the base chain.

  @bind.export(Variant.Type.TYPE_INT)
  accessor tag: number = 0;

  // A base-only instance member, never re-declared by the derived script: `_get_members()` reports
  // the script's *own* members only, so the derived list must not carry this one.
  @bind.export(Variant.Type.TYPE_INT)
  accessor baseOnly: number = 0;

  // --- shared static variable: one value for every JS environment and GDScript ---

  @bind.exposed.shared()
  static score = 0;

  // A real method, so the derived script can exercise *inherited* method resolution from GDScript.
  greet(): number {
    return 1;
  }
}
