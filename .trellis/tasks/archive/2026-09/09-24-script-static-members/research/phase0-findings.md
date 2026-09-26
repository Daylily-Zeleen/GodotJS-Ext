# 阶段 0 前置实测（0.1–0.3 已完成；0.4 随 Q1=(c) 取消）

> 探针目录：`.agent_tmp/tsprobe/`（任务收尾已清理）（TS 产物 dump + Node 描述符 dump）。
> 目标：把 `design.md` 的 `[INFERENCE]` 变成实测事实。

---

## 0.1 TS 产物形态 ✅ 已完成

命令：`node project/node_modules/typescript/bin/tsc --target es2022 --module esnext --outDir .agent_tmp/tsprobe/out .agent_tmp/tsprobe/probe.ts`
（`project/tsconfig.json` 即 `target: es2022`、`useDefineForClassFields` 注释掉 → ES2022 语义下默认 `true`）

### 产物（关键片段）

```js
export class H {
    static SRO = 1;                       // static readonly → 可写 data 属性，readonly 被擦除
    static S = 3;
    static ARR = [1, 2, 3];
    static OBJ = { a: 1, b: "x" };
    static FN = function (a, b) { return a + b; };
    static sf(a, b) { return a + b; }     // 方法形态（非字段）
    static sf0() { return 0; }
    static sfa(...args) { return args.length; }
    static arrow = (a) => a;
    static async asf(a) { return a; }
    static BIG = 123n;
    static UNDEF = undefined;
    static NUL = null;
    static SYM = Symbol("s");
}
export var E;
(function (E) { E[E["A"] = 0] = "A"; E[E["B"] = 1] = "B"; })(E || (E = {}));
export var ES;
(function (ES) { ES["X"] = "x"; ES["Y"] = "y"; })(ES || (ES = {}));
export const MOD_CONST = 42;              // 模块级 const 与 let 产物同形（无 const 信息）
export let mod_let = 43;
export function free_fn(a) { return a; }
```

- `const enum CE { A, B }` **完全消失**（编译期擦除）⇒ R8.1 成立。

### `Object.getOwnPropertyDescriptor(class_obj, n)`（Node dump，`desc.mjs`）

| 名字 | enumerable | writable | configurable | typeof | `Function.length` |
|---|---|---|---|---|---|
| `length` | false | false | true | number | — |
| `name` | false | false | true | string | — |
| `prototype` | false | false | **false** | object | — |
| `sf`（方法） | **false** | true | true | function | 2 |
| `sf0`（方法） | false | true | true | function | 0 |
| `sfa`（rest 方法） | false | true | true | function | **0** |
| `SRO`（`static readonly` 字段） | **true** | **true** | true | number | — |
| `S`（`static` 字段） | **true** | true | true | number | — |
| `FN`（字段持函数） | true | true | true | function | 2 |
| `arrow`（字段持箭头函数） | true | true | true | function | 1 |
| `asf`（async 方法） | false | true | true | function | 1 |
| `BIG`（`123n`） | true | true | true | **bigint** | — |
| `UNDEF`（`undefined`） | true | true | true | undefined | — |
| `NUL`（`null`） | true | true | true | object | — |
| `SYM`（`Symbol()`） | true | true | true | **symbol** | — |

补充（`desc2.mjs`）：
- `static #priv`（私有字段）**不出现**在 `getOwnPropertyNames`，`getOwnPropertySymbols` 为空。
- **静态成员不继承**：`class D extends B` 时 `getOwnPropertyNames(D)` = `length,name,prototype,ds,df`；
  `hasOwn(D,"bs")` = false ⇒ 基类链必须靠 `base` 递归，不能用 `getOwnPropertyNames` 取全链。
- 枚举对象：`getOwnPropertyNames(E)` = `0,1,A,B`；`typeof` = `object`；`Array.isArray` = **false**；
  `E.constructor.name` = `"Object"`（**不是** `Array`）。
- 字符串枚举 `enum ES { X = "x", Y = "y" }`：只有 `X,Y`，**无反向映射**。
- JS 数组：`typeof` = object、`Array.isArray` = true。

### 对 design 的两处修正

1. **§4.1 的枚举理由错了**：`static` **字段**是**可枚举**的，只有**方法**不可枚举。
   `Object.keys` 会漏掉方法、`for...in` 会带上字段。结论不变（必须用 `getOwnPropertyNames`），
   但理由要改写成「同时覆盖字段（可枚举）与方法（不可枚举）」。
2. **必须显式跳过 `length` / `name` / `prototype`**（前两者非枚举但 `getOwnPropertyNames` 会返回；
   `prototype` 是实例原型，另有枚举路径）。
3. `static readonly` 的 `writable` 仍为 `true` ⇒ **运行期无法判定 const**（§4.2 结论得到实测支撑）。
4. `Function.prototype.length` 对 rest 参数（`sfa(...args)`）为 **0**、对默认值参数同样会截断
   ⇒ 参数个数只能当「上限提示」，不能当精确元数（§6.4 需据此写明）。

---

