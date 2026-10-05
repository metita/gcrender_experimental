#pragma once

#include <cstdint>
#include <windows.h>

// Image range of this ASI.  qgl slot checks treat wrappers installed by other
// GCRender modules as compatible; resolving that with a cached range keeps the
// per-pass checks free of VirtualQuery system calls.
namespace modulerange
{
// Resolves [base, base + SizeOfImage) once.  Safe to call repeatedly.
bool Init();

// True only for addresses inside this module's mapped image.
bool ContainsSelf(const void* address);

// Exact provider match, or a wrapper that lives inside this module.
inline bool SelfOrExpected(const void* live, const void* expected)
{
    return live == expected || (live && ContainsSelf(live));
}
} // namespace modulerange
