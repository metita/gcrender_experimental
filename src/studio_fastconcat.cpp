#include "studio_fastconcat.h"
#include "inline_hook.h"
#include "log.h"

#include <cstdint>
#include <cstring>
#include <xmmintrin.h>

namespace studio_fastconcat
{
namespace
{
constexpr std::uint32_t kClientTimestamp = 0x6A49A30Cu;
constexpr std::uint32_t kClientImageSize = 0x0026F000u;
constexpr std::uintptr_t kChildConcatRva = 0x0005E2ABu;
constexpr std::uintptr_t kChildResumeRva = 0x0005E71Du;
constexpr std::uintptr_t kValidationExitRva = 0x0005E71Du;
constexpr int kMaxBones = 128;
constexpr std::size_t kMatrixFloats = 12;
constexpr std::size_t kMatrixBytes = kMatrixFloats * sizeof(float);
constexpr std::size_t kRendererBoneTransforms = 0x40B4;
constexpr std::size_t kRendererLightTransforms = 0x40B8;

std::uint8_t* g_clientBase = nullptr;
cvar_t* g_mode = nullptr;
void* g_childTrampoline = nullptr;
void* g_exitTrampoline = nullptr;
void* g_childResume = nullptr;

volatile LONG g_validateBusy = 0;
volatile LONG g_validatePending = 0;
int g_validateIndex = -1;
unsigned g_validateOffset = 0;
alignas(16) float g_expectedBone[kMatrixFloats]{};
alignas(16) float g_expectedLight[kMatrixFloats]{};
LONGLONG g_stockStart = 0;

std::uint64_t g_calls = 0;
std::uint64_t g_fast = 0;
std::uint64_t g_fallback = 0;
std::uint64_t g_validate = 0;
std::uint64_t g_mismatch = 0;
std::uint64_t g_fastTicks = 0;
std::uint64_t g_stockTicks = 0;
std::uint64_t g_timed = 0;

bool ExactClient(HMODULE module)
{
    if (!module)
        return false;
    __try
    {
        auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(module);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
            return false;
        auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(
            reinterpret_cast<std::uint8_t*>(module) + dos->e_lfanew);
        return nt->Signature == IMAGE_NT_SIGNATURE &&
               nt->FileHeader.TimeDateStamp == kClientTimestamp &&
               nt->OptionalHeader.SizeOfImage == kClientImageSize;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

inline float StackFloat(const std::uint8_t* stack, std::size_t offset)
{
    return *reinterpret_cast<const float*>(stack + offset);
}

// Gold's child path computes out = Concat(parent, local) twice (bone + light).
// Pack the four output columns into SSE lanes while preserving the scalar
// operation grouping: (B1*P1 + B0*P0) + B2*P2. No FMA is used.
inline void FastConcat(const float* parent,
                       __m128 b0,
                       __m128 b1,
                       __m128 b2,
                       float* out)
{
    for (int row = 0; row < 3; ++row)
    {
        const float* p = parent + row * 4;
        __m128 value = _mm_mul_ps(b1, _mm_set1_ps(p[1]));
        value = _mm_add_ps(value, _mm_mul_ps(b0, _mm_set1_ps(p[0])));
        value = _mm_add_ps(value, _mm_mul_ps(b2, _mm_set1_ps(p[2])));
        float* dst = out + row * 4;
        _mm_storeu_ps(dst, value);

        // Gold adds the parent translation only to the fourth scalar. Avoid a
        // packed +0 operation on xyz so signed-zero/NaN behavior there remains
        // exactly the result of the three stock arithmetic stages above.
        __m128 translation = _mm_load_ss(dst + 3);
        translation = _mm_add_ss(translation, _mm_load_ss(p + 3));
        _mm_store_ss(dst + 3, translation);
    }
}

bool BuildFastMatrices(void* renderer,
                       unsigned byteOffset,
                       const std::uint8_t* stack,
                       int boneCount,
                       float* outBone,
                       float* outLight,
                       int& indexOut)
{
    if (!renderer || !stack || !outBone || !outLight ||
        boneCount <= 0 || boneCount > kMaxBones)
        return false;

    __try
    {
        const auto* parentPtr = *reinterpret_cast<const int* const*>(stack + 0x34);
        if (!parentPtr)
            return false;
        const int parent = *parentPtr;
        const int index = *reinterpret_cast<const int*>(stack + 0x38);
        if (parent < 0 || parent >= boneCount || index < 0 || index >= boneCount ||
            byteOffset != static_cast<unsigned>(index) * static_cast<unsigned>(kMatrixBytes))
            return false;

        auto* bytes = static_cast<std::uint8_t*>(renderer);
        auto* boneBase = *reinterpret_cast<float**>(bytes + kRendererBoneTransforms);
        auto* lightBase = *reinterpret_cast<float**>(bytes + kRendererLightTransforms);
        if (!boneBase || !lightBase)
            return false;

        const float* parentBone = boneBase + static_cast<std::size_t>(parent) * kMatrixFloats;
        const float* parentLight = lightBase + static_cast<std::size_t>(parent) * kMatrixFloats;

        // Exact scattered local matrix at client+0x5E2AB, mapped from Gold's
        // preceding inline QuaternionMatrix block.
        const __m128 b0 = _mm_setr_ps(StackFloat(stack, 0x40),
                                      StackFloat(stack, 0x44),
                                      StackFloat(stack, 0x1C),
                                      StackFloat(stack, 0x24));
        const __m128 b1 = _mm_setr_ps(StackFloat(stack, 0x3C),
                                      StackFloat(stack, 0x48),
                                      StackFloat(stack, 0x50),
                                      StackFloat(stack, 0x58));
        const __m128 b2 = _mm_setr_ps(StackFloat(stack, 0x14),
                                      StackFloat(stack, 0x18),
                                      StackFloat(stack, 0x20),
                                      StackFloat(stack, 0x28));

        FastConcat(parentBone, b0, b1, b2, outBone);
        FastConcat(parentLight, b0, b1, b2, outLight);
        indexOut = index;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

void LogStats()
{
    if ((g_calls & 0x3FFFu) != 0)
        return;
    LARGE_INTEGER fq{};
    QueryPerformanceFrequency(&fq);
    const double stockUs = (g_timed && fq.QuadPart)
        ? 1.0e6 * static_cast<double>(g_stockTicks) /
          static_cast<double>(fq.QuadPart) / static_cast<double>(g_timed) : 0.0;
    const double fastUs = (g_timed && fq.QuadPart)
        ? 1.0e6 * static_cast<double>(g_fastTicks) /
          static_cast<double>(fq.QuadPart) / static_cast<double>(g_timed) : 0.0;
    rendererlog::Line("fastconcat: calls=%llu fast=%llu fallback=%llu validate=%llu mismatch=%llu stock_us=%.3f fast_us=%.3f",
               static_cast<unsigned long long>(g_calls),
               static_cast<unsigned long long>(g_fast),
               static_cast<unsigned long long>(g_fallback),
               static_cast<unsigned long long>(g_validate),
               static_cast<unsigned long long>(g_mismatch),
               stockUs, fastUs);
}

// Return 1 when the child concat is fully handled and Gold should resume at
// 0x5E71D. Return 0 for exact stock fall-through (including validator mode).
int __cdecl Dispatch(void* renderer,
                     unsigned byteOffset,
                     const std::uint8_t* stack,
                     int boneCount)
{
    ++g_calls;
    int mode = 0;
    __try { if (g_mode) mode = static_cast<int>(g_mode->value); }
    __except (EXCEPTION_EXECUTE_HANDLER) { mode = 0; }

    // SIMD packs several scalar operations together. With masked exceptions
    // this has the same final sticky-flag union as Gold's scalar SSE sequence,
    // with traps unmasked, preserve exact trap ordering by falling back.
    const unsigned mxcsr = _mm_getcsr();
    if ((mxcsr & 0x1F80u) != 0x1F80u)
    {
        ++g_fallback;
        LogStats();
        return 0;
    }

    alignas(16) float fastBone[kMatrixFloats]{};
    alignas(16) float fastLight[kMatrixFloats]{};
    int index = -1;

    if (mode == 2)
    {
        if (InterlockedCompareExchange(&g_validateBusy, 1, 0) != 0)
        {
            ++g_fallback;
            LogStats();
            return 0;
        }

        LARGE_INTEGER t0{}, t1{};
        QueryPerformanceCounter(&t0);
        const bool ok = BuildFastMatrices(renderer, byteOffset, stack, boneCount,
                                          fastBone, fastLight, index);
        QueryPerformanceCounter(&t1);
        // The speculative packed math must not change the FP environment seen
        // by Gold's validator run.
        _mm_setcsr(mxcsr);
        if (!ok)
        {
            InterlockedExchange(&g_validateBusy, 0);
            ++g_fallback;
            LogStats();
            return 0;
        }

        std::memcpy(g_expectedBone, fastBone, sizeof(g_expectedBone));
        std::memcpy(g_expectedLight, fastLight, sizeof(g_expectedLight));
        g_validateIndex = index;
        g_validateOffset = byteOffset;
        g_fastTicks += static_cast<std::uint64_t>(t1.QuadPart - t0.QuadPart);
        g_stockStart = t1.QuadPart;
        InterlockedExchange(&g_validatePending, 1);
        return 0;
    }

    if (mode != 1 || !BuildFastMatrices(renderer, byteOffset, stack, boneCount,
                                         fastBone, fastLight, index))
    {
        ++g_fallback;
        LogStats();
        return 0;
    }

    __try
    {
        auto* bytes = static_cast<std::uint8_t*>(renderer);
        auto* boneBase = *reinterpret_cast<float**>(bytes + kRendererBoneTransforms);
        auto* lightBase = *reinterpret_cast<float**>(bytes + kRendererLightTransforms);
        float* boneOut = boneBase + static_cast<std::size_t>(index) * kMatrixFloats;
        float* lightOut = lightBase + static_cast<std::size_t>(index) * kMatrixFloats;
        std::memcpy(boneOut, fastBone, kMatrixBytes);
        std::memcpy(lightOut, fastLight, kMatrixBytes);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        ++g_fallback;
        LogStats();
        return 0;
    }

    ++g_fast;
    LogStats();
    return 1;
}

int __cdecl DispatchFast(void* renderer,
                         unsigned byteOffset,
                         const std::uint8_t* stack,
                         int boneCount)
{
    ++g_calls;
    const unsigned mxcsr = _mm_getcsr();
    if ((mxcsr & 0x1F80u) != 0x1F80u)
    {
        ++g_fallback;
        LogStats();
        return 0;
    }

    alignas(16) float fastBone[kMatrixFloats]{};
    alignas(16) float fastLight[kMatrixFloats]{};
    int index = -1;
    if (!BuildFastMatrices(renderer, byteOffset, stack, boneCount,
                           fastBone, fastLight, index))
    {
        ++g_fallback;
        LogStats();
        return 0;
    }

    __try
    {
        auto* bytes = static_cast<std::uint8_t*>(renderer);
        auto* boneBase = *reinterpret_cast<float**>(bytes + kRendererBoneTransforms);
        auto* lightBase = *reinterpret_cast<float**>(bytes + kRendererLightTransforms);
        std::memcpy(boneBase + static_cast<std::size_t>(index) * kMatrixFloats,
                    fastBone, kMatrixBytes);
        std::memcpy(lightBase + static_cast<std::size_t>(index) * kMatrixFloats,
                    fastLight, kMatrixBytes);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        ++g_fallback;
        LogStats();
        return 0;
    }

    ++g_fast;
    LogStats();
    return 1;
}

void __cdecl CompleteValidation(void* renderer,
                                unsigned byteOffset,
                                const std::uint8_t* stack)
{
    if (InterlockedCompareExchange(&g_validatePending, 0, 1) != 1)
        return;

    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    if (now.QuadPart >= g_stockStart)
    {
        g_stockTicks += static_cast<std::uint64_t>(now.QuadPart - g_stockStart);
        ++g_timed;
    }

    bool equal = false;
    __try
    {
        const int index = *reinterpret_cast<const int*>(stack + 0x38);
        auto* bytes = static_cast<std::uint8_t*>(renderer);
        auto* boneBase = *reinterpret_cast<float**>(bytes + kRendererBoneTransforms);
        auto* lightBase = *reinterpret_cast<float**>(bytes + kRendererLightTransforms);
        equal = index == g_validateIndex && byteOffset == g_validateOffset &&
                boneBase && lightBase &&
                std::memcmp(boneBase + static_cast<std::size_t>(index) * kMatrixFloats,
                            g_expectedBone, kMatrixBytes) == 0 &&
                std::memcmp(lightBase + static_cast<std::size_t>(index) * kMatrixFloats,
                            g_expectedLight, kMatrixBytes) == 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        equal = false;
    }

    ++g_validate;
    if (!equal)
        ++g_mismatch;
    g_validateIndex = -1;
    g_validateOffset = 0;
    InterlockedExchange(&g_validateBusy, 0);
    LogStats();
}

__declspec(naked) void ChildConcatHook()
{
    __asm
    {
        // EAX is Gold's live bone count and is required by the stock fallback.
        // EDX is dead here because the stolen first instruction overwrites it.
        mov edx, dword ptr [g_mode]
        test edx, edx
        jz stock
        cmp dword ptr [edx + 0Ch], 03F800000h
        je fast_active
        cmp dword ptr [edx + 0Ch], 040000000h
        jne stock
        jmp validate_active

    fast_active:
        // Mirror Gold's parent guards before doing any packed work. The stolen
        // trampoline repeats them only on fail-open.
        mov edx, dword ptr [esp + 34h]
        cmp dword ptr [edx], 0
        jl stock
        cmp dword ptr [edx], eax
        jge stock

        // Preserve the incoming MXCSR only for a rare fail-open. On success we
        // intentionally keep the sticky flags produced by the equivalent SSE
        // arithmetic, just as Gold does.
        sub esp, 4
        stmxcsr dword ptr [esp]
        lea edx, [esp + 4]
        push eax
        push edx
        push esi
        push edi
        call DispatchFast
        add esp, 16
        test eax, eax
        jnz fast_handled

        // Reconstruct the live scalar values stock consumes, restore its bone
        // count and FP environment, then execute the untouched child block.
        ldmxcsr dword ptr [esp]
        add esp, 4
        mov eax, dword ptr [edi + 4Ch]
        mov eax, dword ptr [eax + 8Ch]
        movss xmm2, dword ptr [esp + 58h]
        movss xmm3, dword ptr [esp + 3Ch]
        movss xmm4, dword ptr [esp + 48h]
        movss xmm5, dword ptr [esp + 50h]
        movss xmm6, dword ptr [esp + 40h]
        movss xmm7, dword ptr [esp + 44h]
        jmp stock

    fast_handled:
        add esp, 4
        jmp dword ptr [g_childResume]

    validate_active:
        // Stock fallback needs XMM2..7 exactly as produced by QuaternionMatrix.
        // Save them cheaply, on a handled child they are dead and need not be
        // restored before the loop tail.
        sub esp, 100
        movups xmmword ptr [esp + 00h], xmm2
        movups xmmword ptr [esp + 10h], xmm3
        movups xmmword ptr [esp + 20h], xmm4
        movups xmmword ptr [esp + 30h], xmm5
        movups xmmword ptr [esp + 40h], xmm6
        movups xmmword ptr [esp + 50h], xmm7
        pushad
        mov ecx, dword ptr [esp + 28]
        mov eax, dword ptr [esp + 12]
        add eax, 100
        push ecx
        push eax
        push esi
        push edi
        call Dispatch
        add esp, 16
        mov dword ptr [esp + 128], eax
        popad
        mov edx, dword ptr [esp + 96]
        test edx, edx
        jnz handled
        movups xmm2, xmmword ptr [esp + 00h]
        movups xmm3, xmmword ptr [esp + 10h]
        movups xmm4, xmmword ptr [esp + 20h]
        movups xmm5, xmmword ptr [esp + 30h]
        movups xmm6, xmmword ptr [esp + 40h]
        movups xmm7, xmmword ptr [esp + 50h]
        add esp, 100
    stock:
        jmp dword ptr [g_childTrampoline]
    handled:
        add esp, 100
        jmp dword ptr [g_childResume]
    }
}

__declspec(naked) void ValidationExitHook()
{
    __asm
    {
        cmp dword ptr [g_validatePending], 0
        je stock
        pushad
        mov eax, dword ptr [esp + 12]
        push eax
        push esi
        push edi
        call CompleteValidation
        add esp, 12
        popad
    stock:
        jmp dword ptr [g_exitTrampoline]
    }
}
} // namespace

bool Install(HMODULE client, cl_enginefunc_t* engine)
{
    if (!ExactClient(client) || !engine || !engine->pfnRegisterVariable)
    {
        rendererlog::Line("fastconcat: exact client/engine unavailable, disabled");
        return false;
    }

    g_clientBase = reinterpret_cast<std::uint8_t*>(client);
    __try { g_mode = engine->pfnRegisterVariable("r_studio_concat", "1", 0); }
    __except (EXCEPTION_EXECUTE_HANDLER) { g_mode = nullptr; }
    if (!g_mode)
        return false;

    static const std::uint8_t expectedChild[] = {0x8B,0x54,0x24,0x34,0x83,0x3A,0x00};
    static const std::uint8_t expectedExit[] = {0x8B,0x4C,0x24,0x4C,0x8B,0x54,0x24,0x38};
    __try
    {
        if (std::memcmp(g_clientBase + kChildConcatRva, expectedChild, sizeof(expectedChild)) != 0 ||
            std::memcmp(g_clientBase + kValidationExitRva, expectedExit, sizeof(expectedExit)) != 0)
        {
            rendererlog::Line("fastconcat: exact SetupBones child signatures mismatch, disabled");
            return false;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }

    g_childResume = g_clientBase + kChildResumeRva;
    g_exitTrampoline = inl::Hook(g_clientBase + kValidationExitRva,
                                 reinterpret_cast<void*>(&ValidationExitHook));
    if (!g_exitTrampoline)
    {
        rendererlog::Line("fastconcat: validation-exit hook failed, disabled");
        return false;
    }
    g_childTrampoline = inl::Hook(g_clientBase + kChildConcatRva,
                                  reinterpret_cast<void*>(&ChildConcatHook));
    if (!g_childTrampoline)
    {
        rendererlog::Line("fastconcat: child hook failed after exit hook, fast path disabled");
        g_mode->value = 0.0f;
        return false;
    }

    rendererlog::Line("fastconcat: hooked SetupBones valid-child dual ConcatTransforms (mode 0 stock, 1 packed SSE, 2 validate)");
    return true;
}
} // namespace studio_fastconcat
