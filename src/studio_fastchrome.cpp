#include "studio_fastchrome.h"
#include "hw_build.h"
#include "log.h"

#include <windows.h>
#include <cstdint>
#include <cstring>
#include <xmmintrin.h>

namespace studio_fastchrome
{
namespace
{
// Exact GoldClient hw.dll build. Only the TransAdd caller is patched: that
// branch repeatedly invokes R_StudioChrome with the same bone+normal while
// advancing only the output pchrome pointer. The normal opaque path advances
// its normal pointer every call and is deliberately left untouched.
constexpr std::uintptr_t kChromeTransAddCallRva = 0x0009B3E6u;
constexpr std::uintptr_t kChromeNormalCallRva   = 0x0009B484u;
constexpr std::uintptr_t kStudioChromeRva       = 0x00098080u;

constexpr std::uintptr_t kStudioModelStampRva  = 0x0288D5B4u;
constexpr std::uintptr_t kChromeAgeRva          = 0x029537A8u;
constexpr std::uintptr_t kChromeRightRva        = 0x02952BA8u;
constexpr std::uintptr_t kChromeUpRva           = 0x029531A8u;

constexpr int kMaxBones = 128;
constexpr std::uint32_t kMxcsrStatusMask = 0x0000003Fu;
constexpr std::uint32_t kMxcsrExceptionMasks = 0x00001F80u;

using ChromeFn = void (__fastcall*)(int* pchrome, int bone, const float* normal);

struct BasisState
{
    std::uint32_t age = 0;
    std::uint32_t right[3]{};
    std::uint32_t up[3]{};
};

struct Entry
{
    bool valid = false;
    bool calibrated = false;
    bool disabled = false;
    int bone = -1;
    const float* normal = nullptr;
    std::uint32_t normalBits[3]{};
    std::uint32_t stamp = 0;
    int result[2]{};
    std::uint32_t mxcsrControl = 0;
    std::uint32_t raisedFlags = 0;
};

std::uint8_t* g_hwBase = nullptr;
ChromeFn g_original = nullptr;
cvar_t* g_mode = nullptr;
Entry g_entry{};
bool g_installed = false;

std::uint64_t g_calls = 0;
std::uint64_t g_stock = 0;
std::uint64_t g_fast = 0;
std::uint64_t g_first = 0;
std::uint64_t g_calibration = 0;
std::uint64_t g_calibrationOk = 0;
std::uint64_t g_validate = 0;
std::uint64_t g_mismatch = 0;
std::uint64_t g_sideEffect = 0;
std::uint64_t g_fpFallback = 0;
std::uint64_t g_stale = 0;

void InvokeStock(int* pchrome, int bone, const float* normal)
{
    __asm
    {
        push normal
        mov edx, bone
        mov ecx, pchrome
        call dword ptr [g_original]
        add esp, 4
    }
}

bool ReadNormalBits(const float* normal, std::uint32_t bits[3])
{
    if (!normal)
        return false;
    __try
    {
        std::memcpy(bits, normal, 12);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool ReadStampAndAge(int bone, std::uint32_t& stamp, std::uint32_t& age)
{
    if (!g_hwBase || bone < 0 || bone >= kMaxBones)
        return false;
    __try
    {
        stamp = *reinterpret_cast<const std::uint32_t*>(g_hwBase + kStudioModelStampRva);
        age = *reinterpret_cast<const std::uint32_t*>(
            g_hwBase + kChromeAgeRva + static_cast<std::size_t>(bone) * 4u);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool SnapshotBasis(int bone, BasisState& out)
{
    if (!g_hwBase || bone < 0 || bone >= kMaxBones)
        return false;
    __try
    {
        out.age = *reinterpret_cast<const std::uint32_t*>(
            g_hwBase + kChromeAgeRva + static_cast<std::size_t>(bone) * 4u);
        std::memcpy(out.right,
                    g_hwBase + kChromeRightRva + static_cast<std::size_t>(bone) * 12u,
                    sizeof(out.right));
        std::memcpy(out.up,
                    g_hwBase + kChromeUpRva + static_cast<std::size_t>(bone) * 12u,
                    sizeof(out.up));
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool SameBasis(const BasisState& a, const BasisState& b)
{
    return a.age == b.age &&
           std::memcmp(a.right, b.right, sizeof(a.right)) == 0 &&
           std::memcmp(a.up, b.up, sizeof(a.up)) == 0;
}

bool RestoreBasis(int bone, const BasisState& state)
{
    if (!g_hwBase || bone < 0 || bone >= kMaxBones)
        return false;
    __try
    {
        *reinterpret_cast<std::uint32_t*>(
            g_hwBase + kChromeAgeRva + static_cast<std::size_t>(bone) * 4u) = state.age;
        std::memcpy(g_hwBase + kChromeRightRva + static_cast<std::size_t>(bone) * 12u,
                    state.right, sizeof(state.right));
        std::memcpy(g_hwBase + kChromeUpRva + static_cast<std::size_t>(bone) * 12u,
                    state.up, sizeof(state.up));
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

void ValidateIdenticalSecondCall(int* pchrome, int bone, const float* normal)
{
    // The first Gold call has already run and therefore established the exact
    // authoritative output, chrome basis/age and FP environment. With age now
    // current, an immediately repeated call executes only the SSE age-hit tail.
    // Run that identical call once, compare it, then restore the first call's
    // state so mode 2 is observationally Gold-authoritative.
    BasisState firstBasis{}, secondBasis{};
    if (!SnapshotBasis(bone, firstBasis))
        return;

    int firstResult[2] = {pchrome[0], pchrome[1]};
    const std::uint32_t firstMxcsr = _mm_getcsr();
    const std::uint32_t firstControl = firstMxcsr & ~kMxcsrStatusMask;
    if ((firstControl & kMxcsrExceptionMasks) != kMxcsrExceptionMasks)
    {
        ++g_fpFallback;
        return;
    }

    InvokeStock(pchrome, bone, normal);
    ++g_stock;
    const std::uint32_t secondMxcsr = _mm_getcsr();
    const bool haveSecondBasis = SnapshotBasis(bone, secondBasis);
    const bool outputOk = pchrome[0] == firstResult[0] && pchrome[1] == firstResult[1];
    const bool basisOk = haveSecondBasis && SameBasis(firstBasis, secondBasis);
    const bool controlOk = ((firstMxcsr ^ secondMxcsr) & ~kMxcsrStatusMask) == 0;

    ++g_validate;
    if (!basisOk)
        ++g_sideEffect;
    if (!outputOk || !controlOk)
        ++g_mismatch;

    // Restore the exact state after the first authoritative Gold call.
    pchrome[0] = firstResult[0];
    pchrome[1] = firstResult[1];
    _mm_setcsr(firstMxcsr);
    if (!basisOk && !RestoreBasis(bone, firstBasis))
        ++g_sideEffect;
}

bool SameKey(int bone, const float* normal, const std::uint32_t bits[3],
             std::uint32_t stamp, std::uint32_t age)
{
    return g_entry.valid &&
           g_entry.bone == bone && g_entry.normal == normal &&
           g_entry.stamp == stamp && age == stamp &&
           g_entry.normalBits[0] == bits[0] &&
           g_entry.normalBits[1] == bits[1] &&
           g_entry.normalBits[2] == bits[2];
}

void StoreFirst(int* pchrome, int bone, const float* normal,
                const std::uint32_t bits[3], std::uint32_t stamp)
{
    g_entry = Entry{};
    g_entry.valid = true;
    g_entry.bone = bone;
    g_entry.normal = normal;
    g_entry.normalBits[0] = bits[0];
    g_entry.normalBits[1] = bits[1];
    g_entry.normalBits[2] = bits[2];
    g_entry.stamp = stamp;
    g_entry.result[0] = pchrome[0];
    g_entry.result[1] = pchrome[1];
}

void LogStats()
{
    if ((g_calls & 0x3FFFu) != 0)
        return;
    rendererlog::Line("fastchrome: calls=%llu stock=%llu fast=%llu first=%llu calib=%llu calibOk=%llu validate=%llu mismatch=%llu side=%llu fpFallback=%llu stale=%llu",
               static_cast<unsigned long long>(g_calls),
               static_cast<unsigned long long>(g_stock),
               static_cast<unsigned long long>(g_fast),
               static_cast<unsigned long long>(g_first),
               static_cast<unsigned long long>(g_calibration),
               static_cast<unsigned long long>(g_calibrationOk),
               static_cast<unsigned long long>(g_validate),
               static_cast<unsigned long long>(g_mismatch),
               static_cast<unsigned long long>(g_sideEffect),
               static_cast<unsigned long long>(g_fpFallback),
               static_cast<unsigned long long>(g_stale));
}

void __cdecl DispatchFast(int* pchrome, int bone, const float* normal)
{
    ++g_calls;

    std::uint32_t bits[3]{};
    std::uint32_t stamp = 0, age = 0;
    if (!pchrome || !ReadNormalBits(normal, bits) || !ReadStampAndAge(bone, stamp, age))
    {
        InvokeStock(pchrome, bone, normal);
        ++g_stock;
        LogStats();
        return;
    }

    if (!SameKey(bone, normal, bits, stamp, age))
    {
        InvokeStock(pchrome, bone, normal);
        ++g_stock;
        ++g_first;

        std::uint32_t postStamp = 0, postAge = 0;
        if (ReadStampAndAge(bone, postStamp, postAge) && postAge == postStamp)
            StoreFirst(pchrome, bone, normal, bits, postStamp);
        else
            g_entry = Entry{};
        LogStats();
        return;
    }

    if (g_entry.disabled)
    {
        InvokeStock(pchrome, bone, normal);
        ++g_stock;
        LogStats();
        return;
    }

    if (g_entry.calibrated)
    {
        const std::uint32_t mxcsr = _mm_getcsr();
        const std::uint32_t control = mxcsr & ~kMxcsrStatusMask;
        const std::uint32_t status = mxcsr & kMxcsrStatusMask;
        if (control == g_entry.mxcsrControl &&
            (control & kMxcsrExceptionMasks) == kMxcsrExceptionMasks &&
            (status & g_entry.raisedFlags) == g_entry.raisedFlags)
        {
            pchrome[0] = g_entry.result[0];
            pchrome[1] = g_entry.result[1];
            ++g_fast;
            LogStats();
            return;
        }

        // If the FP control mode changes, or a status bit that this exact Gold
        // operation would raise was cleared, execute Gold so it can re-establish
        // the exact sticky state before any later reuse.
        InvokeStock(pchrome, bone, normal);
        ++g_stock;
        ++g_fpFallback;
        if (pchrome[0] != g_entry.result[0] || pchrome[1] != g_entry.result[1])
        {
            ++g_mismatch;
            g_entry.disabled = true;
        }
        LogStats();
        return;
    }

    // First duplicate is a one-time exactness calibration. Clear only MXCSR
    // sticky-status bits while preserving all control bits, run Gold once, then
    // reconstruct the exact final sticky state as original|raised. The age-hit
    // R_StudioChrome tail is SSE-only, so this also tells us precisely which
    // flags every identical future call would add.
    BasisState before{}, after{};
    if (!SnapshotBasis(bone, before) || before.age != stamp)
    {
        InvokeStock(pchrome, bone, normal);
        ++g_stock;
        ++g_stale;
        LogStats();
        return;
    }

    const std::uint32_t mxBefore = _mm_getcsr();
    const std::uint32_t controlBefore = mxBefore & ~kMxcsrStatusMask;
    if ((controlBefore & kMxcsrExceptionMasks) != kMxcsrExceptionMasks)
    {
        InvokeStock(pchrome, bone, normal);
        ++g_stock;
        ++g_fpFallback;
        LogStats();
        return;
    }

    _mm_setcsr(controlBefore); // same controls, status flags cleared
    InvokeStock(pchrome, bone, normal);
    const std::uint32_t mxProbe = _mm_getcsr();
    const std::uint32_t raised = mxProbe & kMxcsrStatusMask;
    const std::uint32_t controlAfter = mxProbe & ~kMxcsrStatusMask;
    _mm_setcsr(mxBefore | raised); // exact sticky-state result of the Gold call
    ++g_stock;
    ++g_calibration;

    const bool basisOk = SnapshotBasis(bone, after) && SameBasis(before, after);
    const bool outputOk = pchrome[0] == g_entry.result[0] &&
                          pchrome[1] == g_entry.result[1];
    if (!basisOk)
        ++g_sideEffect;
    if (!outputOk || controlAfter != controlBefore)
        ++g_mismatch;

    if (basisOk && outputOk && controlAfter == controlBefore)
    {
        g_entry.calibrated = true;
        g_entry.mxcsrControl = controlBefore;
        g_entry.raisedFlags = raised;
        ++g_calibrationOk;
    }
    else
    {
        g_entry.disabled = true;
    }
    LogStats();
}

void __cdecl DispatchValidate(int* pchrome, int bone, const float* normal)
{
    ++g_calls;
    std::uint32_t bits[3]{};
    std::uint32_t stamp = 0, age = 0;
    if (!pchrome || !ReadNormalBits(normal, bits) || !ReadStampAndAge(bone, stamp, age))
    {
        InvokeStock(pchrome, bone, normal);
        ++g_stock;
        LogStats();
        return;
    }

    if (!SameKey(bone, normal, bits, stamp, age))
    {
        InvokeStock(pchrome, bone, normal);
        ++g_stock;
        ++g_first;
        std::uint32_t postStamp = 0, postAge = 0;
        if (ReadStampAndAge(bone, postStamp, postAge) && postAge == postStamp)
        {
            StoreFirst(pchrome, bone, normal, bits, postStamp);
            ValidateIdenticalSecondCall(pchrome, bone, normal);
        }
        else
            g_entry = Entry{};
        LogStats();
        return;
    }


    if (g_entry.disabled)
    {
        InvokeStock(pchrome, bone, normal);
        ++g_stock;
        LogStats();
        return;
    }

    BasisState before{}, after{};
    const bool haveBefore = SnapshotBasis(bone, before);
    const std::uint32_t mxBefore = _mm_getcsr();
    InvokeStock(pchrome, bone, normal); // Gold remains authoritative in mode 2.
    const std::uint32_t mxAfter = _mm_getcsr();
    ++g_stock;
    ++g_validate;

    const bool basisOk = haveBefore && SnapshotBasis(bone, after) && SameBasis(before, after);
    const bool outputOk = pchrome[0] == g_entry.result[0] &&
                          pchrome[1] == g_entry.result[1];
    if (!basisOk)
        ++g_sideEffect;
    if (!outputOk || ((mxBefore ^ mxAfter) & ~kMxcsrStatusMask) != 0)
        ++g_mismatch;
    LogStats();
}

__declspec(naked) void ChromeHook()
{
    __asm
    {
        mov eax, dword ptr [g_mode]
        test eax, eax
        jz stock
        cmp dword ptr [eax + 0Ch], 03F800000h
        je fast
        cmp dword ptr [eax + 0Ch], 040000000h
        je validate
        jmp stock
    fast:
        push dword ptr [esp + 4]
        push edx
        push ecx
        call DispatchFast
        add esp, 12
        ret
    validate:
        push dword ptr [esp + 4]
        push edx
        push ecx
        call DispatchValidate
        add esp, 12
        ret
    stock:
        jmp dword ptr [g_original]
    }
}

bool PatchChromeCall(std::uint8_t* callsite)
{
    if (!callsite || callsite[0] != 0xE8)
        return false;
    const std::int32_t oldRel = *reinterpret_cast<const std::int32_t*>(callsite + 1);
    void* oldTarget = callsite + 5 + oldRel;
    if (oldTarget != reinterpret_cast<void*>(g_original))
        return false;

    DWORD oldProtect = 0;
    if (!VirtualProtect(callsite, 5, PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;
    *reinterpret_cast<std::int32_t*>(callsite + 1) =
        static_cast<std::int32_t>(reinterpret_cast<std::uint8_t*>(&ChromeHook) - (callsite + 5));
    DWORD ignored = 0;
    VirtualProtect(callsite, 5, oldProtect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), callsite, 5);
    return true;
}
} // namespace

void BeginDraw()
{
    if (g_installed)
        g_entry = Entry{};
}

bool Install(HMODULE hw, cl_enginefunc_t* engine, bool drawResetReady)
{
    if (!drawResetReady || !hwbuild::MatchesTarget(hw) ||
        !engine || !engine->pfnRegisterVariable)
    {
        rendererlog::Line("fastchrome: exact hw/draw reset unavailable, disabled");
        return false;
    }

    g_hwBase = reinterpret_cast<std::uint8_t*>(hw);
    g_original = reinterpret_cast<ChromeFn>(g_hwBase + kStudioChromeRva);
    __try { g_mode = engine->pfnRegisterVariable("r_studio_chrome", "1", 0); }
    __except (EXCEPTION_EXECUTE_HANDLER) { g_mode = nullptr; }
    if (!g_mode)
        return false;

    static const std::uint8_t expectedCall[] = {0xE8,0x95,0xCC,0xFF,0xFF};
    __try
    {
        if (std::memcmp(g_hwBase + kChromeTransAddCallRva,
                        expectedCall, sizeof(expectedCall)) != 0)
        {
            rendererlog::Line("fastchrome: TransAdd R_StudioChrome callsite signature mismatch, disabled");
            return false;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }

    if (!PatchChromeCall(g_hwBase + kChromeTransAddCallRva))
    {
        rendererlog::Line("fastchrome: callsite patch failed, disabled");
        return false;
    }

    // Validator-only coverage: zetest contains Chrome geometry but does not
    // enter the TransAdd branch. In mode 2 only, temporarily patch the normal
    // Chrome caller too. DispatchValidate runs Gold once as the real call,
    // repeats the exact same arguments solely to validate the age-hit result,
    // then restores the first call's output/basis/MXCSR. Production mode 1
    // never patches this second callsite.
    bool validatorMode = false;
    __try { validatorMode = g_mode->value == 2.0f; }
    __except (EXCEPTION_EXECUTE_HANDLER) { validatorMode = false; }
    if (validatorMode)
    {
        static const std::uint8_t expectedNormalCall[] = {0xE8,0xF7,0xCB,0xFF,0xFF};
        bool normalReady = false;
        __try
        {
            normalReady = std::memcmp(g_hwBase + kChromeNormalCallRva,
                                      expectedNormalCall, sizeof(expectedNormalCall)) == 0 &&
                          PatchChromeCall(g_hwBase + kChromeNormalCallRva);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            normalReady = false;
        }
        if (normalReady)
            rendererlog::Line("fastchrome: mode2 normal-path synthetic duplicate validator enabled");
        else
            rendererlog::Line("fastchrome: normal Chrome validator callsite unavailable, TransAdd validation only");
    }

    g_installed = true;
    rendererlog::Line("fastchrome: patched TransAdd duplicate Chrome call (mode 0 stock, 1 calibrated cache, 2 validate)");
    return true;
}
} // namespace studio_fastchrome
