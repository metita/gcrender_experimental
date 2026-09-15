#include "studio_fastcache.h"
#include "log.h"

#include <cstdint>
#include <cstring>
#include <algorithm>

namespace studio_fastcache
{
namespace
{
constexpr std::uint32_t kClientTimestamp = 0x6A49A30Cu;
constexpr std::uint32_t kClientImageSize = 0x0026F000u;
constexpr std::uint32_t kRendererVtableRva = 0x00161970u;
constexpr std::uint32_t kSaveBonesRva = 0x000AE130u;
constexpr std::uint32_t kSaveBonesSlotOffset = 0x24u;
constexpr std::uint32_t kMergeCompareCallRva = 0x000AE2B5u;
constexpr std::uint32_t kStricmpRva = 0x0011EEA5u;
constexpr std::uint32_t kGaitDecisionRva = 0x0005D9E0u;
constexpr std::uint32_t kGaitStockContinueRva = 0x0005D9E5u;
constexpr std::uint32_t kGaitSkipCopyRva = 0x0005DA19u;
constexpr std::uint32_t kGaitDoCopyRva = 0x0005DE57u;
constexpr std::uint32_t kGaitSpineNameRva = 0x00161914u;

constexpr std::size_t kHeaderId = 0x00;
constexpr std::size_t kHeaderVersion = 0x04;
constexpr std::size_t kHeaderLength = 0x48;
constexpr std::size_t kHeaderNumBones = 0x8C;
constexpr std::size_t kHeaderBoneIndex = 0x90;
constexpr int kStudioMagic = 0x54534449; // IDST
constexpr int kStudioVersion = 10;
constexpr int kMaxBones = 128;
constexpr std::size_t kStudioBoneSize = 112;
constexpr std::size_t kBoneNameBytes = 32;
constexpr std::size_t kMatrixBytes = 48;

constexpr std::size_t kRendererHeader = 0x4C;
constexpr std::size_t kRendererSavedCount = 0x68;
constexpr std::size_t kRendererSavedNames = 0x6C;
constexpr std::size_t kRendererSavedBones = 0x106C;
constexpr std::size_t kRendererSavedLights = 0x286C;
constexpr std::size_t kRendererBoneTransforms = 0x40B4;
constexpr std::size_t kRendererLightTransforms = 0x40B8;

using SaveBonesFn = void(__thiscall*)(void*);
using StricmpFn = int(__cdecl*)(const char*, const char*);

cl_enginefunc_t* g_engine = nullptr;
cvar_t* g_mode = nullptr;
cvar_t* g_mergeMode = nullptr;
cvar_t* g_gaitMode = nullptr;
SaveBonesFn g_original = nullptr;
StricmpFn g_originalStricmp = nullptr;
volatile LONG g_busy = 0;
std::uint8_t* g_clientBase = nullptr;

void* g_gaitStockContinue = nullptr;
void* g_gaitSkipCopy = nullptr;
void* g_gaitDoCopy = nullptr;
const char* g_gaitSpineName = nullptr;

struct ValidationScratch
{
    char names[kMaxBones][kBoneNameBytes];
    std::uint8_t bones[kMaxBones][kMatrixBytes];
    std::uint8_t lights[kMaxBones][kMatrixBytes];
};

ValidationScratch g_scratch{};
std::uint64_t g_calls = 0;
std::uint64_t g_fast = 0;
std::uint64_t g_fallback = 0;
std::uint64_t g_validate = 0;
std::uint64_t g_mismatch = 0;
std::uint64_t g_saveStockTicks = 0;
std::uint64_t g_saveFastTicks = 0;
std::uint64_t g_saveTimed = 0;

struct MergeRelation
{
    const std::uint8_t* childHeader{};
    int childCount{};
    int savedCount{};
    char childNames[kMaxBones][kBoneNameBytes]{};
    char savedNames[kMaxBones][kBoneNameBytes]{};
    std::int16_t match[kMaxBones]{};
    std::uint64_t stamp{};
    bool valid{};
};

constexpr int kMergeRelations = 8;
MergeRelation g_relations[kMergeRelations]{};
MergeRelation* g_activeRelation = nullptr;
void* g_activeRenderer = nullptr;
std::uint64_t g_relationStamp = 0;
std::uint64_t g_mergeLookups = 0;
std::uint64_t g_mergeFast = 0;
std::uint64_t g_mergeFallback = 0;
std::uint64_t g_mergeBuilds = 0;
std::uint64_t g_mergeCacheHits = 0;
std::uint64_t g_mergeValidate = 0;
std::uint64_t g_mergeMismatch = 0;
std::uint64_t g_mergeComparisonsSkipped = 0;

struct GaitMaskEntry
{
    const void* model{};
    const std::uint8_t* header{};
    int length{};
    int count{};
    int boneIndex{};
    char headerName[64]{};
    std::uint8_t mask[kMaxBones]{};
    std::uint64_t stamp{};
    bool valid{};
};

constexpr int kGaitCacheEntries = 16;
GaitMaskEntry g_gaitCache[kGaitCacheEntries]{};
const std::uint8_t* g_activeGaitMask = nullptr;
const std::uint8_t* g_activeGaitHeader = nullptr;
void* g_activeGaitRenderer = nullptr;
int g_activeGaitCount = 0;
std::uint64_t g_gaitStamp = 0;
std::uint64_t g_gaitSetups = 0;
std::uint64_t g_gaitBuilds = 0;
std::uint64_t g_gaitHits = 0;
std::uint64_t g_gaitFallback = 0;
std::uint64_t g_gaitValidate = 0;
std::uint64_t g_gaitMismatch = 0;
std::uint64_t g_gaitBonesFast = 0;

template <class T>
T Read(const void* p, std::size_t off = 0)
{
    T value{};
    std::memcpy(&value, static_cast<const std::uint8_t*>(p) + off, sizeof(value));
    return value;
}

void CopySavedBoneName(char* dest, const char* source)
{
    // Gold SaveBones uses strncpy(dest, source, 31) followed by dest[31]=0.
    // Reproduce strncpy's zero-padding semantics exactly, a raw memcpy would
    // preserve bytes after an embedded NUL and can therefore diverge.
    std::size_t i = 0;
    for (; i < 31 && source[i] != 0; ++i)
        dest[i] = source[i];
    for (; i < 31; ++i)
        dest[i] = 0;
    dest[31] = 0;
}

bool ExactClient(HMODULE client)
{
    if (!client) return false;
    const auto* base = reinterpret_cast<const std::uint8_t*>(client);
    __try
    {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
        return nt->Signature == IMAGE_NT_SIGNATURE &&
               nt->FileHeader.TimeDateStamp == kClientTimestamp &&
               nt->OptionalHeader.SizeOfImage == kClientImageSize;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool GetInputs(void* self, const std::uint8_t*& header, const std::uint8_t*& bones,
               const std::uint8_t*& liveBones, const std::uint8_t*& liveLights, int& count)
{
    if (!self) return false;
    auto* r = static_cast<std::uint8_t*>(self);
    __try
    {
        header = *reinterpret_cast<const std::uint8_t* const*>(r + kRendererHeader);
        liveBones = *reinterpret_cast<const std::uint8_t* const*>(r + kRendererBoneTransforms);
        liveLights = *reinterpret_cast<const std::uint8_t* const*>(r + kRendererLightTransforms);
        if (!header || !liveBones || !liveLights) return false;
        if (Read<int>(header, kHeaderId) != kStudioMagic ||
            Read<int>(header, kHeaderVersion) != kStudioVersion)
            return false;
        const int length = Read<int>(header, kHeaderLength);
        count = Read<int>(header, kHeaderNumBones);
        const int boneIndex = Read<int>(header, kHeaderBoneIndex);
        if (count < 0 || count > kMaxBones || length < 244 || length > 16 * 1024 * 1024 ||
            boneIndex < 0 || static_cast<std::uint64_t>(boneIndex) +
                static_cast<std::uint64_t>(count) * kStudioBoneSize > static_cast<std::uint64_t>(length))
            return false;
        bones = header + boneIndex;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool BuildExpected(const std::uint8_t* bones, const std::uint8_t* liveBones,
                   const std::uint8_t* liveLights, int count, ValidationScratch& out)
{
    __try
    {
        for (int i = 0; i < count; ++i)
        {
            CopySavedBoneName(out.names[i], reinterpret_cast<const char*>(
                bones + static_cast<std::size_t>(i) * kStudioBoneSize));
        }
        if (count > 0)
        {
            const std::size_t bytes = static_cast<std::size_t>(count) * kMatrixBytes;
            std::memcpy(out.bones, liveBones, bytes);
            std::memcpy(out.lights, liveLights, bytes);
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool ReadMergeDomain(void* self, const std::uint8_t*& header,
                     const std::uint8_t*& childBones, int& childCount,
                     const char*& savedNames, int& savedCount)
{
    if (!self) return false;
    auto* r = static_cast<std::uint8_t*>(self);
    __try
    {
        header = *reinterpret_cast<const std::uint8_t* const*>(r + kRendererHeader);
        if (!header || Read<int>(header, kHeaderId) != kStudioMagic ||
            Read<int>(header, kHeaderVersion) != kStudioVersion)
            return false;
        const int length = Read<int>(header, kHeaderLength);
        childCount = Read<int>(header, kHeaderNumBones);
        const int boneIndex = Read<int>(header, kHeaderBoneIndex);
        savedCount = *reinterpret_cast<const int*>(r + kRendererSavedCount);
        if (childCount <= 0 || childCount > kMaxBones || savedCount < 0 || savedCount > kMaxBones ||
            length < 244 || length > 16 * 1024 * 1024 || boneIndex < 0 ||
            static_cast<std::uint64_t>(boneIndex) +
                static_cast<std::uint64_t>(childCount) * kStudioBoneSize > static_cast<std::uint64_t>(length))
            return false;
        childBones = header + boneIndex;
        savedNames = reinterpret_cast<const char*>(r + kRendererSavedNames);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool ReadGaitDomain(void* self, const void*& model, const std::uint8_t*& header,
                    const std::uint8_t*& bones, int& count,
                    int& length, int& boneIndex)
{
    if (!self) return false;
    auto* r = static_cast<std::uint8_t*>(self);
    __try
    {
        model = *reinterpret_cast<const void* const*>(r + 0x38);
        header = *reinterpret_cast<const std::uint8_t* const*>(r + kRendererHeader);
        if (!model || !header || Read<int>(header, kHeaderId) != kStudioMagic ||
            Read<int>(header, kHeaderVersion) != kStudioVersion)
            return false;
        length = Read<int>(header, kHeaderLength);
        count = Read<int>(header, kHeaderNumBones);
        boneIndex = Read<int>(header, kHeaderBoneIndex);
        if (count <= 0 || count > kMaxBones || length < 244 || length > 16 * 1024 * 1024 ||
            boneIndex < 0 || static_cast<std::uint64_t>(boneIndex) +
                static_cast<std::uint64_t>(count) * kStudioBoneSize > static_cast<std::uint64_t>(length))
            return false;
        bones = header + boneIndex;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool NameEqualsLiteral(const std::uint8_t* name, const char* literal)
{
    if (!name || !literal) return false;
    for (std::size_t i = 0; i < kBoneNameBytes; ++i)
    {
        const unsigned char a = name[i];
        const unsigned char b = static_cast<unsigned char>(literal[i]);
        if (a != b) return false;
        if (a == 0) return true;
        if (b == 0) return false;
    }
    return false;
}

bool BuildGaitMask(const std::uint8_t* bones, int count, std::uint8_t* mask)
{
    if (!bones || !mask || count <= 0 || count > kMaxBones) return false;
    __try
    {
        // Gold compares C strings. Fail open for malformed fixed-width Studio
        // names rather than changing its out-of-bounds/string semantics.
        for (int i = 0; i < count; ++i)
        {
            const auto* bone = bones + static_cast<std::size_t>(i) * kStudioBoneSize;
            if (!std::memchr(bone, 0, kBoneNameBytes))
                return false;
        }

        bool copyGait = true;
        for (int i = 0; i < count; ++i)
        {
            const auto* bone = bones + static_cast<std::size_t>(i) * kStudioBoneSize;
            if (NameEqualsLiteral(bone, "Bip01 Spine"))
            {
                copyGait = false;
            }
            else
            {
                const int parent = Read<int>(bone, 0x20);
                if (parent >= 0 && parent < count)
                {
                    const auto* parentBone = bones + static_cast<std::size_t>(parent) * kStudioBoneSize;
                    if (NameEqualsLiteral(parentBone, "Bip01 Pelvis"))
                        copyGait = true;
                }
            }
            mask[i] = copyGait ? 1u : 0u;
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool GaitEntryMatches(const GaitMaskEntry& e, const void* model, const std::uint8_t* header,
                      int length, int count, int boneIndex)
{
    if (!e.valid || e.model != model || e.header != header || e.length != length || e.count != count ||
        e.boneIndex != boneIndex)
        return false;
    __try
    {
        return std::memcmp(e.headerName, header + 8, sizeof(e.headerName)) == 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

GaitMaskEntry* PrepareGaitEntry(const void* model, const std::uint8_t* header, const std::uint8_t* bones,
                                int length, int count, int boneIndex, bool validate)
{
    GaitMaskEntry* found = nullptr;
    for (auto& e : g_gaitCache)
    {
        if (GaitEntryMatches(e, model, header, length, count, boneIndex))
        {
            found = &e;
            break;
        }
    }

    if (found)
    {
        if (validate)
        {
            std::uint8_t fresh[kMaxBones]{};
            ++g_gaitValidate;
            if (!BuildGaitMask(bones, count, fresh) ||
                std::memcmp(found->mask, fresh, static_cast<std::size_t>(count)) != 0)
            {
                ++g_gaitMismatch;
                found->valid = false;
                return nullptr;
            }
        }
        found->stamp = ++g_gaitStamp;
        ++g_gaitHits;
        return found;
    }

    GaitMaskEntry* victim = &g_gaitCache[0];
    for (auto& e : g_gaitCache)
    {
        if (!e.valid) { victim = &e; break; }
        if (e.stamp < victim->stamp) victim = &e;
    }

    std::uint8_t fresh[kMaxBones]{};
    if (!BuildGaitMask(bones, count, fresh))
        return nullptr;

    victim->valid = false;
    victim->model = model;
    victim->header = header;
    victim->length = length;
    victim->count = count;
    victim->boneIndex = boneIndex;
    __try
    {
        std::memcpy(victim->headerName, header + 8, sizeof(victim->headerName));
        std::memcpy(victim->mask, fresh, static_cast<std::size_t>(count));
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
    victim->stamp = ++g_gaitStamp;
    victim->valid = true;
    ++g_gaitBuilds;
    return victim;
}

int ReadGaitMode()
{
    int mode = 0;
    __try { if (g_gaitMode) mode = static_cast<int>(g_gaitMode->value); }
    __except (EXCEPTION_EXECUTE_HANDLER) { mode = 0; }
    return mode;
}

void __cdecl PrepareGaitMask(void* self)
{
    g_activeGaitMask = nullptr;
    g_activeGaitHeader = nullptr;
    g_activeGaitRenderer = nullptr;
    g_activeGaitCount = 0;
    ++g_gaitSetups;

    const int mode = ReadGaitMode();
    if (mode != 1 && mode != 2)
        return;

    const void* model = nullptr;
    const std::uint8_t* header = nullptr;
    const std::uint8_t* bones = nullptr;
    int count = 0, length = 0, boneIndex = 0;
    if (!ReadGaitDomain(self, model, header, bones, count, length, boneIndex))
    {
        ++g_gaitFallback;
        return;
    }

    GaitMaskEntry* entry = PrepareGaitEntry(model, header, bones, length, count, boneIndex, mode == 2);
    if (!entry)
    {
        ++g_gaitFallback;
        return;
    }

    g_activeGaitMask = entry->mask;
    g_activeGaitHeader = header;
    g_activeGaitRenderer = self;
    g_activeGaitCount = count;
    g_gaitBonesFast += static_cast<std::uint64_t>(count);

    if ((g_gaitSetups & 0xFFFu) == 0)
    {
        rendererlog::Line("fastgait: setups=%llu builds=%llu hits=%llu fallback=%llu validate=%llu mismatch=%llu bones=%llu",
                   static_cast<unsigned long long>(g_gaitSetups),
                   static_cast<unsigned long long>(g_gaitBuilds),
                   static_cast<unsigned long long>(g_gaitHits),
                   static_cast<unsigned long long>(g_gaitFallback),
                   static_cast<unsigned long long>(g_gaitValidate),
                   static_cast<unsigned long long>(g_gaitMismatch),
                   static_cast<unsigned long long>(g_gaitBonesFast));
    }
}

bool MergeNamesWellFormed(const std::uint8_t* childBones, int childCount,
                          const char* savedNames, int savedCount)
{
    __try
    {
        // The binary calls _stricmp directly. Only accelerate well-formed fixed
        // Studio names so the optimized lookup never changes unterminated-name behavior.
        // This full scan is intentionally done once per MergeBones invocation
        // (on child bone 0), not once per child bone.
        for (int i = 0; i < childCount; ++i)
            if (!std::memchr(childBones + static_cast<std::size_t>(i) * kStudioBoneSize, 0, kBoneNameBytes))
                return false;
        for (int i = 0; i < savedCount; ++i)
            if (!std::memchr(savedNames + static_cast<std::size_t>(i) * kBoneNameBytes, 0, kBoneNameBytes))
                return false;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool RelationNamesEqual(const MergeRelation& rel, const std::uint8_t* childBones,
                        int childCount, const char* savedNames, int savedCount)
{
    if (!rel.valid || rel.childCount != childCount || rel.savedCount != savedCount) return false;
    __try
    {
        for (int i = 0; i < childCount; ++i)
            if (std::memcmp(rel.childNames[i], childBones + static_cast<std::size_t>(i) * kStudioBoneSize,
                            kBoneNameBytes) != 0)
                return false;
        return savedCount == 0 ||
               std::memcmp(rel.savedNames, savedNames,
                           static_cast<std::size_t>(savedCount) * kBoneNameBytes) == 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

MergeRelation* PrepareRelation(const std::uint8_t* header, const std::uint8_t* childBones,
                               int childCount, const char* savedNames, int savedCount)
{
    for (auto& rel : g_relations)
    {
        if (rel.childHeader == header &&
            RelationNamesEqual(rel, childBones, childCount, savedNames, savedCount))
        {
            rel.stamp = ++g_relationStamp;
            ++g_mergeCacheHits;
            return &rel;
        }
    }

    MergeRelation* victim = &g_relations[0];
    for (auto& rel : g_relations)
    {
        if (!rel.valid) { victim = &rel; break; }
        if (rel.stamp < victim->stamp) victim = &rel;
    }

    victim->valid = false;
    victim->childHeader = header;
    victim->childCount = childCount;
    victim->savedCount = savedCount;
    __try
    {
        for (int i = 0; i < childCount; ++i)
            std::memcpy(victim->childNames[i],
                        childBones + static_cast<std::size_t>(i) * kStudioBoneSize, kBoneNameBytes);
        if (savedCount > 0)
            std::memcpy(victim->savedNames, savedNames,
                        static_cast<std::size_t>(savedCount) * kBoneNameBytes);
        for (int i = 0; i < childCount; ++i)
        {
            victim->match[i] = -1;
            const char* child = reinterpret_cast<const char*>(
                childBones + static_cast<std::size_t>(i) * kStudioBoneSize);
            for (int j = 0; j < savedCount; ++j)
            {
                if (g_originalStricmp(child, savedNames + static_cast<std::size_t>(j) * kBoneNameBytes) == 0)
                {
                    victim->match[i] = static_cast<std::int16_t>(j);
                    break;
                }
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
    victim->stamp = ++g_relationStamp;
    victim->valid = true;
    ++g_mergeBuilds;
    return victim;
}

// Return >=0 for the matching saved-bone index, -1 for no match and -2 to
// execute the untouched stock inner loop.
int __cdecl MergeLookup(void* self, const char* childName)
{
    ++g_mergeLookups;
    float mode = 0.0f;
    if (g_mergeMode)
    {
        __try { mode = g_mergeMode->value; }
        __except (EXCEPTION_EXECUTE_HANDLER) { mode = 0.0f; }
    }
    if (mode < 1.0f || !g_originalStricmp)
    {
        ++g_mergeFallback;
        return -2;
    }

    const std::uint8_t* header = nullptr;
    const std::uint8_t* childBones = nullptr;
    const char* savedNames = nullptr;
    int childCount = 0, savedCount = 0;
    if (!ReadMergeDomain(self, header, childBones, childCount, savedNames, savedCount))
    {
        ++g_mergeFallback;
        return -2;
    }

    const auto* child = reinterpret_cast<const std::uint8_t*>(childName);
    if (child < childBones)
    {
        ++g_mergeFallback;
        return -2;
    }
    const std::size_t delta = static_cast<std::size_t>(child - childBones);
    if (delta % kStudioBoneSize != 0)
    {
        ++g_mergeFallback;
        return -2;
    }
    const int childIndex = static_cast<int>(delta / kStudioBoneSize);
    if (childIndex < 0 || childIndex >= childCount)
    {
        ++g_mergeFallback;
        return -2;
    }

    if (childIndex == 0)
    {
        if (!MergeNamesWellFormed(childBones, childCount, savedNames, savedCount))
        {
            g_activeRelation = nullptr;
            g_activeRenderer = nullptr;
            ++g_mergeFallback;
            return -2;
        }
        g_activeRelation = PrepareRelation(header, childBones, childCount, savedNames, savedCount);
        g_activeRenderer = self;
    }
    if (!g_activeRelation || g_activeRenderer != self || g_activeRelation->childHeader != header ||
        g_activeRelation->childCount != childCount || g_activeRelation->savedCount != savedCount)
    {
        ++g_mergeFallback;
        return -2;
    }

    const int match = g_activeRelation->match[childIndex];
    if (mode >= 2.0f)
    {
        ++g_mergeValidate;
        int stock = -1;
        __try
        {
            for (int j = 0; j < savedCount; ++j)
            {
                if (g_originalStricmp(childName,
                    savedNames + static_cast<std::size_t>(j) * kBoneNameBytes) == 0)
                {
                    stock = j;
                    break;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            ++g_mergeFallback;
            return -2;
        }
        if (stock != match)
        {
            ++g_mergeMismatch;
            ++g_mergeFallback;
            return -2;
        }
    }

    ++g_mergeFast;
    g_mergeComparisonsSkipped += static_cast<std::uint64_t>(match >= 0 ? match : savedCount);
    if ((g_mergeLookups & 0x3FFFu) == 0)
        rendererlog::Line("fastmerge: lookups=%llu fast=%llu fallback=%llu builds=%llu hits=%llu validate=%llu mismatch=%llu skipped=%llu",
                   static_cast<unsigned long long>(g_mergeLookups),
                   static_cast<unsigned long long>(g_mergeFast),
                   static_cast<unsigned long long>(g_mergeFallback),
                   static_cast<unsigned long long>(g_mergeBuilds),
                   static_cast<unsigned long long>(g_mergeCacheHits),
                   static_cast<unsigned long long>(g_mergeValidate),
                   static_cast<unsigned long long>(g_mergeMismatch),
                   static_cast<unsigned long long>(g_mergeComparisonsSkipped));
    return match;
}

__declspec(naked) int __cdecl MergeCompareHook(const char*, const char*)
{
    __asm {
        // Disabled/unknown modes are a true stock tail-jump: no C++ helper,
        // no cache lookup and no extra per-comparison work.
        mov eax, dword ptr [g_mergeMode]
        test eax, eax
        jz fallback
        cmp dword ptr [eax + 0Ch], 03F800000h
        je enabled
        cmp dword ptr [eax + 0Ch], 040000000h
        jne fallback
enabled:
        // We only collapse the inner search from its first iteration. If the
        // stock loop reached us with EBX != 0, preserve the original _stricmp.
        test ebx, ebx
        jne fallback
        mov eax, dword ptr [esp + 4]
        push eax
        push edi
        call MergeLookup
        add esp, 8
        cmp eax, -2
        je fallback
        cmp eax, -1
        jne matched
        mov ecx, dword ptr [edi + 68h]
        lea ebx, [ecx - 1]
        mov eax, 1
        ret
matched:
        mov ebx, eax
        mov ecx, eax
        shl ecx, 5
        lea esi, [edi + ecx + 6Ch]
        xor eax, eax
        ret
fallback:
        jmp dword ptr [g_originalStricmp]
    }
}

bool PatchRelativeCall(std::uint8_t* site, void* expectedTarget, void* replacement)
{
    __try
    {
        if (site[0] != 0xE8) return false;
        std::int32_t oldDisp = 0;
        std::memcpy(&oldDisp, site + 1, sizeof(oldDisp));
        if (site + 5 + oldDisp != expectedTarget) return false;
        const auto delta = reinterpret_cast<std::intptr_t>(replacement) -
                           reinterpret_cast<std::intptr_t>(site + 5);
        if (delta < INT32_MIN || delta > INT32_MAX) return false;
        const std::int32_t newDisp = static_cast<std::int32_t>(delta);
        DWORD oldProtect = 0;
        if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
        std::memcpy(site + 1, &newDisp, sizeof(newDisp));
        DWORD ignored = 0;
        VirtualProtect(site, 5, oldProtect, &ignored);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool PatchRelativeJump5(std::uint8_t* site, const std::uint8_t expected[5], void* replacement)
{
    __try
    {
        if (std::memcmp(site, expected, 5) != 0) return false;
        const auto delta = reinterpret_cast<std::intptr_t>(replacement) -
                           reinterpret_cast<std::intptr_t>(site + 5);
        if (delta < INT32_MIN || delta > INT32_MAX) return false;
        const std::int32_t disp = static_cast<std::int32_t>(delta);
        DWORD oldProtect = 0;
        if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
        site[0] = 0xE9;
        std::memcpy(site + 1, &disp, sizeof(disp));
        DWORD ignored = 0;
        VirtualProtect(site, 5, oldProtect, &ignored);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool RestoreFive(std::uint8_t* site, const std::uint8_t original[5])
{
    __try
    {
        DWORD oldProtect = 0;
        if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
        std::memcpy(site, original, 5);
        DWORD ignored = 0;
        VirtualProtect(site, 5, oldProtect, &ignored);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool FastSave(void* self)
{
    const std::uint8_t *header = nullptr, *bones = nullptr, *liveBones = nullptr, *liveLights = nullptr;
    int count = 0;
    if (!GetInputs(self, header, bones, liveBones, liveLights, count)) return false;
    auto* r = static_cast<std::uint8_t*>(self);
    __try
    {
        *reinterpret_cast<int*>(r + kRendererSavedCount) = count;
        auto* names = reinterpret_cast<char(*)[kBoneNameBytes]>(r + kRendererSavedNames);
        for (int i = 0; i < count; ++i)
        {
            CopySavedBoneName(names[i], reinterpret_cast<const char*>(
                bones + static_cast<std::size_t>(i) * kStudioBoneSize));
        }
        if (count > 0)
        {
            const std::size_t bytes = static_cast<std::size_t>(count) * kMatrixBytes;
            // Stock SaveBones performs these as 2*N memcpy(48) calls. Both
            // source and destination are contiguous matrix arrays, so two
            // bulk copies produce the exact same bytes with far less call overhead.
            std::memcpy(r + kRendererSavedBones, liveBones, bytes);
            std::memcpy(r + kRendererSavedLights, liveLights, bytes);
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool SnapshotSavedResult(void* self, int count, ValidationScratch& out)
{
    auto* r = static_cast<std::uint8_t*>(self);
    __try
    {
        if (*reinterpret_cast<const int*>(r + kRendererSavedCount) != count)
            return false;
        if (count > 0)
        {
            const std::size_t nameBytes = static_cast<std::size_t>(count) * kBoneNameBytes;
            const std::size_t matrixBytes = static_cast<std::size_t>(count) * kMatrixBytes;
            std::memcpy(out.names, r + kRendererSavedNames, nameBytes);
            std::memcpy(out.bones, r + kRendererSavedBones, matrixBytes);
            std::memcpy(out.lights, r + kRendererSavedLights, matrixBytes);
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool CompareSavedResult(void* self, int count, const ValidationScratch& stock)
{
    auto* r = static_cast<std::uint8_t*>(self);
    __try
    {
        if (*reinterpret_cast<const int*>(r + kRendererSavedCount) != count)
            return false;
        if (count <= 0) return true;
        const std::size_t nameBytes = static_cast<std::size_t>(count) * kBoneNameBytes;
        const std::size_t matrixBytes = static_cast<std::size_t>(count) * kMatrixBytes;
        return std::memcmp(r + kRendererSavedNames, stock.names, nameBytes) == 0 &&
               std::memcmp(r + kRendererSavedBones, stock.bones, matrixBytes) == 0 &&
               std::memcmp(r + kRendererSavedLights, stock.lights, matrixBytes) == 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

void RestoreSavedResult(void* self, int count, const ValidationScratch& stock)
{
    auto* r = static_cast<std::uint8_t*>(self);
    __try
    {
        *reinterpret_cast<int*>(r + kRendererSavedCount) = count;
        if (count > 0)
        {
            const std::size_t nameBytes = static_cast<std::size_t>(count) * kBoneNameBytes;
            const std::size_t matrixBytes = static_cast<std::size_t>(count) * kMatrixBytes;
            std::memcpy(r + kRendererSavedNames, stock.names, nameBytes);
            std::memcpy(r + kRendererSavedBones, stock.bones, matrixBytes);
            std::memcpy(r + kRendererSavedLights, stock.lights, matrixBytes);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

void __cdecl SaveBonesDispatch(void* self)
{
    if (!g_original) return;
    ++g_calls;

    float mode = 0.0f;
    if (g_mode)
    {
        __try { mode = g_mode->value; }
        __except (EXCEPTION_EXECUTE_HANDLER) { mode = 0.0f; }
    }

    // The naked entry wrapper already bypasses this function completely when
    // disabled. Keep only the re-entrancy fallback here for active modes.
    if (mode < 1.0f || InterlockedCompareExchange(&g_busy, 1, 0) != 0)
    {
        g_original(self);
        return;
    }

    if (mode >= 2.0f)
    {
        const std::uint8_t *header = nullptr, *bones = nullptr, *liveBones = nullptr, *liveLights = nullptr;
        int count = 0;
        LARGE_INTEGER t0{}, t1{}, t2{};
        QueryPerformanceCounter(&t0);
        g_original(self);
        QueryPerformanceCounter(&t1);
        if (!GetInputs(self, header, bones, liveBones, liveLights, count) ||
            !SnapshotSavedResult(self, count, g_scratch))
        {
            ++g_fallback;
        }
        else
        {
            const bool ok = FastSave(self);
            QueryPerformanceCounter(&t2);
            ++g_validate;
            if (!ok || !CompareSavedResult(self, count, g_scratch))
                ++g_mismatch;
            RestoreSavedResult(self, count, g_scratch);
            if (t1.QuadPart >= t0.QuadPart && t2.QuadPart >= t1.QuadPart)
            {
                g_saveStockTicks += static_cast<std::uint64_t>(t1.QuadPart - t0.QuadPart);
                g_saveFastTicks += static_cast<std::uint64_t>(t2.QuadPart - t1.QuadPart);
                ++g_saveTimed;
            }
        }
    }
    else if (FastSave(self))
    {
        ++g_fast;
    }
    else
    {
        ++g_fallback;
        g_original(self);
    }

    InterlockedExchange(&g_busy, 0);
    if ((g_calls & 0xFFFu) == 0)
    {
        LARGE_INTEGER fq{};
        QueryPerformanceFrequency(&fq);
        const double stockUs = (g_saveTimed && fq.QuadPart)
            ? 1.0e6 * static_cast<double>(g_saveStockTicks) /
              static_cast<double>(fq.QuadPart) / static_cast<double>(g_saveTimed) : 0.0;
        const double fastUs = (g_saveTimed && fq.QuadPart)
            ? 1.0e6 * static_cast<double>(g_saveFastTicks) /
              static_cast<double>(fq.QuadPart) / static_cast<double>(g_saveTimed) : 0.0;
        rendererlog::Line("fastsave: calls=%llu fast=%llu fallback=%llu validate=%llu mismatch=%llu stock_us=%.3f fast_us=%.3f",
                   static_cast<unsigned long long>(g_calls),
                   static_cast<unsigned long long>(g_fast),
                   static_cast<unsigned long long>(g_fallback),
                   static_cast<unsigned long long>(g_validate),
                   static_cast<unsigned long long>(g_mismatch), stockUs, fastUs);
    }
}

__declspec(naked) void SaveBonesHook()
{
    __asm {
        mov eax, dword ptr [g_mode]
        test eax, eax
        jz stock
        cmp dword ptr [eax + 0Ch], 03F800000h
        je active
        cmp dword ptr [eax + 0Ch], 040000000h
        je active
stock:
        jmp dword ptr [g_original]
active:
        push ecx
        call SaveBonesDispatch
        add esp, 4
        ret
    }
}

__declspec(naked) void GaitDecisionHook()
{
    __asm {
        mov eax, dword ptr [g_gaitMode]
        test eax, eax
        jz stock
        cmp dword ptr [eax + 0Ch], 03F800000h
        je enabled
        cmp dword ptr [eax + 0Ch], 040000000h
        jne stock
enabled:
        // i==0 is the only per-SetupBones preparation. All remaining bones
        // take the direct mask lookup below with no C++ call.
        cmp dword ptr [esp + 4Ch], 0
        jne have_mask
        pushfd
        pushad
        push edi
        call PrepareGaitMask
        add esp, 4
        popad
        popfd
have_mask:
        mov eax, dword ptr [g_activeGaitRenderer]
        cmp eax, edi
        jne stock
        mov eax, dword ptr [edi + 4Ch]
        cmp eax, dword ptr [g_activeGaitHeader]
        jne stock
        mov ecx, dword ptr [esp + 4Ch]
        cmp ecx, dword ptr [g_activeGaitCount]
        jae stock
        mov edx, dword ptr [g_activeGaitMask]
        test edx, edx
        jz stock
        mov al, byte ptr [edx + ecx]
        // Keep Gold's persistent gait-copy state coherent so a later fail-open
        // in this same SetupBones invocation can safely resume its stock logic.
        mov byte ptr [esp + 13h], al
        test al, al
        jnz do_copy
        jmp dword ptr [g_gaitSkipCopy]
do_copy:
        jmp dword ptr [g_gaitDoCopy]
stock:
        // Exact stolen instruction at client+0x5D9E0, using the relocated live
        // string pointer instead of a fixed preferred-base immediate.
        mov ecx, dword ptr [g_gaitSpineName]
        jmp dword ptr [g_gaitStockContinue]
    }
}
}

bool Install(HMODULE client, cl_enginefunc_t* engine)
{
    if (!ExactClient(client) || !engine || !engine->pfnRegisterVariable)
    {
        rendererlog::Line("fastsave: exact client/engine unavailable, disabled");
        return false;
    }
    g_engine = engine;
    __try { g_mode = engine->pfnRegisterVariable("r_studio_savebones", "2", 0); }
    __except (EXCEPTION_EXECUTE_HANDLER) { g_mode = nullptr; }
    __try { g_mergeMode = engine->pfnRegisterVariable("r_studio_mergebones", "1", 0); }
    __except (EXCEPTION_EXECUTE_HANDLER) { g_mergeMode = nullptr; }
    __try { g_gaitMode = engine->pfnRegisterVariable("r_studio_gait", "1", 0); }
    __except (EXCEPTION_EXECUTE_HANDLER) { g_gaitMode = nullptr; }

    auto* base = reinterpret_cast<std::uint8_t*>(client);
    g_clientBase = base;

    auto** slot = reinterpret_cast<void**>(base + kRendererVtableRva + kSaveBonesSlotOffset);
    void* expected = base + kSaveBonesRva;
    __try
    {
        if (*slot != expected)
        {
            rendererlog::Line("fastsave: vtable mismatch (%p expected %p), disabled", *slot, expected);
            return false;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }

    DWORD oldProtect = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProtect)) return false;
    g_original = reinterpret_cast<SaveBonesFn>(expected);
    *slot = reinterpret_cast<void*>(&SaveBonesHook);
    VirtualProtect(slot, sizeof(void*), oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));
    rendererlog::Line("fastsave: patched StudioSaveBones vtable slot (mode 0 stock, 1 fast, 2 validate)");

    g_originalStricmp = reinterpret_cast<StricmpFn>(base + kStricmpRva);
    if (PatchRelativeCall(base + kMergeCompareCallRva,
                          reinterpret_cast<void*>(g_originalStricmp),
                          reinterpret_cast<void*>(&MergeCompareHook)))
        rendererlog::Line("fastmerge: patched StudioMergeBones name-search call (mode 0 stock, 1 fast, 2 validate)");
    else
        rendererlog::Line("fastmerge: exact compare callsite mismatch, disabled");

    g_gaitStockContinue = base + kGaitStockContinueRva;
    g_gaitSkipCopy = base + kGaitSkipCopyRva;
    g_gaitDoCopy = base + kGaitDoCopyRva;
    g_gaitSpineName = reinterpret_cast<const char*>(base + kGaitSpineNameRva);
    std::uint8_t gaitExpected[5]{0xB9, 0, 0, 0, 0};
    const std::uint32_t spineAddress = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(g_gaitSpineName));
    std::memcpy(gaitExpected + 1, &spineAddress, sizeof(spineAddress));
    if (PatchRelativeJump5(base + kGaitDecisionRva, gaitExpected,
                           reinterpret_cast<void*>(&GaitDecisionHook)))
        rendererlog::Line("fastgait: patched SetupBones gait classifier (mode 0 stock, 1 cached, 2 validate)");
    else
        rendererlog::Line("fastgait: exact SetupBones gait classifier mismatch, disabled");
    return true;
}
}
