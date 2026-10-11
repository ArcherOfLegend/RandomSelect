// umvc3_randomselect.cpp
// UMvC3 Random Select
#include "utils/MemoryMgr.h"
#include "utils/Trampoline.h"
#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <string>
#include <vector>
#include <set>
#include <fstream>
#include <filesystem>
#include <algorithm>

using namespace Memory::VP;

// Hook sites
static constexpr uintptr_t PICK_SITE_1 = 0x140365358;   // Random: CALL FUN_140360da0 (random char)
static constexpr uintptr_t PICK_SITE_2 = 0x140365F87;   // Random All
static constexpr uintptr_t COLOR_SITE_1 = 0x140365386;  // Random: CALL GoesToRandomColorIDGenerator
static constexpr uintptr_t COLOR_SITE_2 = 0x140365FA9;  // Random All
static constexpr uintptr_t VOICE_SITE_1 = 0x140365405;  // Random: CALL sGameConfig__getCharacterVoiceLanguage
static constexpr uintptr_t VOICE_SITE_2 = 0x14036603B;  // Random All

// Addresses
uint64_t BattleSettingInstance = 0x140004700;
uint64_t GameConfigInstance = 0x140001AB0;
uint64_t GetSecret = 0x140004A30;
uint64_t GetTeamMember = 0x14024CDB0;    
uint64_t IsOwned = 0x140226670;          
uint64_t RngState = 0x1400046D0;
uint64_t RngNext = 0x14000AF80;

uint64_t NameTable = 0x140C553B0;        
uint64_t UnlockTable = 0x140A6FD30;      

// Offsets
static constexpr uintptr_t MenuState_offset = 0x2B4;     

static constexpr int VANILLA_COUNT = 60;
static constexpr int VANILLA_MAX = 50;   
static constexpr int CLONE_ID_BASE = 59; 

typedef void*(__fastcall* GetInstance_t)();
typedef uint32_t(__fastcall* RandomPick_t)(void*, uint32_t);
typedef uint32_t(__fastcall* RandomColor_t)(void*, uint32_t, uint32_t);
typedef uint32_t(__fastcall* VoiceLang_t)(void*, uint32_t);

RandomPick_t oPick1 = nullptr, oPick2 = nullptr;
RandomColor_t oColor1 = nullptr, oColor2 = nullptr;
VoiceLang_t oVoice1 = nullptr, oVoice2 = nullptr;

struct CloneEntry
{
    int section = 0;
    std::string name;
    std::string baseName;
    bool hasSound = false;
    int numColors = 0;
};

static std::vector<CloneEntry> Clones;
static std::set<std::string> ChildNames;

// Config
static std::wstring CloneIniPath;
static bool RandomVanilla = true;
static bool RandomClones = true;
static ULONGLONG lastLoadMs = 0;

void Log(const char* fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    OutputDebugStringA(buf);
}

std::string Trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string Lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)tolower(c); });
    return s;
}

std::wstring GameDir()
{
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    wcsrchr(path, L'\\')[1] = L'\0';
    return path;
}

int VanillaId(const std::string& name)
{
    const char** table = (const char**)NameTable;
    std::string want = Lower(name);
    for (int id = 1; id < VANILLA_COUNT; id++)
    {
        if (table[id] && Lower(table[id]) == want)
            return id;
    }
    return -1;
}

const char* VanillaName(int id)
{
    if (id < 0 || id >= VANILLA_COUNT) return "?";
    const char** table = (const char**)NameTable;
    return table[id] ? table[id] : "?";
}

void LoadCloneIni()
{
    Clones.clear();
    ChildNames.clear();

    std::ifstream file{ std::filesystem::path(CloneIniPath) };
    if (!file.is_open())
    {
        Log("[RandomSelect] Characters.ini not found\n");
        return;
    }

    CloneEntry* cur = nullptr;
    std::string line;
    while (std::getline(file, line))
    {
        line = Trim(line);
        if (line.empty() || line[0] == ';') continue;

        if (line[0] == '[')
        {
            size_t close = line.find(']');
            std::string header = Trim(line.substr(1, close == std::string::npos ? std::string::npos : close - 1));
            cur = nullptr;
            if (Lower(header).rfind("character", 0) == 0)
            {
                int section = atoi(header.c_str() + 9);
                if (section > 0)
                {
                    Clones.push_back({});
                    cur = &Clones.back();
                    cur->section = section;
                }
            }
            continue;
        }

        if (!cur) continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = Lower(Trim(line.substr(0, eq)));
        std::string value = Trim(line.substr(eq + 1));

        if (key == "characterid") cur->name = value;
        else if (key == "basecharacter") cur->baseName = value;
        else if (key == "soundid") cur->hasSound = !value.empty();
        else if (key == "numcolors") cur->numColors = atoi(value.c_str());
        else if (key.rfind("child", 0) == 0 && !value.empty()) ChildNames.insert(Lower(value));
    }

    Log("[RandomSelect] %d clones, %d children\n", (int)Clones.size(), (int)ChildNames.size());
}

void LoadConfig()
{
    std::wstring iniPath = GameDir() + L"umvc3_randomselect.ini";

    wchar_t clonePath[MAX_PATH];
    GetPrivateProfileStringW(L"CloneEngine", L"CharactersIni", L"Characters.ini", clonePath, MAX_PATH, iniPath.c_str());
    CloneIniPath = (wcschr(clonePath, L':') ? std::wstring() : GameDir()) + clonePath;
    LoadCloneIni();

    RandomVanilla = GetPrivateProfileIntW(L"Random", L"IncludeVanilla", 1, iniPath.c_str()) != 0;
    RandomClones = GetPrivateProfileIntW(L"Random", L"IncludeClones", 1, iniPath.c_str()) != 0;
}

