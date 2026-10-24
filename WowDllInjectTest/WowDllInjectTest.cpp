#include <windows.h>
#include <tlhelp32.h>
#include <commctrl.h>
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>
#include <sstream>  // 用于 std::ostringstream
#include <algorithm>
#include <thread>

#pragma comment(lib, "Comctl32.lib")

// 智能指针，自动关闭 Windows 句柄
struct HandleDeleter {
    void operator()(HANDLE handle) const {
        if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
        }
    }
};
using UniqueHandle = std::unique_ptr<void, HandleDeleter>;

constexpr DWORD kRequiredProcessAccess =
    PROCESS_CREATE_THREAD |
    PROCESS_QUERY_INFORMATION |
    PROCESS_VM_OPERATION |
    PROCESS_VM_READ |
    PROCESS_VM_WRITE;

std::string GetWindowsErrorMessage(DWORD error) {
    char* message = nullptr;
    const DWORD length = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<char*>(&message),
        0,
        nullptr);

    std::string result = length && message ? std::string(message, length) : "Unknown Windows error";
    if (message) {
        LocalFree(message);
    }
    while (!result.empty() && (result.back() == '\r' || result.back() == '\n')) {
        result.pop_back();
    }
    return result;
}

// 进程管理类
class ProcessManager {
public:
    // 构造函数：根据进程名查找并打开进程
    ProcessManager(const wchar_t* procName) {
        m_pid = GetProcessIdByName(procName);
        if (m_pid == 0) {
            throw std::runtime_error("Failed to find process ID."); // 找不到进程ID时抛出异常
        }
        std::cout << u8"找到目标进程 PID = " << m_pid << std::endl;

        // 打开进程句柄，并使用UniqueHandle自动管理
        m_hProcess = UniqueHandle(OpenProcess(kRequiredProcessAccess, FALSE, m_pid));
        if (!m_hProcess) {
            const DWORD error = GetLastError();
            throw std::runtime_error(
                "Failed to open process handle. Error " + std::to_string(error) +
                ": " + GetWindowsErrorMessage(error));
        }
    }

    // 构造函数：根据进程名查找并打开进程
    ProcessManager(const DWORD pid) {
        m_pid = pid;
        std::cout << u8"找到目标进程 PID = " << m_pid << std::endl;

        // 打开进程句柄，并使用UniqueHandle自动管理
        m_hProcess = UniqueHandle(OpenProcess(kRequiredProcessAccess, FALSE, pid));
        if (!m_hProcess) {
            const DWORD error = GetLastError();
            throw std::runtime_error(
                "Failed to open process handle. Error " + std::to_string(error) +
                ": " + GetWindowsErrorMessage(error));
        }
    }


    // 获取进程ID
    DWORD GetPid() const {
        return m_pid;
    }

    // 获取进程句柄
    HANDLE GetHandle() const {
        return m_hProcess.get();
    }

    // 静态方法：通过进程名查找进程ID
    static DWORD GetProcessIdByName(const wchar_t* procName) {
        const std::vector<DWORD> processIds = GetProcessIdsByName(procName);
        return processIds.empty() ? 0 : processIds.front();
    }

    static std::vector<DWORD> GetProcessIdsByName(const wchar_t* procName) {
        std::vector<DWORD> processIds;
        PROCESSENTRY32W pe = { sizeof(pe) };
        UniqueHandle hSnap(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)); // 使用UniqueHandle管理快照句柄
        if (!hSnap || hSnap.get() == INVALID_HANDLE_VALUE) return processIds;

