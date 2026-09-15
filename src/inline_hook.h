#pragma once
// Minimal x86 inline hook: overwrite the target's first bytes with a jmp to the
// detour, after relocating the stolen whole instructions into a trampoline that
// the caller invokes as "the original". Works regardless of when the engine
// resolved the export (unlike EAT hooking).
namespace inl
{
// Returns a callable trampoline (== original behaviour) or nullptr on failure.
void* Hook(void* target, void* detour);
}
