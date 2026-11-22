/*+===================================================================
  File:      window_startup_test.cpp

  Summary:   Checks saved application geometry through ROMAW's automatic memory restart using an isolated profile.

  Classes:   Used: WindowProbe, std::wstring.

  Functions: wmain.

  Origin:    Executor Windows application-startup regression fixture.

  Copyright and Legal notices: License reference: COPYING.
===================================================================+*/

#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <string>

struct WindowProbe {
    HANDLE job;
    HWND window;
};

static void require(bool condition, const char* description)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s (Windows error %lu)\n", description, GetLastError());
        std::exit(1); // Closing this process's job handle terminates only its fixture tree.
    }
}

static BOOL CALLBACK find_window(HWND window, LPARAM context)
{
    auto& probe = *reinterpret_cast<WindowProbe*>(context);
    if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER))
        return TRUE;
    DWORD pid;
    GetWindowThreadProcessId(window, &pid);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    BOOL member = FALSE;
    if (process) {
        IsProcessInJob(process, probe.job, &member);
        CloseHandle(process);
    }
    if (member) {
        probe.window = window;
        return FALSE;
    }
    return TRUE;
}

int wmain(int argc, wchar_t** argv)
{
    require(argc == 4, "arguments: ROMAW.exe application isolated-user-profile");
    // This fixture never scans the excluded drive. The real Finder launcher
    // leaves geometry unspecified in exactly the same way as this command.
    require(!wcschr(argv[1], L'"') && !wcschr(argv[2], L'"'), "fixture paths contain no quotes");
    require(SetEnvironmentVariableW(L"USERPROFILE", argv[3]), "select isolated profile");
    for (int attempt = 0; attempt < 2; ++attempt) {
        HANDLE job = CreateJobObjectW(nullptr, nullptr);
        require(job != nullptr, "create fixture job");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        require(SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof limits),
                "confine restarted processes to fixture job");
        std::wstring command = L"\"" + std::wstring(argv[1]) + L"\" -skipdrives Z \"" + argv[2] + L"\"";
        STARTUPINFOW startup{};
        startup.cb = sizeof startup;
        PROCESS_INFORMATION process{};
        require(CreateProcessW(argv[1], command.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr, nullptr,
                               &startup, &process),
                "start ROMAW");
        if (!AssignProcessToJobObject(job, process.hProcess)) {
            TerminateProcess(process.hProcess, 1);
            require(false, "assign suspended fixture before it can restart");
        }
        ResumeThread(process.hThread);
        CloseHandle(process.hThread);
        WindowProbe probe{job, nullptr};
        ULONGLONG deadline = GetTickCount64() + 30000;
        while (!probe.window && GetTickCount64() < deadline) {
            EnumWindows(find_window, reinterpret_cast<LPARAM>(&probe));
            Sleep(50);
        }
        require(probe.window != nullptr, "application window appears after restart");
        RECT client{}, frame{};
        require(GetClientRect(probe.window, &client) && GetWindowRect(probe.window, &frame), "read application bounds");
        std::printf("launch %d: client=%ldx%ld location=%ld,%ld\n", attempt + 1, client.right, client.bottom,
                    frame.left, frame.top);
        require(client.right == 900 && client.bottom == 650, "saved client size survives memory restart");
        require(frame.left == 100 && frame.top == 120, "saved host position survives memory restart");
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
        require(QueryInformationJobObject(job, JobObjectBasicAccountingInformation, &accounting, sizeof accounting,
                                          nullptr),
                "read restart count");
        require(accounting.TotalProcesses >= 2, "exercise actual automatic memory restart");
        // The job owns both bootstrap and replacement ROMAW. Leave no test
        // application alive, even when the bootstrap has already exited.
        require(TerminateJobObject(job, 0), "finish fixture process tree");
        CloseHandle(process.hProcess);
        CloseHandle(job);
    }
    std::puts("PASS: saved size and position survive repeated application launches and automatic memory restarts");
    return 0;
}
