#ifndef GTA_PED_POOL_H
#define GTA_PED_POOL_H

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/mman.h>
#include "Includes/Logger.h"
#include "Includes/obfuscate.h"
#include "Includes/Utils.h"

// ============================================================================
// GTA SAN ANDREAS ENGINE DATA TYPES & CONSTANTS
// ============================================================================

enum ePedType {
    PEDTYPE_PLAYER1        = 0,  // Local Player (CJ)
    PEDTYPE_PLAYER2        = 1,  // 2nd Player
    PEDTYPE_PLAYER_NETWORK = 2,  // Remote Network / Multiplayer Player
    PEDTYPE_PLAYER_UNUSED  = 3,
    PEDTYPE_CIVMALE        = 4,  // Civilian Male (Bot / NPC)
    PEDTYPE_CIVFEMALE      = 5,  // Civilian Female (Bot / NPC)
    PEDTYPE_COP            = 6,  // Law Enforcement Officer
    PEDTYPE_GANG1          = 7,  // Ballas (Enemy)
    PEDTYPE_GANG2          = 8,  // Grove Street Families (Ally / Team)
    PEDTYPE_GANG3          = 9,  // Los Santos Vagos (Enemy)
    PEDTYPE_GANG4          = 10, // San Fierro Rifa (Enemy)
    PEDTYPE_GANG5          = 11, // Da Nang Boys (Enemy)
    PEDTYPE_GANG6          = 12, // Italian Mafia (Enemy)
    PEDTYPE_GANG7          = 13, // Mountain Cloud Triads (Ally)
    PEDTYPE_GANG8          = 14, // Varrio Los Aztecas (Ally)
    PEDTYPE_DEALER         = 19, // Drug Dealer
    PEDTYPE_MEDIC          = 20, // Paramedic
    PEDTYPE_FIREMAN        = 21, // Firefighter
    PEDTYPE_CRIMINAL       = 22, // Criminal
    PEDTYPE_PROSTITUTE     = 25,
    PEDTYPE_SPECIAL        = 26
};

#pragma pack(push, 1)
struct tPoolObjectFlags {
    uint8_t nId : 7;
    uint8_t bEmpty : 1;
};

// Generic GTA SA CPool representation
template <class T>
class CGTAPool {
public:
    T* m_pObjects;                // Array of ped objects
    tPoolObjectFlags* m_pFlags;   // Array of flags (bEmpty bit)
    int32_t m_nSize;              // Pool size (typically 140 for peds)
    int32_t m_nFreeCount;         // Free slots count
    bool m_bOwnsAllocations;

    inline bool IsFree(int32_t slot) const {
        if (!m_pFlags) return true;
        return m_pFlags[slot].bEmpty != 0;
    }

    inline T* GetAt(int32_t slot) const {
        if (slot < 0 || slot >= m_nSize) return nullptr;
        if (IsFree(slot)) return nullptr;
        if (!m_pObjects) return nullptr;
        return &m_pObjects[slot];
    }
};
#pragma pack(pop)

// Structure holding counted HUD statistics
struct HudStats {
    int teamCount;       // Count of player CJ & team members / allies
    int enemyCount;      // Count of enemy players / hostile gangs
    int botCount;        // Count of AI bots / civilian NPCs
    int totalPeds;       // Total active / alive entities
    int isLibLoaded;     // 1 if target game library is loaded, 0 otherwise
    int localPlayerHp;   // Health of local player (0-100)
};

// ============================================================================
// SAFE MEMORY ACCESS HELPER
// ============================================================================
static inline bool IsAddressReadable(const void* addr, size_t length = sizeof(uintptr_t)) {
    if (!addr) return false;
    uintptr_t p = reinterpret_cast<uintptr_t>(addr);
    // Basic canonical user-space address check for Android
    if (p < 0x10000 || p > 0x7FFFFFFFFFFFULL) return false;
    return true;
}

template <typename T>
static inline bool SafeRead(uintptr_t addr, T* outVal) {
    if (!IsAddressReadable(reinterpret_cast<const void*>(addr), sizeof(T))) return false;
    *outVal = *reinterpret_cast<const T*>(addr);
    return true;
}

