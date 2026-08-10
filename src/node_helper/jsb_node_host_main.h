#pragma once

// Entry point of the standalone node helper executable (godotjs-ext.exe /
// godotjs-ext on POSIX). This thin host is used as the execPath for
// child_process.fork() probes: the embedded node runtime reports Godot as
// process.execPath, so forking a probe must start this helper instead.
// The helper is compiled into a small executable (see SConstruct) and the
// real probe logic lives in jsb_node_probe_host.cpp inside the main DLL.
#ifdef WINDOWS_ENABLED
// wide-character console entry (wmainCRTStartup is selected automatically by
// link.exe when wmain is defined)
int wmain(int p_argc, wchar_t **p_argv);
#else
int main(int p_argc, char **p_argv);
#endif
