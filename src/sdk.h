// Minimal, self-contained slice of the GoldSrc client SDK used by gcrender.asi.
// Only what we dereference is declared. cl_entity_t / entity_state_t come from
// the real HLSDK headers (added to the include path) so the compiler computes
// their offsets exactly, everything else is declared here so the build has no
// heavy SDK dependency.
#pragma once

#include <cstddef>
#include <cstdint>

// This SDK's const.h uses vec3_t but does not define it, the HLSDK expects the
// vector types to be provided beforehand (normally by mathlib.h).
typedef float vec_t;
typedef vec_t vec3_t[3];
typedef vec_t vec4_t[4];

// cl_entity.h drags in the server-side progs/edict headers, which we do not use
// and which need engine-only types (string_t, edict_t). Neutralise them by
// pre-defining their include guards, cl_entity_s references none of their types.
#define PROGS_H
#define PROGDEFS_H
#define EDICT_H

#include "const.h"          // byte, qboolean, colorVec
#include "entity_state.h"   // entity_state_t
#include "cl_entity.h"      // cl_entity_t (real layout: origin, curstate, player)
#include "triangleapi.h"    // triangleapi_t

struct SCREENINFO
{
    int   iSize;
    int   iWidth;
    int   iHeight;
    int   iFlags;
    int   iCharHeight;
    short charWidths[256];
};

struct hud_player_info_t
{
    char* name;
    short ping;
    byte  thisplayer;
    byte  spectator;
    byte  packetloss;
    char* model;
    short topcolor;
    short bottomcolor;
    std::uint64_t m_nSteamID; // GoldClient/ReHLDS extension, part of the ABI
};

static_assert(offsetof(hud_player_info_t, model) == 12,
              "hud_player_info_t model offset must match GoldSrc ABI");
static_assert(offsetof(hud_player_info_t, m_nSteamID) == 24 &&
              sizeof(hud_player_info_t) == 32,
              "hud_player_info_t must include GoldClient's 64-bit Steam ID");

struct cvar_s
{
    char*        name;
    char*        string;
    int          flags;
    float        value;
    struct cvar_s* next;
};
typedef struct cvar_s cvar_t;

// cl_enginefunc_t: member order must match the engine's table exactly so the
// typed members we call land at the right offsets. Unused slots are void* fills.
// Indices refer to the position in engine/APIProxy.h (cl_enginefuncs_s).
struct cl_enginefunc_t
{
    void* _f0[11];                                                   // 0..10  SPR_*
    void  (*pfnFillRGBA)(int x, int y, int w, int h, int r, int g, int b, int a); // 11
    void  (*pfnGetScreenInfo)(SCREENINFO* pscrinfo);                 // 12
    void* _f13;                                                      // 13
    cvar_t* (*pfnRegisterVariable)(const char* name, const char* value, int flags); // 14
    float (*pfnGetCvarFloat)(const char* name);                      // 15
    void* _f16[4];                                                   // 16..19
    int   (*pfnClientCmd)(char* command);                            // 20
    void  (*pfnGetPlayerInfo)(int ent_num, hud_player_info_t* pinfo); // 21
    void* _f22[14];                                                  // 22..35
    int   (*GetMaxClients)(void);                                    // 36
    void  (*Cvar_SetValue)(char* name, float value);                 // 37
    void* _f38[2];                                                   // 38..39
    void  (*Con_Printf)(const char* fmt, ...);                       // 40
    void* _f41[10];                                                  // 41..50
    cl_entity_t* (*GetLocalPlayer)(void);                            // 51
    void* _f52;                                                      // 52
    cl_entity_t* (*GetEntityByIndex)(int idx);                       // 53
    void* _f54[5];                                                   // 54..58
    void* (*PM_TraceLine)(float* start, float* end, int flags,
                          int usehull, int ignore_pe);               // 59 -> pmtrace_t*
    void* _f60[22];                                                  // 60..81
    triangleapi_t* pTriAPI;                                          // 82
    // Remaining members are unused and intentionally omitted.
};