// ============================================================================
// GTA SA SCANNER CLASS
// ============================================================================
class GTAPedPoolScanner {
public:
    static GTAPedPoolScanner& Instance() {
        static GTAPedPoolScanner instance;
        return instance;
    }

    // Target library handles and base addresses
    void* m_hGTASA;
    void* m_hSAMP;
    void* m_hBulletEcho;
    uintptr_t m_gtasaBase;
    uintptr_t m_sampBase;

    // Resolved CPool<CPed> pointer
    void* m_pPedPoolPtr;
    typedef void* (*GetPedPool_t)();
    GetPedPool_t m_pfnGetPedPool;

    // Configuration
    bool m_bConfigTeamFilter;
    bool m_bConfigBotFilter;
    bool m_bHudEnabled;
    int  m_nHudOpacity;
    int  m_nHudTextSize;

    GTAPedPoolScanner() {
        m_hGTASA = nullptr;
        m_hSAMP = nullptr;
        m_hBulletEcho = nullptr;
        m_gtasaBase = 0;
        m_sampBase = 0;
        m_pPedPoolPtr = nullptr;
        m_pfnGetPedPool = nullptr;

        m_bConfigTeamFilter = true;
        m_bConfigBotFilter = true;
        m_bHudEnabled = true;
        m_nHudOpacity = 85;
        m_nHudTextSize = 13;
    }

    bool CheckLibraries() {
        if (!m_hGTASA) {
            m_hGTASA = dlopen(OBFUSCATE("libGTASA.so"), RTLD_NOW | RTLD_GLOBAL);
            if (m_hGTASA) {
                m_gtasaBase = findLibrary(OBFUSCATE("libGTASA.so"));
                LOGI(OBFUSCATE("GTAPedPool: libGTASA.so hooked at 0x%lx"), (unsigned long)m_gtasaBase);
                ResolvePedPoolSymbols();
            }
        }

        if (!m_hSAMP) {
            m_hSAMP = dlopen(OBFUSCATE("libsamp.so"), RTLD_NOW | RTLD_GLOBAL);
            if (m_hSAMP) {
                m_sampBase = findLibrary(OBFUSCATE("libsamp.so"));
                LOGI(OBFUSCATE("GTAPedPool: libsamp.so hooked at 0x%lx"), (unsigned long)m_sampBase);
            }
        }

        if (!m_hBulletEcho && !m_hGTASA) {
            m_hBulletEcho = dlopen(OBFUSCATE("libbulletecho.so"), RTLD_NOW | RTLD_GLOBAL);
        }

        return (m_hGTASA != nullptr || m_hSAMP != nullptr || m_hBulletEcho != nullptr);
    }

    void ResolvePedPoolSymbols() {
        if (!m_hGTASA) return;

        // Try direct function symbol: CPools::GetPedPool()
        // Mangled name in GCC/Clang: _ZN6CPools10GetPedPoolEv
        m_pfnGetPedPool = reinterpret_cast<GetPedPool_t>(dlsym(m_hGTASA, OBFUSCATE("_ZN6CPools10GetPedPoolEv")));
        if (m_pfnGetPedPool) {
            LOGI(OBFUSCATE("GTAPedPool: Found CPools::GetPedPool at %p"), m_pfnGetPedPool);
            return;
        }

        // Try variable symbol: CPools::ms_pPedPool
        // Mangled name: _ZN6CPools11ms_pPedPoolE
        void* pSym = dlsym(m_hGTASA, OBFUSCATE("_ZN6CPools11ms_pPedPoolE"));
        if (pSym) {
            m_pPedPoolPtr = pSym;
            LOGI(OBFUSCATE("GTAPedPool: Found CPools::ms_pPedPool symbol at %p"), pSym);
            return;
        }

        // Alternative GTA symbols
        pSym = dlsym(m_hGTASA, OBFUSCATE("CPool_ms_pPedPool"));
        if (pSym) {
            m_pPedPoolPtr = pSym;
            LOGI(OBFUSCATE("GTAPedPool: Found CPool_ms_pPedPool at %p"), pSym);
            return;
        }

        // Fallback for GTA SA Android v2.10 (arm64-v8a) & v2.00 (armeabi-v7a)
        // If symbols are stripped, resolve via offset from base
        if (m_gtasaBase != 0) {
#if defined(__aarch64__)
            // GTA SA v2.10 arm64 known offset for ms_pPedPool
            // Typical offset in data section
            uintptr_t arm64_offset = 0x9380B8; // Offset for v2.10 CPools::ms_pPedPool
            m_pPedPoolPtr = reinterpret_cast<void*>(m_gtasaBase + arm64_offset);
            LOGI(OBFUSCATE("GTAPedPool: Using ARM64 offset 0x%lx"), (unsigned long)m_pPedPoolPtr);
#else
            // GTA SA v2.00 armeabi-v7a known offset
            uintptr_t arm32_offset = 0x673908;
            m_pPedPoolPtr = reinterpret_cast<void*>(m_gtasaBase + arm32_offset);
            LOGI(OBFUSCATE("GTAPedPool: Using ARM32 offset 0x%lx"), (unsigned long)m_pPedPoolPtr);
#endif
        }
    }

