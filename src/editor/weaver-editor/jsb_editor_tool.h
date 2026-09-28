/************************************************************************/
/*  jsb_editor_tool.h                                                   */
/*                                                                      */
/*  This file is part of:                                               */
/*                                GodotJS-Ext                           */
/*              https://github.com/Daylily-Zeleen/GodotJS-Ext           */
/*                                                                      */
/*  Copyright (c) 2026-present 忘忧の (Daylily-Zeleen)                  */
/*                 - Contact: daylily-zeleen@foxmail.com                */
/*                                                                      */
/*  This library is free software; you can redistribute it and/or       */
/*  modify it under the terms of the GNU Lesser General Public          */
/*  License as published by the Free Software Foundation; either        */
/*  version 2.1 of the License, or (at your option) any later version.  */
/*                                                                      */
/*  This library is distributed in the hope that it will be useful,     */
/*  but WITHOUT ANY WARRANTY; without even the implied warranty of      */
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU    */
/*  Lesser General Public License for more details.                     */
/*                                                                      */
/*  You should have received a copy of the GNU Lesser General Public    */
/*  License along with this library; if not,                            */
/*  see <https://www.gnu.org/licenses/>.                                */
/************************************************************************/

#pragma once

#include "jsb_editor_pch.h"

#if JSB_USE_TYPESCRIPT

#	include <condition_variable>
#	include <mutex>

namespace jsb::editor {

/**
 * 常驻 Node 编辑器工具进程的客户端（签名提取 + 帮助文档提取）。
 *
 * 为什么常驻：每次 spawn 都要重新 `require("typescript")`（数十 MB JS 解析 + JIT），而它同时
 * 服务两种提取。协议见 `scripts/jsb.tools/src/jsb.editor.tools.cts` 头部注释：
 * NDJSON over stdin/stdout。
 *
 * 两类时刻：请求是**阻塞**的（上限 `kRequestTimeoutMsec`），因为调用点（编辑器安装/重扫、
 * 脚本加载）本来就在同步上下文里，且作者期望改完立刻看到结果。
 *
 * @note 触达本类的线程只有编辑器主线程，加上工具进程的读取线程（它只把行推进队列）。
 */
class EditorToolClient {
public:
	/** 单次请求的阻塞上限（用户裁决：5s）。 */
	static constexpr int kRequestTimeoutMsec = 5000;

	EditorToolClient();
	~EditorToolClient();

	EditorToolClient(const EditorToolClient &) = delete;
	EditorToolClient &operator=(const EditorToolClient &) = delete;

	/**
	 * 启动项目里的工具进程（重复调用是空操作）。工具产物尚未安装时返回 false。
	 * @param p_project_root 项目根的**绝对**路径（子进程继承引擎 cwd，相对路径不可靠）
	 */
	bool start(const String &p_project_root);

	/** 关闭并 join（析构也会做）。 */
	void stop();

	/**
	 * 发一条请求并等待应答（上限 `kRequestTimeoutMsec`）。
	 *
	 * **故障处理不静默**（用户裁决）：
	 *  - 进程已死/写失败 ⇒ 打印错误 + 明确告知即将重启，重启后**重试一次**；
	 *  - 超时 ⇒ 打印含 `op`/请求内容/超时值的错误，放弃本次请求（**不重启进程**：
	 *    它可能只是慢，`stop()` 会把它连同正在做的工作一起杀掉）。
	 *
	 * @param p_op       `"signatures"` / `"doc"` / …
	 * @param p_payload  请求体的额外字段（写入 JSON 对象；`op` / `id` 由本函数填）
	 * @param r_response 成功时为解析后的应答字典（含 `ok`）
	 * @return 收到**且** `ok==true` 时为 true
	 */
	bool request(const String &p_op, const Dictionary &p_payload, Dictionary &r_response);

private:
	bool _spawn();
	/** 收尾旧进程（`Process::stop()` join 读取线程）并清空句柄。**不得**用 `process_.reset()` 代替。 */
	void _teardown_process();
	void _on_line(const String &p_line);
	void _on_eof(const String &p_eof_reason);

	/** 需要 `mutex_`。把待处理应答移到 `pending_`（不持锁等）。 */
	bool _queue_response(const String &p_line);

	std::shared_ptr<jsb::internal::Process> process_;
	String project_root_;

	// 请求-应答配对。`responded_` 只需保护队列，不在锁内做任何 io 或回调。
	std::mutex mutex_;
	std::condition_variable cv_;
	HashMap<int, Dictionary> responded_;
	int next_request_id_ = 1;
	bool eof_ = false;
	String eof_reason_;
};

} //namespace jsb::editor

#endif // JSB_USE_TYPESCRIPT
