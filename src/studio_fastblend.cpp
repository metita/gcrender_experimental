#include "studio_fastblend.h"
#include "studio_fast_math.h"
#include "log.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace studio_fastblend
{
namespace
{
constexpr std::uint32_t kClientTimestamp = 0x6A49A30Cu;
constexpr std::uint32_t kClientImageSize = 0x0026F000u;
constexpr std::uint32_t kRendererVtableRva = 0x00161970u;
constexpr std::uint32_t kSlerpBonesRva = 0x000AB720u;
constexpr std::uint32_t kSlerpBonesSlotOffset = 0x38u;
constexpr int kStudioMagic = 0x54534449;
constexpr int kStudioVersion = 10;
constexpr int kMaxBones = 128;

struct FpState { alignas(16) unsigned char bytes[512]; };
static_assert(sizeof(FpState) == 512, "fxsave size");

void* g_original = nullptr;
cvar_t* g_mode = nullptr;
std::uint64_t g_calls = 0, g_fast = 0, g_fallback = 0, g_validate = 0, g_mismatch = 0;
std::uint64_t g_stockTicks = 0, g_fastTicks = 0, g_timed = 0;
float g_maxPos = 0.0f, g_maxQ = 0.0f;

bool ExactClient(HMODULE module)
{
    if (!module) return false;
    __try
    {
        auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(module);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
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

void QuaternionSlerp(const float p[4], const float source[4], float t, float out[4])
{
    float q[4];
    std::memcpy(q, source, sizeof(q));
    float a = 0.0f, b = 0.0f;
    for (int i = 0; i < 4; ++i)
    {
        const float d = p[i] - q[i];
        const float s = p[i] + q[i];
        a += d * d;
        b += s * s;
    }
    if (a > b)
        for (float& v : q) v = -v;

    const float cosom = p[0]*q[0] + p[1]*q[1] + p[2]*q[2] + p[3]*q[3];
    if (1.0f + cosom > 0.000001f)
    {
        float sp = 0.0f, sq = 0.0f;
        if (1.0f - cosom > 0.000001f)
        {
            const float omega = studio_fastmath::Acos(cosom);
            __m128 ss, cc;
            studio_fastmath::SinCos4(
                _mm_setr_ps(omega, (1.0f - t) * omega, t * omega, 0.0f), ss, cc);
            float v[4];
            _mm_storeu_ps(v, ss);
            sp = v[1] / v[0];
            sq = v[2] / v[0];
        }
        else
        {
            sp = 1.0f - t;
            sq = t;
        }
        for (int i = 0; i < 4; ++i) out[i] = sp * p[i] + sq * q[i];
    }
    else
    {
        float tmp[4] = {-q[1], q[0], -q[3], q[2]};
        __m128 ss, cc;
        studio_fastmath::SinCos4(
            _mm_setr_ps((1.0f - t) * 1.5707963267948966f,
                        t * 1.5707963267948966f, 0.0f, 0.0f), ss, cc);
        float v[4];
        _mm_storeu_ps(v, ss);
        for (int i = 0; i < 3; ++i) out[i] = v[0] * p[i] + v[1] * tmp[i];
        out[3] = tmp[3];
    }
}

bool Domain(void* renderer, float (*q1)[4], float (*pos1)[3],
            const float (*q2)[4], const float (*pos2)[3], std::uint32_t sBits,
            int& bones, float& s)
{
    if (!renderer || !q1 || !pos1 || !q2 || !pos2) return false;
    std::memcpy(&s, &sBits, sizeof(s));
    if (!std::isfinite(s)) return false;
    __try
    {
        const auto* header = *reinterpret_cast<const std::uint8_t* const*>(
            static_cast<const std::uint8_t*>(renderer) + 0x4C);
        if (!header || *reinterpret_cast<const int*>(header) != kStudioMagic ||
            *reinterpret_cast<const int*>(header + 4) != kStudioVersion)
            return false;
        bones = *reinterpret_cast<const int*>(header + 0x8C);
        return bones >= 1 && bones <= kMaxBones;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

void FastKernel(float (*q1)[4], float (*pos1)[3],
                const float (*q2)[4], const float (*pos2)[3], int bones, float s)
{
    if (s < 0.0f) s = 0.0f;
    else if (s > 1.0f) s = 1.0f;
    const float s1 = 1.0f - s;
    for (int i = 0; i < bones; ++i)
    {
        float q[4];
        QuaternionSlerp(q1[i], q2[i], s, q);
        std::memcpy(q1[i], q, sizeof(q));
        pos1[i][0] = pos1[i][0] * s1 + pos2[i][0] * s;
        pos1[i][1] = pos1[i][1] * s1 + pos2[i][1] * s;
        pos1[i][2] = pos1[i][2] * s1 + pos2[i][2] * s;
    }
}

__declspec(noinline) void InvokeOriginal(void* renderer, void* q1, void* pos1,
                                         const void* q2, const void* pos2,
                                         std::uint32_t sBits,
                                         const FpState* before, FpState* after)
{
    __asm {
        mov eax, before
        fxrstor [eax]
        push sBits
        push pos2
        push q2
        push pos1
        push q1
        mov ecx, renderer
        call dword ptr [g_original]
        mov eax, after
        fxsave [eax]
    }
}

float Diff(float a, float b)
{
    if (!std::isfinite(a) || !std::isfinite(b)) return std::numeric_limits<float>::infinity();
    return std::fabs(a - b);
}

int __cdecl Dispatch(void* renderer, float (*q1)[4], float (*pos1)[3],
                     const float (*q2)[4], const float (*pos2)[3],
                     std::uint32_t sBits, FpState* caller)
{
    ++g_calls;
    const int mode = g_mode ? static_cast<int>(g_mode->value) : 0;
    FpState stockAfter{};
    if (mode <= 0)
    {
        InvokeOriginal(renderer, q1, pos1, q2, pos2, sBits, caller, &stockAfter);
        std::memcpy(caller, &stockAfter, sizeof(*caller));
        return 0;
    }

    int bones = 0;
    float s = 0.0f;
    if (!Domain(renderer, q1, pos1, q2, pos2, sBits, bones, s))
    {
        ++g_fallback;
        InvokeOriginal(renderer, q1, pos1, q2, pos2, sBits, caller, &stockAfter);
        std::memcpy(caller, &stockAfter, sizeof(*caller));
        return 0;
    }

    float inQ1[kMaxBones][4], inPos1[kMaxBones][3];
    float inQ2[kMaxBones][4], inPos2[kMaxBones][3];
    float fastQ[kMaxBones][4], fastPos[kMaxBones][3];
    LARGE_INTEGER t0{}, t1{}, t2{};

    if (mode == 2)
    {
        const std::size_t qBytes = static_cast<std::size_t>(bones) * sizeof(inQ1[0]);
        const std::size_t pBytes = static_cast<std::size_t>(bones) * sizeof(inPos1[0]);
        __try
        {
            std::memcpy(inQ1, q1, qBytes); std::memcpy(inPos1, pos1, pBytes);
            std::memcpy(inQ2, q2, qBytes); std::memcpy(inPos2, pos2, pBytes);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            ++g_fallback;
            InvokeOriginal(renderer, q1, pos1, q2, pos2, sBits, caller, &stockAfter);
            std::memcpy(caller, &stockAfter, sizeof(*caller));
            return 0;
        }
        QueryPerformanceCounter(&t0);
        InvokeOriginal(renderer, q1, pos1, q2, pos2, sBits, caller, &stockAfter);
        QueryPerformanceCounter(&t1);
        std::memcpy(fastQ, inQ1, qBytes); std::memcpy(fastPos, inPos1, pBytes);
        __asm {
            mov eax, caller
            fxrstor [eax]
        }
        FastKernel(fastQ, fastPos, inQ2, inPos2, bones, s);
        QueryPerformanceCounter(&t2);

        ++g_validate;
        if (t1.QuadPart >= t0.QuadPart && t2.QuadPart >= t1.QuadPart)
        {
            g_stockTicks += static_cast<std::uint64_t>(t1.QuadPart - t0.QuadPart);
            g_fastTicks += static_cast<std::uint64_t>(t2.QuadPart - t1.QuadPart);
            ++g_timed;
        }
        float mp = 0.0f, mq = 0.0f;
        for (int i = 0; i < bones; ++i)
        {
            for (int j = 0; j < 4; ++j) mq = (std::max)(mq, Diff(q1[i][j], fastQ[i][j]));
            for (int j = 0; j < 3; ++j) mp = (std::max)(mp, Diff(pos1[i][j], fastPos[i][j]));
        }
        g_maxPos = (std::max)(g_maxPos, mp);
        g_maxQ = (std::max)(g_maxQ, mq);
        if (mp > 0.0005f || mq > 0.0005f) ++g_mismatch;
        std::memcpy(caller, &stockAfter, sizeof(*caller));
    }
    else
    {
        __asm {
            mov eax, caller
            fxrstor [eax]
        }
        __try { FastKernel(q1, pos1, q2, pos2, bones, s); }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            ++g_fallback;
            InvokeOriginal(renderer, q1, pos1, q2, pos2, sBits, caller, &stockAfter);
            std::memcpy(caller, &stockAfter, sizeof(*caller));
            return 0;
        }
        ++g_fast;
        __asm {
            mov eax, caller
            fxsave [eax]
        }
    }

    if ((g_calls & 0x7FFu) == 0)
    {
        LARGE_INTEGER fq{}; QueryPerformanceFrequency(&fq);
        const double stockUs = (g_timed && fq.QuadPart)
            ? 1.0e6 * static_cast<double>(g_stockTicks) / static_cast<double>(fq.QuadPart) / static_cast<double>(g_timed) : 0.0;
        const double fastUs = (g_timed && fq.QuadPart)
            ? 1.0e6 * static_cast<double>(g_fastTicks) / static_cast<double>(fq.QuadPart) / static_cast<double>(g_timed) : 0.0;
        rendererlog::Line("fastslerp: calls=%llu fast=%llu fallback=%llu validate=%llu mismatch=%llu maxPos=%.6g maxQ=%.6g stock_us=%.3f fast_us=%.3f",
                   static_cast<unsigned long long>(g_calls),
                   static_cast<unsigned long long>(g_fast),
                   static_cast<unsigned long long>(g_fallback),
                   static_cast<unsigned long long>(g_validate),
                   static_cast<unsigned long long>(g_mismatch),
                   g_maxPos, g_maxQ, stockUs, fastUs);
    }
    return 1;
}

__declspec(naked) void SlerpHook()
{
    __asm {
        mov eax, dword ptr [g_mode]
        test eax, eax
        jz stock
        cmp dword ptr [eax + 0Ch], 03F800000h
        je fast
        cmp dword ptr [eax + 0Ch], 040000000h
        je fast
stock:
        jmp dword ptr [g_original]
fast:
        push ebp
        mov ebp, esp
        and esp, -16
        sub esp, 512
        fxsave [esp]
        mov eax, esp
        push eax
        push dword ptr [ebp + 24]
        push dword ptr [ebp + 20]
        push dword ptr [ebp + 16]
        push dword ptr [ebp + 12]
        push dword ptr [ebp + 8]
        push ecx
        call Dispatch
        add esp, 28
        fxrstor [esp]
        mov esp, ebp
        pop ebp
        ret 20
    }
}
}

bool Install(HMODULE client, cl_enginefunc_t* engine)
{
    if (!ExactClient(client) || !engine || !engine->pfnRegisterVariable)
    {
        rendererlog::Line("fastslerp: exact client/engine unavailable, disabled");
        return false;
    }
    __try { g_mode = engine->pfnRegisterVariable("r_studio_slerp", "1", 0); }
    __except (EXCEPTION_EXECUTE_HANDLER) { g_mode = nullptr; }

    auto* base = reinterpret_cast<std::uint8_t*>(client);
    auto** slot = reinterpret_cast<void**>(base + kRendererVtableRva + kSlerpBonesSlotOffset);
    void* expected = base + kSlerpBonesRva;
    __try { if (*slot != expected) return false; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }

    DWORD oldProtect = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProtect)) return false;
    g_original = expected;
    *slot = reinterpret_cast<void*>(&SlerpHook);
    VirtualProtect(slot, sizeof(void*), oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));
    rendererlog::Line("fastslerp: patched StudioSlerpBones vtable slot (mode 0 stock, 1 fast, 2 validate)");
    return true;
}
}