    void* GetPedPoolInstance() {
        if (m_pfnGetPedPool) {
            return m_pfnGetPedPool();
        }
        if (m_pPedPoolPtr) {
            uintptr_t p = 0;
            if (SafeRead(reinterpret_cast<uintptr_t>(m_pPedPoolPtr), &p)) {
                return reinterpret_cast<void*>(p);
            }
        }
        return nullptr;
    }

    // Read Ped type from CPed memory structure
    int GetPedType(uintptr_t pedAddr) {
        if (!pedAddr) return -1;

#if defined(__aarch64__)
        // 64-bit ARM64 CPed offsets:
        // m_nPedType is typically located around 0x7A0 - 0x7E0 or 0x598 depending on patch version
        uint32_t pedType = 0;
        // Test primary 64-bit offset
        if (SafeRead(pedAddr + 0x7C0, &pedType) && pedType < 32) return static_cast<int>(pedType);
        if (SafeRead(pedAddr + 0x7A4, &pedType) && pedType < 32) return static_cast<int>(pedType);
        if (SafeRead(pedAddr + 0x598, &pedType) && pedType < 32) return static_cast<int>(pedType);
        if (SafeRead(pedAddr + 0x5A4, &pedType) && pedType < 32) return static_cast<int>(pedType);
#else
        // 32-bit ARM CPed offset:
        uint32_t pedType = 0;
        if (SafeRead(pedAddr + 0x598, &pedType) && pedType < 32) return static_cast<int>(pedType);
        if (SafeRead(pedAddr + 0x5A4, &pedType) && pedType < 32) return static_cast<int>(pedType);
#endif
        return -1;
    }

    // Read Ped health from CPed memory structure
    float GetPedHealth(uintptr_t pedAddr) {
        if (!pedAddr) return 0.0f;

#if defined(__aarch64__)
        float hp = 0.0f;
        if (SafeRead(pedAddr + 0x730, &hp) && hp > 0.0f && hp <= 50000.0f) return hp;
        if (SafeRead(pedAddr + 0x544, &hp) && hp > 0.0f && hp <= 50000.0f) return hp;
        if (SafeRead(pedAddr + 0x540, &hp) && hp > 0.0f && hp <= 50000.0f) return hp;
#else
        float hp = 0.0f;
        if (SafeRead(pedAddr + 0x544, &hp) && hp > 0.0f && hp <= 50000.0f) return hp;
        if (SafeRead(pedAddr + 0x540, &hp) && hp > 0.0f && hp <= 50000.0f) return hp;
#endif
        return 100.0f; // Default alive assumption if health read is non-standard
    }

