// dllmain.cpp : 定义 DLL 应用程序的入口点。
#include "pch.h" // 预编译头文件，如果您的项目使用的话
#include <windows.h>
#include <iostream>
#include <string>
#include <thread> // 用于管理管道服务器线程
#include <atomic> // 用于线程安全地控制线程停止
#include <memory> // 用于智能指针管理HANDLE
#include <functional> // 用于传递数据生成函数
#include <sstream> // 用于构建动态字符串
#include <vector>
#include <mutex>
#include <fstream>
#include <cmath>
#include <unordered_map>
#include "healing_observer.h"
#include "loot_observer.h"

// 定义获取玩家GUID的函数指针类型
typedef unsigned long long(__cdecl* GetPlayerGuidFunc)();
// 定义右键点击对象的函数指针类型
typedef void(__thiscall* RightClickFunc)(void* thisPtr, int autoLoot);

typedef void* (__thiscall* GetObjectPtrFun)(unsigned long long guid);

typedef void(__cdecl* EnumerateVisibleObjectsFun)(void* callback, int filter);



// 游戏内获取玩家GUID的硬编码地址
// 注意：这个地址是硬编码的，如果游戏更新，可能需要修改此地址
const uintptr_t funcAddress_GetPlayerGuid = 0x00468550;

// 游戏内右键点击对象的硬编码地址
const uintptr_t funcAddress_RightClickFunc = 0x005F8660;

// 获取对象指针的函数指针类型,
const uintptr_t funcAddress_GetObjectPtrFunPtr = 0x00464870;

// 获取可见对象的函数指针类型
const uintptr_t funcAddress_EnumerateVisibleObjectsFunPtr = 0x00468380;
const uintptr_t funcAddress_ClickToMove = 0x00611130;
const uintptr_t funcAddress_DoString = 0x00704CD0;
const uintptr_t address_ObjectManager = 0x00B41414;

const int kGameObjectType = 5;
const int kFishingBobberObjectId = 35591;
const uintptr_t offset_ObjectType = 0x14;
const uintptr_t offset_ObjectDescriptor = 0x08;
const uintptr_t offset_GameObjectCreatedBy = 0x18;
const uintptr_t offset_GameObjectBusy = 0x27C;
const uintptr_t offset_GameObjectId = 0x294;
const uintptr_t offset_ObjectManagerFirstObject = 0xAC;
const uintptr_t offset_ObjectManagerPlayerGuid = 0xC0;
const uintptr_t offset_ObjectGuid = 0x30;
const uintptr_t offset_ObjectNext = 0x3C;

// WoW 1.12.1 local player object position offsets.
const uintptr_t offset_UnitPositionX = 0x09B8;
const uintptr_t offset_UnitPositionY = 0x09BC;
const uintptr_t offset_UnitPositionZ = 0x09C0;

// Confirmed player-object fields for this WoW 1.12.1 client build.
// Only ground speed is currently exposed as a command; the remaining offsets
// are kept here for future features.
const uintptr_t offset_UnitMovementState = 0x0A2C;
const uintptr_t offset_UnitGroundSpeed = 0x0A34;
const uintptr_t offset_UnitWaterOrFlightSpeed = 0x0A3C;
const uintptr_t offset_UnitGravityOrFlight = 0x0A5C;
const uintptr_t offset_UnitHillClimb = 0x0A60;
const uintptr_t address_Levitate = 0x0087D894;
const float kDefaultGroundSpeed = 7.0f;
const float kMinimumGroundSpeed = 1.0f;
const float kMaximumGroundSpeed = 100.0f;

struct Vector3 {
    float x;
    float y;
    float z;
};

struct RoutePoint {
    DWORD elapsedMs;
    Vector3 position;
};

enum class ClickToMoveType : int {
    Move = 4,
    Stop = 0xD,
};

typedef int(__thiscall* ClickToMoveFunc)(
    void* player,
    int clickType,
    unsigned long long* interactGuid,
    Vector3* position,
    float precision);
typedef int(__fastcall* DoStringFunc)(const char* script, int unused);

std::vector<RoutePoint> g_route;
std::mutex g_routeMutex;
std::mutex g_routeNameMutex;
std::string g_activeRouteName = "default";
std::atomic<bool> g_recording(false);
std::atomic<bool> g_playing(false);
std::atomic<bool> g_routeThreadRunning(false);
HANDLE g_routeThread = nullptr;
HANDLE g_routeControlThread = nullptr;
std::atomic<bool> g_routeControlRunning(false);
HMODULE g_module = nullptr;
DWORD g_recordingStarted = 0;
const float kMinimumRoutePointDistance = 1.0f;
const float kClickToMovePrecision = 2.0f;
const float kPlaybackArrivalDistance = kClickToMovePrecision + 0.25f;
const float kMaximumRouteStartDistance = 30.0f;
size_t g_playbackIndex = 0;
size_t g_lastIssuedPlaybackIndex = static_cast<size_t>(-1);
DWORD g_lastClickToMoveTick = 0;
const UINT kClickToMoveMessage = WM_APP + 112;
HHOOK g_mainThreadHook = nullptr;
HWND g_wowWindow = nullptr;
std::mutex g_clickToMoveMutex;
bool g_clickToMovePending = false;
ClickToMoveType g_pendingClickToMoveType = ClickToMoveType::Stop;
Vector3 g_pendingClickToMovePosition{};
std::atomic<unsigned long> g_clickToMoveQueued(0);
std::atomic<unsigned long> g_clickToMoveExecuted(0);
std::atomic<bool> g_lastClickToMoveSucceeded(false);
std::atomic<bool> g_fishing(false);
std::atomic<bool> g_fishingResetRequested(false);
std::atomic<unsigned long> g_fishingCasts(0);
std::atomic<unsigned long> g_fishingCatches(0);
std::atomic<bool> g_lastFishingActionSucceeded(false);
std::atomic<unsigned long> g_fishingObjectsScanned(0);
std::atomic<unsigned long> g_fishingBobberCandidates(0);
std::atomic<unsigned long long> g_lastFishingCandidateGuid(0);
std::atomic<unsigned long long> g_lastFishingCandidateOwner(0);
std::atomic<int> g_lastFishingBusyRaw(-1);
const UINT kFishingMessage = WM_APP + 113;
const UINT kHealingMessage = WM_APP + 114;
const UINT kHealingStateMessage = WM_APP + 115;
std::string g_pendingHealingCommand;
std::string g_healingResponse;
std::mutex g_healingMutex;
std::string g_pendingHealingLua;
bool g_healingPending = false;
std::atomic<bool> g_healingSucceeded(false);
unsigned long long g_fishingBobberGuid = 0;
uintptr_t g_fishingBobberPointer = 0;
unsigned long long g_lastFishingBobberGuid = 0;
DWORD g_lastFishingCastTick = 0;
DWORD g_bobberFoundTick = 0;
DWORD g_biteDetectedTick = 0;
DWORD g_lastFishingCatchTick = 0;
DWORD g_lastFishingScanLogTick = 0;
std::mutex g_fishingEventMutex;
std::string g_lastFishingEvent = "not started";


// 智能指针，自动关闭 Windows 句柄
struct HandleDeleter {
    void operator()(HANDLE handle) const {
        if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
        }
    }
};
using UniqueHandle = std::unique_ptr<void, HandleDeleter>;

// 获取玩家GUID的实际函数
// 这个函数将调用游戏内部的函数来获取GUID
unsigned long long GetPlayerGuid() {
    GetPlayerGuidFunc GetPlayerGuidPtr = (GetPlayerGuidFunc)funcAddress_GetPlayerGuid;
    return GetPlayerGuidPtr();
}

// 根据guid获取对象指针的函数
void* GetObjectPtr(unsigned long long guid) {
    GetObjectPtrFun func = (GetObjectPtrFun)funcAddress_GetObjectPtrFunPtr;
    return func(guid);
}


// 遍历可见对象的函数
void EnumerateVisibleObjects(void* callback, int filter) {
    // 将硬编码的地址转换为函数指针
    EnumerateVisibleObjectsFun func = (EnumerateVisibleObjectsFun)funcAddress_EnumerateVisibleObjectsFunPtr;
    // 调用游戏内部函数
    __try { func(callback, filter); }
    __except (EXCEPTION_EXECUTE_HANDLER) { }
}


bool IsLoggedIn() { return GetPlayerGuid() > 0; };

bool GetPlayerPosition(Vector3& position) {
    __try {
        unsigned long long guid = GetPlayerGuid();
        if (guid == 0) {
            return false;
        }

        auto player = reinterpret_cast<uint8_t*>(GetObjectPtr(guid));
        if (player == nullptr) {
            return false;
        }

        position.x = *reinterpret_cast<float*>(player + offset_UnitPositionX);
        position.y = *reinterpret_cast<float*>(player + offset_UnitPositionY);
        position.z = *reinterpret_cast<float*>(player + offset_UnitPositionZ);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }

    return std::isfinite(position.x) &&
        std::isfinite(position.y) &&
        std::isfinite(position.z);
}

