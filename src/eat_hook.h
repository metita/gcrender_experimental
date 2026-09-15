// Export Address Table hooking: repoint a module's exported RVA to our own
// function before the engine resolves it via GetProcAddress. No code bytes in
// the target module are patched, so this never touches GoldClient's files or
// its .text — only the export table of the already-loaded client.dll in memory.
#pragma once
#include <windows.h>
#include <cstdint>

namespace eat
{
// Repoint export `name` in module `mod` to `replacement`. Returns the original
// resolved address (module base + old RVA), or nullptr on failure.
void* Hook(HMODULE mod, const char* name, void* replacement);
}
