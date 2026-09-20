import { appendFileSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";

import type { ExtensionAPI } from "@oh-my-pi/pi-coding-agent";

// ---------------------------------------------------------------------------
// Tool-loop repeat marker
// ---------------------------------------------------------------------------
//
// 问题（实测，2026-09-20）：一个用户轮次内，每次工具循环迭代都产出一句面向用户
// 的文本。单个 54 轮会话统计：1579 个中段文本块、合计 230,745 字符（约为最终答复
// 的 2.0 倍），69% 的带工具调用的 assistant 消息同时输出文本。按内容分类：进度旁白
// 约 93%、复述已完成状态 4%、复述工作流步骤 2%、重述用户要求 0.8%。
//
// 修法：工具结果收尾时，在**发往 provider 的消息**末尾追加一条短标记（custom
// 角色，转 wire 时映射为 developer），并在系统提示最前端声明该标记的含义。
//
// `context` 事件给的是**临时**消息数组，原会话消息不被修改，故标记不落盘、不
// 污染历史，也无须清理（每次请求从原始消息重建）。`accumulate` 模式用于压力
// 测试「若标记堆积会怎样」，见 withMarker。

/** 仅供 `registerMessageRenderer` 与调试识别用；**不参与任何判定**。 */
export const MARKER_TYPE = "loop-marker";

/**
 * 标记正文 —— 判定与查重的**唯一来源**（见 `isInjectedMessage`）。
 *
 * 约束对象是「面向用户的文本」与「计划/待办/进度」，不是「重复回答原问题」：
 * 实测（2026-09-20，单轮 1579 个中段文本块）重述用户要求仅占 0.8%，复述计划
 * 与待办占 6%，其余 93% 是进度旁白。
 */
export const MARKER_TEXT = `[System] You are in a multi-turn tool-calling loop. Between tool calls, output no user-facing text — issue the next tool call. Do not restate the plan, the todo list, or progress; those belong in the task's report.md. Only when the work is finished, write the final answer once.`;

/**
 * 规则文本。`write the final answer once the work is finished` 是关键：把结束
 * 条件定义为「工作完成」而非「答复已发出」，避免标记被读成「任务结束」。
 */
export const RULE = `## Tool-loop output discipline
Between tool calls, emit no user-facing text: issue the next tool call, and write the final answer once the work is finished. Progress and plans belong in the task's report.md, not in the reply.
A trailing developer message reading exactly "${MARKER_TEXT}" means this turn's reply was already delivered. Keep working with tools; do not restate it, narrate progress, or answer again.`;

export type Mode = "off" | "replace" | "accumulate";
export type ActiveMode = Exclude<Mode, "off">;

/** 静态字符串键查表。 */
export const VALID_MODE: Record<string, true> = {
   off: true,
   replace: true,
   accumulate: true,
};

/** 标记消息；结构上满足 CustomMessage（role "system"），故可送入消息数组。 */
export interface MarkerMessage {
   role: "system";
   // customType: string;
   content: string;
   display: boolean;
   timestamp: number;
}

// --- 运行时窄化（不依赖调用方的类型声明）------------------------------------

/** 取字符串属性；缺失或非字符串返回 undefined。 */
function stringProp(value: unknown, key: string): string | undefined {
   if (typeof value !== "object" || value === null) return undefined;
   if (!(key in value)) return undefined;
   const found = value[key];
   return typeof found === "string" ? found : undefined;
}

/** 数组项是否是 toolCall 块。 */
function isToolCallBlock(block: unknown): boolean {
   return stringProp(block, "type") === "toolCall";
}

/** 该消息的 content 里是否含 toolCall 块。 */
function hasToolCall(value: unknown): boolean {
   if (typeof value !== "object" || value === null) return false;
   if (!("content" in value)) return false;
   const content = value.content;
   if (!Array.isArray(content)) return false;
   return content.some(isToolCallBlock);
}

function roleOf(value: unknown): string | undefined {
   return stringProp(value, "role");
}

/** 判定本 hook 自己注入的标记：**必须比对正文**，不是比对 `MARKER_TYPE`。 */
function isInjectedMessage(value: unknown): boolean {
   return stringProp(value, "role") === "system" && stringProp(value, "content") === MARKER_TEXT;
}

function isNotInjectedMessage(value: unknown): boolean {
   return stringProp(value, "role") != "system" || stringProp(value, "content") != MARKER_TEXT;
}


/**
 * 尾部是工具结果 → 循环续轮；新 prompt 必然以 user 收尾。
 *
 * 跳过自身标记：让判定在「尾部已有标记」时仍成立。不跳过的话，第二次调用会把带
 * 标记的尾部误判成新轮而整条跳过，幂等性失效（replace 会退化成只注入一次）。
 */
export function isToolLoopTail(messages: readonly unknown[]): boolean {
   for (let i = messages.length - 1; i >= 0; i--) {
      const message = messages[i];
      if (isInjectedMessage(message)) continue;
      return roleOf(message) === "toolResult";
   }

   return false;
}

/**
 * 系统提示注入：规则置于最前端。已含规则则返回 null（防重复追加 → 幂等）。
 * 规则文本恒定 → 前缀字节不变 → provider 前缀缓存不受影响。
 */
export function withRule(base: readonly string[]): string[] | null {
   for (const block of base) {
      if (block.includes(MARKER_TEXT)) return null;
   }
   return [RULE, ...base];
}

/** 本轮已发生的工具迭代数。 */
export function countIterations(messages: readonly unknown[]): number {
   let n = 0;

   for (let i = messages.length - 1; i >= 0; i--) {
      const message = messages[i];
      if (roleOf(message) === "user") break;
      if (roleOf(message) !== "assistant") continue;
      if (hasToolCall(message)) n++;
   }

   return n;
}

/** 该消息的 content 里是否含非空 text 块（即面向用户的文本）。 */
function hasVisibleText(value: unknown): boolean {
   if (typeof value !== "object" || value === null) return false;
   if (!("content" in value)) return false;
   if (typeof value.content === "string") return value.content.trim().length > 0;
   if (!Array.isArray(value.content)) return false;

   return value.content.some((block) => {
      if (stringProp(block, "type") !== "text") return false;
      const text = stringProp(block, "text");
      return text !== undefined && text.trim().length > 0;
   });
}

/**
 * 本轮是否已经给出过面向用户的文本。
 *
 * 标记的全部价值在于「有/无」的**转换**：文本已出 → 注入标记（别再说一遍）；
 * 文本未出 → 不注入。若不管有没有答复都恒定注入，标记就成了假陈述，模型仍会
 * 找机会回答——实测证据：标记在任何文本出现前就已注入，该轮 6 个文本块里 5 个
 * 仍在重复「hook 生效」。
 */
export function hasDeliveredReply(messages: readonly unknown[]): boolean {
   for (let i = messages.length - 1; i >= 0; i--) {
      const message = messages[i];
      const role = roleOf(message);
      if (role === "user") return false;
      if (role === "assistant" && hasVisibleText(message)) return true;
   }

   return false;
}

/**
 * 重建发往 LLM 的消息：先清掉已存在的标记，再按模式追加。
 *
 * - `replace`：尾部恒为 **1** 条（目标行为；每次请求从零重建，天然等价于「清理
 *   掉上次的」）
 * - `accumulate`：尾部 **N** 条（N = 本轮迭代数），模拟「标记若持久化且逐轮堆积」
 *   的情形，用来验证清理是否真有必要
 *
 * 尾部不是工具结果（首轮）或本轮尚未给出答复时返回 null。其余消息顺序与内容逐字保留。
 */
export function withMarker<T>(
   messages: readonly T[],
   mode: ActiveMode,
   now: number,
): (MarkerMessage | T)[] | null {
   if (!isToolLoopTail(messages)) return null;
   if (!hasDeliveredReply(messages)) return null;

   const cleaned = messages.filter(isNotInjectedMessage);
   const copies = mode === "accumulate" ? Math.max(1, countIterations(cleaned)) : 1;

   const markers = Array.from({ length: copies }, (): MarkerMessage => ({
      role: "system",
      // customType: MARKER_TYPE,
      content: MARKER_TEXT,
      display: false,
      timestamp: now,
   }));

   return [...cleaned, ...markers];
}

export default function (pi: ExtensionAPI): void {
   let mode: Mode = "replace";
   let logPath: string | null = null;
   let modeFilePath: string | null = null;

   // 验证用日志：记录两个事件各自的触发次数与注入量。这是「系统提示注入是否
   // 一次性」的直接证据。失败不影响功能，静默吞掉。
   const log = (line: string): void => {
      if (!logPath) return;
      try {
         appendFileSync(logPath, `${new Date().toISOString()} ${line}\n`);
      } catch {
         // 日志是辅助物
      }
   };

   pi.on("session_start", async (_event, ctx) => {
      const tmpDir = join(ctx.cwd, ".agent_tmp");
      try {
         mkdirSync(tmpDir, { recursive: true });
      } catch {
         // 已存在
      }

      logPath = join(tmpDir, "loop-marker.log");
      modeFilePath = join(tmpDir, "loop-marker.mode");

      try {
         const stored = readFileSync(modeFilePath, "utf-8").trim();
         if (VALID_MODE[stored]) mode = stored as Mode;
      } catch {
         // 首次运行无此文件
      }

      log(`--- session_start mode=${mode} pid=${String(process.pid)}`);
   });

   pi.registerCommand("loop-marker", {
      description: "Tool-loop repeat marker mode: off | replace | accumulate",
      handler: async (args, ctx) => {
         const wanted = String(args ?? "").trim();

         if (!VALID_MODE[wanted]) {
            ctx.ui.notify(`loop-marker mode=${mode}  |  usage: /loop-marker off|replace|accumulate`, "info");
            return;
         }

         mode = wanted as Mode;

         if (modeFilePath) {
            try {
               writeFileSync(modeFilePath, `${mode}\n`);
            } catch {
               // 持久化失败不阻断切换
            }
         }

         log(`--- mode -> ${mode}`);
         ctx.ui.notify(`loop-marker: ${mode}`, "info");
      },
   });

   pi.on("before_agent_start", async (event) => {
      if (mode === "off") return;

      const base = event.systemPrompt ?? [];
      const next = withRule(base);

      if (!next) {
         log("sysprompt skip: rule already present");
         return;
      }

      log(`sysprompt inject rule (baseBlocks=${String(base.length)})`);
      return { systemPrompt: next };
   });

   pi.on("context", async (event) => {
      if (mode === "off") return;

      const next = withMarker(event.messages, mode, Date.now());

      if (!next) {
         log("ctx skip: tail is not toolResult (first prompt)");
         return;
      }

      const markers = next.filter(isInjectedMessage).length;
      log(`ctx inject markers=${String(markers)} size ${String(event.messages.length)}->${String(next.length)}`);
      return { messages: next };
   });
}