    // Scan Ped Pool and populate stats
    HudStats ScanGTA() {
        HudStats stats;
        stats.teamCount = 0;
        stats.enemyCount = 0;
        stats.botCount = 0;
        stats.totalPeds = 0;
        stats.isLibLoaded = 0;
        stats.localPlayerHp = 100;

        bool hasLib = CheckLibraries();
        if (!hasLib) {
            return stats;
        }
        stats.isLibLoaded = 1;

        void* pPoolRaw = GetPedPoolInstance();
        if (!pPoolRaw || !IsAddressReadable(pPoolRaw)) {
            // If GTA ped pool pointer is not yet populated (game loading), return empty active stats
            return stats;
        }

        // Interpret pool structure
#if defined(__aarch64__)
        uintptr_t pObjects = 0;
        uintptr_t pFlags = 0;
        int32_t poolSize = 0;

        if (!SafeRead(reinterpret_cast<uintptr_t>(pPoolRaw) + 0x00, &pObjects) ||
            !SafeRead(reinterpret_cast<uintptr_t>(pPoolRaw) + 0x08, &pFlags) ||
            !SafeRead(reinterpret_cast<uintptr_t>(pPoolRaw) + 0x10, &poolSize)) {
            return stats;
        }
#else
        uint32_t pObjects32 = 0;
        uint32_t pFlags32 = 0;
        int32_t poolSize = 0;

        if (!SafeRead(reinterpret_cast<uintptr_t>(pPoolRaw) + 0x00, &pObjects32) ||
            !SafeRead(reinterpret_cast<uintptr_t>(pPoolRaw) + 0x04, &pFlags32) ||
            !SafeRead(reinterpret_cast<uintptr_t>(pPoolRaw) + 0x08, &poolSize)) {
            return stats;
        }
        uintptr_t pObjects = pObjects32;
        uintptr_t pFlags = pFlags32;
#endif

        if (poolSize <= 0 || poolSize > 1000) {
            poolSize = 140; // Default GTA SA ped pool cap
        }

        if (!IsAddressReadable(reinterpret_cast<const void*>(pFlags)) ||
            !IsAddressReadable(reinterpret_cast<const void*>(pObjects))) {
            return stats;
        }

        // Iterate through all slots in CPool<CPed>
        const size_t pedStructSize = (sizeof(void*) == 8) ? 0x880 : 0x7C4;

        for (int i = 0; i < poolSize; ++i) {
            // Check slot occupancy flag (bEmpty != 0 means slot is empty)
            uint8_t flagByte = 0;
            if (!SafeRead(pFlags + i, &flagByte)) continue;
            if ((flagByte & 0x80) != 0) {
                continue; // Slot is free
            }

            uintptr_t pedAddr = pObjects + (i * pedStructSize);
            if (!IsAddressReadable(reinterpret_cast<const void*>(pedAddr))) continue;

            float health = GetPedHealth(pedAddr);
            if (health <= 0.0f) continue; // Dead ped

            int pedType = GetPedType(pedAddr);
            stats.totalPeds++;

            if (i == 0 || pedType == PEDTYPE_PLAYER1) {
                // Local Player (CJ)
                stats.teamCount++;
                stats.localPlayerHp = static_cast<int>(health);
                continue;
            }

            // Categorize by Ped Type & Gang Allegiance
            switch (pedType) {
                // Allies / Team Members:
                case PEDTYPE_GANG2: // Grove Street Families (Player's Gang)
                case PEDTYPE_GANG7: // Triads (Allies)
                case PEDTYPE_GANG8: // Varrio Los Aztecas (Allies)
                case PEDTYPE_PLAYER2:
                case PEDTYPE_PLAYER_NETWORK: // Multiplayer Teammate / Player
                    stats.teamCount++;
                    break;

                // Enemies / Rival Gangs:
                case PEDTYPE_GANG1: // Ballas
                case PEDTYPE_GANG3: // Los Santos Vagos
                case PEDTYPE_GANG4: // San Fierro Rifa
                case PEDTYPE_GANG5: // Da Nang Boys
                case PEDTYPE_GANG6: // Mafia
                case PEDTYPE_COP:   // Cops / SWAT / FBI
                case PEDTYPE_CRIMINAL:
                    stats.enemyCount++;
                    break;

                // Bots / Civilian NPCs:
                case PEDTYPE_CIVMALE:
                case PEDTYPE_CIVFEMALE:
                case PEDTYPE_DEALER:
                case PEDTYPE_MEDIC:
                case PEDTYPE_FIREMAN:
                case PEDTYPE_PROSTITUTE:
                default:
                    stats.botCount++;
                    break;
            }
        }

        return stats;
    }
};

#endif // GTA_PED_POOL_H
