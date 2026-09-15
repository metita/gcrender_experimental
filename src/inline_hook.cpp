#include "inline_hook.h"
#include <windows.h>
#include <cstdint>
#include <cstring>

// Bounded x86-32 length decoder. Handles the instruction forms that appear in
// MSVC function prologues and returns 0 for anything it does not recognise, so
// Hook() aborts cleanly instead of corrupting code on an unknown opcode.
namespace
{
// Bytes consumed by a ModR/M operand (the ModR/M byte itself + SIB + disp).
int ModRMLen(const uint8_t* m)
{
    uint8_t mod = m[0] >> 6;
    uint8_t rm  = m[0] & 7;
    int len = 1;              // ModR/M byte
    int disp = 0;
    bool sib = false;
    if (mod != 3 && rm == 4) { sib = true; len += 1; }
    if (mod == 0)
    {
        if (rm == 5) disp = 4;                       // disp32 (no base)
        if (sib && (m[1] & 7) == 5) disp = 4;        // SIB with no base
    }
    else if (mod == 1) disp = 1;                     // disp8
    else if (mod == 2) disp = 4;                     // disp32
    return len + disp;
}

// Length of one instruction, or 0 if unrecognised.
int InsLen(const uint8_t* p)
{
    int i = 0;
    // Prefixes seen in GoldClient's MSVC/SSE renderer prologues. Keep this
    // deliberately narrow: unsupported prefixed opcodes still return 0.
    while (p[i] == 0x66 || p[i] == 0x67 || p[i] == 0xF2 || p[i] == 0xF3)
        i++;
    uint8_t op = p[i++];

    if (op == 0x0F)
    {
        const uint8_t op2 = p[i++];
        // movups/movupd/movss/movsd load/store. The ModR/M addressing is
        // position independent here (GoldClient uses absolute disp32).
        if (op2 == 0x10 || op2 == 0x11)
            return i + ModRMLen(p + i);
        return 0;
    }

    if (op >= 0x50 && op <= 0x5F) return i;          // push/pop reg
    if (op == 0x90) return i;                         // nop
    if (op == 0x6A) return i + 1;                     // push imm8
    if (op == 0x68) return i + 4;                     // push imm32
    if (op >= 0xB8 && op <= 0xBF) return i + 4;       // mov reg, imm32
    if (op == 0xA1 || op == 0xA3) return i + 4;       // mov eax, moffs32 / moffs32, eax

    // Opcodes with a ModR/M operand.
    if (op == 0x88 || op == 0x89 || op == 0x8A || op == 0x8B || // mov
        op == 0x8D ||                                          // lea
        op == 0x01 || op == 0x03 || op == 0x29 || op == 0x2B || // add/sub r/m,r
        op == 0x31 || op == 0x33 || op == 0x39 || op == 0x3B || // xor/cmp
        op == 0x85 || op == 0x84 ||                            // test
        op == 0xFF)                                            // inc/dec/push/call/jmp r/m
        return i + ModRMLen(p + i);

    // Group 1 with imm8: 83 /r ib (add/or/adc/sbb/and/sub/xor/cmp r/m32, imm8).
    if (op == 0x83) return i + ModRMLen(p + i) + 1;
    // Group 1 with imm32: 81 /r id.
    if (op == 0x81) return i + ModRMLen(p + i) + 4;
    // mov r/m32, imm32: C7 /0 id.
    if (op == 0xC7) return i + ModRMLen(p + i) + 4;

    return 0; // unknown -> abort in Hook()
}
} // namespace

namespace inl
{
void* Hook(void* target, void* detour)
{
    if (!target || !detour) return nullptr;
    uint8_t* t = reinterpret_cast<uint8_t*>(target);

    // Accumulate whole instructions until we have room for a 5-byte jmp.
    int stolen = 0;
    while (stolen < 5)
    {
        int len = InsLen(t + stolen);
        if (len <= 0 || len > 16) return nullptr;
        uint8_t op = t[stolen];
        // Refuse to relocate relative branches / calls / returns.
        if (op == 0xE8 || op == 0xE9 || op == 0xEB || op == 0xC3 || op == 0xC2 ||
            (op == 0x0F && (t[stolen + 1] & 0xF0) == 0x80))
            return nullptr;
        stolen += len;
    }
    if (stolen > 32) return nullptr;

    // Trampoline: [stolen bytes][jmp target+stolen].
    uint8_t* tramp = reinterpret_cast<uint8_t*>(
        VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!tramp) return nullptr;
    memcpy(tramp, t, stolen);
    tramp[stolen] = 0xE9;
    *reinterpret_cast<int32_t*>(tramp + stolen + 1) =
        static_cast<int32_t>((t + stolen) - (tramp + stolen + 5));

    // Patch target: jmp detour (5 bytes), pad remaining stolen bytes with NOP.
    DWORD oldProtect = 0;
    if (!VirtualProtect(t, stolen, PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        VirtualFree(tramp, 0, MEM_RELEASE);
        return nullptr;
    }
    t[0] = 0xE9;
    *reinterpret_cast<int32_t*>(t + 1) =
        static_cast<int32_t>(reinterpret_cast<uint8_t*>(detour) - (t + 5));
    for (int k = 5; k < stolen; ++k) t[k] = 0x90;
    VirtualProtect(t, stolen, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), t, stolen);
    return tramp;
}
}