        if (Process32FirstW(hSnap.get(), &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, procName) == 0) {
                    processIds.push_back(pe.th32ProcessID);
                }
            } while (Process32NextW(hSnap.get(), &pe));
        }
        return processIds;
    }

    // 静态方法：检查DLL是否已注入
    static bool IsDllInjected(DWORD pid, const wchar_t* dllName) {
        UniqueHandle hSnap(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid)); // 使用UniqueHandle管理快照句柄
        if (!hSnap) return false;

        MODULEENTRY32W me = { sizeof(me) };
        if (Module32FirstW(hSnap.get(), &me)) {
            do {
                if (_wcsicmp(me.szModule, dllName) == 0) {
                    return true;
                }
            } while (Module32NextW(hSnap.get(), &me));
        }
        return false;
    }

    // 注入DLL
    bool InjectDLL(const std::string& dllPath) {
        if (!m_hProcess) {
            std::cerr << u8"进程句柄无效。\n";
            return false;
        }

        // 分配远程内存
        size_t pathSize = dllPath.size() + 1;
        LPVOID remoteMem = VirtualAllocEx(m_hProcess.get(), nullptr, pathSize, MEM_COMMIT, PAGE_READWRITE);
        if (!remoteMem) {
            std::cerr << u8"远程内存分配失败\n";
            return false;
        }

        // 写入DLL路径到远程内存
        if (!WriteProcessMemory(m_hProcess.get(), remoteMem, dllPath.c_str(), pathSize, nullptr)) {
            std::cerr << u8"写入远程内存失败\n";
            VirtualFreeEx(m_hProcess.get(), remoteMem, 0, MEM_RELEASE); // 释放内存
            return false;
        }

        // 获取LoadLibraryA的地址
        FARPROC loadLibraryAddr = GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");
        if (!loadLibraryAddr) {
            std::cerr << u8"获取 LoadLibraryA 地址失败\n";
            VirtualFreeEx(m_hProcess.get(), remoteMem, 0, MEM_RELEASE); // 释放内存
            return false;
        }

        // 创建远程线程加载DLL
        UniqueHandle hThread(CreateRemoteThread(m_hProcess.get(), nullptr, 0, (LPTHREAD_START_ROUTINE)loadLibraryAddr, remoteMem, 0, nullptr));
        if (!hThread) {
            std::cerr << u8"创建远程线程失败\n";
            VirtualFreeEx(m_hProcess.get(), remoteMem, 0, MEM_RELEASE); // 释放内存
            return false;
        }

        WaitForSingleObject(hThread.get(), INFINITE); // 等待远程线程完成
        VirtualFreeEx(m_hProcess.get(), remoteMem, 0, MEM_RELEASE); // 释放远程内存
        return true;
    }

    // 从进程内存中读取指定类型的数据
    template <typename T>
    T ReadMemory(uintptr_t address) {
        T buffer{};
        SIZE_T bytesRead = 0;
        if (!ReadProcessMemory(m_hProcess.get(), reinterpret_cast<LPCVOID>(address), &buffer, sizeof(T), &bytesRead) || bytesRead != sizeof(T)) {
            std::cerr << u8"读取内存失败，错误码: " << GetLastError() << std::endl;
            return T{}; // 返回默认构造的值
        }
        return buffer;
    }

    // 从进程内存中读取字符串
    std::string ReadString(uintptr_t address, size_t maxLength = 256) {
        std::vector<char> buffer(maxLength, 0); // 使用vector动态分配缓冲区
        SIZE_T bytesRead = 0;
        if (!ReadProcessMemory(m_hProcess.get(), reinterpret_cast<LPCVOID>(address), buffer.data(), maxLength, &bytesRead)) {
            std::cerr << u8"读取内存失败，错误码: " << GetLastError() << std::endl;
            return "";
        }
        // 确保字符串以空字符结尾，防止读取到垃圾数据
        buffer[bytesRead < maxLength ? bytesRead : maxLength - 1] = '\0';
        return std::string(buffer.data());
    }

private:
    DWORD m_pid = 0; // 进程ID
    UniqueHandle m_hProcess; // 进程句柄，使用智能指针管理
};

// 命名管道客户端类
class PipeClient {
public:
    PipeClient(const std::string& pipeName) : m_pipeName(pipeName) {}

    // 连接管道并读取数据
    bool ReadData(uint64_t& data) {
        std::cout << u8"尝试连接管道...\n";
        UniqueHandle hPipe; // 使用智能指针管理管道句柄
        for (int i = 0; i < 10; ++i) { // 尝试连接多次
            hPipe = UniqueHandle(CreateFileA(
                m_pipeName.c_str(),
                GENERIC_READ,
                0,
                nullptr,
                OPEN_EXISTING,
                0,
                nullptr));

            if (hPipe && hPipe.get() != INVALID_HANDLE_VALUE) break; // 连接成功则跳出循环
            hPipe.reset();
            Sleep(500); // 否则等待500毫秒后重试
        }

        if (!hPipe || hPipe.get() == INVALID_HANDLE_VALUE) {
            std::cerr << u8"管道连接失败，请确认DLL注入并创建了管道。\n";
            return false;
        }

        DWORD bytesRead = 0;
        if (!ReadFile(hPipe.get(), &data, sizeof(data), &bytesRead, nullptr) || bytesRead != sizeof(data)) {
            std::cerr << u8"读取管道数据失败\n";
            return false;
        }

        std::cout << u8"收到玩家 GUID: " << data << std::endl;
        return true;
    }


    // 连接管道并读取数据
    bool readDataU8(std::string& data) {
        std::cout << "尝试连接管道...\n";
        UniqueHandle hPipe;

        char buffer[1024] = { 0 };

        for (int i = 0; i < 10; ++i) {
            hPipe = UniqueHandle(CreateFileA(
                m_pipeName.c_str(),
                GENERIC_READ,
                0,
                nullptr,
                OPEN_EXISTING,
                0,
                nullptr));

            if (hPipe && hPipe.get() != INVALID_HANDLE_VALUE) break;
            hPipe.reset();
            Sleep(500);
        }

        if (!hPipe || hPipe.get() == INVALID_HANDLE_VALUE) {
            std::cerr << "管道连接失败，请确认DLL注入并创建了管道。\n";
            return false;
        }

        DWORD bytesRead = 0;

        BOOL success = ReadFile(
            hPipe.get(),
            buffer,
            sizeof(buffer),
            &bytesRead,
            NULL);

        if (success && bytesRead > 0) {
            data.assign(buffer, bytesRead); // 安全赋值，不依赖 null
            std::cout << "收到数据（UTF-8）: " << data << std::endl;
            return true;
        }
        else {
            std::cerr << "读取管道失败，错误码: " << GetLastError() << std::endl;
            return false;
        }
    }