## 0.2 `js_to_gd_var` 判定表 ✅ 源码实读完成（运行期待验）

两处入口（`src/runtime/bridge/jsb_type_convert.cpp`）：

| 入口 | 行 | 用途 |
|---|---|---|
| 无类型提示 `js_to_gd_var(isolate, ctx, val, Variant&)` | `:469-560` | 通用（`_get` 返回、参数无提示） |
| 带类型提示 `js_to_gd_var(isolate, ctx, val, Variant::Type, Variant&)` | `:149-324` | 期望类型已知时 |

### 无提示路径（`:469-560`）判定

| JS 值 | 结果 |
|---|---|
| `null` / `undefined` / empty | `true`，`r_cvar = {}`（**NIL**） |
| `boolean` | `true` → BOOL |
| `int32` / `uint32` / `number` | `true` → INT / INT / FLOAT |
| `string` | `true` → 命中 StringName 缓存则 STRING_NAME，否则 STRING |
| `ArrayBuffer` | `true` → `PackedByteArray` |
| **BigInt** | `true` → INT（`Int64Value()`；`JSB_WITH_BIGINT = 1`，`src/jsb.config.h:155`） |
| 带 `ProxyTarget` 的对象 | 递归解包后再转 |
| `InternalFieldCount == IF_VariantFieldCount` | `true` → 该 Variant 原样（`GArray`/`GDictionary`/`GVector2`…） |
| `InternalFieldCount == IF_ObjectFieldCount` 且 `GodotObject` | `true` → OBJECT |
| **JS 原生 `Array`** | `InternalFieldCount() == 0` → `default:` → **`false`** |
| **纯 JS 对象 `{a:1}`** | 同上 → **`false`** |
| **`Symbol`** | 落到函数尾 `JSB_LOG(Error, "js_to_gd_var: unhandled type")` 后 **`false`** |
| `function` | 未处理（`:503-506` 的 TODO 分支被注释）→ **`false`** |

⇒ **两条硬结论**：
1. **常量里的 JS 原生数组**：无提示路径返回 `false`（`InternalFieldCount() == 0` ⇒ `default:`）；
   带 `Variant::ARRAY` 提示的重载走 `try_convert_array_any`（`:265`、`:74-101`）可转换，但它
   **新建**一个 `Array`（新 `_p`）⇒ 与 JS 原数组**无共享**，施加只读保护不了 JS 侧。
   ⇒ **作者必须用 `GArray.create([...])`**（命中 `IF_VariantFieldCount`，与 JS 侧共享 `_p`，
   `make_read_only()` 才能两侧同时冻结 —— 见 R2.3）。
2. **纯 JS 对象字面量在任何路径都转不成 `Dictionary`**：带提示的 `case Variant::DICTIONARY`
   走 `FALLBACK_TO_VARIANT`，要求 `is_variant(self)`（即必须是 `GDictionary` 包装）。
   ⇒ `static readonly D = { a: 1 }` **不能**作常量；作者须用 `GDictionary.create({a: 1})`，
   枚举对象则由我方代码手工构造 `Dictionary`（§4.3 的归一化），不指望 `js_to_gd_var`。
3. **不可先无脑调 `js_to_gd_var`**：`Symbol` 会打 `Error` 日志。必须先按 `typeof` 预筛
   （`boolean`/`number`/`string`/`bigint`/`object`），否则未注解的静态属性会污染日志（R1.2 要求静默）。

---

## 0.3 `make_read_only()` 运行期语义 ✅ 已完成（GDScript 探针 `ro.gd` 实跑）

`Array::make_read_only()` / `Dictionary::make_read_only()` 的**浅层**语义（`array.cpp:936-940`、
`dictionary.cpp:595-598` 只置 `_p->read_only` 指针，不递归）。

| 操作 | `is_read_only()` |
|---|---|
| `var a := [1,2,3]` 初始 | `false` |
| `a.make_read_only()` 后 | `true` |
| `a.duplicate()` | **`false`** ← 只读标记**不**随副本传递 |
| `a.duplicate(true)` | **`false`** |
| 浅层：`nested.make_read_only()` 后 `nested` / `nested[0]` | `true` / **`false`** |
| `Dictionary` 同上（`make_read_only` → `true`；`duplicate()` → `false`） | 同 |
| 只读 Array 放进 `Dictionary` 后取出 | 仍 `true`（标记随 `_p` 走） |
| 只读元素的 `duplicate()` | `false` |

⇒ `Object::get` 返回的 Variant 副本与原存储**共享 `_p`**，只读标记保留 —— 这正是容器常量
"两侧同时冻结"的机制基础（2026-09-25 用户拍板纳入容器后，该标记用于**枚举 `Dictionary` 与
`ARRAY`/`DICTIONARY` 容器常量**，容器需**递归**施加以闭合与 GDScript 的深度差，见 §0.6 与 R2.3）。