void* GetLocalPlayer() {
    __try {
        const unsigned long long guid = GetPlayerGuid();
        return guid == 0 ? nullptr : GetObjectPtr(guid);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

float Distance3D(const Vector3& first, const Vector3& second) {
    const float dx = second.x - first.x;
    const float dy = second.y - first.y;
    const float dz = second.z - first.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

float Distance2D(const Vector3& first, const Vector3& second) {
    const float dx = second.x - first.x;
    const float dy = second.y - first.y;
    return std::sqrt(dx * dx + dy * dy);
}

std::string GetDllDirectory() {
    char modulePath[MAX_PATH]{};
    GetModuleFileNameA(g_module, modulePath, MAX_PATH);

    std::string path(modulePath);
    const std::string::size_type separator = path.find_last_of("\\/");
    if (separator != std::string::npos) {
        path.resize(separator + 1);
    }
    else {
        path.clear();
    }

    return path;
}

std::string GetRoutesDirectory() {
    const std::string directory = GetDllDirectory() + "routes";
    CreateDirectoryA(directory.c_str(), nullptr);
    return directory;
}

std::string GetRouteFilePath() {
    std::lock_guard<std::mutex> lock(g_routeNameMutex);
    return GetRoutesDirectory() + "\\" + g_activeRouteName + ".csv";
}

bool SetActiveRouteName(const std::string& name) {
    if (name.empty() || name.size() > 64) {
        return false;
    }
    for (unsigned char character : name) {
        if (!(character >= 'a' && character <= 'z')
            && !(character >= 'A' && character <= 'Z')
            && !(character >= '0' && character <= '9')
            && character != '_' && character != '-') {
            return false;
        }
    }

    std::lock_guard<std::mutex> lock(g_routeNameMutex);
    g_activeRouteName = name;
    return true;
}

std::string GetActiveRouteName() {
    std::lock_guard<std::mutex> lock(g_routeNameMutex);
    return g_activeRouteName;
}

std::string ListRoutes() {
    const std::string searchPath = GetRoutesDirectory() + "\\*.csv";
    WIN32_FIND_DATAA data{};
    HANDLE search = FindFirstFileA(searchPath.c_str(), &data);
    if (search == INVALID_HANDLE_VALUE) {
        return "no routes";
    }

    std::ostringstream result;
    bool first = true;
    do {
        std::string fileName(data.cFileName);
        if (fileName.size() > 4 && fileName.substr(fileName.size() - 4) == ".csv") {
            fileName.resize(fileName.size() - 4);
            if (!first) {
                result << ", ";
            }
            result << fileName;
            first = false;
        }
    } while (FindNextFileA(search, &data));
    FindClose(search);
    return first ? "no routes" : result.str();
}

bool SaveRoute() {
    std::lock_guard<std::mutex> lock(g_routeMutex);
    std::ofstream output(GetRouteFilePath(), std::ios::trunc);
    if (!output) {
        return false;
    }

    output << "elapsed_ms,x,y,z\n";
    for (const RoutePoint& point : g_route) {
        output << point.elapsedMs << ','
            << point.position.x << ','
            << point.position.y << ','
            << point.position.z << '\n';
    }
    return output.good();
}

bool LoadRoute() {
    std::ifstream input(GetRouteFilePath());
    if (!input) {
        return false;
    }

    std::string line;
    std::getline(input, line);
    std::vector<RoutePoint> loaded;
    while (std::getline(input, line)) {
        RoutePoint point{};
        char comma1 = 0;
        char comma2 = 0;
        char comma3 = 0;
        std::istringstream row(line);
        if (row >> point.elapsedMs >> comma1
            >> point.position.x >> comma2
            >> point.position.y >> comma3
            >> point.position.z
            && comma1 == ',' && comma2 == ',' && comma3 == ','
            && std::isfinite(point.position.x)
            && std::isfinite(point.position.y)
            && std::isfinite(point.position.z)) {
            loaded.push_back(point);
        }
    }

    if (loaded.empty()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(g_routeMutex);
    g_route.swap(loaded);
    return true;
}

bool IsExecutableAddress(uintptr_t address) {
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &memory, sizeof(memory)) == 0
        || memory.State != MEM_COMMIT
        || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
        return false;
    }

    const DWORD executable = PAGE_EXECUTE | PAGE_EXECUTE_READ |
        PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return (memory.Protect & executable) != 0;
}

bool ExecuteClickToMoveOnMainThread(ClickToMoveType type, const Vector3& destination) {
    void* player = GetLocalPlayer();
    if (player == nullptr) {
        return false;
    }

    unsigned long long interactGuid = 0;
    Vector3 position = destination;
    __try {
        ClickToMoveFunc function = reinterpret_cast<ClickToMoveFunc>(funcAddress_ClickToMove);
        function(player, static_cast<int>(type), &interactGuid, &position, kClickToMovePrecision);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        OutputDebugStringA("TravelerWoW112Dll: ClickToMove raised an exception.\n");
        return false;
    }
}

bool ReadGroundSpeed(float& speed) {
    __try {
        auto* player = reinterpret_cast<uint8_t*>(GetLocalPlayer());
        if (player == nullptr) {
            return false;
        }
        speed = *reinterpret_cast<float*>(player + offset_UnitGroundSpeed);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return std::isfinite(speed);
}

bool WriteGroundSpeed(float speed) {
    if (!std::isfinite(speed) || speed < kMinimumGroundSpeed || speed > kMaximumGroundSpeed) {
        return false;
    }
    __try {
        auto* player = reinterpret_cast<uint8_t*>(GetLocalPlayer());
        if (player == nullptr) {
            return false;
        }
        *reinterpret_cast<float*>(player + offset_UnitGroundSpeed) = speed;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return true;
}

void LogFishingEvent(const std::string& event) {
    {
        std::lock_guard<std::mutex> lock(g_fishingEventMutex);
        g_lastFishingEvent = event;
    }
    const std::string line = "[" + std::to_string(GetTickCount()) + "][PID "
        + std::to_string(GetCurrentProcessId()) + "] " + event + "\r\n";
    OutputDebugStringA(("TravelerWoW112Dll: " + line).c_str());
}

std::string GetLastFishingEvent() {
    std::lock_guard<std::mutex> lock(g_fishingEventMutex);
    return g_lastFishingEvent;
}

bool ExecuteLuaOnMainThread(const char* script) {
    if (!IsExecutableAddress(funcAddress_DoString)) {
        return false;
    }
    __try {
        DoStringFunc function = reinterpret_cast<DoStringFunc>(funcAddress_DoString);
        function(script, 0);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        OutputDebugStringA("TravelerWoW112Dll: Lua execution raised an exception.\n");
        return false;
    }
}

bool ReadFishingBobber(void* object, unsigned long long playerGuid, bool& busy) {
    __try {
        auto bytes = reinterpret_cast<uint8_t*>(object);
        if (*reinterpret_cast<int*>(bytes + offset_ObjectType) != kGameObjectType) {
            return false;
        }
        const uintptr_t descriptor = *reinterpret_cast<uintptr_t*>(bytes + offset_ObjectDescriptor);
        if (descriptor == 0
            || *reinterpret_cast<unsigned long long*>(descriptor + offset_GameObjectCreatedBy) != playerGuid
            || *reinterpret_cast<int*>(bytes + offset_GameObjectId) != kFishingBobberObjectId) {
            return false;
        }
        const int busyRaw = *reinterpret_cast<uint8_t*>(bytes + offset_GameObjectBusy);
        g_lastFishingBusyRaw.store(busyRaw);
        busy = (busyRaw ^ 1) != 0;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool InspectFishingObject(unsigned long long guid, void*& object, bool& busy) {
    __try {
        object = GetObjectPtr(guid);
        g_fishingObjectsScanned.fetch_add(1);
        if (object == nullptr) {
            return false;
        }
        auto bytes = reinterpret_cast<uint8_t*>(object);
        if (*reinterpret_cast<int*>(bytes + offset_ObjectType) == kGameObjectType
            && *reinterpret_cast<int*>(bytes + offset_GameObjectId) == kFishingBobberObjectId) {
            const uintptr_t descriptor = *reinterpret_cast<uintptr_t*>(bytes + offset_ObjectDescriptor);
            const unsigned long long owner = descriptor == 0
                ? 0
                : *reinterpret_cast<unsigned long long*>(descriptor + offset_GameObjectCreatedBy);
            g_fishingBobberCandidates.fetch_add(1);
            g_lastFishingCandidateGuid.store(guid);
            g_lastFishingCandidateOwner.store(owner);
            g_lastFishingBusyRaw.store(*reinterpret_cast<uint8_t*>(bytes + offset_GameObjectBusy));
        }
        return ReadFishingBobber(object, GetPlayerGuid(), busy);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        g_lastFishingActionSucceeded.store(false);
        object = nullptr;
        busy = false;
        return false;
    }
}

void* SafeGetObjectPtr(unsigned long long guid) {
    __try {
        return GetObjectPtr(guid);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool SafeRightClickFishingBobber(void* bobber) {
    __try {
        RightClickFunc function = reinterpret_cast<RightClickFunc>(funcAddress_RightClickFunc);
        function(bobber, 1);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

int __cdecl FindFishingBobberCallback(unsigned long long guid, int) {
    if (g_fishingBobberGuid != 0 || guid == 0 || guid == g_lastFishingBobberGuid) {
        return 1;
    }
    void* object = nullptr;
    bool busy = false;
    if (InspectFishingObject(guid, object, busy)) {
        g_fishingBobberGuid = guid;
        g_fishingBobberPointer = reinterpret_cast<uintptr_t>(object);
        g_bobberFoundTick = GetTickCount();
        std::ostringstream event;
        event << "BOBBER_FOUND guid=" << guid
            << " ptr=0x" << std::hex << reinterpret_cast<uintptr_t>(object)
            << std::dec << " busyRaw=" << g_lastFishingBusyRaw.load();
        LogFishingEvent(event.str());
        if (busy) {
            g_biteDetectedTick = g_bobberFoundTick;
            LogFishingEvent("BITE_ALREADY_ACTIVE busyRaw=0");
        }
    }
    return 1;
}

bool FindFishingBobberByObjectManager(
    unsigned long long& foundGuid,
    void*& foundObject,
    bool& foundBusy,
    unsigned long& scannedThisPass) {
    __try {
        const uintptr_t manager = *reinterpret_cast<uintptr_t*>(address_ObjectManager);
        if (manager == 0) {
            return false;
        }
        const unsigned long long playerGuid =
            *reinterpret_cast<unsigned long long*>(manager + offset_ObjectManagerPlayerGuid);
        uintptr_t current = *reinterpret_cast<uintptr_t*>(manager + offset_ObjectManagerFirstObject);
        for (unsigned long guard = 0;
            current != 0 && (current & 1) == 0 && guard < 10000;
            ++guard) {
            ++scannedThisPass;
            g_fishingObjectsScanned.fetch_add(1);

            auto bytes = reinterpret_cast<uint8_t*>(current);
            if (*reinterpret_cast<int*>(bytes + offset_ObjectType) == kGameObjectType
                && *reinterpret_cast<int*>(bytes + offset_GameObjectId) == kFishingBobberObjectId) {
                const uintptr_t descriptor = *reinterpret_cast<uintptr_t*>(bytes + offset_ObjectDescriptor);
                const unsigned long long owner = descriptor == 0
                    ? 0
                    : *reinterpret_cast<unsigned long long*>(descriptor + offset_GameObjectCreatedBy);
                const int busyRaw = *reinterpret_cast<uint8_t*>(bytes + offset_GameObjectBusy);
                const unsigned long long guid =
                    *reinterpret_cast<unsigned long long*>(bytes + offset_ObjectGuid);
                g_fishingBobberCandidates.fetch_add(1);
                g_lastFishingCandidateGuid.store(guid);
                g_lastFishingCandidateOwner.store(owner);
                g_lastFishingBusyRaw.store(busyRaw);
                if (owner == playerGuid && guid != 0 && guid != g_lastFishingBobberGuid) {
                    foundGuid = guid;
                    foundObject = reinterpret_cast<void*>(current);
                    foundBusy = (busyRaw ^ 1) != 0;
                    return true;
                }
            }

            const uintptr_t next = *reinterpret_cast<uintptr_t*>(bytes + offset_ObjectNext);
            if (next == current) {
                break;
            }
            current = next;
        }
        return false;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool TryEnumerateFishingObjects() {
    __try {
        EnumerateVisibleObjects(reinterpret_cast<void*>(FindFishingBobberCallback), 0);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        g_lastFishingActionSucceeded.store(false);
        return false;
    }
}

bool FindFishingBobberOnMainThread() {
    TryEnumerateFishingObjects();

    if (g_fishingBobberGuid != 0) {
        return true;
    }

    unsigned long long guid = 0;
    void* object = nullptr;
    bool busy = false;
    unsigned long scannedThisPass = 0;
    if (FindFishingBobberByObjectManager(guid, object, busy, scannedThisPass)) {
        g_fishingBobberGuid = guid;
        g_fishingBobberPointer = reinterpret_cast<uintptr_t>(object);
        g_bobberFoundTick = GetTickCount();
        std::ostringstream event;
        event << "BOBBER_FOUND_DIRECT guid=" << guid
            << " ptr=0x" << std::hex << reinterpret_cast<uintptr_t>(object)
            << std::dec << " busyRaw=" << g_lastFishingBusyRaw.load()
            << " scanned=" << scannedThisPass;
        LogFishingEvent(event.str());
        if (busy) {
            g_biteDetectedTick = g_bobberFoundTick;
            LogFishingEvent("BITE_ALREADY_ACTIVE_DIRECT busyRaw=0");
        }
        return true;
    }

    return false;
}

bool CastFishingOnMainThread() {
    const bool english = ExecuteLuaOnMainThread("CastSpellByName('fishing')");
    const bool chinese = ExecuteLuaOnMainThread("CastSpellByName('\xE9\x92\x93\xE9\xB1\xBC')");
    if (english || chinese) {
        g_fishingCasts.fetch_add(1);
        g_lastFishingCastTick = GetTickCount();
        LogFishingEvent("CAST_TRIGGERED lua=ok");
        return true;
    }
    LogFishingEvent("CAST_FAILED lua address/call failed");
    return false;
}

void UpdateFishingOnMainThread() {
    if (!g_fishing.load()) {
        return;
    }
    if (g_fishingResetRequested.exchange(false)) {
        g_fishingObjectsScanned.store(0);
        g_fishingBobberCandidates.store(0);
        g_lastFishingCandidateGuid.store(0);
        g_lastFishingCandidateOwner.store(0);
        g_lastFishingBusyRaw.store(-1);
        g_fishingBobberGuid = 0;
        g_fishingBobberPointer = 0;
        g_lastFishingBobberGuid = 0;
        g_lastFishingCastTick = 0;
        g_bobberFoundTick = 0;
        g_biteDetectedTick = 0;
        g_lastFishingCatchTick = 0;
        g_lastFishingScanLogTick = 0;
    }

    const DWORD now = GetTickCount();
    if (g_fishingBobberGuid == 0) {
        FindFishingBobberOnMainThread();
        if (g_fishingBobberGuid == 0
            && (g_lastFishingScanLogTick == 0 || now - g_lastFishingScanLogTick >= 5000)) {
            std::ostringstream event;
            event << "SCAN_WAITING objects=" << g_fishingObjectsScanned.load()
                << " bobberCandidates=" << g_fishingBobberCandidates.load()
                << " lastCandidateGuid=" << g_lastFishingCandidateGuid.load()
                << " lastCandidateOwner=" << g_lastFishingCandidateOwner.load()
                << " playerGuid=" << GetPlayerGuid()
                << " busyRaw=" << g_lastFishingBusyRaw.load();
            LogFishingEvent(event.str());
            g_lastFishingScanLogTick = now;
        }
    }

    if (g_fishingBobberGuid != 0) {
        void* bobber = g_fishingBobberPointer != 0
            ? reinterpret_cast<void*>(g_fishingBobberPointer)
            : SafeGetObjectPtr(g_fishingBobberGuid);
        bool busy = false;
        if (bobber == nullptr || !ReadFishingBobber(bobber, GetPlayerGuid(), busy)) {
            LogFishingEvent("BOBBER_DISAPPEARED before bite/catch");
            g_lastFishingBobberGuid = g_fishingBobberGuid;
            g_fishingBobberGuid = 0;
            g_fishingBobberPointer = 0;
            g_biteDetectedTick = 0;
            g_lastFishingCastTick = 0;
            return;
        }
        if (busy && g_biteDetectedTick == 0) {
            g_biteDetectedTick = now;
            LogFishingEvent("BITE_DETECTED busyRaw=0; waiting reaction delay");
        }
        const DWORD reactionDelay = 500 + (g_fishingCatches.load() % 201);
        if (busy && now - g_biteDetectedTick >= reactionDelay) {
            std::ostringstream trigger;
            trigger << "REEL_TRIGGER ptr=0x" << std::hex << reinterpret_cast<uintptr_t>(bobber)
                << " function=0x" << funcAddress_RightClickFunc << std::dec
                << " delayMs=" << reactionDelay;
            LogFishingEvent(trigger.str());
            if (SafeRightClickFishingBobber(bobber)) {
                g_lastFishingActionSucceeded.store(true);
                g_fishingCatches.fetch_add(1);
                LogFishingEvent("REEL_CALL_RETURNED result=ok");
            }
            else {
                g_lastFishingActionSucceeded.store(false);
                LogFishingEvent("REEL_CALL_EXCEPTION result=failed");
            }
            g_lastFishingBobberGuid = g_fishingBobberGuid;
            g_fishingBobberGuid = 0;
            g_fishingBobberPointer = 0;
            g_biteDetectedTick = 0;
            g_lastFishingCatchTick = now;
            g_lastFishingCastTick = 0;
        }
        return;
    }

    const DWORD delayAfterCatch = 1600;
    if (g_lastFishingCatchTick != 0 && now - g_lastFishingCatchTick < delayAfterCatch) {
        return;
    }
    if (g_lastFishingCastTick == 0 || now - g_lastFishingCastTick >= 35000) {
        g_lastFishingActionSucceeded.store(CastFishingOnMainThread());
    }
}

struct HealingOffsets {
    int hp = -1, maxhp = -1, mana = -1, maxmana = -1, classByte = -1;
};
HealingOffsets g_healingOffsets;
bool g_healingOffsetsLoaded = false;

bool ReadHealingMemory(uintptr_t address, void* destination, size_t length) {
    __try { memcpy(destination, reinterpret_cast<void*>(address), length); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool ReadHealingDescriptors(void* object, uint32_t* values) {
    uintptr_t descriptor = 0;
    return object != nullptr
        && ReadHealingMemory(reinterpret_cast<uintptr_t>(object) + offset_ObjectDescriptor,
            &descriptor, sizeof(descriptor)) && descriptor != 0
        && ReadHealingMemory(descriptor, values, 256 * sizeof(uint32_t));
}

const char* HealingClass(unsigned char id) {
    switch (id) {
    case 1: return "WARRIOR"; case 2: return "PALADIN"; case 3: return "HUNTER";
    case 4: return "ROGUE"; case 5: return "PRIEST"; case 7: return "SHAMAN";
    case 8: return "MAGE"; case 9: return "WARLOCK"; case 11: return "DRUID";
    default: return "UNKNOWN";
    }
}

std::string HealingJsonString(const std::string& value) {
    std::string result = "\"";
    const char* hex = "0123456789abcdef";
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') { result += '\\'; result += c; }
        else if (c < 32) { result += "\\u00"; result += hex[c >> 4]; result += hex[c & 15]; }
        else result += c;
    }
    return result + "\"";
}

std::string HealingObjectName(uint64_t guid) {
    uintptr_t node = 0;
    if (!ReadHealingMemory(0xC0E230, &node, sizeof(node))) return "";
    for (unsigned int count = 0; node && count < 4096; ++count) {
        uint64_t candidate = 0;
        if (!ReadHealingMemory(node + 0xC, &candidate, sizeof(candidate))) break;
        if (candidate == guid) {
            std::string name;
            for (size_t i = 0; i < 128; ++i) {
                char c = 0;
                if (!ReadHealingMemory(node + 0x14 + i, &c, 1)) return "";
                if (!c) return name;
                name += c;
            }
            return "";
        }
        uintptr_t next = 0;
        if (!ReadHealingMemory(node, &next, sizeof(next)) || next == node) break;
        node = next;
    }
    return "";
}

// These offsets were traced in D:\code\csv\pihusuowow\WoW.exe.
const uintptr_t offset_HealingAttributes = 0x110;
const uintptr_t address_HealingRaidCount = 0xB713E0;
const uintptr_t address_HealingRaidMembers = 0xB712A8;
const uintptr_t address_HealingPartyGuids = 0xBC6F48;

void* HealingObjectByGuid(uint64_t guid) {
    __try { return guid ? GetObjectPtr(guid) : nullptr; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

struct HealingAttributes {
    uint32_t hp{}, maxhp{}, mana{}, maxmana{}, intellect{}, spirit{};
    unsigned char classId{}, powerType{};
};

bool ReadHealingAttributes(void* object, HealingAttributes& result) {
    uintptr_t attributes = 0;
    if (!object || !ReadHealingMemory(reinterpret_cast<uintptr_t>(object) + offset_HealingAttributes,
        &attributes, sizeof(attributes)) || !attributes) return false;
    return ReadHealingMemory(attributes + 0x40, &result.hp, 4)
        && ReadHealingMemory(attributes + 0x58, &result.maxhp, 4)
        && ReadHealingMemory(attributes + 0x44, &result.mana, 4)
        && ReadHealingMemory(attributes + 0x5C, &result.maxmana, 4)
        && ReadHealingMemory(attributes + 0x79, &result.classId, 1)
        && ReadHealingMemory(attributes + 0x7B, &result.powerType, 1)
        && ReadHealingMemory(attributes + 0x24C, &result.intellect, 4)
        && ReadHealingMemory(attributes + 0x250, &result.spirit, 4)
        && result.maxhp > 0 && result.hp <= result.maxhp
        && result.mana <= result.maxmana
        && std::string(HealingClass(result.classId)) != "UNKNOWN";
}

bool ReadHealingPosition(void* object, Vector3& position) {
    const uintptr_t base = reinterpret_cast<uintptr_t>(object);
    return object && ReadHealingMemory(base + offset_UnitPositionX, &position.x, 4)
        && ReadHealingMemory(base + offset_UnitPositionY, &position.y, 4)
        && ReadHealingMemory(base + offset_UnitPositionZ, &position.z, 4)
        && std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z);
}

// Traced from UnitAffectingCombat (0x517E10), UnitBuff (0x519500),
// UnitDebuff (0x5198F0), and GetSpellName (0x4B3FE0) in this client.
bool HealingSpellText(uint32_t id, std::string& name, std::string* rank = nullptr) {
    uint32_t maximum = 0, locale = 0;
    uintptr_t table = 0, record = 0, text = 0;
    if (!ReadHealingMemory(0xC0D78C, &maximum, 4) || !id || id > maximum
        || !ReadHealingMemory(0xC0D788, &table, 4) || !table
        || !ReadHealingMemory(table + id * 4, &record, 4) || !record
        || !ReadHealingMemory(0xC0E080, &locale, 4) || locale > 8) return false;
    auto readText = [&](uintptr_t field, std::string& result) {
        if (!ReadHealingMemory(field, &text, 4) || !text) return false;
        result.clear();
        for (size_t i = 0; i < 512; ++i) {
            char value = 0;
            if (!ReadHealingMemory(text + i, &value, 1)) return false;
            if (!value) return true;
            result += value;
        }
        return false;
    };
    return readText(record + 0x1E0 + locale * 4, name) && !name.empty()
        && (!rank || readText(record + 0x204 + locale * 4, *rank));
}

bool ReadHealingAuras(void* object, bool debuffs, std::vector<std::string>& names) {
    uintptr_t attributes = 0;
    if (!object || !ReadHealingMemory(reinterpret_cast<uintptr_t>(object) + 0x110,
        &attributes, 4) || !attributes) return false;
    for (unsigned int i = debuffs ? 32 : 0; i < (debuffs ? 48u : 32u); ++i) {
        uint32_t id = 0;
        unsigned char flags = 0;
        if (!ReadHealingMemory(attributes + 0xA4 + i * 4, &id, 4)
            || !ReadHealingMemory(attributes + 0x164 + i / 2, &flags, 1)) return false;
        if (!id || !((flags >> ((i % 2) * 4)) & 0xE)) continue;
        std::string name;
        if (!HealingSpellText(id, name)) return false;
        names.push_back(name);
    }
    return true;
}

struct HealingAuraCacheEntry {
    void* object = nullptr;
    ULONGLONG refreshed = 0;
    uint64_t generation = 0;
    bool buffsValid = false, debuffsValid = false;
    std::vector<std::string> buffs, debuffs;
};
// Accessed only by the game main-thread dispatcher.
std::unordered_map<uint64_t, HealingAuraCacheEntry> g_healingAuraCache;
uint64_t g_healingAuraPlayer = 0, g_healingAuraGeneration = 0;

const HealingAuraCacheEntry& CachedHealingAuras(uint64_t guid, void* object, ULONGLONG now) {
    auto& entry = g_healingAuraCache[guid];
    if (entry.object != object || now - entry.refreshed >= 1000) {
        entry.object = object;
        entry.buffs.clear();
        entry.debuffs.clear();
        entry.buffsValid = ReadHealingAuras(object, false, entry.buffs);
        entry.debuffsValid = ReadHealingAuras(object, true, entry.debuffs);
        entry.refreshed = now;
    }
    entry.generation = g_healingAuraGeneration;
    return entry;
}

std::string HealingNamesJson(const std::vector<std::string>& names) {
    std::string result = "[";
    for (const auto& name : names) {
        if (result.size() > 1) result += ',';
        result += HealingJsonString(name);
    }
    return result + ']';
}

// Read one global using Lua 5.0 pushstring/rawget, restoring the original stack.
// This is not a DoString return value and runs exclusively on the game main thread.
bool ReadHealingLuaGlobal(const char* name, double* number, char* text, size_t capacity) {
    void* state = nullptr;
    int oldTop = 0;
    __try {
        state = *reinterpret_cast<void**>(0xCEEF74);
        if (!state) return false;
        const uintptr_t top = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(state) + 8);
        const uintptr_t base = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(state) + 12);
        if (top < base || (top - base) % 16 || (top - base) / 16 > 65536) return false;
        oldTop = static_cast<int>((top - base) / 16);
        bool valid = false;
        __try {
            // 0x6F3BC0 is lua_rawgeti, NOT lua_getfield.
            typedef void(__fastcall* PushString)(void*, const char*);
            typedef void(__fastcall* RawGet)(void*, int);
            reinterpret_cast<PushString>(0x6F3890)(state, name);
            reinterpret_cast<RawGet>(0x6F3B00)(state, -10001);
            const uintptr_t value = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(state) + 8) - 16;
            const int type = *reinterpret_cast<int*>(value);
            if (number && type == 3) {
                *number = *reinterpret_cast<double*>(value + 8);
                valid = std::isfinite(*number);
            }
            if (text && capacity && type == 4) {
                const char* string = *reinterpret_cast<const char**>(value + 8) + 16;
                for (size_t i = 0; i < capacity; ++i) {
                    text[i] = string[i];
                    if (!text[i]) { valid = true; break; }
                }
            }
        }
        __finally {
            typedef void(__fastcall* SetTop)(void*, int);
            reinterpret_cast<SetTop>(0x6F3080)(state, oldTop);
        }
        return valid;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool HealingRuntimeMatches() {
    unsigned char actual[6]{};
    const unsigned char expected[6] = {0xA1, 0x74, 0xEF, 0xCE, 0x00, 0xC3};
    unsigned char getField[6]{}, setTop[6]{};
    unsigned char pushString[6]{};
    const unsigned char expectedPush[6] = {0x85, 0xD2, 0x56, 0x8B, 0xF1, 0x75};
    const unsigned char expectedGet[6] = {0x85, 0xD2, 0x56, 0x57, 0x8B, 0xF1};
    const unsigned char expectedSet[6] = {0x85, 0xD2, 0x8B, 0x41, 0x08, 0x7C};
    return ReadHealingMemory(0x7040D0, actual, sizeof(actual))
        && memcmp(actual, expected, sizeof(actual)) == 0
        && ReadHealingMemory(0x6F3B00, getField, sizeof(getField))
        && memcmp(getField, expectedGet, sizeof(getField)) == 0
        && ReadHealingMemory(0x6F3080, setTop, sizeof(setTop))
        && memcmp(setTop, expectedSet, sizeof(setTop)) == 0
        && ReadHealingMemory(0x6F3890, pushString, sizeof(pushString))
        && memcmp(pushString, expectedPush, sizeof(pushString)) == 0
        && IsExecutableAddress(0x6F3B00) && IsExecutableAddress(0x6F3890) && IsExecutableAddress(0x6F3080);
}

bool EnsureHealingObserver() {
    if (!HealingRuntimeMatches()) return false;
    double ready = 0;
    if (ReadHealingLuaGlobal("Traveler112CastReady", &ready, nullptr, 0) && ready == 1) return true;
    return ExecuteLuaOnMainThread(kHealingObserverLua)
        && ReadHealingLuaGlobal("Traveler112CastReady", &ready, nullptr, 0) && ready == 1;
}

bool HealingClientTime(uint32_t& time) {
    __try {
        typedef uint32_t(__cdecl* Clock)();
        time = reinterpret_cast<Clock>(0x42C010)();
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Native GetSpellCooldown traverses this list (0x6E13E0).
// Node +0x28 is StartRecoveryCategory; 133 is the shared spell GCD category.
bool ReadHealingGcd(uint32_t now, int& remaining) {
    remaining = 0;
    uintptr_t node = 0;
    if (!ReadHealingMemory(0xCECAEC + 8, &node, 4)) return false;
    for (unsigned int i = 0; node && !(node & 1); ++i) {
        if (i >= 4096) return false;
        uint32_t category = 0, start = 0, duration = 0;
        uintptr_t next = 0;
        if (!ReadHealingMemory(node + 0x28, &category, 4)
            || !ReadHealingMemory(node + 0x10, &start, 4)
            || !ReadHealingMemory(node + 0x2C, &duration, 4)
            || !ReadHealingMemory(node + 4, &next, 4) || next == node) return false;
        if (category == 133 && duration && duration <= 60000) {
            const int32_t delta = static_cast<int32_t>(start + duration - now);
            if (delta > 0 && delta <= 60000 && delta > remaining) remaining = delta;
        }
        node = next;
    }
    return true;
}

void AppendHealingCastState(std::ostringstream& json, void* player) {
    // Main-thread-only fallback, scoped to the current character and spell.
    static uint64_t cachedGuid = 0;
    static uint32_t cachedSpell = 0;
    static int cachedRemaining = 0;
    uint32_t now = 0, spellId = 0;
    if (!HealingRuntimeMatches() || !HealingClientTime(now)) return;
    int gcd = 0;
    if (ReadHealingGcd(now, gcd)) json << ",\"gcd_remaining_ms\":" << gcd;
    // 0x60D020 sets the active cast ID; 0x60D040 clears it on stop/failure.
    if (!ReadHealingMemory(reinterpret_cast<uintptr_t>(player) + 0xC8C, &spellId, 4)) return;
    uintptr_t attributes = 0;
    uint32_t channelId = 0;
    if (ReadHealingMemory(reinterpret_cast<uintptr_t>(player) + 0x110, &attributes, 4) && attributes)
        ReadHealingMemory(attributes + 0x228, &channelId, 4);
    if (!spellId) spellId = channelId;
    std::string name, rank;
    if (spellId && !HealingSpellText(spellId, name, &rank)) return;
    char requested[512]{};
    const std::string level = "\xE7\xAD\x89\xE7\xBA\xA7";
    if (rank.compare(0, level.size(), level) == 0 && rank.size() > level.size()
        && rank[level.size()] != ' ') rank.insert(level.size(), " ");
    std::string display = name + (rank.empty() ? "" : "(" + rank + ")");
    if (ReadHealingLuaGlobal("Traveler112RequestedSpell", nullptr, requested, sizeof(requested))
        && std::string(requested).substr(0, std::string(requested).find('(')) == name)
        display = requested;
    json << ",\"casting\":" << (spellId ? "true" : "false")
        << ",\"cast_spell\":" << HealingJsonString(spellId ? display : "");
    const uint64_t guid = GetPlayerGuid();
    if (cachedGuid != guid || cachedSpell != spellId || !spellId) {
        cachedGuid = guid;
        cachedSpell = spellId;
        cachedRemaining = 0;
    }
    if (spellId) {
        double end = 0;
        char eventName[512]{};
        if (ReadHealingLuaGlobal("Traveler112CastEnd", &end, nullptr, 0)
            && ReadHealingLuaGlobal("Traveler112CastName", nullptr, eventName, sizeof(eventName))
            && name == eventName && end > 0) {
            const double delta = end * 1000 - now;
            if (delta >= -1000 && delta <= 600000)
                cachedRemaining = static_cast<int>(delta > 0 ? delta : 0);
        }
    }
    json << ",\"cast_remaining_ms\":" << cachedRemaining;
}

void AppendHealingCastResult(std::ostringstream& json) {
    char result[1024]{};
    if (!HealingRuntimeMatches()
        || !ReadHealingLuaGlobal("Traveler112CastResult", nullptr, result, sizeof(result)) || !result[0]) return;
    std::istringstream stream(result);
    std::string seq, spell, success, reason, verified;
    if (!std::getline(stream, seq, '\x1f') || !std::getline(stream, spell, '\x1f')
        || !std::getline(stream, success, '\x1f') || !std::getline(stream, reason, '\x1f')
        || !std::getline(stream, verified)) return;
    if (seq.empty() || seq.find_first_not_of("0123456789") != std::string::npos
        || (success != "0" && success != "1") || (verified != "0" && verified != "1")) return;
    json << ",\"last_cast\":{\"seq\":" << seq << ",\"spell\":" << HealingJsonString(spell)
        << ",\"success\":" << (success == "1" ? "true" : "false")
        << ",\"reason\":" << HealingJsonString(reason)
        << ",\"verified\":" << (verified == "1" ? "true" : "false")
        << ",\"source\":\"" << (verified == "1" ? "client_event" : "unconfirmed") << "\"}";
}

bool ReadHealingSpellCount(uint32_t& total) {
    total = 0;
    uint32_t tabs = 0;
    uintptr_t table = 0;
    if (!ReadHealingMemory(0xB71170, &tabs, 4) || tabs > 32
        || !ReadHealingMemory(0xB71160, &table, 4) || (tabs && !table)) return false;
    for (uint32_t i = 0; i < tabs; ++i) {
        uintptr_t tab = 0;
        uint32_t count = 0;
        if (!ReadHealingMemory(table + i * 4, &tab, 4) || !tab
            || !ReadHealingMemory(tab + 4, &count, 4) || count > 1024 - total) return false;
        total += count;
    }
    return true;
}

bool ReadHealingStandState(void* player, unsigned char& state) {
    uintptr_t attributes = 0;
    // SitOrStand 0x48B920 reads this byte; 0=stand, 1=sit, 4..6=chair.
    return player && ReadHealingMemory(reinterpret_cast<uintptr_t>(player) + 0x110,
        &attributes, 4) && attributes && ReadHealingMemory(attributes + 0x210, &state, 1);
}


bool ReadHealingSpellbook(std::vector<std::string>& spells) {
    uint32_t total = 0;
    if (!ReadHealingSpellCount(total)) return false;
    for (uint32_t i = 0; i < total; ++i) {
        uint32_t id = 0;
        std::string name, rank;
        if (!ReadHealingMemory(0xB700F0 + i * 4, &id, 4)
            || !HealingSpellText(id, name, &rank)) return false;
        std::string compactRank;
        for (unsigned char c : rank) {
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n') compactRank += c;
        }
        spells.push_back(name + (compactRank.empty() ? "" : "(" + compactRank + ")"));
    }
    return true;
}

std::atomic<bool> g_lootAll(false);
const UINT kLootTickMessage = WM_APP + 0x221;
std::vector<uint64_t> g_lootAttempted; // Game main thread only.
Vector3 g_lootOrigin{};
uint64_t g_lootBest = 0;
float g_lootDistance = 4.5f;
ULONGLONG g_lootStarted = 0, g_lootLastTick = 0;
bool g_lootPending = false;
uint64_t g_lootPlayer = 0;
std::string g_lootStatus = "idle";

bool EnsureNearbyLootLua() {
    if (!HealingRuntimeMatches()) return false;
    double ready = 0;
    if (ReadHealingLuaGlobal("Traveler112LootReady", &ready, nullptr, 0) && ready == 1) return true;
    return ExecuteLuaOnMainThread(kNearbyLootLua)
        && ReadHealingLuaGlobal("Traveler112LootReady", &ready, nullptr, 0) && ready == 1;
}

void StopNearbyLoot(const char* reason) {
    g_lootAll.store(false);
    g_lootPending = false;
    g_lootStatus = reason;
    ExecuteLuaOnMainThread("Traveler112LootActive=0");
}

int __cdecl FindNearbyLoot(uint64_t guid, int) {
    if (!guid || std::find(g_lootAttempted.begin(), g_lootAttempted.end(), guid) != g_lootAttempted.end()) return 1;
    void* object = HealingObjectByGuid(guid);
    uintptr_t descriptors = 0;
    uint32_t type = 0, hp = 1, flags = 0;
    Vector3 position{};
    const uintptr_t address = reinterpret_cast<uintptr_t>(object);
    if (!object || !ReadHealingMemory(address + 0x14, &type, 4) || type != 3
        || !ReadHealingMemory(address + 8, &descriptors, 4) || !descriptors
        || !ReadHealingMemory(descriptors + 0x58, &hp, 4) || hp != 0
        || !ReadHealingMemory(descriptors + 0x23C, &flags, 4) || !(flags & 1)
        || !ReadHealingPosition(object, position)) return 1;
    const float distance = Distance3D(g_lootOrigin, position);
    if (distance < g_lootDistance) { g_lootDistance = distance; g_lootBest = guid; }
    return 1;
}

bool CallLootUnit(void* object) {
    __try {
        const unsigned char expected[] = {0x53, 0x8B, 0xDC, 0x83, 0xEC, 0x08, 0x83, 0xE4};
        if (memcmp(reinterpret_cast<void*>(0x60BEA0), expected, sizeof(expected))) return false;
        reinterpret_cast<void(__thiscall*)(void*, int)>(0x60BEA0)(object, 1);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

std::string LootNearbyOnce() {
    if (!ReadHealingPosition(GetLocalPlayer(), g_lootOrigin)) return "error: player unavailable";
    g_lootBest = 0;
    g_lootDistance = 4.5f;
    EnumerateVisibleObjects(reinterpret_cast<void*>(FindNearbyLoot), 0);
    if (!g_lootBest) return "empty";
    void* object = HealingObjectByGuid(g_lootBest);
    if (!ExecuteLuaOnMainThread("Traveler112LootState=1; Traveler112LootItems=0; Traveler112LootActive=1"))
        return "error: loot observer failed";
    if (!object || !CallLootUnit(object)) return "error: loot interaction failed or unsupported client";
    g_lootAttempted.push_back(g_lootBest);
    g_lootPending = true;
    g_lootLastTick = GetTickCount64();
    g_lootStatus = "waiting_for_loot";
    return "ok";
}

std::string StartNearbyLoot(bool all) {
    if (!EnsureNearbyLootLua()) return "error: loot observer unavailable";
    if (g_lootPending || g_lootAll.load()) return "error: loot already running; use lootnear stop";
    g_lootAttempted.clear();
    g_lootPlayer = GetPlayerGuid();
    g_lootStarted = GetTickCount64();
    const std::string result = LootNearbyOnce();
    if (result == "ok") g_lootAll.store(all);
    else StopNearbyLoot(result == "empty" ? "no_nearby_loot" : "interaction_failed");
    return result == "empty" ? "ok: no nearby lootable corpses" : result;
}

void TickNearbyLoot() {
    const ULONGLONG now = GetTickCount64();
    if (!GetPlayerGuid()) { if (g_lootPending || g_lootAll.load()) StopNearbyLoot("logged_out"); return; }
    if (!EnsureNearbyLootLua()) return;
    if ((g_lootPending || g_lootAll.load()) && g_lootPlayer != GetPlayerGuid()) StopNearbyLoot("character_changed");
    double request = 0;
    if (ReadHealingLuaGlobal("Traveler112LootRequest", &request, nullptr, 0) && request != 0) {
        ExecuteLuaOnMainThread("Traveler112LootRequest=0");
        if (request == 3) StopNearbyLoot("stopped");
        else if (!g_lootPending && !g_lootAll.load()) StartNearbyLoot(request == 2);
    }
    if (!g_lootPending && !g_lootAll.load()) return;
    if (now - g_lootStarted > 30000) { StopNearbyLoot("timeout"); return; }
    if (g_lootPending) {
        double state = 0;
        if (!ReadHealingLuaGlobal("Traveler112LootState", &state, nullptr, 0)) { StopNearbyLoot("observer_read_failed"); return; }
        if (state == 3) { g_lootPending = false; ExecuteLuaOnMainThread("Traveler112LootActive=0"); }
        else {
            // Wait for the server/window instead of overwriting its current corpse.
            if (now - g_lootLastTick > 3000) StopNearbyLoot(state == 2 ? "loot_window_blocked" : "no_loot_response");
            return;
        }
    }
    if (!g_lootAll.load()) { g_lootStatus = "completed"; return; }
    if (g_lootAttempted.size() >= 20) { StopNearbyLoot("limit_20_reached"); return; }
    if (now - g_lootLastTick < 200) return;
    const std::string result = LootNearbyOnce();
    if (result != "ok") StopNearbyLoot(result == "empty" ? "completed" : "interaction_failed");
}

uint64_t HealingUnitGuid(const char* token) {
    __try {
        const unsigned char expected[] = {0x55, 0x8B, 0xEC, 0x51, 0x56, 0x68, 0x90};
        if (memcmp(reinterpret_cast<const void*>(0x515970), expected, sizeof(expected))) return 0;
        typedef uint64_t(__fastcall* Resolve)(const char*);
        return reinterpret_cast<Resolve>(0x515970)(token);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

struct HealingPetOwner { uint64_t guid; std::string name, token; };

void AppendHealingPets(std::ostringstream& json, const std::vector<HealingPetOwner>& owners,
    const Vector3& ownPosition, bool positionValid, ULONGLONG now) {
    // Names are read by token, never by name lookup; GUIDs stay native uint64.
    std::string script;
    std::vector<uint64_t> snapshot;
    for (size_t i = 0; i < owners.size(); ++i) {
        snapshot.push_back(HealingUnitGuid(owners[i].token.c_str()));
        script += "Traveler112PetName" + std::to_string(i) + "=UnitName('" + owners[i].token + "') or '';";
    }
    const bool namesReady = ExecuteLuaOnMainThread(script.c_str());
    std::vector<uint64_t> seen;
    json << ",\"pets\":[";
    bool first = true;
    for (size_t i = 0; i < owners.size(); ++i) {
        const auto& owner = owners[i];
        const uint64_t guid = snapshot[i];
        if (!guid || std::find(seen.begin(), seen.end(), guid) != seen.end()) continue;
        seen.push_back(guid);
        void* object = HealingObjectByGuid(guid);
        HealingAttributes attributes;
        Vector3 position{};
        const bool available = ReadHealingAttributes(object, attributes)
            && positionValid && ReadHealingPosition(object, position);
        char name[512]{};
        if (namesReady) ReadHealingLuaGlobal(("Traveler112PetName" + std::to_string(i)).c_str(), nullptr, name, sizeof(name));
        // Resolve again to avoid pairing a changed summon with the previous name.
        if (HealingUnitGuid(owner.token.c_str()) != guid) continue;
        if (!first) json << ',';
        first = false;
        json << "{\"guid\":\"" << guid << "\",\"name\":" << HealingJsonString(name)
            << ",\"owner\":" << HealingJsonString(owner.name) << ",\"unit\":" << HealingJsonString(owner.token)
            << ",\"available\":" << (available ? "true" : "false")
            << ",\"hp\":" << (available ? std::to_string(attributes.hp) : "null")
            << ",\"max_hp\":" << (available ? std::to_string(attributes.maxhp) : "null")
            << ",\"dead\":" << (available ? (attributes.hp == 0 ? "true" : "false") : "null")
            << ",\"distance\":" << (available ? std::to_string(Distance3D(ownPosition, position)) : "null");
        if (available) {
            const auto& auras = CachedHealingAuras(guid, object, now);
            if (auras.buffsValid) json << ",\"buffs\":" << HealingNamesJson(auras.buffs);
            if (auras.debuffsValid) json << ",\"debuffs\":" << HealingNamesJson(auras.debuffs);
        } else g_healingAuraCache.erase(guid);
        json << '}';
    }
    json << ']';
}

std::vector<uint64_t> g_bossRoster;
uint64_t g_bossGuid = 0;
uint32_t g_bossHp = 0, g_bossMaxHp = 0;
std::string g_bossName, g_bossClassification;

int __cdecl ScanHealingBoss(uint64_t guid, int) {
    void* object = HealingObjectByGuid(guid);
    const uintptr_t ptr = reinterpret_cast<uintptr_t>(object);
    uintptr_t attributes = 0, creature = 0;
    uint32_t type = 0, classification = 0, flags = 0, hp = 0, maxhp = 0;
    uint64_t target = 0;
    if (!object || !ReadHealingMemory(ptr + 0x14, &type, 4) || type != 3
        || !ReadHealingMemory(ptr + 0x110, &attributes, 4) || !attributes
        || !ReadHealingMemory(ptr + 0xB30, &creature, 4) || !creature
        || !ReadHealingMemory(creature + 0x20, &classification, 4)
        || (classification != 1 && classification != 3)
        || !ReadHealingMemory(attributes + 0xA0, &flags, 4) || !(flags & 0x80000)
        || !ReadHealingMemory(attributes + 0x40, &hp, 4) || !hp
        || !ReadHealingMemory(attributes + 0x58, &maxhp, 4) || maxhp < hp
        || !ReadHealingMemory(attributes + 0x28, &target, 8)) return 1;
    bool engaged = std::find(g_bossRoster.begin(), g_bossRoster.end(), target) != g_bossRoster.end();
    // A boss can temporarily target nobody; accept a fighting roster member targeting it.
    for (uint64_t member : g_bossRoster) {
        if (engaged) break;
        void* unit = HealingObjectByGuid(member);
        uintptr_t fields = 0;
        uint64_t memberTarget = 0;
        uint32_t memberFlags = 0;
        if (unit && ReadHealingMemory(reinterpret_cast<uintptr_t>(unit) + 0x110, &fields, 4) && fields
            && ReadHealingMemory(fields + 0x28, &memberTarget, 8) && memberTarget == guid
            && ReadHealingMemory(fields + 0xA0, &memberFlags, 4) && (memberFlags & 0x80000)) engaged = true;
    }
    if (!engaged) return 1;
    // Elite fallback is deliberately disabled until a server-specific list/threshold exists.
    if (classification != 3) return 1;
    if (g_bossGuid && hp >= g_bossHp) return 1;
    uintptr_t namePtr = 0;
    std::string name;
    if (ReadHealingMemory(creature, &namePtr, 4) && namePtr) {
        for (size_t i = 0; i < 255; ++i) {
            char ch = 0;
            if (!ReadHealingMemory(namePtr + i, &ch, 1)) { name.clear(); break; }
            if (!ch) break;
            name += ch;
        }
    }
    g_bossGuid = guid; g_bossHp = hp; g_bossMaxHp = maxhp;
    g_bossName = name; g_bossClassification = "worldboss";
    return 1;
}

void AppendHealingBoss(std::ostringstream& json, const std::vector<HealingPetOwner>& owners) {
    g_bossRoster.clear();
    for (const auto& owner : owners) {
        g_bossRoster.push_back(owner.guid);
        const uint64_t pet = HealingUnitGuid(owner.token.c_str());
        if (pet) g_bossRoster.push_back(pet);
    }
    g_bossGuid = 0;
    EnumerateVisibleObjects(reinterpret_cast<void*>(ScanHealingBoss), 0);
    json << ",\"boss\":";
    if (!g_bossGuid) { json << "null"; return; }
    json << "{\"guid\":\"" << g_bossGuid << "\",\"name\":" << HealingJsonString(g_bossName)
        << ",\"classification\":" << HealingJsonString(g_bossClassification)
        << ",\"hp\":" << g_bossHp << ",\"max_hp\":" << g_bossMaxHp << ",\"in_combat\":true}";
}

bool ReadHealingFollowing(bool& following) {
    // FollowUnit -> 0x60FED0(action=3) -> 0x611130 sets action/target.
    // 0x60FB60 stops follow and clears target; idle action is 12.
    const unsigned char expected[] = {0x89, 0x15, 0x88, 0xD8, 0xC4, 0x00};
    unsigned char actual[sizeof(expected)]{};
    uint32_t action = 0;
    uint64_t target = 0;
    if (!ReadHealingMemory(0x611178, actual, sizeof(actual))
        || memcmp(actual, expected, sizeof(expected)) != 0
        || !ReadHealingMemory(0xC4D888, &action, sizeof(action))
        || !ReadHealingMemory(0xC4D980, &target, sizeof(target))) return false;
    following = action == 3 && target != 0;
    return true;
}

std::string ReadHealingState() {
    void* player = GetLocalPlayer();
    HealingAttributes own;
    if (!ReadHealingAttributes(player, own)) {
        g_healingAuraCache.clear();
        g_healingAuraPlayer = 0;
        return "error: player attributes unavailable or invalid";
    }
    EnsureHealingObserver();
    uint32_t count = 0;
    if (!ReadHealingMemory(address_HealingRaidCount, &count, 4) || count > 40)
        return "error: invalid raid count";
    const uint64_t playerGuid = GetPlayerGuid();
    Vector3 ownPosition{};
    const bool positionValid = ReadHealingPosition(player, ownPosition);
    static uint64_t previousGuid = 0;
    static Vector3 previousPosition{};
    static ULONGLONG previousTime = 0;
    const ULONGLONG now = GetTickCount64();
    if (g_healingAuraPlayer != playerGuid) {
        g_healingAuraCache.clear();
        g_healingAuraPlayer = playerGuid;
    }
    ++g_healingAuraGeneration;
    const bool moving = positionValid && previousGuid == playerGuid && previousTime
        && now - previousTime <= 2000 && Distance3D(previousPosition, ownPosition) > 0.01f;
    previousGuid = positionValid ? playerGuid : 0;
    previousPosition = ownPosition;
    previousTime = positionValid ? now : 0;
    bool rosterVerified = true;
    uintptr_t ownAttributes = 0;
    uint32_t unitFlags = 0;
    const bool combatValid = ReadHealingMemory(reinterpret_cast<uintptr_t>(player) + 0x110,
        &ownAttributes, 4) && ownAttributes && ReadHealingMemory(ownAttributes + 0xA0, &unitFlags, 4);
    const auto& ownAuras = CachedHealingAuras(playerGuid, player, now);
    const bool buffsValid = ownAuras.buffsValid;
    const bool debuffsValid = ownAuras.debuffsValid;
    const auto& ownBuffs = ownAuras.buffs;
    const auto& ownDebuffs = ownAuras.debuffs;
    bool hasParty = false;
    if (!count) {
        for (uint32_t i = 0; i < 4; ++i) {
            uint64_t guid = 0;
            if (!ReadHealingMemory(address_HealingPartyGuids + i * 8, &guid, 8))
                return "error: party table unreadable";
            hasParty = hasParty || guid != 0;
        }
    }
    std::vector<HealingPetOwner> petOwners;
    petOwners.push_back({playerGuid, HealingObjectName(playerGuid), "pet"});
    uint32_t playerSubgroup = 0;
    if (count) {
        for (uint32_t i = 0; i < count; ++i) {
            uintptr_t record = 0;
            uint64_t guid = 0;
            uint32_t group = 0;
            if (ReadHealingMemory(address_HealingRaidMembers + i * 4, &record, 4) && record
                && ReadHealingMemory(record, &guid, 8) && guid == playerGuid
                && ReadHealingMemory(record + 8, &group, 4) && group < 8) playerSubgroup = group + 1;
        }
    }
    std::ostringstream json;
    json << "{\"scope\":\"" << (count ? "raid" : hasParty ? "group" : "solo")
        << "\",\"player\":{\"name\":"
        << HealingJsonString(HealingObjectName(GetPlayerGuid()))
        << ",\"subgroup\":" << playerSubgroup
        << ",\"hp\":" << own.hp << ",\"max_hp\":" << own.maxhp
        << ",\"mana\":" << (own.powerType == 0 ? own.mana : 0)
        << ",\"max_mana\":" << (own.powerType == 0 ? own.maxmana : 0)
        << ",\"power_type\":" << static_cast<unsigned int>(own.powerType)
        << ",\"class\":\"" << HealingClass(own.classId)
        << "\",\"spirit\":" << own.spirit << ",\"intellect\":" << own.intellect
        << ",\"plus_healing\":0,\"plus_healing_verified\":false,\"moving\":"
        << (moving ? "true" : "false");
    if (buffsValid) {
        bool drinking = false;
        for (const auto& name : ownBuffs) if (name == "喝水" || name == "Drink") drinking = true;
        json << ",\"buffs\":" << HealingNamesJson(ownBuffs)
            << ",\"drinking\":" << (drinking ? "true" : "false");
    }
    if (debuffsValid) json << ",\"debuffs\":" << HealingNamesJson(ownDebuffs);
    unsigned char standState = 0;
    if (ReadHealingStandState(player, standState))
        json << ",\"sitting\":" << ((standState == 1 || (standState >= 4 && standState <= 6)) ? "true" : "false");
    AppendHealingCastState(json, player);
    bool following = false;
    if (ReadHealingFollowing(following))
        json << ",\"following\":" << (following ? "true" : "false");
    json << "},\"raid\":[";
    bool first = true;
    const uint32_t members = count ? count : 4;
    for (uint32_t i = 0; i < members; ++i) {
        uint64_t guid = 0;
        uint32_t subgroup = 0;
        if (count) {
            uintptr_t record = 0;
            if (!ReadHealingMemory(address_HealingRaidMembers + i * 4, &record, 4))
                return "error: raid member table unreadable";
            if (!record) { rosterVerified = false; continue; }
            if (!ReadHealingMemory(record, &guid, 8)
                || !ReadHealingMemory(record + 8, &subgroup, 4)) return "error: raid record unreadable";
            ++subgroup;
        }
        else if (!ReadHealingMemory(address_HealingPartyGuids + i * 8, &guid, 8))
            return "error: party table unreadable";
        if (!guid) { if (count) rosterVerified = false; continue; }
        if (guid == playerGuid) continue;
        void* object = HealingObjectByGuid(guid);
        if (object == player) continue;
        HealingAttributes attributes;
        Vector3 memberPosition{};
        const bool available = ReadHealingAttributes(object, attributes)
            && positionValid && ReadHealingPosition(object, memberPosition);
        if (!available) g_healingAuraCache.erase(guid);
        const std::string memberName = HealingObjectName(guid);
        petOwners.push_back({guid, memberName, std::string(count ? "raidpet" : "partypet") + std::to_string(i + 1)});
        if (memberName.empty()) rosterVerified = false;
        if (!first) json << ',';
        first = false;
        json << "{\"guid\":\"" << guid << "\",\"name\":" << HealingJsonString(memberName)
            << ",\"unit\":\"" << (count ? "raid" : "party") << i + 1
            << "\",\"subgroup\":" << subgroup << ",\"available\":" << (available ? "true" : "false")
            << ",\"hp\":" << (available ? std::to_string(attributes.hp) : "null")
            << ",\"max_hp\":" << (available ? std::to_string(attributes.maxhp) : "null")
            << ",\"mana\":" << (available ? std::to_string(attributes.powerType == 0 ? attributes.mana : 0) : "null")
            << ",\"max_mana\":" << (available ? std::to_string(attributes.powerType == 0 ? attributes.maxmana : 0) : "null")
            << ",\"class\":\"" << (available ? HealingClass(attributes.classId) : "UNKNOWN")
            << "\",\"distance\":" << (available ? std::to_string(Distance3D(ownPosition, memberPosition)) : "null")
            << ",\"dead\":" << (available ? (attributes.hp == 0 ? "true" : "false") : "null");
        if (available) {
            const auto& auras = CachedHealingAuras(guid, object, now);
            if (auras.buffsValid) json << ",\"buffs\":" << HealingNamesJson(auras.buffs);
            if (auras.debuffsValid) json << ",\"debuffs\":" << HealingNamesJson(auras.debuffs);
        }
        json << '}';
    }
    json << "],\"roster_verified\":" << (rosterVerified ? "true" : "false");
    AppendHealingPets(json, petOwners, ownPosition, positionValid, now);
    AppendHealingBoss(json, petOwners);
    if (combatValid) json << ",\"in_combat\":" << ((unitFlags & 0x80000) ? "true" : "false");
    uint32_t spellsCount = 0;
    if (ReadHealingSpellCount(spellsCount)) json << ",\"spells_count\":" << spellsCount;
    AppendHealingCastResult(json);
    json << '}';
    for (auto it = g_healingAuraCache.begin(); it != g_healingAuraCache.end();) {
        if (it->second.generation != g_healingAuraGeneration) it = g_healingAuraCache.erase(it);
        else ++it;
    }
    return json.str();
}

int ExecuteNativeStopFollow() {
    const uintptr_t address = 0x00515090;
    if (!IsExecutableAddress(address)) return 1;
    __try {
        const unsigned char expected[] = {0x55, 0x8B, 0xEC, 0x53, 0x8B, 0x5D, 0x08, 0x56};
        if (memcmp(reinterpret_cast<const void*>(address), expected, sizeof(expected)) != 0) return 2;
        void* controller = *reinterpret_cast<void**>(0x00BE1148);
        if (!controller || !GetLocalPlayer()) return 4;
        // TurnLeftStart 0x513EE0: mask 0x100, pressed=1.
        // TurnLeftStop 0x513F10: same mask, pressed=0. Always release last.
        const uint32_t timestamp = *reinterpret_cast<const uint32_t*>(0x00CF0BC8);
        typedef void(__thiscall* MovementAction)(void*, uint32_t, int, uint32_t, int);
        MovementAction action = reinterpret_cast<MovementAction>(address);
        action(controller, 0x100, 1, timestamp, 0);
        action(controller, 0x100, 0, timestamp, 0);
        return 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 3; }
}

int ExecuteNativeJump() {
    const uintptr_t address = 0x0060DEA0;
    if (!IsExecutableAddress(address)) return 1;
    __try {
        const unsigned char expected[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x18, 0x56, 0x57};
        if (memcmp(reinterpret_cast<const void*>(address), expected, sizeof(expected)) != 0)
            return 2;
        void* player = reinterpret_cast<void*>(GetLocalPlayer());
        if (!player) return 4;
        // Jump action 0x513BD0 ultimately passes the controlled player and
        // the client's movement timestamp to this method. Avoid UI input gates.
        const uint32_t timestamp = *reinterpret_cast<const uint32_t*>(0x00CF0BC8);
        reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(address)(player, timestamp);
        return 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 3; }
}

std::string HealingMemoryCommand(const std::string& input) {
    if (input == "lootnear stop") { StopNearbyLoot("stopped"); return "ok"; }
    if (input == "lootnear status") return "{\"running\":" + std::string(g_lootAll.load() || g_lootPending ? "true" : "false")
        + ",\"attempted\":" + std::to_string(g_lootAttempted.size()) + ",\"reason\":" + HealingJsonString(g_lootStatus) + "}";
    if (input == "lootnear" || input == "lootnear all") return StartNearbyLoot(input == "lootnear all");
    if (input == "stopfollow") {
        const int result = ExecuteNativeStopFollow();
        if (result == 1) return "error: movement function unavailable";
        if (result == 2) return "error: unsupported movement function signature";
        if (result == 4) return "error: player or movement controller unavailable";
        return result == 0 ? "ok" : "error: stopfollow call failed";
    }
    if (input == "jump") {
        // Native player jump method, traced from Jump action-table entry.
        // Runs only through the existing game-main-thread dispatcher.
        if (!GetLocalPlayer()) return "error: player unavailable";
        const int result = ExecuteNativeJump();
        if (result == 1) return "error: jump function unavailable";
        if (result == 2) return "error: unsupported jump function signature";
        if (result == 4) return "error: player unavailable";
        return result == 0 ? "ok" : "error: jump call failed";
    }
    if (input.compare(0, 9, "healcast ") == 0) {
        if (!EnsureHealingObserver()) return "error: cast observer initialization failed";
        return ExecuteLuaOnMainThread(input.substr(9).c_str()) ? "ok" : "error: lua failed";
    }
    if (input == "healstate") return ReadHealingState();
    if (input == "spells") {
        HealingAttributes own;
        if (!ReadHealingAttributes(GetLocalPlayer(), own)) return "error: player unavailable";
        std::vector<std::string> spells;
        if (!ReadHealingSpellbook(spells)) return "error: spellbook unavailable";
        return "{\"spells\":" + HealingNamesJson(spells) + "}";
    }
    if (!g_healingOffsetsLoaded) {
        g_healingOffsetsLoaded = true;
        std::ifstream file(GetDllDirectory() + "heal_offsets.dat");
        std::string version;
        HealingOffsets loaded;
        if (file >> version >> loaded.hp >> loaded.maxhp >> loaded.mana >> loaded.maxmana >> loaded.classByte
            && version == "TravelerHealV1"
            && loaded.hp >= -1 && loaded.hp < 256 && loaded.maxhp >= -1 && loaded.maxhp < 256
            && loaded.mana >= -1 && loaded.mana < 256 && loaded.maxmana >= -1 && loaded.maxmana < 256
            && loaded.classByte >= -1 && loaded.classByte < 512) g_healingOffsets = loaded;
    }
    std::istringstream parser(input);
    std::string command;
    parser >> command;
    void* player = GetLocalPlayer();
    uint32_t values[256]{};
    if (!ReadHealingDescriptors(player, values)) return "error: player descriptors unavailable";
    if (command == "calibrate" || command == "calibrate_class") {
        HealingOffsets candidate = g_healingOffsets;
        if (command == "calibrate") {
            uint64_t requested[4]{};
            std::string extra;
            if (!(parser >> requested[0] >> requested[1] >> requested[2] >> requested[3])
                || (parser >> extra) || requested[0] > requested[1] || requested[2] > requested[3]
                || requested[1] == 0 || requested[3] == 0) return "error: invalid calibration values";
            int slots[4]{};
            for (int field = 0; field < 4; ++field) {
                int matches = 0;
                for (int i = 0; i < 256; ++i) if (values[i] == requested[field]) { slots[field] = i; ++matches; }
                if (matches != 1) return "error: ambiguous or missing descriptor value; change hp/mana and retry";
                for (int previous = 0; previous < field; ++previous)
                    if (slots[previous] == slots[field]) return "error: ambiguous identical field values";
            }
            candidate.hp = slots[0]; candidate.maxhp = slots[1];
            candidate.mana = slots[2]; candidate.maxmana = slots[3];
        }
        else {
            int id = 0;
            std::string extra;
            if (!(parser >> id) || (parser >> extra) || id < 1 || id > 11
                || std::string(HealingClass(static_cast<unsigned char>(id))) == "UNKNOWN")
                return "error: invalid class ID";
            unsigned char bytes[512]{};
            if (!ReadHealingMemory(reinterpret_cast<uintptr_t>(player), bytes, sizeof(bytes)))
                return "error: player memory unavailable";
            int matches = 0;
            for (int i = 0; i < 512; ++i) if (bytes[i] == id) { candidate.classByte = i; ++matches; }
            if (matches != 1) return "error: ambiguous class byte; no offset saved";
        }
        std::ofstream file(GetDllDirectory() + "heal_offsets.dat", std::ios::trunc);
        file << "TravelerHealV1 " << candidate.hp << ' ' << candidate.maxhp << ' '
            << candidate.mana << ' ' << candidate.maxmana << ' ' << candidate.classByte << '\n';
        file.close();
        if (!file) return "error: calibration save failed";
        g_healingOffsets = candidate;
        std::ostringstream result;
        result << "ok health_slot=" << candidate.hp << " maxhealth_slot=" << candidate.maxhp
            << " mana_slot=" << candidate.mana << " maxmana_slot=" << candidate.maxmana
            << " class_offset=" << candidate.classByte;
        return result.str();
    }
    const HealingOffsets& offsets = g_healingOffsets;
    if (offsets.hp < 0 || offsets.maxhp < 0 || offsets.mana < 0 || offsets.maxmana < 0)
        return "error: run calibrate first";
    unsigned char playerClass = 0;
    if (offsets.classByte >= 0) ReadHealingMemory(reinterpret_cast<uintptr_t>(player)
        + offsets.classByte, &playerClass, 1);
    std::ostringstream json;
    json << "{\"scope\":\"visible_players\",\"roster_verified\":false,\"player\":{\"mana\":"
        << values[offsets.mana] << ",\"max_mana\":" << values[offsets.maxmana]
        << ",\"class\":\"" << HealingClass(playerClass) << "\",\"plus_healing\":0},\"raid\":[";
    uintptr_t manager = 0, object = 0;
    if (!ReadHealingMemory(address_ObjectManager, &manager, sizeof(manager)) || !manager
        || !ReadHealingMemory(manager + offset_ObjectManagerFirstObject, &object, sizeof(object)))
        return "error: object manager unavailable";
    bool first = true;
    unsigned int index = 0;
    for (unsigned int visited = 0; object && !(object & 1) && visited < 4096; ++visited) {
        int type = 0;
        uint64_t guid = 0;
        uintptr_t next = 0;
        if (!ReadHealingMemory(object + offset_ObjectType, &type, sizeof(type))
            || !ReadHealingMemory(object + offset_ObjectNext, &next, sizeof(next))) break;
        if (type == 4 && reinterpret_cast<void*>(object) != player
            && ReadHealingMemory(object + offset_ObjectGuid, &guid, sizeof(guid))) {
            uint32_t unitValues[256]{};
            const std::string name = HealingObjectName(guid);
            if (!name.empty() && ReadHealingDescriptors(reinterpret_cast<void*>(object), unitValues)) {
                unsigned char cls = 0;
                if (offsets.classByte >= 0) ReadHealingMemory(object + offsets.classByte, &cls, 1);
                if (!first) json << ',';
                first = false;
                json << "{\"name\":" << HealingJsonString(name) << ",\"unit\":\"raid" << ++index
                    << "\",\"hp\":" << unitValues[offsets.hp] << ",\"max_hp\":" << unitValues[offsets.maxhp]
                    << ",\"class\":\"" << HealingClass(cls) << "\",\"dead\":"
                    << (unitValues[offsets.hp] == 0 ? "true" : "false") << '}';
            }
        }
        if (next == object) break;
        object = next;
        if (index >= 40) break;
    }
    json << "]}";
    return json.str();
}

std::string DispatchHealingMemory(const std::string& command) {
    if (!g_mainThreadHook || !g_wowWindow) return "error: main thread dispatcher unavailable";
    {
        std::lock_guard<std::mutex> lock(g_healingMutex);
        g_pendingHealingCommand = command;
        g_healingResponse = "error: main thread timeout";
    }
    DWORD_PTR result = 0;
    const bool sent = SendMessageTimeoutW(g_wowWindow, kHealingStateMessage, 0, 0,
        SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &result) != 0;
    std::lock_guard<std::mutex> lock(g_healingMutex);
    g_pendingHealingCommand.clear();
    return sent ? g_healingResponse : "error: main thread timeout";
}

// Quote untrusted pipe input as a Lua string; never interpolate executable text.
std::string QuoteHealingLua(const std::string& value) {
    std::string quoted = "\"";
    for (unsigned char c : value) {
        if (c == '\\' || c == '"') {
            quoted += '\\';
            quoted += static_cast<char>(c);
        }
        else if (c < 32 || c == 127) {
            quoted += "\\";
            quoted += static_cast<char>('0' + c / 100);
            quoted += static_cast<char>('0' + (c / 10) % 10);
            quoted += static_cast<char>('0' + c % 10);
        }
        else {
            quoted += static_cast<char>(c);
        }
    }
    return quoted + "\"";
}

bool IsHealingUnit(const std::string& unit) {
    if (unit == "player") return true;
    const size_t prefix = unit.compare(0, 4, "raid") == 0 ? 4
        : unit.compare(0, 5, "party") == 0 ? 5 : 0;
    if (prefix == 0 || unit.size() == prefix) return false;
    int index = 0;
    for (size_t i = prefix; i < unit.size(); ++i) {
        if (unit[i] < '0' || unit[i] > '9' || index > 40) return false;
        index = index * 10 + unit[i] - '0';
    }
    return index >= 1 && index <= (prefix == 4 ? 40 : 4)
        && unit.substr(prefix) == std::to_string(index);
}

std::string DispatchHealingLua(const std::string& script) {
    if (g_mainThreadHook == nullptr || g_wowWindow == nullptr) {
        return "error: main thread dispatcher unavailable";
    }
    {
        std::lock_guard<std::mutex> lock(g_healingMutex);
        g_pendingHealingLua = script;
        g_healingPending = true;
        g_healingSucceeded.store(false);
    }
    DWORD_PTR result = 0;
    const bool sent = SendMessageTimeoutW(g_wowWindow, kHealingMessage, 0, 0,
        SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &result) != 0;
    {
        std::lock_guard<std::mutex> lock(g_healingMutex);
        g_healingPending = false;
    }
    if (!sent) return "error: main thread timeout";
    return g_healingSucceeded.load() ? "ok" : "error: lua failed";
}

LRESULT CALLBACK ClickToMoveMainThreadHook(int code, WPARAM wParam, LPARAM lParam) {
    if (code >= 0) {
        const CWPSTRUCT* message = reinterpret_cast<const CWPSTRUCT*>(lParam);
        if (message && message->message == kLootTickMessage) TickNearbyLoot();
        if (message != nullptr && message->message == kClickToMoveMessage) {
            ClickToMoveType type = ClickToMoveType::Stop;
            Vector3 position{};
            bool pending = false;
            {
                std::lock_guard<std::mutex> lock(g_clickToMoveMutex);
                pending = g_clickToMovePending;
                if (pending) {
                    type = g_pendingClickToMoveType;
                    position = g_pendingClickToMovePosition;
                    g_clickToMovePending = false;
                }
            }
            if (pending) {
                g_lastClickToMoveSucceeded.store(ExecuteClickToMoveOnMainThread(type, position));
                g_clickToMoveExecuted.fetch_add(1);
            }
        }
        if (message != nullptr && message->message == kFishingMessage) {
            UpdateFishingOnMainThread();
        }
        if (message != nullptr && message->message == kHealingMessage) {
            std::string script;
            {
                std::lock_guard<std::mutex> lock(g_healingMutex);
                if (g_healingPending) {
                    script.swap(g_pendingHealingLua);
                    g_healingPending = false;
                }
            }
            if (!script.empty()) {
                g_healingSucceeded.store(ExecuteLuaOnMainThread(script.c_str()));
            }
        }
        if (message != nullptr && message->message == kHealingStateMessage) {
            std::lock_guard<std::mutex> lock(g_healingMutex);
            if (!g_pendingHealingCommand.empty()) {
                g_healingResponse = HealingMemoryCommand(g_pendingHealingCommand);
                g_pendingHealingCommand.clear();
            }
        }
    }
    return CallNextHookEx(g_mainThreadHook, code, wParam, lParam);
}

BOOL CALLBACK FindWowWindowCallback(HWND window, LPARAM parameter) {
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (processId == GetCurrentProcessId()
        && GetWindow(window, GW_OWNER) == nullptr
        && IsWindowVisible(window)) {
        *reinterpret_cast<HWND*>(parameter) = window;
        return FALSE;
    }
    return TRUE;
}

bool InitializeMainThreadDispatcher() {
    EnumWindows(FindWowWindowCallback, reinterpret_cast<LPARAM>(&g_wowWindow));
    if (g_wowWindow == nullptr) {
        OutputDebugStringA("TravelerWoW112Dll: could not find the WoW window.\n");
        return false;
    }

    const DWORD windowThreadId = GetWindowThreadProcessId(g_wowWindow, nullptr);
    g_mainThreadHook = SetWindowsHookExW(
        WH_CALLWNDPROC,
        ClickToMoveMainThreadHook,
        nullptr,
        windowThreadId);
    if (g_mainThreadHook == nullptr) {
        OutputDebugStringA("TravelerWoW112Dll: could not install the main-thread dispatcher.\n");
        return false;
    }
    return true;
}

bool IssueClickToMove(ClickToMoveType type, const Vector3& destination) {
    if (!IsExecutableAddress(funcAddress_ClickToMove)) {
        OutputDebugStringA("TravelerWoW112Dll: ClickToMove address is not executable.\n");
        return false;
    }
    if (g_mainThreadHook == nullptr || g_wowWindow == nullptr) {
        OutputDebugStringA("TravelerWoW112Dll: main-thread dispatcher is unavailable.\n");
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(g_clickToMoveMutex);
        g_pendingClickToMoveType = type;
        g_pendingClickToMovePosition = destination;
        g_clickToMovePending = true;
    }
    const unsigned long executedBefore = g_clickToMoveExecuted.load();
    g_clickToMoveQueued.fetch_add(1);
    DWORD_PTR messageResult = 0;
    const LRESULT sent = SendMessageTimeoutW(
        g_wowWindow,
        kClickToMoveMessage,
        0,
        0,
        SMTO_ABORTIFHUNG | SMTO_BLOCK,
        1000,
        &messageResult);
    return sent != 0
        && g_clickToMoveExecuted.load() > executedBefore
        && g_lastClickToMoveSucceeded.load();
}

bool IssueFishingTick() {
    if (g_mainThreadHook == nullptr || g_wowWindow == nullptr) {
        return false;
    }
    DWORD_PTR messageResult = 0;
    return SendMessageTimeoutW(
        g_wowWindow,
        kFishingMessage,
        0,
        0,
        SMTO_ABORTIFHUNG | SMTO_BLOCK,
        1000,
        &messageResult) != 0;
}

void StopPlayback() {
    if (!g_playing.exchange(false)) {
        return;
    }

    Vector3 current{};
    if (GetPlayerPosition(current)) {
        IssueClickToMove(ClickToMoveType::Stop, current);
    }
    OutputDebugStringA("TravelerWoW112Dll: route playback stopped.\n");
}

std::string StartPlayback() {
    if (!LoadRoute()) {
        OutputDebugStringA("TravelerWoW112Dll: route file is missing or empty.\n");
        return "error: route not found or empty";
    }

    Vector3 current{};
    Vector3 routeStart{};
    {
        std::lock_guard<std::mutex> lock(g_routeMutex);
        routeStart = g_route.front().position;
    }
    if (!GetPlayerPosition(current)) {
        return "error: player position is unavailable";
    }
    const float startDistance = Distance2D(current, routeStart);
    if (startDistance > kMaximumRouteStartDistance) {
        std::ostringstream result;
        result << "error: route start is " << startDistance
            << " away (maximum " << kMaximumRouteStartDistance << ")";
        return result.str();
    }

    g_playbackIndex = 0;
    g_lastIssuedPlaybackIndex = static_cast<size_t>(-1);
    g_lastClickToMoveTick = 0;
    g_playing.store(true);
    OutputDebugStringA("TravelerWoW112Dll: route playback started.\n");
    std::ostringstream result;
    result << "playing: " << GetActiveRouteName() << " startDistance=" << startDistance;
    return result.str();
}

void UpdatePlayback() {
    Vector3 current{};
    if (!GetPlayerPosition(current)) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_routeMutex);
    while (g_playbackIndex < g_route.size()
        && Distance2D(current, g_route[g_playbackIndex].position) <= kPlaybackArrivalDistance) {
        ++g_playbackIndex;
    }

    if (g_playbackIndex >= g_route.size()) {
        g_playing.store(false);
        IssueClickToMove(ClickToMoveType::Stop, current);
        OutputDebugStringA("TravelerWoW112Dll: route playback completed.\n");
        return;
    }

    const DWORD now = GetTickCount();
    if (g_lastIssuedPlaybackIndex != g_playbackIndex
        || now - g_lastClickToMoveTick >= 1000) {
        if (!IssueClickToMove(ClickToMoveType::Move, g_route[g_playbackIndex].position)) {
            g_playing.store(false);
            return;
        }
        g_lastIssuedPlaybackIndex = g_playbackIndex;
        g_lastClickToMoveTick = now;
    }
}

void StartRouteRecording() {
    g_fishing.store(false);
    if (g_playing.load()) {
        StopPlayback();
    }
    std::lock_guard<std::mutex> lock(g_routeMutex);
    g_route.clear();
    g_recordingStarted = GetTickCount();
    g_recording.store(true);
    OutputDebugStringA("TravelerWoW112Dll: route recording started.\n");
}

void StopRouteRecording() {
    g_recording.store(false);
    const bool saved = SaveRoute();
    OutputDebugStringA(saved
        ? "TravelerWoW112Dll: route recording stopped and saved.\n"
        : "TravelerWoW112Dll: failed to save route.\n");
}

void StartFishing() {
    if (g_recording.load()) {
        StopRouteRecording();
    }
    if (g_playing.load()) {
        StopPlayback();
    }
    g_fishingResetRequested.store(true);
    g_lastFishingActionSucceeded.store(false);
    g_fishing.store(true);
    LogFishingEvent("FISHING_STARTED");
    OutputDebugStringA("TravelerWoW112Dll: fishing started.\n");
}

void StopFishing() {
    if (g_fishing.exchange(false)) {
        LogFishingEvent("FISHING_STOPPED");
        OutputDebugStringA("TravelerWoW112Dll: fishing stopped.\n");
    }
}

void RecordRoutePoint() {
    Vector3 current{};
    if (!GetPlayerPosition(current)) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_routeMutex);
    if (!g_route.empty() && Distance3D(g_route.back().position, current) < kMinimumRoutePointDistance) {
        return;
    }

    g_route.push_back({ GetTickCount() - g_recordingStarted, current });
}

DWORD WINAPI RouteRecorderThread(LPVOID) {
    InitializeMainThreadDispatcher();
    while (g_routeThreadRunning.load()) {
        if (g_wowWindow) {
            DWORD_PTR ignored = 0;
            SendMessageTimeoutW(g_wowWindow, kLootTickMessage, 0, 0,
                SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &ignored);
        }
        if (g_recording.load()) {
            RecordRoutePoint();
        }
        if (g_playing.load()) {
            UpdatePlayback();
        }
        if (g_fishing.load()) {
            IssueFishingTick();
        }
        Sleep(200);
    }
    if (g_mainThreadHook != nullptr) {
        UnhookWindowsHookEx(g_mainThreadHook);
        g_mainThreadHook = nullptr;
    }
    return 0;
}

std::string HandleRouteCommand(const std::string& input) {
    if (input.size() > 1023 || input.find('\0') != std::string::npos)
        return "error: invalid command length or NUL";
    if (input == "lua") return "error: use lua <one-line script>";
    if (input.compare(0, 4, "lua ") == 0) {
        const std::string script = input.substr(4);
        if (script.find_first_not_of(" \t") == std::string::npos
            || script.find_first_of("\r\n") != std::string::npos)
            return "error: use lua <one-line script>";
        if (script.find("Traveler112BeginCast") != std::string::npos)
            return DispatchHealingMemory("healcast " + script);
        return DispatchHealingLua(script);
    }
    std::istringstream parser(input);
    std::string command;
    std::string routeName;
    parser >> command >> routeName;
    if (command == "lootnear") {
        if (!routeName.empty() && routeName != "all" && routeName != "stop" && routeName != "status")
            return "error: use lootnear [all|stop|status]";
        std::string extra;
        if (parser >> extra) return "error: use lootnear [all|stop]";
        return DispatchHealingMemory(routeName.empty() ? "lootnear" : "lootnear " + routeName);
    }

    if (command == "stopfollow") {
        if (!routeName.empty()) return "error: use stopfollow without arguments";
        return DispatchHealingMemory("stopfollow");
    }
    if (command == "jump") {
        if (!routeName.empty()) return "error: use jump without arguments";
        return DispatchHealingMemory("jump");
    }
    if (command == "healstate" || command == "spells" || command == "calibrate" || command == "calibrate_class") {
        return DispatchHealingMemory(input);
    }
    if (command == "stopcast") {
        return DispatchHealingLua("if Traveler112CancelCast then Traveler112CancelCast() end; SpellStopCasting()");
    }
    if (command == "cast") {
        const size_t first = input.find_first_not_of(" \t", input.find(command) + command.size());
        const size_t last = input.find_last_not_of(" \t\r\n");
        if (first == std::string::npos || last == std::string::npos) {
            return "error: use cast <spell> <unit>";
        }
        const size_t split = input.find_last_of(" \t", last);
        if (split == std::string::npos || split <= first) {
            return "error: use cast <spell> <unit>";
        }
        const std::string unit = input.substr(split + 1, last - split);
        const size_t spellEnd = input.find_last_not_of(" \t", split);
        const std::string spell = input.substr(first, spellEnd - first + 1);
        if (unit.empty() || unit.size() > 128 || spell.empty() || spell.size() > 256) {
            return "error: invalid spell or unit";
        }
        const std::string target = QuoteHealingLua(unit);
        return DispatchHealingMemory("healcast if Traveler112BeginCast then Traveler112BeginCast("
            + QuoteHealingLua(spell) + ") end; TargetByName(" + target
            + ",true); if UnitName('target') == " + target
            + " and (UnitInParty('target') or UnitInRaid('target') or UnitIsUnit('target','player'))"
            + " then CastSpellByName(" + QuoteHealingLua(spell)
            + ") elseif Traveler112RejectCast then Traveler112RejectCast('invalid_target') end");
    }

    if (command == "record") {
        if (!SetActiveRouteName(routeName)) {
            return "error: route name must use 1-64 letters, numbers, _ or -";
        }
        if (g_playing.load()) {
            StopPlayback();
        }
        StartRouteRecording();
        return "recording: " + routeName;
    }
    if (command == "play") {
        if (!SetActiveRouteName(routeName)) {
            return "error: route name must use 1-64 letters, numbers, _ or -";
        }
        if (g_recording.load()) {
            StopRouteRecording();
        }
        if (g_playing.load()) {
            StopPlayback();
        }
        const std::string result = StartPlayback();
        if (g_playing.load()) {
            StopFishing();
        }
        return result;
    }
    if (command == "fish") {
        if (routeName == "start") {
            StartFishing();
            return "fishing started";
        }
        if (routeName == "stop") {
            StopFishing();
            return "fishing stopped";
        }
        return "error: use fish start or fish stop";
    }
    if (command == "speed") {
        if (routeName == "get" || routeName.empty()) {
            float speed = 0.0f;
            if (!ReadGroundSpeed(speed)) {
                return "error: player or ground speed is unavailable";
            }
            std::ostringstream result;
            result << "ground speed=" << speed;
            return result.str();
        }

        float speed = 0.0f;
        if (routeName == "reset") {
            speed = kDefaultGroundSpeed;
        }
        else {
            std::istringstream valueParser(routeName);
            char trailing = 0;
            if (!(valueParser >> speed) || (valueParser >> trailing)) {
                return "error: speed must be a number from 1 to 100";
            }
        }
        if (!WriteGroundSpeed(speed)) {
            return "error: speed must be 1-100 and the player must be logged in";
        }
        std::ostringstream result;
        result << "ground speed set to " << speed;
        return result.str();
    }
    if (command == "save") {
        return SaveRoute() ? "saved: " + GetActiveRouteName() : "error: save failed";
    }
    if (command == "stop" || command == "halt") {
        if (g_recording.load()) {
            StopRouteRecording();
        }
        if (g_playing.load()) {
            StopPlayback();
        }
        StopFishing();
        return "stopped";
    }
    if (command == "list") {
        return "routes: " + ListRoutes();
    }
    if (command == "status") {
        std::ostringstream status;
        const char* task = g_recording.load() ? "recording"
            : g_playing.load() ? "route"
            : g_fishing.load() ? "fishing"
            : "idle";
        status << "task=" << task
            << " route=" << GetActiveRouteName()
            << " recording=" << (g_recording.load() ? "yes" : "no")
            << " playing=" << (g_playing.load() ? "yes" : "no")
            << " fishing=" << (g_fishing.load() ? "yes" : "no")
            << " casts=" << g_fishingCasts.load()
            << " catches=" << g_fishingCatches.load()
            << " scanned=" << g_fishingObjectsScanned.load()
            << " candidates=" << g_fishingBobberCandidates.load()
            << " busyRaw=" << g_lastFishingBusyRaw.load()
            << " fishResult=" << (g_lastFishingActionSucceeded.load() ? "ok" : "waiting")
            << " dispatcher=" << (g_mainThreadHook != nullptr ? "ready" : "missing")
            << " ctmQueued=" << g_clickToMoveQueued.load()
            << " ctmExecuted=" << g_clickToMoveExecuted.load()
            << " ctmResult=" << (g_lastClickToMoveSucceeded.load() ? "ok" : "failed")
            << " lastFishEvent=" << GetLastFishingEvent();
        return status.str();
    }
    return "commands: jump, stopfollow, lua <one-line script>, healstate, spells, calibrate <hp> <maxhp> <mana> <maxmana>, calibrate_class <id>, cast <spell> <name>, stopcast, record <name>, stop, save, play <name>, fish start, fish stop, speed <1-100>, speed reset, speed get, halt, list, status";
}

DWORD WINAPI RouteControlThread(LPVOID) {
    std::ostringstream pipeNameBuilder;
    pipeNameBuilder << R"(\\.\pipe\TravelerRouteControl_PID_)" << GetCurrentProcessId();
    const std::string pipeName = pipeNameBuilder.str();

    while (g_routeControlRunning.load()) {
        HANDLE pipe = CreateNamedPipeA(
            pipeName.c_str(),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            1,
            4096,
            4096,
            0,
            nullptr);
        if (pipe == INVALID_HANDLE_VALUE) {
            Sleep(500);
            continue;
        }

        const BOOL connected = ConnectNamedPipe(pipe, nullptr)
            ? TRUE
            : GetLastError() == ERROR_PIPE_CONNECTED;
        if (connected) {
            char buffer[1024]{};
            DWORD bytesRead = 0;
            if (ReadFile(pipe, buffer, sizeof(buffer) - 1, &bytesRead, nullptr) && bytesRead > 0) {
                const std::string response = HandleRouteCommand(std::string(buffer, bytesRead));
                DWORD bytesWritten = 0;
                WriteFile(pipe, response.data(), static_cast<DWORD>(response.size()), &bytesWritten, nullptr);
                FlushFileBuffers(pipe);
            }
        }
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
    return 0;
}

int __cdecl EnumerateVisibleObjectsCallback(unsigned long long guid, int filter)
{
    //TODO  rightClickObjectByGuid

    return 1;
}


static void* callbackPtr = EnumerateVisibleObjectsCallback;


// 右键点击对象的函数实现
void __stdcall RightClickObject(void* unitPtr, int autoLoot)
{
    // 在这里写对游戏内存或函数的调用逻辑
    // 例如通过指针模拟调用游戏内部的函数
    // 注意：你需要自己通过逆向找到地址并确认调用约定

    // 示例打印（调试用途）
    //MessageBoxA(0, "RightClickObject Called", "Info", 0);

    // WoW 1.12.1 游戏对象右键交互函数。
  
    RightClickFunc func = (RightClickFunc)funcAddress_RightClickFunc;

    // 调用游戏内部函数（注意：需要你的 DLL 注入到游戏进程中执行）
    func(unitPtr, autoLoot);
}



//给外部使用
void rightClickObjectByGuid(uint64_t guid) { 
    // 获取对象指针
    void* unitPtr = GetObjectPtr(guid);
    if (unitPtr == nullptr) {
        std::cerr << "无法获取对象指针，可能对象不存在或GUID无效。" << std::endl;
        return;
    }
    // 调用右键点击函数
    RightClickObject(unitPtr, 1);
}


std::string GetUserNameByGuid(uint64_t guid) {
    std::cout << u8"读取玩家名字: " << guid << std::endl;

    uintptr_t namePtr = *reinterpret_cast<uintptr_t*>(0xC0E230); // 初始链表头地址

    while (namePtr != 0) {
        uint64_t currentGuid = *reinterpret_cast<uint64_t*>(namePtr + 0xC); // GUID 偏移

        if (currentGuid == guid) {
            const char* name = reinterpret_cast<const char*>(namePtr + 0x14); // 字符串偏移

            // 确保 name 不为空并且以 null 结尾
            if (name && strlen(name) > 0) {
                return std::string(name);
            }
            else {
                return "";
            }
        }

        namePtr = *reinterpret_cast<uintptr_t*>(namePtr); // 下一节点
    }

    return ""; // 没找到
}

// 自动拾取函数
std::string AutoLoot() {
    if(IsLoggedIn() == false) {
        return "未登录";
	}
	EnumerateVisibleObjects(callbackPtr, 0); // 0x1 是过滤器，表示只处理可见对象

    return "";
}


// 获取玩家GUID的实际函数
// 这个函数将调用游戏内部的函数来获取GUID
std::string GetPlayerName() {
    unsigned long long guid = GetPlayerGuid();

    if (guid == 0) {
        return "";
    }

    return GetUserNameByGuid(guid);
 
}

// 命名管道服务器类，使用模板使其可以处理任意数据类型T
template<typename T>
class NamedPipeServer {
public:
    // 构造函数：初始化管道名称和数据生成回调函数
    NamedPipeServer(const std::string& pipeName, std::function<T()> dataGenerator)
        : m_pipeName(pipeName), m_dataGenerator(dataGenerator), m_running(false) {
    }

    // 启动管道服务器线程
    void Start() {
        if (!m_running.load()) {
            m_running.store(true);
            m_serverThread = std::thread(&NamedPipeServer::Run, this);
            m_serverThread.detach();
            std::cout << "管道服务器 [" << m_pipeName << "] 已启动。\n";
        }
    }

    // 停止管道服务器线程
    void Stop() {
        if (m_running.load()) {
            m_running.store(false);
            std::cout << "管道服务器 [" << m_pipeName << "] 正在停止...\n";
        }
    }

private:
    // 管道服务器的主运行逻辑
    void Run() {
        while (m_running.load()) {
            UniqueHandle hPipe(CreateNamedPipeA(
                m_pipeName.c_str(),
                PIPE_ACCESS_OUTBOUND,
                PIPE_TYPE_BYTE | PIPE_WAIT,
                1, 0, 0, 0, nullptr));

            if (!hPipe) {
                std::cerr << "创建命名管道 [" << m_pipeName << "] 失败，错误码: " << GetLastError() << std::endl;
                Sleep(1000);
                continue;
            }

            BOOL connected = ConnectNamedPipe(hPipe.get(), nullptr) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
            if (connected) {
                // 如果连接成功，生成数据并写入管道
                T data = m_dataGenerator();
                DWORD bytesWritten = 0;

                // 使用一个助手函数来处理不同类型的写入操作
                if (!WriteToPipe(hPipe.get(), data)) {
                    std::cerr << "写入管道 [" << m_pipeName << "] 数据失败，错误码: " << GetLastError() << std::endl;
                }
                else {
                    //std::cout << "数据写入管道 [" << m_pipeName << "] 成功。" << std::endl;
                }
                FlushFileBuffers(hPipe.get()); // 刷新缓冲区，确保数据立即发送
            }

            DisconnectNamedPipe(hPipe.get());
            Sleep(100);
        }
        std::cout << "管道服务器 [" << m_pipeName << "] 已停止。\n";
    }

    // 针对不同数据类型T的WriteToPipe实现
    // 特化版本：用于字符串
    bool WriteToPipe(HANDLE hPipe, const std::string& data) {
        DWORD bytesWritten = 0;
        // 写入字符串，包括空终止符
        return WriteFile(hPipe, data.c_str(), data.size() + 1, &bytesWritten, nullptr) && bytesWritten == data.size() + 1;
    }

    // 通用版本：用于普通类型，如uint64_t
    template<typename U = T>
    bool WriteToPipe(HANDLE hPipe, const U& data, std::enable_if_t<!std::is_same<U, std::string>::value>* = 0) {
        DWORD bytesWritten = 0;
        return WriteFile(hPipe, &data, sizeof(data), &bytesWritten, nullptr) && bytesWritten == sizeof(data);
    }

    std::string m_pipeName; // 管道名称
    std::function<T()> m_dataGenerator; // 数据生成回调函数
    std::atomic<bool> m_running; // 控制线程运行状态的原子标志
    std::thread m_serverThread; // 管道服务器线程对象
};

const std::string pipeName_KylePlayerGuidPipe = "KylePlayerGuidPipe"; // 命名管道名称
const std::string pipeName_KylePlayerNamePipe = "KylePlayerNamePipe"; // 命名管道名称
const std::string pipeName_KyleKyleAutoLootPipe = "KyleAutoLoot"; // 命名管道名称


// 封装构建管道名称的函数
std::string BuildPipeName(const std::string& baseName, DWORD pid) {
    std::ostringstream oss;
    oss << R"(\\.\pipe\)" << baseName << "_PID_" << pid;
    return oss.str();
}

// 全局的管道服务器实例（或多个实例的容器）
// 使用unique_ptr确保自动内存管理
std::unique_ptr<NamedPipeServer<uint64_t>> g_playerGuidPipeServer;
std::unique_ptr<NamedPipeServer<std::string>> g_playerNamePipeServer;
std::unique_ptr<NamedPipeServer<std::string>> g_AutoLootPipeServer;


// DLL 入口点函数
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    switch (fdwReason) {
    case DLL_PROCESS_ATTACH: { // 添加花括号
        // 当DLL被注入到进程时
        DisableThreadLibraryCalls(hinstDLL); // 禁用不必要的线程库调用，提高性能

        g_module = hinstDLL;
        g_routeThreadRunning.store(true);
        g_routeThread = CreateThread(nullptr, 0, RouteRecorderThread, nullptr, 0, nullptr);
        if (g_routeThread == nullptr) {
            g_routeThreadRunning.store(false);
            return FALSE;
        }

        g_routeControlRunning.store(true);
        g_routeControlThread = CreateThread(nullptr, 0, RouteControlThread, nullptr, 0, nullptr);
        if (g_routeControlThread == nullptr) {
            g_routeControlRunning.store(false);
            g_routeThreadRunning.store(false);
            return FALSE;
        }

        // 获取当前进程的PID
        DWORD currentPid = GetCurrentProcessId();
        // 构建动态管道名称，包含进程PID
    

        // 初始化玩家GUID管道服务器，使用动态管道名称
        g_playerGuidPipeServer = std::make_unique<NamedPipeServer<uint64_t>>(
            BuildPipeName(pipeName_KylePlayerGuidPipe, currentPid), // 动态管道名称
            []() { return GetPlayerGuid(); } // 使用lambda表达式包装GetPlayerGuid函数作为回调
        );
        g_playerGuidPipeServer->Start(); // 启动管道服务器

         
        // 初始化玩家name管道服务器，使用动态管道名称
        g_playerNamePipeServer = std::make_unique<NamedPipeServer<std::string>>(
            BuildPipeName(pipeName_KylePlayerNamePipe, currentPid), // 动态管道名称
            []() { return GetPlayerName(); } 
        );
        g_playerNamePipeServer->Start(); // 启动管道服务器

        // 初始化玩家GUID管道服务器，使用动态管道名称
        g_AutoLootPipeServer = std::make_unique<NamedPipeServer<std::string>>(
            BuildPipeName(pipeName_KyleKyleAutoLootPipe, currentPid), // 动态管道名称
            []() { return AutoLoot(); }
        );
        g_AutoLootPipeServer->Start(); // 启动管道服务器





        break;
    } // 添加花括号

    case DLL_PROCESS_DETACH: { // 添加花括号
        g_recording.store(false);
        g_playing.store(false);
        g_fishing.store(false);
        g_routeThreadRunning.store(false);
        g_routeControlRunning.store(false);
        if (g_routeControlThread != nullptr) {
            CancelSynchronousIo(g_routeControlThread);
            if (lpvReserved == nullptr) {
                WaitForSingleObject(g_routeControlThread, 1000);
            }
            CloseHandle(g_routeControlThread);
            g_routeControlThread = nullptr;
        }
        if (g_routeThread != nullptr) {
            if (lpvReserved == nullptr) {
                WaitForSingleObject(g_routeThread, 1000);
            }
            CloseHandle(g_routeThread);
            g_routeThread = nullptr;
        }

        // 当DLL从进程中卸载时
        if (g_playerGuidPipeServer) {
            g_playerGuidPipeServer->Stop(); // 停止管道服务器
            // unique_ptr在超出作用域时会自动释放内存
        }

        // 当DLL从进程中卸载时
        if (g_playerNamePipeServer) {
            g_playerNamePipeServer->Stop(); // 停止管道服务器
            // unique_ptr在超出作用域时会自动释放内存
        }
        if (g_AutoLootPipeServer) {
            g_AutoLootPipeServer->Stop();
        }
        break;
    } // 添加花括号
    }
    return TRUE;
}