    bool SendCommand(const std::string& command, std::string& response) {
        HANDLE pipe = INVALID_HANDLE_VALUE;
        for (int i = 0; i < 10; ++i) {
            pipe = CreateFileA(
                m_pipeName.c_str(),
                GENERIC_READ | GENERIC_WRITE,
                0,
                nullptr,
                OPEN_EXISTING,
                FILE_FLAG_OVERLAPPED,
                nullptr);
            if (pipe != INVALID_HANDLE_VALUE) {
                break;
            }
            Sleep(200);
        }

        if (pipe == INVALID_HANDLE_VALUE) {
            return false;
        }

        UniqueHandle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!event) {
            CloseHandle(pipe);
            return false;
        }

        auto waitForIo = [&](OVERLAPPED& operation, BOOL started, DWORD& transferred) {
            if (started) {
                return true;
            }
            if (GetLastError() != ERROR_IO_PENDING
                || WaitForSingleObject(event.get(), 2000) != WAIT_OBJECT_0
                || !GetOverlappedResult(pipe, &operation, &transferred, FALSE)) {
                CancelIoEx(pipe, &operation);
                return false;
            }
            return true;
        };

        OVERLAPPED writeOperation{};
        writeOperation.hEvent = event.get();
        DWORD bytesWritten = 0;
        const BOOL wrote = WriteFile(pipe, command.data(), static_cast<DWORD>(command.size()),
            &bytesWritten, &writeOperation);
        if (!waitForIo(writeOperation, wrote, bytesWritten) || bytesWritten != command.size()) {
            CloseHandle(pipe);
            return false;
        }

        char buffer[4096]{};
        ResetEvent(event.get());
        OVERLAPPED readOperation{};
        readOperation.hEvent = event.get();
        DWORD bytesRead = 0;
        const BOOL read = ReadFile(pipe, buffer, sizeof(buffer) - 1, &bytesRead, &readOperation);
        const bool readCompleted = waitForIo(readOperation, read, bytesRead);
        CloseHandle(pipe);
        if (!readCompleted) {
            return false;
        }

        response.assign(buffer, bytesRead);
        return true;
    }

private:
    std::string m_pipeName; // 管道名称
};

struct ClientSession {
    DWORD pid{};
    std::string playerName;
    std::string status = "等待查询";
};

// 通过 GUID 查找玩家名称
std::string GetUserNameByGuid(ProcessManager& procManager, uint64_t guid) {
    std::cout << u8"读取玩家名字: " << guid << std::endl;
    uintptr_t namePtr = procManager.ReadMemory<uintptr_t>(0xC0E230); // 初始指针地址

    // 循环遍历链表查找匹配的GUID
    while (namePtr != 0) {
        uint64_t currentGuid = procManager.ReadMemory<uint64_t>(namePtr + 0xC); // 读取当前结构的GUID
        if (currentGuid == guid) {
            return procManager.ReadString(namePtr + 0x14); // 找到匹配的 GUID，读取名称
        }
        namePtr = procManager.ReadMemory<uintptr_t>(namePtr); // 指向下一个结构
    }
    return ""; // 未找到匹配的GUID
}



const std::string pipeName_KylePlayerGuidPipe = "KylePlayerGuidPipe"; // 命名管道名称
const std::string pipeName_KylePlayerNamePipe = "KylePlayerNamePipe"; // 命名管道名称
const std::string pipeName_TravelerRouteControl = "TravelerRouteControl";


// 封装构建管道名称的函数
std::string BuildPipeName(const std::string& baseName, DWORD pid) {
    std::ostringstream oss;
    oss << R"(\\.\pipe\)" << baseName << "_PID_" << pid;
    return oss.str();
}

std::string GetExecutableDirectory() {
    char executablePath[MAX_PATH]{};
    const DWORD length = GetModuleFileNameA(nullptr, executablePath, MAX_PATH);
    if (length == 0 || length == MAX_PATH) {
        throw std::runtime_error("Failed to get injector executable path.");
    }

    std::string directory(executablePath, length);
    const std::string::size_type separator = directory.find_last_of("\\/");
    if (separator == std::string::npos) {
        return ".";
    }
    directory.resize(separator);
    return directory;
}

std::vector<ClientSession> DiscoverClients(const std::string& dllPath) {
    const wchar_t* dllName = L"TravelerWoW112Dll.dll";
    std::vector<ClientSession> clients;
    for (const DWORD pid : ProcessManager::GetProcessIdsByName(L"WoW.exe")) {
        try {
            ProcessManager process(pid);
            if (!ProcessManager::IsDllInjected(pid, dllName)) {
                if (!process.InjectDLL(dllPath)) {
                    continue;
                }
                Sleep(250);
            }

            PipeClient playerNamePipe(BuildPipeName(pipeName_KylePlayerNamePipe, pid));
            std::string playerName;
            if (!playerNamePipe.readDataU8(playerName)) {
                playerName = "Not logged in";
            }
            while (!playerName.empty() && playerName.back() == '\0') {
                playerName.pop_back();
            }
            if (playerName.empty()) {
                playerName = "Not logged in";
            }
            clients.push_back({ pid, playerName, "等待查询" });
        }
        catch (const std::exception&) {
            // A client may exit while the process list is being refreshed.
        }
    }
    return clients;
}

