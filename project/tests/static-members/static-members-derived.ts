import { Variant } from "godot";
import { createClassBinder } from "godot.annotations";
import StaticMembersTarget from "./static-members-target";

const bind = createClassBinder();

// A derived script, to cover the base-chain semantics. Three different things are exercised:
//  - `N` shadows the inherited constant. `_get_constants()` reports own constants only (aligned
//    with GDScript), so `get_script_constant_map()` carries `N` and not the inherited `F`; the
//    inherited value is still readable at runtime because `_get` walks `base`;
//  - `score` is a *separate* shared-static slot - the store is keyed by module id, so "shadowing"
//    here means the derived class resolves to its own entry rather than overwriting the base one;
//  - `tag` re-declares the inherited instance property. `_get_members()` reports own members only
//    (its single caller, the remote debugger, walks the base chain itself), so this script's list
//    must carry `tag` exactly once while the base-only `baseOnly` stays out of it.
@bind()
export default class StaticMembersDerived extends StaticMembersTarget {
	@bind.exposed.const()
	static readonly N: number = 22;

	@bind.exposed.shared()
	static score = 2;

	@bind.export(Variant.Type.TYPE_INT)
	accessor tag: number = 0;
}
