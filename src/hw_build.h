#pragma once
#include <windows.h>
#include <cstdint>

namespace hwbuild
{
constexpr std::uint32_t kTimeDateStamp = 0x6A49A361u;
constexpr std::uint32_t kSizeOfImage   = 0x03CA7000u;

inline bool MatchesTarget(HMODULE hw)
{
    if (!hw)
        return false;
    auto* base = reinterpret_cast<std::uint8_t*>(hw);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return false;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    return nt->Signature == IMAGE_NT_SIGNATURE &&
           nt->FileHeader.TimeDateStamp == kTimeDateStamp &&
           nt->OptionalHeader.SizeOfImage == kSizeOfImage;
}
}
