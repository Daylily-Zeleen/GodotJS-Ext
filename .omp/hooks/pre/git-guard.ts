import type { ExtensionAPI } from "@oh-my-pi/pi-coding-agent";

// ---------------------------------------------------------------------------
// git 危险操作闸门
// ---------------------------------------------------------------------------
//
// 目标：拦住「不可逆且会丢数据」的 git 操作，要求用户当场确认。
// 明确不拦：commit / push / reset --soft / checkout <branch> / branch -d
// 等可恢复操作——它们由 AGENTS.md 的授权约定约束，不在此处制造摩擦。
//
// 这是绊线（tripwire），不是保险箱：它只覆盖 `bash` 工具的参数文本，覆盖不到
// 其他工具、脚本文件内部、以及无法廉价识别的间接写法（如变量拼接、复杂引号嵌套）。
// 作用是拦住疏忽，不是防蓄意规避。

/**
 * 短选项簇里是否含某个字母：`-f`、`-fd`、`-xdf` 都命中 `f`。
 * 带参数的长选项（`--force`）不匹配，须单独列举；大小写敏感（`-D` ≠ `-d`）。
 */
function hasShort(tokens: string[], letter: string): boolean {
   return tokens.some((t) => /^-[a-zA-Z]+$/.test(t) && t.slice(1).includes(letter));
}