int RunRouteControlWindow(std::vector<ClientSession>& clients, const std::string& dllPath);





// 主函数
int main() {
    // 设置控制台输出编码为 UTF-8
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);


    try {
        const std::string dllPath = GetExecutableDirectory() + "\\TravelerWoW112Dll.dll";

        const DWORD dllAttributes = GetFileAttributesA(dllPath.c_str());
        if (dllAttributes == INVALID_FILE_ATTRIBUTES ||
            (dllAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            std::cerr << u8"找不到 DLL，请将 TravelerWoW112Dll.dll 放在注入器旁边：\n"
                << dllPath << std::endl;
            return -1;
        }

        std::cout << u8"准备加载 DLL：" << dllPath << std::endl;


        std::vector<ClientSession> clients = DiscoverClients(dllPath);

        ShowWindow(GetConsoleWindow(), SW_HIDE);
        return RunRouteControlWindow(clients, dllPath);
    }
    catch (const std::exception& e) {
        // 捕获并打印所有标准异常
        std::cerr << u8"程序异常: " << e.what() << std::endl;
        return -1;
    }

    return 0;
}

enum RouteControlId {
    IDC_CLIENT_SELECT = 1000,
    IDC_REFRESH_CLIENTS,
    IDC_SCOPE_SELECTED,
    IDC_SCOPE_ALL,
    IDC_TASK_TAB,
    IDC_ROUTE_LABEL,
    IDC_ROUTE_NAME,
    IDC_RECORD,
    IDC_STOP,
    IDC_SAVE,
    IDC_PLAY,
    IDC_LIST,
    IDC_ROUTE_STATUS,
    IDC_FISH_START,
    IDC_FISH_STOP,
    IDC_FISH_STATUS,
    IDC_SPEED_LABEL,
    IDC_SPEED_VALUE,
    IDC_SPEED_APPLY,
    IDC_SPEED_RESET,
    IDC_SPEED_READ,
    IDC_OUTPUT,
    IDC_EXIT,
    IDC_TESTPOS_ENABLE, IDC_TESTPOS_X, IDC_TESTPOS_Y, IDC_TESTPOS_APPLY,
};

constexpr UINT WM_APP_COMMAND_RESULT = WM_APP + 201;
constexpr UINT WM_APP_REFRESH_RESULT = WM_APP + 202;
constexpr UINT_PTR IDT_STATUS_REFRESH = 1;

struct WindowState {
    std::vector<ClientSession>* clients{};
    std::string dllPath;
    bool refreshRunning = false;
};

struct CommandResult {
    DWORD pid{};
    std::string playerName;
    std::string command;
    std::string response;
    bool succeeded = false;
};

void SetControlFont(HWND control) {
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
}

void SetOutput(HWND window, const std::string& text) {
    SetWindowTextA(GetDlgItem(window, IDC_OUTPUT), text.c_str());
}

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) {
        return L"";
    }
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) {
        return std::wstring(text.begin(), text.end());
    }
    std::wstring result(length, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &result[0], length);
    return result;
}

std::string GetRouteName(HWND window) {
    char routeName[128]{};
    GetWindowTextA(GetDlgItem(window, IDC_ROUTE_NAME), routeName, sizeof(routeName));
    return routeName;
}

ClientSession* GetSelectedClient(HWND window) {
    auto* state = reinterpret_cast<WindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    auto* clients = state == nullptr ? nullptr : state->clients;
    const HWND selector = GetDlgItem(window, IDC_CLIENT_SELECT);
    const LRESULT selection = SendMessageW(selector, CB_GETCURSEL, 0, 0);
    if (clients == nullptr || selection == CB_ERR) {
        return nullptr;
    }
    const LRESULT index = SendMessageW(selector, CB_GETITEMDATA, selection, 0);
    if (index == CB_ERR || static_cast<size_t>(index) >= clients->size()) {
        return nullptr;
    }
    return &(*clients)[static_cast<size_t>(index)];
}

