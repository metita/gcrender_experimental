#pragma once
// Diagnostic log written next to gcrender.asi (gcrender.log). Lets us see exactly how
// far load/hook got without a debugger or console.
namespace rendererlog
{
void Line(const char* fmt, ...);
// While deferred, Line() only appends to an in-memory buffer. Disabling
// deferred mode flushes the buffered block to gcrender.log in one file write.
void SetDeferred(bool deferred);
}
