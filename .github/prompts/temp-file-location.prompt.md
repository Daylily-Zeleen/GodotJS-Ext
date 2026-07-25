---
name: temp-file-location
description: "确保所有临时文件都创建在 ./.agent_tmp 目录下，避免散落在项目其他位置。"
---

**Purpose**
- Enforce a consistent location for any temporary files generated during tasks.
- Prevents clutter and accidental file creation in unrelated directories.

**Guidelines**
1. When a task requires creating a temporary file, always use the path `./.agent_tmp/<filename>` relative to the workspace root.
2. If the `.agent_tmp` directory does not exist, create it before writing the file.
3. Do not write temporary files to any other location (e.g., the project root, `src/`, `test/`, etc.).
4. Clean up temporary files after the task completes if they are no longer needed.

**Example Usage**
```bash
# Correct
mkdir -p ./.agent_tmp
echo "data" > ./.agent_tmp/temp.txt

# Incorrect
echo "data" > temp.txt  # ❌ This violates the prompt
```

**Integration**
- Include this prompt in your workflow when writing scripts, tests, or automation that generate intermediate files.
- Reference the prompt in code comments or documentation to remind contributors of the rule.

**Note**
- The `.agent_tmp` folder itself should be added to `.gitignore` to avoid committing temporary artifacts.