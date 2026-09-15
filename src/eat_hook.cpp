#include "eat_hook.h"

namespace eat
{
void* Hook(HMODULE mod, const char* name, void* replacement)
{
    auto base = reinterpret_cast<uint8_t*>(mod);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return nullptr;
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return nullptr;

    auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (dir.VirtualAddress == 0 || dir.Size == 0)
        return nullptr;
    auto exp = reinterpret_cast<IMAGE_EXPORT_DIRECTORY*>(base + dir.VirtualAddress);
    auto funcs = reinterpret_cast<uint32_t*>(base + exp->AddressOfFunctions);
    auto names = reinterpret_cast<uint32_t*>(base + exp->AddressOfNames);
    auto ords  = reinterpret_cast<uint16_t*>(base + exp->AddressOfNameOrdinals);

    for (uint32_t i = 0; i < exp->NumberOfNames; ++i)
    {
        const char* exportName = reinterpret_cast<const char*>(base + names[i]);
        if (lstrcmpA(exportName, name) != 0)
            continue;

        uint32_t* slot = &funcs[ords[i]];
        void* original = base + *slot;

        DWORD oldProtect = 0;
        if (!VirtualProtect(slot, sizeof(uint32_t), PAGE_READWRITE, &oldProtect))
            return nullptr;
        *slot = static_cast<uint32_t>(reinterpret_cast<uint8_t*>(replacement) - base);
        VirtualProtect(slot, sizeof(uint32_t), oldProtect, &oldProtect);
        return original;
    }
    return nullptr;
}
}