void RebuildClientSelector(HWND window, DWORD preferredPid = 0) {
    auto* state = reinterpret_cast<WindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (state == nullptr || state->clients == nullptr) {
        return;
    }
    HWND selector = GetDlgItem(window, IDC_CLIENT_SELECT);
    SendMessageW(selector, CB_RESETCONTENT, 0, 0);
    int selectedItem = -1;
    for (size_t index = 0; index < state->clients->size(); ++index) {
        const ClientSession& client = (*state->clients)[index];
        const std::wstring label = Utf8ToWide(
            client.playerName + " [PID " + std::to_string(client.pid) + "]");
        const LRESULT item = SendMessageW(selector, CB_ADDSTRING, 0,
            reinterpret_cast<LPARAM>(label.c_str()));
        if (item != CB_ERR && item != CB_ERRSPACE) {
            SendMessageW(selector, CB_SETITEMDATA, item, static_cast<LPARAM>(index));
            if (client.pid == preferredPid) {
                selectedItem = static_cast<int>(item);
            }
        }
    }
    if (selectedItem < 0 && !state->clients->empty()) {
        selectedItem = 0;
    }
    SendMessageW(selector, CB_SETCURSEL, selectedItem, 0);
}

std::string GetSpeedValue(HWND window) {
    char value[32]{};
    GetWindowTextA(GetDlgItem(window, IDC_SPEED_VALUE), value, sizeof(value));
    return value;
}

bool ConfirmStartRecording(HWND window, const std::string& routeName) {
    ClientSession* client = GetSelectedClient(window);
    if (client == nullptr) {
        MessageBoxW(window, L"请先选择要执行录制的角色。", L"无法开始录制", MB_OK | MB_ICONWARNING);
        return false;
    }

    if (routeName.empty()) {
        MessageBoxW(window, L"请先输入路线名称。", L"无法开始录制", MB_OK | MB_ICONWARNING);
        return false;
    }

    const std::wstring character = Utf8ToWide(client->playerName);
    const std::wstring route = Utf8ToWide(routeName);
    const std::wstring confirmation =
        L"角色：" + character + L"\n路线：" + route + L"\n\n确定开始录制吗？";
    if (MessageBoxW(window, confirmation.c_str(), L"确认开始录制",
        MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) {
        return false;
    }

    const std::string routePath = GetExecutableDirectory() + "\\routes\\" + routeName + ".csv";
    const DWORD attributes = GetFileAttributesA(routePath.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        const std::wstring overwrite =
            L"路线“" + route + L"”已经存在。\n\n开始新录制会清空内存中的旧路线，保存或停止时将覆盖原文件。是否继续？";
        if (MessageBoxW(window, overwrite.c_str(), L"确认覆盖路线",
            MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
            return false;
        }
    }

    return true;
}

void QueueCommand(HWND window, DWORD pid, const std::string& playerName, const std::string& command) {
    std::thread([window, pid, playerName, command]() {
        auto* result = new CommandResult{};
        result->pid = pid;
        result->playerName = playerName;
        result->command = command;
        PipeClient pipe(BuildPipeName(pipeName_TravelerRouteControl, pid));
        result->succeeded = pipe.SendCommand(command, result->response);
        if (!result->succeeded) {
            result->response = "无法连接控制管道，客户端可能已经退出。";
        }
        if (!PostMessageW(window, WM_APP_COMMAND_RESULT, 0, reinterpret_cast<LPARAM>(result))) {
            delete result;
        }
    }).detach();
}

void SendRouteUiCommand(HWND window, const std::string& command) {
    auto* state = reinterpret_cast<WindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (state == nullptr || state->clients == nullptr || state->clients->empty()) {
        SetOutput(window, "当前没有可用的 WoW 客户端，请点击刷新客户端。");
        return;
    }

    if (IsDlgButtonChecked(window, IDC_SCOPE_ALL) == BST_CHECKED) {
        for (const ClientSession& client : *state->clients) {
            QueueCommand(window, client.pid, client.playerName, command);
        }
        SetOutput(window, "命令已发送到全部角色：" + command);
    }
    else {
        ClientSession* client = GetSelectedClient(window);
        if (client == nullptr) {
            SetOutput(window, "请先选择要执行命令的角色。");
            return;
        }
        QueueCommand(window, client->pid, client->playerName, command);
        SetOutput(window, "命令已发送：" + client->playerName + " - " + command);
    }
}

void RefreshClientsAsync(HWND window) {
    auto* state = reinterpret_cast<WindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (state == nullptr || state->refreshRunning) {
        return;
    }
    state->refreshRunning = true;
    EnableWindow(GetDlgItem(window, IDC_REFRESH_CLIENTS), FALSE);
    const std::string dllPath = state->dllPath;
    std::thread([window, dllPath]() {
        auto* clients = new std::vector<ClientSession>(DiscoverClients(dllPath));
        if (!PostMessageW(window, WM_APP_REFRESH_RESULT, 0, reinterpret_cast<LPARAM>(clients))) {
            delete clients;
        }
    }).detach();
}

void QueryAllStatuses(HWND window) {
    auto* state = reinterpret_cast<WindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (state == nullptr || state->clients == nullptr) {
        return;
    }
    for (const ClientSession& client : *state->clients) {
        QueueCommand(window, client.pid, client.playerName, "status");
    }
}

void SetTaskPage(HWND window, int page) {
    const int routeControls[] = {
        IDC_ROUTE_LABEL, IDC_ROUTE_NAME, IDC_RECORD, IDC_STOP,
        IDC_SAVE, IDC_PLAY, IDC_LIST, IDC_ROUTE_STATUS,
    };
    const int fishingControls[] = {
        IDC_FISH_START, IDC_FISH_STOP, IDC_FISH_STATUS,
    };
    const int movementControls[] = {
        IDC_SPEED_LABEL, IDC_SPEED_VALUE, IDC_SPEED_APPLY,
        IDC_SPEED_RESET, IDC_SPEED_READ,
    };
    for (const int id : routeControls) {
        ShowWindow(GetDlgItem(window, id), page == 0 ? SW_SHOW : SW_HIDE);
    }
    for (const int id : fishingControls) {
        ShowWindow(GetDlgItem(window, id), page == 1 ? SW_SHOW : SW_HIDE);
    }
    for (const int id : movementControls) {
        ShowWindow(GetDlgItem(window, id), page == 2 ? SW_SHOW : SW_HIDE);
    }
#if defined(TRAVELER_TEST_POSITION_WRITE)
    for (int id : {IDC_TESTPOS_ENABLE, IDC_TESTPOS_X, IDC_TESTPOS_Y, IDC_TESTPOS_APPLY})
        ShowWindow(GetDlgItem(window, id), page == 2 ? SW_SHOW : SW_HIDE);
#endif
}

LRESULT CALLBACK RouteControlWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_CREATE) {
        const CREATESTRUCTW* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));

        SetControlFont(CreateWindowExW(0, L"STATIC", L"选择角色：", WS_CHILD | WS_VISIBLE,
            18, 20, 80, 24, window, nullptr, nullptr, nullptr));
        HWND clientSelector = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            98, 17, 225, 200, window, reinterpret_cast<HMENU>(IDC_CLIENT_SELECT), nullptr, nullptr);
        SetControlFont(clientSelector);
        SetControlFont(CreateWindowExW(0, L"BUTTON", L"刷新客户端", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            333, 16, 75, 27, window, reinterpret_cast<HMENU>(IDC_REFRESH_CLIENTS), nullptr, nullptr));
        SetControlFont(CreateWindowExW(0, L"BUTTON", L"仅当前角色",
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP,
            98, 45, 105, 22, window, reinterpret_cast<HMENU>(IDC_SCOPE_SELECTED), nullptr, nullptr));
        SetControlFont(CreateWindowExW(0, L"BUTTON", L"全部角色",
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
            215, 45, 95, 22, window, reinterpret_cast<HMENU>(IDC_SCOPE_ALL), nullptr, nullptr));
        CheckRadioButton(window, IDC_SCOPE_SELECTED, IDC_SCOPE_ALL, IDC_SCOPE_SELECTED);
        RebuildClientSelector(window);

        HWND taskTab = CreateWindowExW(0, WC_TABCONTROLW, L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            18, 70, 390, 175, window, reinterpret_cast<HMENU>(IDC_TASK_TAB), nullptr, nullptr);
        SetControlFont(taskTab);
        TCITEMW tabItem{};
        tabItem.mask = TCIF_TEXT;
        tabItem.pszText = const_cast<LPWSTR>(L"路线任务");
        TabCtrl_InsertItem(taskTab, 0, &tabItem);
        tabItem.pszText = const_cast<LPWSTR>(L"钓鱼任务");
        TabCtrl_InsertItem(taskTab, 1, &tabItem);
        tabItem.pszText = const_cast<LPWSTR>(L"移动工具");
        TabCtrl_InsertItem(taskTab, 2, &tabItem);