/** 单个 shell 片段的危险判定；返回原因即为命中，返回 null 表示放行。 */
function checkGit(sub: string, rest: string[]): string | null {
   switch (sub) {
      case "reset":
         // --soft / --mixed 不动工作区，只有 --hard 丢改动
         return rest.includes("--hard") ? "`git reset --hard` 会丢弃工作区改动" : null;

      case "clean":
         // -n / --dry-run 只预演，优先级高于 -f（与 git 自身语义一致）
         if (hasShort(rest, "n") || rest.includes("--dry-run")) return null;
         // -f 簇与等价的 --force 都会删未跟踪文件
         return hasShort(rest, "f") || rest.includes("--force")
            ? "`git clean -f` / `--force` 会删除未跟踪文件"
            : null;

      case "push": {
         for (const t of rest) {
            // --force-with-lease 是安全的强制推送方式（远端有新提交即失败），放行
            if (/^--force-with-lease(=.*)?$/.test(t)) continue;
            if (t === "--force" || hasShort([t], "f")) {
               return "`git push --force` / `-f` 会覆盖远端历史；如需强制推送请改用 `--force-with-lease`";
            }
            // 删远端分支/标签：远端独有的提交就此失去引用
            if (t === "--delete" || hasShort([t], "d")) {
               return "`git push --delete` / `-d` 会删除远端分支或标签";
            }
            // 镜像推送让远端引用与本地完全一致（含删掉远端多出的）
            if (t === "--mirror") {
               return "`git push --mirror` 会让远端引用与本地完全一致（含删除）";
            }
            // 空 source 的 refspec（`git push origin :branch`）同样是删远端引用
            if (/^:[^:]+$/.test(t)) {
               return "`git push origin :<ref>` 会删除远端引用";
            }
            // `+<refspec>` 是另一种强制推送写法
            if (t.startsWith("+")) {
               return "`git push +<refspec>` 会覆盖远端历史；如需强制推送请改用 `--force-with-lease`";
            }
         }
         return null;
      }

      case "checkout":
         // `git checkout -- <路径>` 丢弃指定路径改动；`-f`/`--force` 丢弃全部未提交改动
         if (rest.includes("--")) return "`git checkout --` 会丢弃工作区改动";
         return hasShort(rest, "f") || rest.includes("--force")
            ? "`git checkout -f` / `--force` 会丢弃未提交改动"
            : null;

      case "switch":
         // 与 checkout -f 等价：强制切换即丢弃未提交改动
         return hasShort(rest, "f") || rest.includes("--force") || rest.includes("--discard-changes")
            ? "`git switch -f` / `--discard-changes` 会丢弃未提交改动"
            : null;

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

      case "branch": {
         // `-d`/`--delete` 单独用时未合并会被拒绝（安全）；但配合 `-f`/`--force`（含 `-d --force`
         // 与 `-D` 等价，实测确认）就无条件删——所以「删除」与「强制」两个条件同时成立才拦。
         const deleting = hasShort(rest, "d") || hasShort(rest, "D") || rest.includes("--delete");
         const forced = hasShort(rest, "f") || hasShort(rest, "D") || rest.includes("--force");
         return deleting && forced ? "`git branch -D` / `-d --force` 会强制删除未合并分支" : null;
      }

      case "tag":
         return hasShort(rest, "d") || hasShort(rest, "D") || rest.includes("--delete")
            ? "`git tag -d` 会删除标签，且标签不在 reflog 保护范围内"
            : null;

      case "update-ref":
         return hasShort(rest, "d") || rest.includes("--delete")
            ? "`git update-ref -d` 会直接删除引用，绕过 reflog 提示"
            : null;

      case "worktree":
         // 不带 --force 时脏工作树会被拒绝；带 --force 直接丢弃
         return rest[0] === "remove" && (hasShort(rest, "f") || rest.includes("--force"))
            ? "`git worktree remove --force` 会丢弃该工作树的未提交改动"
            : null;

      case "reflog":
         return rest[0] === "expire" ? "`git reflog expire` 会清除对象的恢复路径" : null;

      case "gc":
         // 默认保留 2 周可达性保护；--prune=now|all 立即清除不可达对象
         return rest.some((t) => t === "--prune=now" || t === "--prune=all")
            ? "`git gc --prune=now` 会立即清除不可达对象，reflog 兜不住"
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

/**
 * 会吞掉下一个 token 的包装命令选项（如 `sudo -u root` 的 `-u`）。
 * 长选项带 `=` 时自含值，不必列出。`command` 的选项全是无值的。
 * `timeout` 另有一个位置参数（时长），由 `WRAPPER_POSITIONAL` 处理。
 */
const WRAPPER_VALUE_FLAGS: Record<string, Record<string, true>> = {
   sudo: {
      "-u": true,
      "-g": true,
      "-p": true,
      "-C": true,
      "-D": true,
      "-h": true,
      "-r": true,
      "-t": true,
      "-T": true,
      "-U": true,
   },
   env: { "-u": true, "-C": true, "-S": true },
   command: {},
   nice: { "-n": true },
   stdbuf: { "-i": true, "-o": true, "-e": true },
   timeout: { "-s": true, "-k": true },
   time: {},
   nohup: {},
};

/** 包装命令在选项之后还有几个位置参数（`timeout 5 git …` 的时长）。 */
const WRAPPER_POSITIONAL: Record<string, number> = { timeout: 1 };

/** 剥掉 env 赋值与包装命令（含包装自身选项），返回剩余 token 的起点。 */
function skipWrappers(tokens: string[]): number {
   let i = 0;
   while (i < tokens.length && ENV_ASSIGN.test(tokens[i]!)) i++;
   while (i < tokens.length) {
      const name = tokens[i]!;
      const valueFlags = WRAPPER_VALUE_FLAGS[name];
      if (!valueFlags) break;
      i++;
      // 包装自身的选项：取值的吞掉下一个 token，其余只吞自己；`--` 结束选项
      while (i < tokens.length && tokens[i]!.startsWith("-")) {
         const flag = tokens[i]!;
         i++;
         if (valueFlags[flag]) i++;
         if (flag === "--") break;
      }
      // 选项之后的位置参数（如 `timeout 5 git …` 的时长）
      for (let n = WRAPPER_POSITIONAL[name] ?? 0; n > 0 && i < tokens.length; n--) i++;
      while (i < tokens.length && ENV_ASSIGN.test(tokens[i]!)) i++;
   }
   return i;
}

/**
 * 判定某 token 是否是 git 可执行文件。Windows 上 `git.exe` 与
 * `C:\...\Git\bin\git.exe` 都合法，故按基名比较而忽略路径与 `.exe` 后缀。
 */
function isGitBinary(token: string): boolean {
   const base = token.split(/[\\/]/).pop() ?? token;
   return /^git(\.exe)?$/i.test(base);
}

/** 剥掉 env 赋值与包装命令，取 `git` 之后的参数；非 git 命令返回 null。 */
function gitArgs(tokens: string[]): string[] | null {
   let i = skipWrappers(tokens);
   if (!tokens[i] || !isGitBinary(tokens[i]!)) return null;
   i++;
   // git 全局参数；其中 -C/-c 等会吞掉下一个 token
   while (i < tokens.length && tokens[i]!.startsWith("-")) {
      if (GIT_VALUE_FLAGS[tokens[i]!]) i++;
      i++;
   }
   return tokens.slice(i);
}

/**
 * 会重新执行一段命令串的 shell；`-c` 之后的参数即待执行命令。
 * 注意 `cmd /c`、`powershell -Command` 等价于这些 shell 的 `-c`，一并处理。
 */
const SHELLS: Record<string, true> = {
   bash: true,
   sh: true,
   zsh: true,
   dash: true,
   ksh: true,
   ash: true,
};

/** 内层命令由开关引导的宿主：`cmd /c`、`powershell -c|-Command`、`pwsh -c`。 */
const CMD_SWITCHES_BY_HEAD: Record<string, Record<string, true>> = {
   cmd: { "/c": true },
   "cmd.exe": { "/c": true },
   powershell: { "-c": true, "-command": true },
   "powershell.exe": { "-c": true, "-command": true },
   pwsh: { "-c": true, "-command": true },
   "pwsh.exe": { "-c": true, "-command": true },
};

/**
 * `bash -c '...'` / `sh -lc "..."` / `eval '...'` / `cmd /c "..."` 的内层命令串；
 * 非此类返回 null。不递归展开的包装（`sudo git ...`）由 gitArgs 处理，不走这里。
 */
function innerCommand(tokens: string[]): string | null {
   const head = tokens[0];
   if (!head) return null;
   if (head === "eval") return tokens.slice(1).join(" ") || null;

   if (SHELLS[head]) {
      // `-c` 可与其他短选项同簇（-lc / -ec / -xc）
      for (let i = 1; i < tokens.length; i++) {
         if (!/^-[a-zA-Z]*c$/.test(tokens[i]!)) continue;
         return tokens.slice(i + 1).join(" ") || null;
      }
      return null;
   }

   const switches = CMD_SWITCHES_BY_HEAD[head.toLowerCase()];
   if (!switches) return null;
   for (let i = 1; i < tokens.length; i++) {
      if (!switches[tokens[i]!.toLowerCase()]) continue;
      return tokens.slice(i + 1).join(" ") || null;
   }
   return null;
}

/** 判定整条命令；命中返回中文原因，否则返回 null。 */
export function classify(command: string): string | null {
   for (const raw of splitSegments(command)) {
      // 子 shell 语法 `( ... )` 只多一层括号，剥掉后仍按普通片段判
      const trimmed = raw.trim();
      const segment =
         trimmed.startsWith("(") && trimmed.endsWith(")")
            ? trimmed.slice(1, -1).trim()
            : trimmed;
      const tokens = tokenize(segment);
      // 剥 env 赋值与 sudo/command/env 包装后，先看是否是 shell 复读
      const nested = innerCommand(tokens.slice(skipWrappers(tokens)));
      if (nested) {
         const hit = classify(nested);
         if (hit) return hit;
         continue;
      }
      const args = gitArgs(tokens);
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
