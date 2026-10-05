#include "module_range.h"

namespace modulerange
{
namespace
{
std::uintptr_t g_begin = 0;
std::uintptr_t g_end = 0;
} // namespace

bool Init()
{
    if (g_begin)
        return true;

    HMODULE self = nullptr;
    if (!GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&Init), &self) ||
        !self)
        return false;

    __try
    {
        const auto* base = reinterpret_cast<const std::uint8_t*>(self);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
            return false;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
            base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE ||
            nt->OptionalHeader.SizeOfImage == 0)
            return false;
        g_end = reinterpret_cast<std::uintptr_t>(base) +
                nt->OptionalHeader.SizeOfImage;
        g_begin = reinterpret_cast<std::uintptr_t>(base);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        g_begin = g_end = 0;
        return false;
    }
    return true;
}

bool ContainsSelf(const void* address)
{
    const auto value = reinterpret_cast<std::uintptr_t>(address);
    return g_begin && value >= g_begin && value < g_end;
}
} // namespace modulerange
