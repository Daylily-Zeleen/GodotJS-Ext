import { GArray, GDictionary, Node } from "godot";
import { createClassBinder } from "godot.annotations";

// The *class form* of the exposed annotations: the members are declared outside the class body, in
// a same-named `namespace` merged with the class, and named at the class level.
//
// TypeScript rejects a decorator on a namespace member (TS1206), so the names have to be listed on
// the class instead. The class decorators run before the namespace IIFE, so at decoration time the
// named members do not exist yet - the names are recorded and resolved when the module is parsed.
// That is why the class form and the member form share a single code path on the Godot side.
//
// A merged declaration cannot itself carry `export default` (TS2652), hence the separate statement.
export enum NsColor {
  Cyan,
  Magenta,
}

const bind = createClassBinder();

@bind()
// `NS_MISSING` deliberately does not exist: a name that resolves to nothing is the class form's
// only failure mode, and it must be ignored with a warning rather than silently dropped.
@bind.exposed.const("NS_N", "NS_E", "NS_ARR", "NS_DICT", "NS_MISSING")
@bind.exposed.shared("nsScore")
class StaticMembersNamespaced extends Node {
  // the member form, in the same class: both declaration forms must coexist
  @bind.exposed.const()
  static readonly NS_INNER = 5;

  // contradictory annotations: the constant wins and the shared-static annotation is dropped, so
  // the member is not declared writable anywhere
  @bind.exposed.const()
  @bind.exposed.shared()
  static readonly NS_CONFLICT = 6;

  // A real class method. `_get_method_info()` must report exactly this name and nothing else - the
  // engine reaches it before the module is loaded (`Object::get_method_argument_count` ->
  // `Script::get_script_method_argument_count` -> `get_method_info`), which the C++ suite pins.
  greet(): number {
    return 1;
  }
}

namespace StaticMembersNamespaced {
  export const NS_N = 7;
  export const NS_E = NsColor;
  export const NS_ARR = GArray.create([9, 8, 7]);
  export const NS_DICT = GDictionary.create({ z: 26 });
  export let nsScore = 3;
}

export default StaticMembersNamespaced;