目标 API 4.7 内含该方法：`extension_api-4-7.json` 的 `Array` / `Dictionary` 均含
`make_read_only` / `is_read_only`；godot-cpp 绑定 `array.hpp:224-225`、`dictionary.hpp:148-149`。
**`Packed*Array` 无 `is_read_only` 方法**（已在 api json 核对）⇒ 无法靠只读标记保护
（`Packed*` 不进常量白名单，见 R2.3 —— 它既无只读 API、也不像值类型那样走写回）。

## 0.4 运行期 MethodBind 注册探针

~~未做 —— 仅当 Q1 选 (b) 时才需要（`design.md` §6.3 选项 B）。~~
**不做**：用户已选 Q1=(c)，静态函数整体剔除（`prd.md` R5 / R8.5），该探针失去前提。

## 0.5 GDScript 侧「写入常量」的失败形态（补测，写入 `design.md` §5.3）

> 起因：`design.md` §5.3 曾记「写入只读容器会让进程**永久挂起**」。补测后判定该结论是**探针假象**。

探针：`.agent_tmp/gdcallable/`（任务收尾已清理） 的 `y1`–`y8`（const 基）、`w1`–`w6`、`z1`–`z6`（非 const 基）、`v1.gd`。

| 基表达式形态 | `H.V.x = 9` / `H.ARR[0] = 7` / `H.N = 5` 的结果 |
|---|---|
| `const H := preload(...)`（const 基） | **编译期** `Cannot assign a new value to a constant.`（`y3`/`y4`/`y5`），脚本根本不加载 |
| `var h = load(...)` / 未标注类型的函数参数 | **运行期**脚本错误：值类型 `Invalid assignment of property or key 'V' …`；容器 `Invalid assignment on read-only value (on base: 'Array')`（`w1`–`w6`、`z1`–`z6`） |

**「挂起」是假象**：那批早期探针把 `quit(0)` 写在 `_initialize()` 末尾，而运行期脚本错误只**终止当前
GDScript 函数**，于是 `quit` 永不执行、进程空转。反证：

- `z1`–`z6`：错误放在被 `_initialize` 调用的辅助函数里 ⇒ `_initialize` **继续执行**并正常 `quit`
  （全部 rc=0，`STEP2-OK` 打印）。
- `y6`–`y8`：错误放在 Node 的 `_ready` 里 ⇒ 只终止 `_ready`，进程 rc=0。

**存储值未被改动**（`v1.gd` 取证）：同一 `preload` 脚本上连续执行三种写入，
`BEFORE V=(3.0, 4.0) ARR=[1, 2, 3] N=7` → `AFTER` **三项完全相同**，3 条脚本错误，rc=0。

⇒ 真实后果 = **打印一条脚本错误 + 终止当前 GDScript 函数**，与任何其它脚本错误同级。
**GDScript 侧**值类型与容器的常量性都成立，**不需要**靠"挂起"来保护。

> 2026-09-25 用户拍板后我方支持面 = 「JS 基础值 + enum + 容器（`GArray`/`GDictionary` 包装）」
> （R2.3）⇒ 上表 `H.N` 与 `H.ARR[0]` 两行对应对我方暴露的常量类型；`H.V.*`（值类型）仅作机制参照。

## 0.6 冻结深度：GDScript `const` 是**深层**，我方 `make_read_only()` 是**浅层**（2026-09-24 补测）

> 起因：`prd.md` Background 曾写「`const` 的容器**只有顶层** read-only（内层 `Array` 可写）」。
> 该结论来自早期 `p_i.gd`（**同脚本内裸标识符** `NESTED[0].append(99)` 成功），但跨脚本经
> `Object::get` 取常量时行为不同。补测后判定原结论**只在裸标识符形态下成立**，需改写。

探针 `p_ro4.gd`（`h = preload("res://h_n.gd")`，`h_n.gd` 为 `extends RefCounted` 的常量宿主）：

```
ARR      ro=true
NESTED   ro=true
NESTED[0] ro=true      ← 内层也只读
D        ro=true
D['k']   ro=true       ← 容器内嵌的值也只读
after inner.append(99): NESTED=[[1, 2], [3]]   ← 改不动；报 Array is in read-only state
```

对照探针 `p_ro3.gd`（我方手工施加）：

```
mine ARR    ro=true
mine NESTED ro=true  inner ro=false   ← 浅层，内层可写
```

⇒ **不对称**：

| 来源 | 冻结深度 |
|---|---|
| GDScript `const` 容器（经脚本对象取） | **深层**（内层容器一并只读） |
| 我方 `Array::make_read_only()` | **浅层**（引擎 API 语义，内层容器仍可写） |

**状态（2026-09-25 用户拍板后）**：本节从"分歧记录"变成**实现依据** —— 容器常量**已纳入**
（R2.3），因此必须**递归**施加 `make_read_only()`，才能与 GDScript 经脚本对象取常量时的**深层**
冻结对齐。浅层 API + 自行递归遍历内层，即得到深层效果。

保留本节的价值：① 它证明「GDScript 的 `const` 冻结深度随取值形态变化」（裸标识符浅、经脚本对象
取深），这是决定"必须递归"的依据；② 它给出 `make_read_only()` 是浅层的直接证据（`p_ro3.gd`）。
