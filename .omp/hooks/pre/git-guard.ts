import type { ExtensionAPI } from "@oh-my-pi/pi-coding-agent";

// ---------------------------------------------------------------------------
// git 危险操作闸门
// ---------------------------------------------------------------------------
//
// 目标：拦住「不可逆且会丢数据」的 git 操作，要求用户当场确认。
// 明确不拦：commit / push / reset --soft / checkout <branch> / branch -d
// 等可恢复操作——它们由 AGENTS.md 的授权约定约束，不在此处制造摩擦。
//
// 这是绊线（tripwire），不是保险箱：它只覆盖 bash 工具，不覆盖经 eval 起的
// 子进程。作用是拦住疏忽，不是防蓄意规避。

/** 单个 shell 片段的危险判定；返回原因即为命中，返回 null 表示放行。 */
function checkGit(sub: string, rest: string[]): string | null {
   switch (sub) {
      case "reset":
         // --soft / --mixed 不动工作区，只有 --hard 丢改动
         return rest.includes("--hard") ? "`git reset --hard` 会丢弃工作区改动" : null;

      case "clean":
         // -n / --dry-run 无害；含 f 的短标志组合（-f、-fd、-fdx）才删文件
         return rest.some((t) => /^-[a-zA-Z]*f/.test(t))
            ? "`git clean -f` 会删除未跟踪文件"
            : null;

      case "push": {
         for (const t of rest) {
            // --force-with-lease 是安全的强制推送方式（远端有新提交即失败），放行
            if (/^--force-with-lease(=.*)?$/.test(t)) continue;
            if (t === "--force" || /^-[a-zA-Z]*f$/.test(t)) {
               return "`git push --force` / `-f` 会覆盖远端历史；如需强制推送请改用 `--force-with-lease`";
            }
            // `+<refspec>` 是另一种强制推送写法
            if (t.startsWith("+")) {
               return "`git push +<refspec>` 会覆盖远端历史；如需强制推送请改用 `--force-with-lease`";
            }
         }
         return null;
      }

      case "checkout":
         // `git checkout -- <路径>` 丢弃改动；`git checkout <分支>` 只是切换
         return rest.includes("--") ? "`git checkout --` 会丢弃工作区改动" : null;

      case "restore": {
         const staged = rest.some((t) => t === "--staged" || t === "-S");
         const worktree = rest.some((t) => t === "--worktree" || t === "-W");
         // 只动暂存区的 restore 是安全的
         return staged && !worktree ? null : "`git restore` 会丢弃工作区改动（仅 `--staged` 时放行）";
      }

      case "stash": {
         const action = rest[0];
         return action === "drop" || action === "clear" ? "丢弃 stash 不可恢复" : null;
      }

      case "branch":
         return rest.some((t) => /^-[a-zA-Z]*D$/.test(t))
            ? "`git branch -D` 会强制删除未合并分支"
            : null;

      case "filter-branch":
      case "filter-repo":
         return "重写历史不可逆";

      default:
         return null;
   }
}

/** 按未加引号的 `&&` `||` `;` `|` `&` 与换行拆分命令。 */
function splitSegments(command: string): string[] {
   const parts: string[] = [];
   let cur = "";
   let quote: '"' | "'" | null = null;

   for (let i = 0; i < command.length; i++) {
      const ch = command[i]!;
      if (quote) {
         cur += ch;
         if (ch === "\\" && quote === '"' && i + 1 < command.length) {
            cur += command[++i]!;
         } else if (ch === quote) {
            quote = null;
         }
         continue;
      }
      if (ch === '"' || ch === "'") {
         quote = ch;
         cur += ch;
         continue;
      }
      if (ch === "\\" && i + 1 < command.length) {
         cur += ch + command[++i]!;
         continue;
      }
      if (ch === "&" || ch === "|" || ch === ";" || ch === "\n") {
         parts.push(cur);
         cur = "";
         if ((ch === "&" || ch === "|") && command[i + 1] === ch) i++;
         continue;
      }
      cur += ch;
   }
   parts.push(cur);
   return parts;
}

/** 按 shell 词法拆分单个片段（引号内不分词）。 */
function tokenize(segment: string): string[] {
   const out: string[] = [];
   let cur = "";
   let quote: '"' | "'" | null = null;

   for (let i = 0; i < segment.length; i++) {
      const ch = segment[i]!;
      if (quote) {
         if (ch === "\\" && quote === '"' && i + 1 < segment.length) {
            cur += segment[++i]!;
            continue;
         }
         if (ch === quote) {
            quote = null;
            continue;
         }
         cur += ch;
         continue;
      }
      if (ch === '"' || ch === "'") {
         quote = ch;
         continue;
      }
      if (ch === "\\" && i + 1 < segment.length) {
         cur += segment[++i]!;
         continue;
      }
      if (/\s/.test(ch)) {
         if (cur) {
            out.push(cur);
            cur = "";
         }
         continue;
      }
      cur += ch;
   }
   if (cur) out.push(cur);
   return out;
}

const ENV_ASSIGN = /^[A-Za-z_][A-Za-z0-9_]*=/;
/** 会吞掉下一个 token 的 git 全局参数。 */
const GIT_VALUE_FLAGS: Record<string, true> = {
   "-C": true,
   "-c": true,
   "--git-dir": true,
   "--work-tree": true,
   "--namespace": true,
   "--exec-path": true,
};

/** 剥掉 env 赋值与包装命令，取 `git` 之后的参数；非 git 命令返回 null。 */
function gitArgs(tokens: string[]): string[] | null {
   let i = 0;
   while (i < tokens.length && ENV_ASSIGN.test(tokens[i]!)) i++;
   while (i < tokens.length && (tokens[i] === "sudo" || tokens[i] === "command" || tokens[i] === "env")) {
      i++;
      while (i < tokens.length && ENV_ASSIGN.test(tokens[i]!)) i++;
   }
   if (tokens[i] !== "git") return null;
   i++;
   // git 全局参数；其中 -C/-c 等会吞掉下一个 token
   while (i < tokens.length && tokens[i]!.startsWith("-")) {
      if (GIT_VALUE_FLAGS[tokens[i]!]) i++;
      i++;
   }
   return tokens.slice(i);
}

/** 判定整条命令；命中返回中文原因，否则返回 null。 */
export function classify(command: string): string | null {
   for (const segment of splitSegments(command)) {
      const args = gitArgs(tokenize(segment));
      if (!args || args.length === 0) continue;
      const hit = checkGit(args[0]!, args.slice(1));
      if (hit) return hit;
   }
   return null;
}

export default function (pi: ExtensionAPI): void {
   pi.on("tool_call", async (event, ctx) => {
      if (event.toolName !== "bash") return;

      const input = event.input as { command?: unknown } | undefined;
      const command = typeof input?.command === "string" ? input.command : "";
      if (!command) return;

      const reason = classify(command);
      if (!reason) return;

      // 无交互界面（子代理 / 无头）无法取得用户确认 → 默认阻断
      if (!ctx.hasUI) {
         return { block: true, reason: `${reason}。当前无交互界面，默认阻断。` };
      }

      const approved = await ctx.ui.confirm("危险 git 操作", `${reason}\n\n$ ${command}`);
      if (!approved) {
         return { block: true, reason: `用户拒绝了该操作：${reason}` };
      }
      return;
   });
}