bool IsSelectableClone(const CloneEntry& c)
{
    return !c.name.empty() && c.hasSound && ChildNames.count(Lower(c.name)) == 0;
}

const CloneEntry* FindClone(int id)
{
    if (id <= CLONE_ID_BASE) return nullptr;
    for (const CloneEntry& c : Clones)
    {
        if (CLONE_ID_BASE + c.section == id)
            return &c;
    }
    return nullptr;
}

int BaseId(int id)
{
    const CloneEntry* c = FindClone(id);
    if (!c) return id;
    int baseId = VanillaId(c->baseName);
    return baseId > 0 ? baseId : id;
}

uint32_t GameRandom()
{
    uint32_t* state = ((uint32_t*(__fastcall*)())RngState)();
    return ((uint32_t(__fastcall*)(uint32_t*))RngNext)(state);
}

bool IsAvailable(int id)
{
    if (id < 1 || id >= VANILLA_COUNT) return false;
    if (((uint8_t*)UnlockTable)[id] == 0) return false;
    void* cfg = ((GetInstance_t)GameConfigInstance)();
    if (!cfg) return false;
    void* secret = ((void*(__fastcall*)(void*))GetSecret)(cfg);
    if (!secret) return false;
    return ((bool(__fastcall*)(void*, uint32_t))IsOwned)(secret, id);
}

uint32_t RandomPick(void* menu, uint32_t player, RandomPick_t original)
{
    ULONGLONG now = GetTickCount64();
    if (now - lastLoadMs > 2000)
        LoadConfig();
    lastLoadMs = now;

    std::set<int> taken;
    void* bs = ((GetInstance_t)BattleSettingInstance)();
    if (player < 2 && bs && menu)
    {
        typedef int(__fastcall* GetTeamMember_t)(void*, uint32_t, int);
        int state = *(int*)((uint8_t*)menu + MenuState_offset + player * 4);
        if (state > 2) taken.insert(((GetTeamMember_t)GetTeamMember)(bs, player, 0));
        if (state > 4) taken.insert(((GetTeamMember_t)GetTeamMember)(bs, player, 1));
        if (state > 6) taken.insert(((GetTeamMember_t)GetTeamMember)(bs, player, 2));
    }

    std::vector<int> pool;
    if (RandomVanilla)
    {
        for (int id = 1; id <= VANILLA_MAX; id++)
        {
            if (IsAvailable(id) && !taken.count(id))
                pool.push_back(id);
        }
    }
    if (RandomClones)
    {
        for (const CloneEntry& c : Clones)
        {
            int id = CLONE_ID_BASE + c.section;
            if (IsSelectableClone(c) && IsAvailable(VanillaId(c.baseName)) && !taken.count(id))
                pool.push_back(id);
        }
    }

    if (pool.empty())
        return original(menu, player);

    int id = pool[GameRandom() % pool.size()];
    const CloneEntry* c = FindClone(id);
    Log("[RandomSelect] P%u -> %s (%d), pool %d\n", player + 1, c ? c->name.c_str() : VanillaName(id), id, (int)pool.size());
    return id;
}

uint32_t RandomColor(void* menu, uint32_t player, uint32_t id, RandomColor_t original)
{
    const CloneEntry* c = FindClone(id);
    if (!c) return original(menu, player, id);
    int colors = c->numColors > 0 ? c->numColors : 6;
    return GameRandom() % colors;
}

uint32_t __fastcall HookedPick1(void* menu, uint32_t player) { return RandomPick(menu, player, oPick1); }
uint32_t __fastcall HookedPick2(void* menu, uint32_t player) { return RandomPick(menu, player, oPick2); }
uint32_t __fastcall HookedColor1(void* menu, uint32_t player, uint32_t id) { return RandomColor(menu, player, id, oColor1); }
uint32_t __fastcall HookedColor2(void* menu, uint32_t player, uint32_t id) { return RandomColor(menu, player, id, oColor2); }
uint32_t __fastcall HookedVoice1(void* cfg, uint32_t id) { return oVoice1(cfg, BaseId(id)); }  // over 25 is forced english
uint32_t __fastcall HookedVoice2(void* cfg, uint32_t id) { return oVoice2(cfg, BaseId(id)); }

void OnInitializeHook()
{
    LoadConfig();
    lastLoadMs = GetTickCount64();

    ReadCall(PICK_SITE_1, oPick1);
    ReadCall(PICK_SITE_2, oPick2);
    ReadCall(COLOR_SITE_1, oColor1);
    ReadCall(COLOR_SITE_2, oColor2);
    ReadCall(VOICE_SITE_1, oVoice1);
    ReadCall(VOICE_SITE_2, oVoice2);
    Trampoline* tramp = Trampoline::MakeTrampoline(GetModuleHandle(nullptr), 8 * 14, 1);
    InjectHook(PICK_SITE_1, tramp->Jump(HookedPick1), HookType::Call);
    InjectHook(PICK_SITE_2, tramp->Jump(HookedPick2), HookType::Call);
    InjectHook(COLOR_SITE_1, tramp->Jump(HookedColor1), HookType::Call);
    InjectHook(COLOR_SITE_2, tramp->Jump(HookedColor2), HookType::Call);
    InjectHook(VOICE_SITE_1, tramp->Jump(HookedVoice1), HookType::Call);
    InjectHook(VOICE_SITE_2, tramp->Jump(HookedVoice2), HookType::Call);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)OnInitializeHook, hModule, 0, nullptr);
        break;
    }
    return TRUE;
}