#if defined(TRAVELER_TEST_POSITION_WRITE)
        SetControlFont(CreateWindowExW(0, L"BUTTON", L"启用 XY 写入测试（当前角色）", WS_CHILD | BS_AUTOCHECKBOX,
            32, 178, 330, 20, window, reinterpret_cast<HMENU>(IDC_TESTPOS_ENABLE), nullptr, nullptr));
        SetControlFont(CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL,
            32, 205, 100, 25, window, reinterpret_cast<HMENU>(IDC_TESTPOS_X), nullptr, nullptr));
        SetControlFont(CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL,
            142, 205, 100, 25, window, reinterpret_cast<HMENU>(IDC_TESTPOS_Y), nullptr, nullptr));
        SendDlgItemMessageW(window, IDC_TESTPOS_X, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"X"));
        SendDlgItemMessageW(window, IDC_TESTPOS_Y, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Y"));
        SetControlFont(CreateWindowExW(0, L"BUTTON", L"确认写入 XY", WS_CHILD,
            252, 205, 120, 25, window, reinterpret_cast<HMENU>(IDC_TESTPOS_APPLY), nullptr, nullptr));
#endif

        SetControlFont(CreateWindowExW(0, L"STATIC", L"路线名称：", WS_CHILD | WS_VISIBLE,
            32, 95, 80, 24, window, reinterpret_cast<HMENU>(IDC_ROUTE_LABEL), nullptr, nullptr));
        SetControlFont(CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"route1",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            112, 92, 250, 26, window, reinterpret_cast<HMENU>(IDC_ROUTE_NAME), nullptr, nullptr));

        SetControlFont(CreateWindowExW(0, L"STATIC", L"地面速度：", WS_CHILD | WS_VISIBLE,
            42, 100, 80, 24, window, reinterpret_cast<HMENU>(IDC_SPEED_LABEL), nullptr, nullptr));
        SetControlFont(CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"7.0",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            122, 97, 100, 26, window, reinterpret_cast<HMENU>(IDC_SPEED_VALUE), nullptr, nullptr));

        struct ButtonDefinition { int id; const wchar_t* text; int x; int y; int width; };
        const ButtonDefinition buttons[] = {
            { IDC_RECORD, L"开始录制", 32, 133, 90 },
            { IDC_STOP, L"停止路线", 132, 133, 80 },
            { IDC_SAVE, L"保存路线", 222, 133, 80 },
            { IDC_PLAY, L"播放路线", 312, 133, 80 },
            { IDC_LIST, L"路线列表", 32, 176, 90 },
            { IDC_ROUTE_STATUS, L"路线状态", 132, 176, 90 },
            { IDC_FISH_START, L"开始钓鱼", 42, 110, 100 },
            { IDC_FISH_STOP, L"停止钓鱼", 152, 110, 100 },
            { IDC_FISH_STATUS, L"钓鱼状态", 262, 110, 100 },
            { IDC_SPEED_APPLY, L"应用速度", 42, 142, 100 },
            { IDC_SPEED_RESET, L"恢复默认", 152, 142, 100 },
            { IDC_SPEED_READ, L"读取速度", 262, 142, 100 },
            { IDC_EXIT, L"退出界面", 308, 345, 100 },
        };
        for (const auto& button : buttons) {
            SetControlFont(CreateWindowExW(0, L"BUTTON", button.text,
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                button.x, button.y, button.width, 30,
                window, reinterpret_cast<HMENU>(button.id), nullptr, nullptr));
        }

        SetControlFont(CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"DLL 已连接，可以录制、播放路线或自动钓鱼。",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | WS_VSCROLL,
            18, 255, 390, 80, window, reinterpret_cast<HMENU>(IDC_OUTPUT), nullptr, nullptr));
        SetTaskPage(window, 0);
        SetTimer(window, IDT_STATUS_REFRESH, 5000, nullptr);
        QueryAllStatuses(window);
        return 0;
    }

    if (message == WM_APP_COMMAND_RESULT) {
        std::unique_ptr<CommandResult> result(reinterpret_cast<CommandResult*>(lParam));
        auto* state = reinterpret_cast<WindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (state != nullptr && state->clients != nullptr) {
            auto found = std::find_if(state->clients->begin(), state->clients->end(),
                [&](const ClientSession& client) { return client.pid == result->pid; });
            if (found != state->clients->end() && result->command == "status") {
                found->status = result->response;
            }
        }
        ClientSession* selected = GetSelectedClient(window);
        if (result->command != "status" || (selected != nullptr && selected->pid == result->pid)) {
            SetOutput(window, result->playerName + " [PID " + std::to_string(result->pid)
                + "]: " + result->response);
        }
        return 0;
    }

    if (message == WM_APP_REFRESH_RESULT) {
        std::unique_ptr<std::vector<ClientSession>> refreshed(
            reinterpret_cast<std::vector<ClientSession>*>(lParam));
        auto* state = reinterpret_cast<WindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        ClientSession* selected = GetSelectedClient(window);
        const DWORD preferredPid = selected == nullptr ? 0 : selected->pid;
        if (state != nullptr && state->clients != nullptr) {
            for (ClientSession& client : *refreshed) {
                auto old = std::find_if(state->clients->begin(), state->clients->end(),
                    [&](const ClientSession& existing) { return existing.pid == client.pid; });
                if (old != state->clients->end()) {
                    client.status = old->status;
                }
            }
            *state->clients = std::move(*refreshed);
            state->refreshRunning = false;
            EnableWindow(GetDlgItem(window, IDC_REFRESH_CLIENTS), TRUE);
            RebuildClientSelector(window, preferredPid);
            SetOutput(window, "客户端刷新完成，共发现 " + std::to_string(state->clients->size()) + " 个角色。");
            QueryAllStatuses(window);
        }
        return 0;
    }

    if (message == WM_TIMER && wParam == IDT_STATUS_REFRESH) {
        auto* state = reinterpret_cast<WindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (state != nullptr && state->clients != nullptr) {
            ClientSession* selected = GetSelectedClient(window);
            const DWORD preferredPid = selected == nullptr ? 0 : selected->pid;
            const auto oldSize = state->clients->size();
            state->clients->erase(std::remove_if(state->clients->begin(), state->clients->end(),
                [](const ClientSession& client) {
                    UniqueHandle process(OpenProcess(SYNCHRONIZE, FALSE, client.pid));
                    return !process || WaitForSingleObject(process.get(), 0) == WAIT_OBJECT_0;
                }), state->clients->end());
            if (state->clients->size() != oldSize) {
                RebuildClientSelector(window, preferredPid);
            }
            QueryAllStatuses(window);
        }
        return 0;
    }

    if (message == WM_NOTIFY) {
        const NMHDR* notification = reinterpret_cast<const NMHDR*>(lParam);
        if (notification != nullptr
            && notification->idFrom == IDC_TASK_TAB
            && notification->code == TCN_SELCHANGE) {
            const int page = TabCtrl_GetCurSel(GetDlgItem(window, IDC_TASK_TAB));
            SetTaskPage(window, page < 0 ? 0 : page);
            return 0;
        }
    }

    if (message == WM_COMMAND) {
        const int controlId = LOWORD(wParam);
        if (controlId == IDC_CLIENT_SELECT && HIWORD(wParam) == CBN_SELCHANGE) {
            ClientSession* selected = GetSelectedClient(window);
            if (selected != nullptr) {
                SetOutput(window, selected->playerName + " [PID " + std::to_string(selected->pid)
                    + "]: " + selected->status);
                QueueCommand(window, selected->pid, selected->playerName, "status");
            }
            return 0;
        }

        const std::string routeName = GetRouteName(window);
        switch (controlId) {
        case IDC_REFRESH_CLIENTS:
            RefreshClientsAsync(window);
            SetOutput(window, "正在后台刷新客户端...");
            return 0;
        case IDC_RECORD:
            if (ConfirmStartRecording(window, routeName)) {
                SendRouteUiCommand(window, "record " + routeName);
            }
            return 0;
        case IDC_STOP:
            SendRouteUiCommand(window, "stop");
            return 0;
        case IDC_SAVE:
            SendRouteUiCommand(window, "save");
            return 0;
        case IDC_PLAY:
            SendRouteUiCommand(window, "play " + routeName);
            return 0;
        case IDC_FISH_START:
            SendRouteUiCommand(window, "fish start");
            return 0;
        case IDC_LIST:
            SendRouteUiCommand(window, "list");
            return 0;
        case IDC_ROUTE_STATUS:
        case IDC_FISH_STATUS:
            SendRouteUiCommand(window, "status");
            return 0;
        case IDC_FISH_STOP:
            SendRouteUiCommand(window, "fish stop");
            return 0;
        case IDC_SPEED_APPLY: {
            const std::string speed = GetSpeedValue(window);
            if (speed.empty()) {
                MessageBoxW(window, L"请输入 1 到 100 之间的速度。", L"速度无效", MB_OK | MB_ICONWARNING);
                return 0;
            }
            SendRouteUiCommand(window, "speed " + speed);
            return 0;
        }
        case IDC_SPEED_RESET:
            SetWindowTextW(GetDlgItem(window, IDC_SPEED_VALUE), L"7.0");
            SendRouteUiCommand(window, "speed reset");
            return 0;
        case IDC_SPEED_READ:
            SendRouteUiCommand(window, "speed get");
            return 0;
        case IDC_EXIT:
#if defined(TRAVELER_TEST_POSITION_WRITE)
            DestroyWindow(window);
            return 0;
        case IDC_TESTPOS_APPLY: {
            if (IsDlgButtonChecked(window, IDC_TESTPOS_ENABLE) != BST_CHECKED
                || IsDlgButtonChecked(window, IDC_SCOPE_ALL) == BST_CHECKED) {
                MessageBoxW(window, L"请启用测试开关并选择仅当前角色。", L"XY 测试", MB_OK);
                return 0;
            }
            char x[64]{}, y[64]{};
            GetDlgItemTextA(window, IDC_TESTPOS_X, x, sizeof(x));
            GetDlgItemTextA(window, IDC_TESTPOS_Y, y, sizeof(y));
            const std::wstring prompt = Utf8ToWide(std::string("当前选中角色 X=") + x + " Y=" + y + "，Z 不变。确认写入？");
            if (MessageBoxW(window, prompt.c_str(), L"XY 测试", MB_YESNO | MB_DEFBUTTON2) == IDYES)
                SendRouteUiCommand(window, std::string("testpos confirm ") + x + " " + y);
            return 0;
        }
#endif
            DestroyWindow(window);
            return 0;
        }
    }

    if (message == WM_DESTROY) {
        KillTimer(window, IDT_STATUS_REFRESH);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

int RunRouteControlWindow(std::vector<ClientSession>& clients, const std::string& dllPath) {
    INITCOMMONCONTROLSEX commonControls{};
    commonControls.dwSize = sizeof(commonControls);
    commonControls.dwICC = ICC_TAB_CLASSES;
    InitCommonControlsEx(&commonControls);

    const wchar_t className[] = L"TravelerRouteControlWindow";
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = RouteControlWindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = className;
    if (!RegisterClassW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return -1;
    }

    WindowState state{ &clients, dllPath, false };
    HWND window = CreateWindowExW(
        0,
        className,
        L"Traveler WoW 1.12 多任务控制",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        445,
        420,
        nullptr,
        nullptr,
        windowClass.hInstance,
        &state);
    if (window == nullptr) {
        return -1;
    }

    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
