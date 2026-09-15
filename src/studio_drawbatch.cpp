#include "studio_drawbatch.h"
#include "hw_build.h"
#include "log.h"
#include "world_vbo.h"
#include "studio_fastlighting.h"
#include "studio_fastchrome.h"
#include "studio_fastskin.h"
#include "profile.h"
#include "inline_hook.h"

#include <windows.h>
#include <atomic>
#include <climits>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>
#include <xmmintrin.h>
#include <intrin.h>

namespace studio_drawbatch
{
namespace
{
constexpr std::uint32_t kClientTimestamp = 0x6A49A30Cu;
constexpr std::uint32_t kClientImageSize = 0x0026F000u;
constexpr std::uintptr_t kEngineStudioDrawPointsSlotRva = 0x001B25ACu;
constexpr std::uintptr_t kClientNormalDrawReturnRva = 0x000B0A0Cu;
// IEngineStudio.StudioDrawPoints points at the public wrapper 0x9B570. That
// wrapper performs Gold's matrix-mode/modelview setup and calls the hot inner
// draw kernel at 0x9B0A0. Hook the public slot target so all stock setup stays
// intact while our qgl scope covers the inner emitters.
constexpr std::uintptr_t kHwStudioDrawPointsRva = 0x0009B570u;
constexpr std::uintptr_t kCacheFreeRva = 0x0011E2F0u;
constexpr std::uintptr_t kKnownModelTableRva = 0x03337298u;
constexpr std::uintptr_t kKnownModelCountRva = 0x03339298u;
constexpr int kKnownModelMax = 2048;
constexpr std::uintptr_t kEmitStandardRva = 0x0009A840u;
constexpr std::uintptr_t kEmitAltUvRva = 0x0009AA20u;
constexpr std::uintptr_t kEmitChromeRva = 0x0009ACA0u;
constexpr std::uintptr_t kEmitChromeCallRva = 0x0009B002u;
constexpr std::uintptr_t kEmitAltUvCallRva = 0x0009B00Du;
constexpr std::uintptr_t kEmitStandardCallRva = 0x0009B01Au;

// Immutable Studio topology lives in the loaded studio header.  These Gold
// globals remain dynamic (lighting/positions/blend) and are intentionally read
// every emitter call, only the tri-command structure is cached.
constexpr std::uintptr_t kStudioHdrPtrRva = 0x029F5888u;
constexpr std::uintptr_t kStudioBlendRva = 0x029F588Cu;
constexpr std::uintptr_t kForceFaceFlagsRva = 0x029839B0u;
constexpr std::uintptr_t kCurrentStudioModelRva = 0x029839BCu;
constexpr std::uintptr_t kPvLightValuesRva = 0x0298E248u;
constexpr std::uintptr_t kAuxVertsRva = 0x029BE248u;
constexpr std::uintptr_t kLightPosRva = 0x02892B68u;
constexpr std::uintptr_t kNumStudioLightsRva = 0x02952B98u;
constexpr std::uintptr_t kMirrorNormalRva = 0x027EF521u;
constexpr std::uintptr_t kChromeTableRva = 0x029639A8u;
constexpr std::uintptr_t kFirstNormalByVertexRva = 0x029539A8u;
constexpr std::uintptr_t kLightLambertRva = 0x00097DE0u;
constexpr std::uintptr_t kBoneTransformRva = 0x029F3A58u;
constexpr std::uintptr_t kStudioShadowMirrorRva = 0x027EF520u;
constexpr std::uintptr_t kStudioSetupRenderModeRva = 0x027EF524u;
constexpr std::uintptr_t kStudioRenderModelPtrRva = 0x029839C8u;
constexpr std::uintptr_t kStudioShadowsRawRva = 0x03339800u;

constexpr std::uintptr_t kQglTexCoord2fRva = 0x027E3778u;
constexpr std::uintptr_t kQglActiveTextureRva = 0x027E3784u;
constexpr std::uintptr_t kQglVertex3fRva = 0x027E3798u;
constexpr std::uintptr_t kQglColor4fRva = 0x027E3DD4u;
constexpr std::uintptr_t kQglDrawElementsRva = 0x027E3CE8u;
constexpr std::uintptr_t kQglDrawArraysRva = 0x027E3CECu;
constexpr std::uintptr_t kQglDrawRangeElementsRva = 0x027E3888u;
constexpr std::uintptr_t kQglEnableClientStateRva = 0x027E3CD4u;
constexpr std::uintptr_t kQglDisableClientStateRva = 0x027E3CF0u;
constexpr std::uintptr_t kQglVertexPointerRva = 0x027E3958u;
constexpr std::uintptr_t kQglTexCoordPointerRva = 0x027E39E4u;
constexpr std::uintptr_t kQglColorPointerRva = 0x027E3D0Cu;
constexpr std::uintptr_t kQglPushClientAttribRva = 0x027E3B04u;
constexpr std::uintptr_t kQglPopClientAttribRva = 0x027E3B14u;
constexpr std::uintptr_t kQglGetIntegervRva = 0x027E3DD0u;
constexpr std::uintptr_t kQglClientActiveTextureRva = 0x027E386Cu;
constexpr std::uintptr_t kQglBindBufferRva = 0x027E3DB8u;
constexpr std::uintptr_t kQglDeleteBuffersRva = 0x027E3DC8u;
constexpr std::uintptr_t kQglGenBuffersRva = 0x027E3DCCu;
constexpr std::uintptr_t kQglBufferDataRva = 0x027E3DB4u;
constexpr std::uintptr_t kQglIsEnabledRva = 0x027E3DF4u;
constexpr std::uintptr_t kQglGetFloatvRva = 0x027E37DCu;
constexpr std::uintptr_t kSdlGetProcAddressIatRva = 0x001FA330u;

constexpr unsigned GL_TRIANGLE_STRIP = 0x0005u;
constexpr unsigned GL_TRIANGLE_FAN = 0x0006u;
constexpr unsigned GL_TRIANGLES = 0x0004u;
constexpr unsigned GL_FLOAT = 0x1406u;
constexpr unsigned GL_SHORT = 0x1402u;
constexpr unsigned GL_UNSIGNED_SHORT = 0x1403u;
constexpr unsigned GL_UNSIGNED_INT = 0x1405u;
constexpr unsigned GL_VERTEX_ARRAY = 0x8074u;
constexpr unsigned GL_NORMAL_ARRAY = 0x8075u;
constexpr unsigned GL_COLOR_ARRAY = 0x8076u;
constexpr unsigned GL_INDEX_ARRAY = 0x8077u;
constexpr unsigned GL_TEXTURE_COORD_ARRAY = 0x8078u;
constexpr unsigned GL_EDGE_FLAG_ARRAY = 0x8079u;
constexpr unsigned GL_FOG_COORDINATE_ARRAY = 0x8457u;
constexpr unsigned GL_SECONDARY_COLOR_ARRAY = 0x845Eu;
constexpr unsigned GL_TEXTURE0 = 0x84C0u;
constexpr unsigned GL_ACTIVE_TEXTURE = 0x84E0u;
constexpr unsigned GL_CLIENT_ACTIVE_TEXTURE = 0x84E1u;
constexpr unsigned GL_MAX_TEXTURE_UNITS = 0x84E2u;
constexpr unsigned GL_TEXTURE_2D = 0x0DE1u;
constexpr unsigned GL_CURRENT_COLOR = 0x0B00u;
constexpr unsigned GL_LIGHTING = 0x0B50u;
constexpr unsigned GL_FOG = 0x0B60u;
constexpr unsigned GL_COLOR_SUM = 0x8458u;
constexpr unsigned GL_TEXTURE_GEN_S = 0x0C60u;
constexpr unsigned GL_TEXTURE_GEN_T = 0x0C61u;
constexpr unsigned GL_TEXTURE_GEN_R = 0x0C62u;
constexpr unsigned GL_TEXTURE_GEN_Q = 0x0C63u;
constexpr unsigned GL_CLIP_PLANE0 = 0x3000u;
constexpr unsigned GL_ARRAY_BUFFER = 0x8892u;
constexpr unsigned GL_ELEMENT_ARRAY_BUFFER = 0x8893u;
constexpr unsigned GL_ARRAY_BUFFER_BINDING = 0x8894u;
constexpr unsigned GL_ELEMENT_ARRAY_BUFFER_BINDING = 0x8895u;
constexpr unsigned GL_STATIC_DRAW = 0x88E4u;
constexpr unsigned GL_STREAM_DRAW = 0x88E0u;
constexpr unsigned GL_BUFFER_SIZE = 0x8764u;
constexpr unsigned GL_CLIENT_VERTEX_ARRAY_BIT = 0x00000002u;
constexpr unsigned GL_VERTEX_SHADER = 0x8B31u;
constexpr unsigned GL_COMPILE_STATUS = 0x8B81u;
constexpr unsigned GL_LINK_STATUS = 0x8B82u;
constexpr unsigned GL_INFO_LOG_LENGTH = 0x8B84u;
constexpr unsigned GL_CURRENT_PROGRAM = 0x8B8Du;
constexpr unsigned GL_MAX_VERTEX_ATTRIBS = 0x8869u;
constexpr unsigned GL_MAX_VERTEX_UNIFORM_COMPONENTS = 0x8B4Au;
constexpr unsigned GL_VERTEX_ATTRIB_ARRAY_ENABLED = 0x8622u;
constexpr unsigned char GL_FALSE_VALUE = 0;

constexpr unsigned kGpuAttribPosition = 4;
constexpr unsigned kGpuAttribTexCoord = 5;
constexpr unsigned kGpuAttribBone = 6;

using StudioDrawPointsFn = void (__cdecl*)();
using GlTexCoord2fFn = void (WINAPI*)(float s, float t);
using GlVertex3fFn = void (WINAPI*)(float x, float y, float z);
using GlColor4fFn = void (WINAPI*)(float r, float g, float b, float a);
using GlDrawElementsFn = void (WINAPI*)(unsigned mode, int count, unsigned type, const void* indices);
using GlDrawArraysFn = void (WINAPI*)(unsigned mode, int first, int count);
using GlDrawRangeElementsFn = void (WINAPI*)(unsigned mode, unsigned start, unsigned end,
                                            int count, unsigned type, const void* indices);
using GlEnableClientStateFn = void (WINAPI*)(unsigned array);
using GlDisableClientStateFn = void (WINAPI*)(unsigned array);
using GlVertexPointerFn = void (WINAPI*)(int size, unsigned type, int stride, const void* ptr);
using GlTexCoordPointerFn = void (WINAPI*)(int size, unsigned type, int stride, const void* ptr);
using GlColorPointerFn = void (WINAPI*)(int size, unsigned type, int stride, const void* ptr);
using GlPushClientAttribFn = void (WINAPI*)(unsigned mask);
using GlPopClientAttribFn = void (WINAPI*)();
using GlGetIntegervFn = void (WINAPI*)(unsigned pname, int* params);
using GlActiveTextureFn = void (WINAPI*)(unsigned texture);
using GlClientActiveTextureFn = void (WINAPI*)(unsigned texture);
using GlBindBufferFn = void (WINAPI*)(unsigned target, unsigned buffer);
using GlDeleteBuffersFn = void (WINAPI*)(int count, const unsigned* buffers);
using GlGenBuffersFn = void (WINAPI*)(int count, unsigned* buffers);
using GlBufferDataFn = void (WINAPI*)(unsigned target, std::ptrdiff_t size,
                                      const void* data, unsigned usage);
using GlGetBufferSubDataFn = void (WINAPI*)(unsigned target, std::ptrdiff_t offset,
                                            std::ptrdiff_t size, void* data);
using GlGetBufferParameterivFn = void (WINAPI*)(unsigned target, unsigned pname, int* params);
using GlIsEnabledFn = unsigned char (WINAPI*)(unsigned cap);
using GlGetFloatvFn = void (WINAPI*)(unsigned pname, float* params);
using SdlGetProcAddressFn = void* (__cdecl*)(const char* name);
using GlCreateShaderFn = unsigned (WINAPI*)(unsigned type);
using GlShaderSourceFn = void (WINAPI*)(unsigned shader, int count,
                                       const char* const* strings, const int* lengths);
using GlCompileShaderFn = void (WINAPI*)(unsigned shader);
using GlGetShaderivFn = void (WINAPI*)(unsigned shader, unsigned pname, int* params);
using GlGetShaderInfoLogFn = void (WINAPI*)(unsigned shader, int maxLength,
                                           int* length, char* infoLog);
using GlDeleteShaderFn = void (WINAPI*)(unsigned shader);
using GlCreateProgramFn = unsigned (WINAPI*)();
using GlAttachShaderFn = void (WINAPI*)(unsigned program, unsigned shader);
using GlBindAttribLocationFn = void (WINAPI*)(unsigned program, unsigned index,
                                             const char* name);
using GlLinkProgramFn = void (WINAPI*)(unsigned program);
using GlGetProgramivFn = void (WINAPI*)(unsigned program, unsigned pname, int* params);
using GlGetProgramInfoLogFn = void (WINAPI*)(unsigned program, int maxLength,
                                            int* length, char* infoLog);
using GlUseProgramFn = void (WINAPI*)(unsigned program);
using GlDeleteProgramFn = void (WINAPI*)(unsigned program);
using GlGetUniformLocationFn = int (WINAPI*)(unsigned program, const char* name);
using GlUniform4fvFn = void (WINAPI*)(int location, int count, const float* value);
using GlVertexAttribPointerFn = void (WINAPI*)(unsigned index, int size, unsigned type,
                                              unsigned char normalized, int stride,
                                              const void* pointer);
using GlEnableVertexAttribArrayFn = void (WINAPI*)(unsigned index);
using GlDisableVertexAttribArrayFn = void (WINAPI*)(unsigned index);
using GlGetVertexAttribivFn = void (WINAPI*)(unsigned index, unsigned pname, int* params);

struct Vertex
{
    float xyz[3];
    float rgba[4];
    float st[2];
    bool hasTex;
    bool hasColor;
};

// Production predecoded draws never need the capture-only hasTex/hasColor
// flags. Keep the exact nine float attributes contiguous so the driver sees
// the same bits with a 36-byte stride instead of Vertex's 40-byte stride.
struct DrawVertex
{
    float xyz[3];
    float rgba[4];
    float st[2];
};
static_assert(sizeof(DrawVertex) == 36, "DrawVertex must stay nine floats");

struct StudioHeaderRaw
{
    int id, version;
    char name[64];
    int length;
    float eye[3], min[3], max[3], bbmin[3], bbmax[3];
    int flags;
    int numbones, boneindex, numbonecontrollers, bonecontrollerindex;
    int numhitboxes, hitboxindex, numseq, seqindex, numseqgroups, seqgroupindex;
    int numtextures, textureindex, texturedataindex, numskinref, numskinfamilies, skinindex;
    int numbodyparts, bodypartindex, numattachments, attachmentindex;
    int soundtable, soundindex, soundgroups, soundgroupindex, numtransitions, transitionindex;
};

struct StudioBodypartRaw
{
    char name[64];
    int nummodels;
    int base;
    int modelindex;
};

struct StudioModelRaw
{
    char name[64];
    int type;
    float boundingradius;
    int nummesh;
    int meshindex;
    int numverts;
    int vertinfoindex;
    int vertindex;
    int numnorms;
    int norminfoindex;
    int normindex;
    int numgroups;
    int groupindex;
};

static_assert(sizeof(StudioHeaderRaw) == 244, "StudioHeaderRaw layout mismatch");
static_assert(sizeof(StudioBodypartRaw) == 76, "StudioBodypartRaw layout mismatch");
static_assert(sizeof(StudioModelRaw) == 112, "StudioModelRaw layout mismatch");

struct StaticStudioVertex
{
    float xyz[3];
    std::int16_t rawST[2];
    std::uint16_t indices[2]; // compact bone slot, compact light slot
};
static_assert(sizeof(StaticStudioVertex) == 20, "StaticStudioVertex layout mismatch");

struct PrimitiveRange
{
    unsigned mode = 0;
    std::size_t first = 0;
    std::size_t count = 0;
};

struct CachedCorner
{
    std::uint16_t vert = 0;
    std::uint16_t norm = 0;
    std::int16_t rawS = 0;
    std::int16_t rawT = 0;
    float studioS = 0.0f;
    float studioT = 0.0f;
    std::uint32_t renderSlot = 0;
    std::uint16_t lightSlot = 0;
    bool lightFirst = false;
    float altS = 0.0f;
    float altT = 0.0f;
};

struct TopologyPrimitive
{
    unsigned mode = 0;
    std::uint32_t first = 0;
    std::uint32_t count = 0;
};

struct TopologyCacheEntry
{
    const std::uint8_t* ownerModel = nullptr;
    int ownerModelIndex = -1;
    const std::uint8_t* header = nullptr;
    const std::int16_t* stream = nullptr;
    const std::uint8_t* mesh = nullptr;
    const StudioModelRaw* studioModel = nullptr;
    std::uint32_t headerLength = 0;
    std::uint32_t headerId = 0;
    std::uint32_t headerVersion = 0;
    char headerName[64]{};
    std::int32_t expectedNumTris = 0;
    std::size_t streamBytes = 0;
    std::uint8_t streamHead[16]{};
    std::uint8_t streamTail[16]{};
    std::vector<std::uint8_t> rawStream;
    std::vector<CachedCorner> corners;
    std::vector<CachedCorner> uniqueCorners;
    std::vector<std::uint32_t> uniqueCornerIndices;
    std::vector<std::uint32_t> lightFirstRenderSlots;
    std::vector<TopologyPrimitive> primitives;
    std::vector<std::uint32_t> triangleIndices;
    std::vector<std::uint32_t> renderTriangleIndices;
    std::uint32_t renderMinIndex = 0;
    std::uint32_t renderMaxIndex = 0;
    std::uint32_t lightPairCount = 0;
    std::uint32_t modelGeneration = 0;
    mutable unsigned indexBuffer = 0;
    mutable std::uint32_t indexBufferGeneration = 0;
    mutable std::uint32_t indexBufferValidatedGeneration = 0;
    mutable std::vector<StaticStudioVertex> staticVertices;
    mutable int staticBoneCount = 0;
    mutable int staticNormalCount = 0;
    mutable bool staticGeometryReady = false;
    mutable std::vector<std::uint8_t> staticBonePalette;
    mutable std::vector<std::uint16_t> staticLightPalette;
    mutable unsigned staticVertexBuffer = 0;
    mutable std::uint32_t staticVertexBufferGeneration = 0;
    mutable std::uint32_t staticVertexBufferValidatedGeneration = 0;
    mutable bool drawRangeValidated = false;
};

struct TopologyCacheKey
{
    std::uintptr_t stream = 0;
    std::uintptr_t mesh = 0;
    std::uintptr_t studioModel = 0;

    bool operator==(const TopologyCacheKey& other) const noexcept
    {
        return stream == other.stream && mesh == other.mesh &&
               studioModel == other.studioModel;
    }
};

struct TopologyCacheKeyHash
{
    std::size_t operator()(const TopologyCacheKey& key) const noexcept
    {
        const std::size_t a = static_cast<std::size_t>(key.stream);
        const std::size_t b = static_cast<std::size_t>(key.mesh);
        const std::size_t c = static_cast<std::size_t>(key.studioModel);
        const std::size_t ab =
            a ^ (b + static_cast<std::size_t>(0x9E3779B9u) +
                 (a << 6) + (a >> 2));
        return ab ^ (c + static_cast<std::size_t>(0x85EBCA6Bu) +
                     (ab << 6) + (ab >> 2));
    }
};

std::uint8_t* g_clientBase = nullptr;
std::uint8_t* g_hwBase = nullptr;
cvar_t* g_mode = nullptr;
cvar_t* g_predecodeMode = nullptr;
cvar_t* g_indexBufferMode = nullptr;
cvar_t* g_drawRangeMode = nullptr;
cvar_t* g_vertexBufferMode = nullptr;
cvar_t* g_gpuMode = nullptr;
StudioDrawPointsFn g_originalDrawPoints = nullptr;
void* g_cacheFreeOriginal = nullptr;
std::atomic<std::uint32_t> g_knownModelGenerations[kKnownModelMax];
bool g_modelGenerationReady = false;
std::atomic<std::uint32_t> g_studioGenerationBumps{0u};

GlTexCoord2fFn g_texCoord2f = nullptr;
GlVertex3fFn g_vertex3f = nullptr;
GlColor4fFn g_color4f = nullptr;
GlDrawElementsFn g_drawElements = nullptr;
GlDrawArraysFn g_drawArrays = nullptr;
GlDrawRangeElementsFn g_drawRangeElements = nullptr;
std::uint32_t g_drawRangeGeneration = 0;
GlEnableClientStateFn g_enableClientState = nullptr;
GlVertexPointerFn g_vertexPointer = nullptr;
GlTexCoordPointerFn g_texCoordPointer = nullptr;
GlColorPointerFn g_colorPointer = nullptr;
GlDisableClientStateFn g_disableClientState = nullptr;
GlPushClientAttribFn g_pushClientAttrib = nullptr;
GlPopClientAttribFn g_popClientAttrib = nullptr;
GlGetIntegervFn g_getIntegerv = nullptr;
GlActiveTextureFn g_activeTexture = nullptr;
GlClientActiveTextureFn g_clientActiveTexture = nullptr;
GlBindBufferFn g_bindBuffer = nullptr;
GlDeleteBuffersFn g_deleteBuffers = nullptr;
GlGenBuffersFn g_genBuffers = nullptr;
GlBufferDataFn g_bufferData = nullptr;
GlGetBufferSubDataFn g_getBufferSubData = nullptr;
GlGetBufferParameterivFn g_getBufferParameteriv = nullptr;
std::uint32_t g_getBufferSubDataGeneration = 0;
GlIsEnabledFn g_isEnabled = nullptr;
GlGetFloatvFn g_getFloatv = nullptr;
GlCreateShaderFn g_createShader = nullptr;
GlShaderSourceFn g_shaderSource = nullptr;
GlCompileShaderFn g_compileShader = nullptr;
GlGetShaderivFn g_getShaderiv = nullptr;
GlGetShaderInfoLogFn g_getShaderInfoLog = nullptr;
GlDeleteShaderFn g_deleteShader = nullptr;
GlCreateProgramFn g_createProgram = nullptr;
GlAttachShaderFn g_attachShader = nullptr;
GlBindAttribLocationFn g_bindAttribLocation = nullptr;
GlLinkProgramFn g_linkProgram = nullptr;
GlGetProgramivFn g_getProgramiv = nullptr;
GlGetProgramInfoLogFn g_getProgramInfoLog = nullptr;
GlUseProgramFn g_useProgram = nullptr;
GlDeleteProgramFn g_deleteProgram = nullptr;
GlGetUniformLocationFn g_getUniformLocation = nullptr;
GlUniform4fvFn g_uniform4fv = nullptr;
GlVertexAttribPointerFn g_vertexAttribPointer = nullptr;
GlEnableVertexAttribArrayFn g_enableVertexAttribArray = nullptr;
GlDisableVertexAttribArrayFn g_disableVertexAttribArray = nullptr;
GlGetVertexAttribivFn g_getVertexAttribiv = nullptr;
void* g_emitStandard = nullptr;
void* g_emitAltUv = nullptr;
void* g_emitChrome = nullptr;
void* g_lightLambert = nullptr;

DWORD g_scopeThread = 0;
LONG g_scopeDepth = 0;
int g_activeMode = 0;
int g_scopePredecodeMode = 0;
int g_scopeIndexBufferMode = 0;
int g_scopeDrawRangeMode = 0;
int g_scopeVertexBufferMode = 0;
int g_scopeGpuMode = 0;
bool g_capturing = false;
bool g_passthrough = false;
bool g_haveTex = false;
bool g_haveColor = false;
bool g_exactPattern = true;
bool g_arrayScopeActive = false;
bool g_arrayScopeRejected = false;
const void* g_arrayPointerBase = nullptr;
const void* g_arrayColorPointerBase = nullptr;
int g_arrayPointerStride = 0;
int g_arrayColorPointerStride = 0;
int g_arrayTexCoordSize = 0;
bool g_arrayColorEnabled = false;
bool g_arrayUsingVertexVbo = false;
bool g_emitterActive = false;
bool g_emitterFailed = false;
bool g_effectiveColorKnown = false;
unsigned g_primitiveMode = 0;
float g_pendingTex[2]{};
float g_pendingColor[4]{};
float g_effectiveColor[4]{1.0f, 1.0f, 1.0f, 1.0f};
float g_lastTex[2]{};
bool g_deferredTexValid = false;
bool g_deferredColorValid = false;
float g_deferredTex[2]{};
float g_deferredColor[4]{};
std::vector<Vertex> g_vertices;
std::vector<Vertex> g_emitterRawVertices;
std::vector<Vertex> g_emitterTriangles;
std::vector<std::uint32_t> g_emitterIndices;
std::vector<PrimitiveRange> g_emitterPrimitives;
std::vector<DrawVertex> g_predecodedVertices;
std::vector<Vertex> g_predecodeValidation;
std::vector<std::uint32_t> g_predecodeLightW;
std::vector<std::uint32_t> g_predecodeLightWGenerated;
std::vector<std::uint32_t> g_predecodeLightWGold;
std::vector<std::uint32_t> g_indexValidationScratch;
std::vector<StaticStudioVertex> g_staticVertexValidationScratch;
std::unordered_map<TopologyCacheKey, TopologyCacheEntry, TopologyCacheKeyHash> g_topologyCache;
const TopologyCacheEntry* g_predecodeValidationEntry = nullptr;
bool g_predecodeValidationPending = false;
bool g_predecodeValidateLightW = false;

std::uint64_t g_drawPointsCalls = 0;
std::uint64_t g_primitives = 0;
std::uint64_t g_batched = 0;
std::uint64_t g_replayed = 0;
std::uint64_t g_fallback = 0;
std::uint64_t g_verticesBatched = 0;
std::uint64_t g_verticesCaptured = 0;
std::uint64_t g_exactPrimitives = 0;
std::uint64_t g_meshBatches = 0;
std::uint64_t g_meshReplays = 0;
std::uint64_t g_predecodeBuilds = 0;
std::uint64_t g_predecodeHits = 0;
std::uint64_t g_predecodeGenerationHits = 0;
std::uint64_t g_predecodeByteChecks = 0;
std::uint64_t g_topologyOwnerResolved = 0;
std::uint64_t g_topologyOwnerFallback = 0;
std::uint64_t g_predecodeFast = 0;
std::uint64_t g_predecodeFallback = 0;
std::uint64_t g_predecodeValidate = 0;
std::uint64_t g_predecodeMismatch = 0;
std::uint64_t g_predecodeStdAttempts = 0;
std::uint64_t g_predecodeAltAttempts = 0;
std::uint64_t g_predecodeChromeAttempts = 0;
std::uint64_t g_predecodeStdFast = 0;
std::uint64_t g_predecodeAltFast = 0;
std::uint64_t g_predecodeChromeFast = 0;
std::uint64_t g_predecodeStdValidate = 0;
std::uint64_t g_predecodeAltValidate = 0;
std::uint64_t g_predecodeChromeValidate = 0;
std::uint64_t g_predecodeChromeForcedFallback = 0;
std::uint64_t g_predecodeChromeForcedFast = 0;
std::uint64_t g_predecodeChromeForcedValidate = 0;
int g_predecodeValidationKind = 0; // 1=standard, 2=altUV, 3=regular chrome, 4=forced chrome
std::uint64_t g_lambertCalls = 0;
std::uint64_t g_lambertSkipped = 0;
std::uint64_t g_lambertZeroBypass = 0;
std::uint64_t g_cornerInputs = 0;
std::uint64_t g_uniqueCornerOutputs = 0;
std::uint64_t g_indexBufferBuilds = 0;
std::uint64_t g_indexBufferHits = 0;
std::uint64_t g_indexBufferFallbacks = 0;
std::uint64_t g_indexBufferValidate = 0;
std::uint64_t g_indexBufferMismatch = 0;
std::uint64_t g_drawRangeCalls = 0;
std::uint64_t g_drawRangeFallbacks = 0;
std::uint64_t g_drawRangeValidate = 0;
std::uint64_t g_drawRangeMismatch = 0;
std::uint64_t g_gpuStaticBuilds = 0;
std::uint64_t g_gpuStaticHits = 0;
std::uint64_t g_gpuStaticValidate = 0;
std::uint64_t g_gpuStaticMismatch = 0;
std::uint64_t g_gpuStaticFallback = 0;
bool g_phaseSampleActive = false;
bool g_collectStats = false;
std::uint64_t g_phaseMeshes = 0;
std::uint64_t g_phaseMixed = 0;
std::uint64_t g_phaseCorners = 0;
std::uint64_t g_phaseIndices = 0;
std::uint64_t g_phaseTopologyTicks = 0;
std::uint64_t g_phaseGenerateTicks = 0;
std::uint64_t g_phaseSubmitTicks = 0;
std::uint64_t g_phaseSubmitSetupTicks = 0;
std::uint64_t g_phaseSubmitDrawTicks = 0;
std::uint64_t g_phaseSubmitRestoreTicks = 0;
bool g_predecodeMismatchLogged = false;
std::uint64_t g_abSequence = 0;
std::uint64_t g_abStockTicks = 0;
std::uint64_t g_abBatchTicks = 0;
std::uint64_t g_abStockCalls = 0;
std::uint64_t g_abBatchCalls = 0;
volatile LONG g_retainedRendererActive = 0;
volatile LONG g_retainedDirectBridgeActive = 0;
void* g_retainedDirectBridgeCaller = nullptr;
int g_previousArrayBuffer = 0;
int g_previousElementArrayBuffer = 0;
int g_previousClientTexture = static_cast<int>(GL_TEXTURE0);
unsigned g_studioVertexBuffer = 0;
std::uint32_t g_studioVertexBufferGeneration = 0;
unsigned g_gpuProgram = 0;
std::uint32_t g_gpuProgramGeneration = 0;
std::uint32_t g_gpuFunctionsGeneration = 0;
int g_gpuMaxRows = 0;
int g_gpuDataLocation = -1;
int g_gpuParamsLocation = -1;
bool g_gpuScopeActive = false;
int g_gpuPreviousArrayBuffer = 0;
int g_gpuPreviousElementArrayBuffer = 0;
bool g_gpuArrayScopeActive = false;
unsigned g_gpuStaticArrayBuffer = 0;
unsigned g_gpuElementBuffer = 0;
std::vector<float> g_gpuUniformRows;
std::uint64_t g_gpuAttempts = 0;
std::uint64_t g_gpuDraws = 0;
std::uint64_t g_gpuFallbacks = 0;
std::uint64_t g_gpuFailStatic = 0;
std::uint64_t g_gpuFailIndex = 0;
std::uint64_t g_gpuFailProgram = 0;
std::uint64_t g_gpuFailLimits = 0;
std::uint64_t g_gpuFailLights = 0;
std::uint64_t g_gpuFailDraw = 0;
bool g_slotWarned = false;
bool g_emitHooksReady = false;

void StudioDrawPoints_Hook();
void EmitStandard_Hook();
void EmitAltUv_Hook();
void EmitChrome_Hook();
void __cdecl BeginEmitterCapture();
void __cdecl EndEmitterCapture();
void EndGpuArrayScope();
void EndGpuProgramScope();
void EndArrayScopeImmediate();
void QueueDeferredCurrentState(const DrawVertex& last, bool useColor);
float ScaleStudioCoord(float raw, float scale);
int ReadNumStudioLightsSafe();

bool ClientMatches(HMODULE client)
{
    if (!client) return false;
    __try
    {
        auto* base = reinterpret_cast<std::uint8_t*>(client);
        auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
        auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
        return nt->Signature == IMAGE_NT_SIGNATURE &&
               nt->FileHeader.TimeDateStamp == kClientTimestamp &&
               nt->OptionalHeader.SizeOfImage == kClientImageSize;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool PatchPointer(void** slot, void* expected, void* replacement)
{
    if (!slot || !expected || !replacement) return false;
    __try
    {
        if (*slot != expected) return false;
        DWORD oldProtect = 0;
        if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProtect)) return false;
        *slot = replacement;
        DWORD ignored = 0;
        VirtualProtect(slot, sizeof(void*), oldProtect, &ignored);
        FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool PatchRelativeCall(std::uint8_t* site, void* expectedTarget, void* replacement)
{
    if (!site || !expectedTarget || !replacement) return false;
    __try
    {
        if (site[0] != 0xE8) return false;
        std::int32_t oldDisp = 0;
        std::memcpy(&oldDisp, site + 1, sizeof(oldDisp));
        if (site + 5 + oldDisp != expectedTarget) return false;
        const std::intptr_t delta = reinterpret_cast<std::intptr_t>(replacement) -
                                    reinterpret_cast<std::intptr_t>(site + 5);
        if (delta < static_cast<std::intptr_t>(-2147483647 - 1) ||
            delta > static_cast<std::intptr_t>(2147483647))
            return false;
        const std::int32_t newDisp = static_cast<std::int32_t>(delta);
        DWORD oldProtect = 0;
        if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
        std::memcpy(site + 1, &newDisp, sizeof(newDisp));
        DWORD ignored = 0;
        VirtualProtect(site, 5, oldProtect, &ignored);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

template <typename Fn>
Fn ReadQgl(std::uintptr_t rva)
{
    if (!g_hwBase) return nullptr;
    __try { return reinterpret_cast<Fn>(*reinterpret_cast<void**>(g_hwBase + rva)); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

bool RefreshGpuFunctions()
{
    if (!g_hwBase || !worldvbo::ContextGenerationReady())
        return false;
    const std::uint32_t generation = worldvbo::ContextGeneration();
    if (g_gpuFunctionsGeneration == generation &&
        g_createShader && g_shaderSource && g_compileShader && g_getShaderiv &&
        g_deleteShader && g_createProgram && g_attachShader &&
        g_bindAttribLocation && g_linkProgram && g_getProgramiv &&
        g_useProgram && g_deleteProgram && g_getUniformLocation &&
        g_uniform4fv && g_vertexAttribPointer && g_enableVertexAttribArray &&
        g_disableVertexAttribArray && g_getVertexAttribiv)
        return true;

    g_createShader = nullptr;
    g_shaderSource = nullptr;
    g_compileShader = nullptr;
    g_getShaderiv = nullptr;
    g_getShaderInfoLog = nullptr;
    g_deleteShader = nullptr;
    g_createProgram = nullptr;
    g_attachShader = nullptr;
    g_bindAttribLocation = nullptr;
    g_linkProgram = nullptr;
    g_getProgramiv = nullptr;
    g_getProgramInfoLog = nullptr;
    g_useProgram = nullptr;
    g_deleteProgram = nullptr;
    g_getUniformLocation = nullptr;
    g_uniform4fv = nullptr;
    g_vertexAttribPointer = nullptr;
    g_enableVertexAttribArray = nullptr;
    g_disableVertexAttribArray = nullptr;
    g_getVertexAttribiv = nullptr;

    __try
    {
        auto getProc = *reinterpret_cast<SdlGetProcAddressFn*>(
            g_hwBase + kSdlGetProcAddressIatRva);
        if (!getProc)
            return false;
#define LOAD_GPU_PROC(dst, type, name) dst = reinterpret_cast<type>(getProc(name))
        LOAD_GPU_PROC(g_createShader, GlCreateShaderFn, "glCreateShader");
        LOAD_GPU_PROC(g_shaderSource, GlShaderSourceFn, "glShaderSource");
        LOAD_GPU_PROC(g_compileShader, GlCompileShaderFn, "glCompileShader");
        LOAD_GPU_PROC(g_getShaderiv, GlGetShaderivFn, "glGetShaderiv");
        LOAD_GPU_PROC(g_getShaderInfoLog, GlGetShaderInfoLogFn, "glGetShaderInfoLog");
        LOAD_GPU_PROC(g_deleteShader, GlDeleteShaderFn, "glDeleteShader");
        LOAD_GPU_PROC(g_createProgram, GlCreateProgramFn, "glCreateProgram");
        LOAD_GPU_PROC(g_attachShader, GlAttachShaderFn, "glAttachShader");
        LOAD_GPU_PROC(g_bindAttribLocation, GlBindAttribLocationFn, "glBindAttribLocation");
        LOAD_GPU_PROC(g_linkProgram, GlLinkProgramFn, "glLinkProgram");
        LOAD_GPU_PROC(g_getProgramiv, GlGetProgramivFn, "glGetProgramiv");
        LOAD_GPU_PROC(g_getProgramInfoLog, GlGetProgramInfoLogFn, "glGetProgramInfoLog");
        LOAD_GPU_PROC(g_useProgram, GlUseProgramFn, "glUseProgram");
        LOAD_GPU_PROC(g_deleteProgram, GlDeleteProgramFn, "glDeleteProgram");
        LOAD_GPU_PROC(g_getUniformLocation, GlGetUniformLocationFn, "glGetUniformLocation");
        LOAD_GPU_PROC(g_uniform4fv, GlUniform4fvFn, "glUniform4fv");
        LOAD_GPU_PROC(g_vertexAttribPointer, GlVertexAttribPointerFn, "glVertexAttribPointer");
        LOAD_GPU_PROC(g_enableVertexAttribArray, GlEnableVertexAttribArrayFn, "glEnableVertexAttribArray");
        LOAD_GPU_PROC(g_disableVertexAttribArray, GlDisableVertexAttribArrayFn, "glDisableVertexAttribArray");
        LOAD_GPU_PROC(g_getVertexAttribiv, GlGetVertexAttribivFn, "glGetVertexAttribiv");
#undef LOAD_GPU_PROC
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }

    g_gpuFunctionsGeneration = generation;
    return g_createShader && g_shaderSource && g_compileShader && g_getShaderiv &&
           g_deleteShader && g_createProgram && g_attachShader &&
           g_bindAttribLocation && g_linkProgram && g_getProgramiv &&
           g_useProgram && g_deleteProgram && g_getUniformLocation &&
           g_uniform4fv && g_vertexAttribPointer && g_enableVertexAttribArray &&
           g_disableVertexAttribArray && g_getVertexAttribiv;
}

unsigned CompileGpuProgram(int maxRows)
{
    if (maxRows <= 0 || maxRows > 2048 || !RefreshGpuFunctions())
        return 0;

    char source[4096]{};
    const int written = std::snprintf(
        source, sizeof(source),
        "#version 120\n"
        "attribute vec3 aPosition;\n"
        "attribute vec2 aRawST;\n"
        "attribute vec2 aIndex;\n"
        "uniform vec4 uData[%d];\n"
        "uniform vec4 uParams;\n"
        "void main() {\n"
        "  int i = int(aIndex.x) * 3;\n"
        "  vec4 r0 = uData[i + 0];\n"
        "  vec4 r1 = uData[i + 1];\n"
        "  vec4 r2 = uData[i + 2];\n"
        "  vec3 p;\n"
        "  p.x = ((aPosition.y * r0.y + aPosition.x * r0.x) + aPosition.z * r0.z) + r0.w;\n"
        "  p.y = ((aPosition.y * r1.y + aPosition.x * r1.x) + aPosition.z * r1.z) + r1.w;\n"
        "  p.z = ((aPosition.y * r2.y + aPosition.x * r2.x) + aPosition.z * r2.z) + r2.w;\n"
        "  gl_Position = gl_ModelViewProjectionMatrix * vec4(p, 1.0);\n"
        "  vec2 st = aRawST * uParams.xy;\n"
        "  gl_TexCoord[0] = gl_TextureMatrix[0] * vec4(st, 0.0, 1.0);\n"
        "  vec4 light = uData[int(uParams.w) + int(aIndex.y)];\n"
        "  vec4 c = vec4(light.rgb, uParams.z);\n"
        "  gl_FrontColor = c;\n"
        "  gl_BackColor = c;\n"
        "}\n",
        maxRows);
    if (written <= 0 || written >= static_cast<int>(sizeof(source)))
        return 0;

    unsigned shader = 0;
    unsigned program = 0;
    __try
    {
        shader = g_createShader(GL_VERTEX_SHADER);
        if (!shader)
            return 0;
        const char* sourcePtr = source;
        g_shaderSource(shader, 1, &sourcePtr, nullptr);
        g_compileShader(shader);
        int compiled = 0;
        g_getShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (!compiled)
        {
            if (g_getShaderInfoLog)
            {
                char info[1024]{};
                int length = 0;
                g_getShaderInfoLog(shader, static_cast<int>(sizeof(info) - 1), &length, info);
                rendererlog::Line("studio gpu: vertex shader compile failed: %s", info);
            }
            g_deleteShader(shader);
            return 0;
        }

        program = g_createProgram();
        if (!program)
        {
            g_deleteShader(shader);
            return 0;
        }
        g_attachShader(program, shader);
        g_bindAttribLocation(program, kGpuAttribPosition, "aPosition");
        g_bindAttribLocation(program, kGpuAttribTexCoord, "aRawST");
        g_bindAttribLocation(program, kGpuAttribBone, "aIndex");
        g_linkProgram(program);
        int linked = 0;
        g_getProgramiv(program, GL_LINK_STATUS, &linked);
        g_deleteShader(shader);
        shader = 0;
        if (!linked)
        {
            if (g_getProgramInfoLog)
            {
                char info[1024]{};
                int length = 0;
                g_getProgramInfoLog(program, static_cast<int>(sizeof(info) - 1), &length, info);
                rendererlog::Line("studio gpu: program link failed: %s", info);
            }
            g_deleteProgram(program);
            return 0;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        if (shader && g_deleteShader)
        {
            __try { g_deleteShader(shader); }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        if (program && g_deleteProgram)
        {
            __try { g_deleteProgram(program); }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        return 0;
    }
    return program;
}

bool EnsureGpuProgram()
{
    if (!g_hwBase || !g_getIntegerv || !worldvbo::ContextGenerationReady() ||
        !RefreshGpuFunctions())
        return false;
    const std::uint32_t generation = worldvbo::ContextGeneration();
    if (g_gpuProgramGeneration != generation)
    {
        // Program names belong to the context that created them.
        g_gpuProgram = 0;
        g_gpuProgramGeneration = generation;
        g_gpuMaxRows = 0;
        g_gpuDataLocation = -1;
        g_gpuParamsLocation = -1;
        g_gpuScopeActive = false;
    }
    if (g_gpuProgram)
        return g_gpuMaxRows > 0 &&
               g_gpuDataLocation >= 0 && g_gpuParamsLocation >= 0;

    int uniformComponents = 0;
    __try { g_getIntegerv(GL_MAX_VERTEX_UNIFORM_COMPONENTS, &uniformComponents); }
    __except (EXCEPTION_EXECUTE_HANDLER) { uniformComponents = 0; }
    if (uniformComponents <= 0)
        return false;

    // Keep one query-sized vec4 table for both compact bone rows and Gold's
    // per-normal colors.  A mesh is eligible iff 3*usedBones+usedNormals fits,
    // unlike split arrays, unused bone capacity never steals light capacity.
    const int maxVec4 = uniformComponents / 4;
    constexpr int kUniformReserveVec4 = 9; // built-ins/driver headroom + uParams
    const int maxRows = maxVec4 - kUniformReserveVec4;
    if (maxRows < 32)
        return false;

    const unsigned program = CompileGpuProgram(maxRows);
    if (!program)
        return false;
    int dataLocation = -1;
    int paramsLocation = -1;
    __try
    {
        dataLocation = g_getUniformLocation(program, "uData");
        paramsLocation = g_getUniformLocation(program, "uParams");
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        dataLocation = -1;
        paramsLocation = -1;
    }
    if (dataLocation < 0 || paramsLocation < 0)
    {
        __try { g_deleteProgram(program); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        return false;
    }

    g_gpuProgram = program;
    g_gpuMaxRows = maxRows;
    g_gpuDataLocation = dataLocation;
    g_gpuParamsLocation = paramsLocation;
    rendererlog::Line(
        "studio gpu: static program ready (uniformComponents=%d rows=%d)",
        uniformComponents, maxRows);
    return true;
}

bool ScopeActive()
{
    return g_scopeDepth > 0 && g_scopeThread == GetCurrentThreadId() && g_activeMode != 0;
}

float FloatFromBits(std::uint32_t bits)
{
    float out = 0.0f;
    std::memcpy(&out, &bits, sizeof(out));
    return out;
}

std::uint32_t GoldAltUvBits(std::uint16_t raw)
{
    // Exact integer expansion used by Gold hw+0x9AA20.  This is deliberately
    // NOT IEEE half conversion: exponent=31 is treated as a large finite value.
    std::uint32_t eax = raw & 0x7FFFu;
    std::uint32_t edx = (static_cast<std::uint32_t>(raw) & 0x8000u) << 16;
    if (eax > 0x3FFu)
    {
        eax += 0x1C000u;
        eax <<= 13;
        return edx | eax;
    }

    std::uint32_t ecx = eax & 0x3FFu;
    if (ecx == 0)
        return edx;

    std::int32_t exp = static_cast<std::int32_t>(eax >> 10);
    do
    {
        ecx += ecx;
        --exp;
    } while ((ecx & 0x400u) == 0);
    ecx &= 0x3FFu;
    exp += 0x71;
    ecx <<= 13;
    const std::uint32_t expBits = static_cast<std::uint32_t>(exp) << 23;
    return edx | ecx | expBits;
}

bool ReadStudioHeader(const std::int16_t* stream,
                      const std::uint8_t*& header,
                      std::uint32_t& length,
                      std::uint32_t& id,
                      std::uint32_t& version,
                      char (&name)[64])
{
    if (!g_hwBase || !stream)
        return false;
    __try
    {
        header = *reinterpret_cast<const std::uint8_t* const*>(g_hwBase + kStudioHdrPtrRva);
        if (!header)
            return false;
        id = *reinterpret_cast<const std::uint32_t*>(header + 0x00);
        version = *reinterpret_cast<const std::uint32_t*>(header + 0x04);
        std::memcpy(name, header + 0x08, sizeof(name));
        length = *reinterpret_cast<const std::uint32_t*>(header + 0x48);
        if (length < 0x4Cu || length > 128u * 1024u * 1024u)
            return false;
        const auto* begin = header;
        const auto* end = header + length;
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(stream);
        return bytes >= begin && bytes + sizeof(std::int16_t) <= end;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool ReadMeshInfo(const std::uint8_t* mesh,
                  std::int32_t& numtris,
                  std::int32_t& triindex)
{
    if (!mesh)
        return false;
    __try
    {
        numtris = *reinterpret_cast<const std::int32_t*>(mesh + 0x00);
        triindex = *reinterpret_cast<const std::int32_t*>(mesh + 0x04);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool ResolveStudioModelOwner(const std::uint8_t* header,
                             const std::uint8_t*& modelOut,
                             int& indexOut,
                             std::uint32_t& generationOut)
{
    modelOut = nullptr;
    indexOut = -1;
    generationOut = 0;
    if (!header || !g_hwBase)
        return false;
    __try
    {
        const int count = *reinterpret_cast<const int*>(g_hwBase + kKnownModelCountRva);
        if (count < 0 || count > kKnownModelMax)
            return false;
        const auto* known = reinterpret_cast<const std::uint8_t* const*>(
            g_hwBase + kKnownModelTableRva);
        for (int i = 0; i < count; ++i)
        {
            const auto* model = known[i];
            if (!model || *reinterpret_cast<const std::int32_t*>(model + 0x44) != 3)
                continue;
            const auto* cacheData = *reinterpret_cast<const std::uint8_t* const*>(model + 0x184);
            if (cacheData != header)
                continue;
            modelOut = model;
            indexOut = i;
            generationOut = g_knownModelGenerations[i].load(std::memory_order_relaxed);
            return true;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
    return false;
}

void __cdecl OnCacheFree(void* cacheUser)
{
    if (!cacheUser || !g_hwBase)
        return;
    __try
    {
        // Exact Gold model_t layout: cache_user_t lives at +0x184 and model
        // type at +0x44.  Prove the candidate is one of Gold's known models
        // before reading the type, so unrelated sound/WAD cache churn cannot
        // accidentally invalidate Studio topology.
        const auto* model = static_cast<const std::uint8_t*>(cacheUser) - 0x184;
        const int count = *reinterpret_cast<const int*>(g_hwBase + kKnownModelCountRva);
        if (count < 0 || count > kKnownModelMax)
            return;
        const auto* known = reinterpret_cast<const std::uint8_t* const*>(
            g_hwBase + kKnownModelTableRva);
        for (int i = 0; i < count; ++i)
        {
            if (known[i] != model)
                continue;
            if (*reinterpret_cast<const std::int32_t*>(model + 0x44) == 3)
            {
                const std::uint32_t previous =
                    g_knownModelGenerations[i].fetch_add(1u, std::memory_order_relaxed);
                if (previous == 0xFFFFFFFFu)
                    g_knownModelGenerations[i].store(1u, std::memory_order_relaxed);
                g_studioGenerationBumps.fetch_add(1u, std::memory_order_relaxed);
            }
            return;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

__declspec(naked) void CacheFree_Hook()
{
    __asm
    {
        // Gold Cache_Free is __fastcall(cache_user_t* ECX). Preserve the
        // complete incoming machine state before observing the owner type.
        pushfd
        pushad
        push ecx
        call OnCacheFree
        add esp, 4
        popad
        popfd
        jmp dword ptr [g_cacheFreeOriginal]
    }
}

bool InstallModelGenerationHook()
{
    if (!g_hwBase)
        return false;
    static const std::uint8_t expected[] = {0x56, 0x8B, 0xF1, 0x8B, 0x16};
    auto* target = g_hwBase + kCacheFreeRva;
    __try
    {
        if (std::memcmp(target, expected, sizeof(expected)) != 0)
            return false;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }

    g_cacheFreeOriginal = inl::Hook(target, reinterpret_cast<void*>(&CacheFree_Hook));
    return g_cacheFreeOriginal != nullptr;
}

void AppendCachedTriangles(TopologyCacheEntry& out,
                           unsigned mode,
                           std::uint32_t first,
                           std::uint32_t count)
{
    if (count < 3)
        return;
    if (mode == GL_TRIANGLE_FAN)
    {
        for (std::uint32_t i = 1; i + 1 < count; ++i)
        {
            out.triangleIndices.push_back(first);
            out.triangleIndices.push_back(first + i);
            out.triangleIndices.push_back(first + i + 1);
        }
    }
    else
    {
        for (std::uint32_t i = 0; i + 2 < count; ++i)
        {
            if ((i & 1u) == 0)
            {
                out.triangleIndices.push_back(first + i);
                out.triangleIndices.push_back(first + i + 1);
                out.triangleIndices.push_back(first + i + 2);
            }
            else
            {
                out.triangleIndices.push_back(first + i + 1);
                out.triangleIndices.push_back(first + i);
                out.triangleIndices.push_back(first + i + 2);
            }
        }
    }
}

const StudioModelRaw* ReadCurrentStudioModelSeh()
{
    if (!g_hwBase)
        return nullptr;
    __try
    {
        return *reinterpret_cast<const StudioModelRaw* const*>(
            g_hwBase + kCurrentStudioModelRva);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
}

bool ValidateStudioModelForMeshSeh(const std::uint8_t* header,
                                   std::uint32_t headerLength,
                                   const StudioModelRaw* model,
                                   const std::uint8_t* mesh)
{
    if (!header || !model || !mesh ||
        headerLength < sizeof(StudioHeaderRaw))
        return false;
    const auto* end = header + headerLength;
    const auto* modelBytes = reinterpret_cast<const std::uint8_t*>(model);
    if (modelBytes < header ||
        static_cast<std::size_t>(end - modelBytes) < sizeof(StudioModelRaw))
        return false;

    __try
    {
        if (model->nummesh <= 0 || model->nummesh > 4096 ||
            model->numverts <= 0 || model->numverts > 0x4000 ||
            model->numnorms <= 0 || model->numnorms > 0x4000 ||
            model->meshindex < 0)
            return false;
        const std::size_t meshBytes =
            static_cast<std::size_t>(model->nummesh) * 20u;
        const std::size_t meshOffset =
            static_cast<std::size_t>(model->meshindex);
        if (meshOffset > headerLength ||
            meshBytes > static_cast<std::size_t>(headerLength) - meshOffset)
            return false;
        const auto* meshBegin = header + meshOffset;
        const auto* meshEnd = meshBegin + meshBytes;
        return mesh >= meshBegin && mesh < meshEnd &&
               ((mesh - meshBegin) % 20u) == 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool BuildTopologyCache(const std::int16_t* stream,
                        const std::uint8_t* mesh,
                        const StudioModelRaw* studioModel,
                        TopologyCacheEntry& out)
{
    const std::uint8_t* header = nullptr;
    std::uint32_t headerLength = 0;
    std::uint32_t headerId = 0;
    std::uint32_t headerVersion = 0;
    char headerName[64]{};
    if (!ReadStudioHeader(stream, header, headerLength, headerId, headerVersion, headerName))
        return false;

    const auto* end = header + headerLength;
    if (!mesh || mesh < header || mesh + 20 > end)
        return false;
    if (!ValidateStudioModelForMeshSeh(header, headerLength, studioModel, mesh))
        return false;
    std::int32_t expectedNumTris = 0;
    std::int32_t triindex = 0;
    if (!ReadMeshInfo(mesh, expectedNumTris, triindex))
        return false;
    if (expectedNumTris < 0 || triindex < 0 || header + triindex != reinterpret_cast<const std::uint8_t*>(stream))
        return false;

    const auto* p = stream;
    const auto* startBytes = reinterpret_cast<const std::uint8_t*>(stream);
    std::uint64_t decodedTris = 0;
    try
    {
        out = {};
        out.header = header;
        out.stream = stream;
        out.mesh = mesh;
        out.studioModel = studioModel;
        out.headerLength = headerLength;
        out.headerId = headerId;
        out.headerVersion = headerVersion;
        std::memcpy(out.headerName, headerName, sizeof(out.headerName));
        out.expectedNumTris = expectedNumTris;
        if (ResolveStudioModelOwner(header, out.ownerModel, out.ownerModelIndex,
                                    out.modelGeneration))
            ++g_topologyOwnerResolved;
        else
            ++g_topologyOwnerFallback;
        out.corners.reserve(256);
        out.uniqueCorners.reserve(256);
        out.uniqueCornerIndices.reserve(256);
        out.lightFirstRenderSlots.reserve(256);
        out.primitives.reserve(32);
        out.triangleIndices.reserve(768);
        out.renderTriangleIndices.reserve(768);
        std::unordered_map<std::uint32_t, std::uint16_t> lightSlots;
        lightSlots.reserve(256);
        std::unordered_map<std::uint64_t, std::uint32_t> renderSlots;
        renderSlots.reserve(256);

        for (;;)
        {
            if (reinterpret_cast<const std::uint8_t*>(p) + sizeof(std::int16_t) > end)
                return false;
            const std::int32_t signedCount = *p++;
            if (signedCount == 0)
                break;
            if (signedCount == -32768)
                return false;

            const unsigned mode = signedCount < 0 ? GL_TRIANGLE_FAN : GL_TRIANGLE_STRIP;
            const std::uint32_t count = static_cast<std::uint32_t>(signedCount < 0 ? -signedCount : signedCount);
            if (count == 0 || count > 32767u)
                return false;
            const std::size_t bytesNeeded = static_cast<std::size_t>(count) * 4u * sizeof(std::int16_t);
            if (reinterpret_cast<const std::uint8_t*>(p) + bytesNeeded > end)
                return false;

            TopologyPrimitive prim{};
            prim.mode = mode;
            prim.first = static_cast<std::uint32_t>(out.corners.size());
            prim.count = count;
            out.primitives.push_back(prim);
            if (count >= 3)
                decodedTris += static_cast<std::uint64_t>(count - 2u);

            for (std::uint32_t i = 0; i < count; ++i, p += 4)
            {
                const std::int32_t vert = p[0];
                const std::int32_t norm = p[1];
                // Exact Gold emitters treat negative or >0x3FFF indices as
                // malformed. Do not optimize such a stream, stock stays oracle.
                if (vert < 0 || vert > 0x3FFF || norm < 0 || norm > 0x3FFF)
                    return false;
                CachedCorner c{};
                c.vert = static_cast<std::uint16_t>(vert);
                c.norm = static_cast<std::uint16_t>(norm);
                c.rawS = p[2];
                c.rawT = p[3];
                // Gold converts these signed 16-bit texture coordinates with
                // CVTSI2SS on every emitted corner. Every int16 value is exactly
                // representable as float, so cache that exact conversion once
                // with the immutable topology and keep only Gold's MULSS in the
                // dynamic draw path. The skipped exact conversion cannot raise
                // an SSE precision/overflow/underflow exception.
                c.studioS = static_cast<float>(c.rawS);
                c.studioT = static_cast<float>(c.rawT);
                const std::uint64_t renderKey =
                    (static_cast<std::uint64_t>(c.vert) << 48) |
                    (static_cast<std::uint64_t>(c.norm) << 32) |
                    (static_cast<std::uint64_t>(static_cast<std::uint16_t>(c.rawS)) << 16) |
                    static_cast<std::uint64_t>(static_cast<std::uint16_t>(c.rawT));
                const auto renderIt = renderSlots.find(renderKey);
                const bool uniqueRenderCorner = renderIt == renderSlots.end();
                if (renderIt == renderSlots.end())
                {
                    if (out.uniqueCornerIndices.size() >= static_cast<std::size_t>(UINT32_MAX))
                        return false;
                    c.renderSlot = static_cast<std::uint32_t>(out.uniqueCornerIndices.size());
                    if (out.corners.size() >= static_cast<std::size_t>(UINT32_MAX))
                        return false;
                    out.uniqueCornerIndices.push_back(static_cast<std::uint32_t>(out.corners.size()));
                    renderSlots.emplace(renderKey, c.renderSlot);
                }
                else
                {
                    c.renderSlot = renderIt->second;
                }
                const std::uint32_t lightKey =
                    (static_cast<std::uint32_t>(c.vert) << 16) |
                    static_cast<std::uint32_t>(c.norm);
                const auto lightIt = lightSlots.find(lightKey);
                if (lightIt == lightSlots.end())
                {
                    if (out.lightPairCount >= 32768u)
                        return false;
                    c.lightSlot = static_cast<std::uint16_t>(out.lightPairCount++);
                    c.lightFirst = true;
                    out.lightFirstRenderSlots.push_back(c.renderSlot);
                    lightSlots.emplace(lightKey, c.lightSlot);
                }
                else
                {
                    c.lightSlot = lightIt->second;
                    c.lightFirst = false;
                }
                c.altS = FloatFromBits(GoldAltUvBits(static_cast<std::uint16_t>(p[2])));
                c.altT = FloatFromBits(GoldAltUvBits(static_cast<std::uint16_t>(p[3])));
                out.corners.push_back(c);
                if (uniqueRenderCorner)
                    out.uniqueCorners.push_back(c);
            }
            AppendCachedTriangles(out, mode, prim.first, prim.count);
        }

        out.streamBytes = static_cast<std::size_t>(reinterpret_cast<const std::uint8_t*>(p) - startBytes);
        if (out.streamBytes < sizeof(std::int16_t) || out.streamBytes > headerLength)
            return false;
        if (decodedTris != static_cast<std::uint64_t>(expectedNumTris))
            return false;
        out.renderTriangleIndices.resize(out.triangleIndices.size());
        out.renderMinIndex = UINT32_MAX;
        out.renderMaxIndex = 0;
        for (std::size_t i = 0; i < out.triangleIndices.size(); ++i)
        {
            const std::uint32_t cornerIndex = out.triangleIndices[i];
            if (cornerIndex >= out.corners.size())
                return false;
            const std::uint32_t slot = out.corners[cornerIndex].renderSlot;
            if (slot >= out.uniqueCornerIndices.size())
                return false;
            out.renderTriangleIndices[i] = slot;
            if (slot < out.renderMinIndex) out.renderMinIndex = slot;
            if (slot > out.renderMaxIndex) out.renderMaxIndex = slot;
        }
        out.rawStream.assign(startBytes, startBytes + out.streamBytes);
        const std::size_t headN = out.streamBytes < sizeof(out.streamHead) ? out.streamBytes : sizeof(out.streamHead);
        const std::size_t tailN = out.streamBytes < sizeof(out.streamTail) ? out.streamBytes : sizeof(out.streamTail);
        std::memcpy(out.streamHead, startBytes, headN);
        std::memcpy(out.streamTail, startBytes + out.streamBytes - tailN, tailN);
        return !out.corners.empty() && !out.uniqueCornerIndices.empty() &&
               out.uniqueCorners.size() == out.uniqueCornerIndices.size() &&
               out.lightFirstRenderSlots.size() == out.lightPairCount &&
               !out.triangleIndices.empty() &&
               out.renderTriangleIndices.size() == out.triangleIndices.size();
    }
    catch (...)
    {
        return false;
    }
}

bool CacheEntryStillExact(const TopologyCacheEntry& e,
                          const std::int16_t* stream,
                          const std::uint8_t* mesh,
                          bool strictByteCompare)
{
    if (ReadCurrentStudioModelSeh() != e.studioModel)
        return false;
    bool generationExact = false;
    if (g_modelGenerationReady && e.ownerModel &&
        e.ownerModelIndex >= 0 && e.ownerModelIndex < kKnownModelMax)
    {
        __try
        {
            const int count = *reinterpret_cast<const int*>(g_hwBase + kKnownModelCountRva);
            if (count < 0 || count > kKnownModelMax || e.ownerModelIndex >= count)
                return false;
            const auto* known = reinterpret_cast<const std::uint8_t* const*>(
                g_hwBase + kKnownModelTableRva);
            if (known[e.ownerModelIndex] != e.ownerModel ||
                *reinterpret_cast<const std::int32_t*>(e.ownerModel + 0x44) != 3 ||
                *reinterpret_cast<const std::uint8_t* const*>(e.ownerModel + 0x184) != e.header ||
                g_knownModelGenerations[e.ownerModelIndex].load(std::memory_order_relaxed) !=
                    e.modelGeneration)
                return false;
            generationExact = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
    if (generationExact && !strictByteCompare && e.stream == stream && e.mesh == mesh)
    {
        if (g_collectStats) ++g_predecodeGenerationHits;
        return true;
    }
    const std::uint8_t* header = nullptr;
    std::uint32_t length = 0;
    std::uint32_t id = 0;
    std::uint32_t version = 0;
    char name[64]{};
    if (!ReadStudioHeader(stream, header, length, id, version, name))
        return false;
    if (header != e.header || length != e.headerLength || id != e.headerId ||
        version != e.headerVersion || std::memcmp(name, e.headerName, sizeof(name)) != 0)
        return false;
    const auto* end = header + length;
    if (!mesh || mesh < header || mesh + 20 > end)
        return false;
    __try
    {
        const std::int32_t numtris = *reinterpret_cast<const std::int32_t*>(mesh + 0x00);
        const std::int32_t triindex = *reinterpret_cast<const std::int32_t*>(mesh + 0x04);
        if (numtris != e.expectedNumTris || triindex < 0 ||
            header + triindex != reinterpret_cast<const std::uint8_t*>(stream))
            return false;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(stream);
    if (e.streamBytes == 0 || bytes + e.streamBytes > end || e.rawStream.size() != e.streamBytes)
        return false;
    if (generationExact && !strictByteCompare)
    {
        if (g_collectStats) ++g_predecodeGenerationHits;
        return true;
    }
    __try
    {
        // Exact byte compare intentionally handles Gold cache-address reuse: a
        // recycled pointer cannot consume stale topology even if model names and
        // allocation addresses happen to repeat.
        if (g_collectStats) ++g_predecodeByteChecks;
        return std::memcmp(bytes, e.rawStream.data(), e.streamBytes) == 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

struct StaticStudioSource
{
    const float* positions = nullptr;
    const std::uint8_t* vertexBones = nullptr;
    int numVertices = 0;
    int numNormals = 0;
    int numBones = 0;
};

bool HeaderSpanValid(const TopologyCacheEntry& topology,
                     int offset,
                     std::size_t bytes)
{
    if (!topology.header || offset < 0)
        return false;
    const std::size_t uoffset = static_cast<std::size_t>(offset);
    return uoffset <= topology.headerLength &&
           bytes <= static_cast<std::size_t>(topology.headerLength) - uoffset;
}

bool ResolveStaticStudioSource(const TopologyCacheEntry& topology,
                               StaticStudioSource& source)
{
    source = {};
    if (!topology.header || !topology.mesh || !topology.studioModel ||
        topology.headerLength < sizeof(StudioHeaderRaw))
        return false;

    __try
    {
        const auto* header = reinterpret_cast<const StudioHeaderRaw*>(topology.header);
        if (header->id != 0x54534449 || header->version != 10 ||
            header->length != static_cast<int>(topology.headerLength) ||
            header->numbones <= 0 || header->numbones > 128)
            return false;
        if (!ValidateStudioModelForMeshSeh(topology.header,
                                           topology.headerLength,
                                           topology.studioModel,
                                           topology.mesh))
            return false;
        const StudioModelRaw& model = *topology.studioModel;
        const std::size_t vertexBytes =
            static_cast<std::size_t>(model.numverts) * 3u * sizeof(float);
        const std::size_t boneBytes = static_cast<std::size_t>(model.numverts);
        if (!HeaderSpanValid(topology, model.vertindex, vertexBytes) ||
            !HeaderSpanValid(topology, model.vertinfoindex, boneBytes))
            return false;

        source.positions = reinterpret_cast<const float*>(
            topology.header + model.vertindex);
        source.vertexBones = topology.header + model.vertinfoindex;
        source.numVertices = model.numverts;
        source.numNormals = model.numnorms;
        source.numBones = header->numbones;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
    return false;
}

bool FillStaticStudioGeometrySeh(const TopologyCacheEntry& topology,
                                 const StaticStudioSource& source,
                                 StaticStudioVertex* vertices,
                                 std::size_t vertexCount,
                                 std::uint8_t* bonePalette,
                                 int* bonePaletteCount,
                                 int* lightRemap,
                                 std::size_t lightRemapCount,
                                 std::uint16_t* lightPalette,
                                 int* lightPaletteCount)
{
    if (!vertices || !bonePalette || !bonePaletteCount ||
        !lightRemap || !lightPalette || !lightPaletteCount ||
        vertexCount != topology.uniqueCorners.size())
        return false;

    __try
    {
        std::uint8_t boneRemap[128];
        std::memset(boneRemap, 0xFF, sizeof(boneRemap));
        int usedBones = 0;
        int usedLights = 0;
        for (std::size_t i = 0; i < vertexCount; ++i)
        {
            const CachedCorner& corner = topology.uniqueCorners[i];
            if (corner.vert >= source.numVertices ||
                corner.norm >= source.numNormals ||
                corner.norm >= lightRemapCount)
                return false;

            const unsigned bone = source.vertexBones[corner.vert];
            if (bone >= static_cast<unsigned>(source.numBones))
                return false;
            if (boneRemap[bone] == 0xFFu)
            {
                if (usedBones >= 128)
                    return false;
                boneRemap[bone] = static_cast<std::uint8_t>(usedBones);
                bonePalette[usedBones++] = static_cast<std::uint8_t>(bone);
            }

            int lightSlot = lightRemap[corner.norm];
            if (lightSlot < 0)
            {
                if (usedLights >= source.numNormals)
                    return false;
                lightSlot = usedLights;
                lightRemap[corner.norm] = usedLights;
                lightPalette[usedLights++] = corner.norm;
            }

            StaticStudioVertex& vertex = vertices[i];
            const float* position =
                source.positions + static_cast<std::size_t>(corner.vert) * 3u;
            std::memcpy(vertex.xyz, position, sizeof(vertex.xyz));
            vertex.rawST[0] = corner.rawS;
            vertex.rawST[1] = corner.rawT;
            vertex.indices[0] = boneRemap[bone];
            vertex.indices[1] = static_cast<std::uint16_t>(lightSlot);
        }
        *bonePaletteCount = usedBones;
        *lightPaletteCount = usedLights;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
    return true;
}

bool EnsureStaticStudioGeometry(const TopologyCacheEntry& topology)
{
    if (topology.staticGeometryReady)
        return !topology.staticVertices.empty() &&
               topology.staticBoneCount > 0 &&
               topology.staticNormalCount > 0 &&
               !topology.staticBonePalette.empty() &&
               !topology.staticLightPalette.empty();

    StaticStudioSource source{};
    if (!ResolveStaticStudioSource(topology, source))
        return false;

    std::uint8_t bonePalette[128]{};
    int bonePaletteCount = 0;
    int lightPaletteCount = 0;
    std::vector<int> lightRemap;
    std::vector<std::uint16_t> lightPalette;
    try
    {
        topology.staticVertices.resize(topology.uniqueCorners.size());
        lightRemap.assign(static_cast<std::size_t>(source.numNormals), -1);
        lightPalette.resize(static_cast<std::size_t>(source.numNormals));
    }
    catch (...)
    {
        topology.staticVertices.clear();
        topology.staticBonePalette.clear();
        topology.staticLightPalette.clear();
        return false;
    }

    if (!FillStaticStudioGeometrySeh(topology,
                                     source,
                                     topology.staticVertices.data(),
                                     topology.staticVertices.size(),
                                     bonePalette,
                                     &bonePaletteCount,
                                     lightRemap.data(),
                                     lightRemap.size(),
                                     lightPalette.data(),
                                     &lightPaletteCount) ||
        bonePaletteCount <= 0 || bonePaletteCount > source.numBones ||
        lightPaletteCount <= 0 || lightPaletteCount > source.numNormals)
    {
        topology.staticVertices.clear();
        topology.staticBonePalette.clear();
        topology.staticLightPalette.clear();
        return false;
    }

    try
    {
        topology.staticBonePalette.assign(
            bonePalette, bonePalette + bonePaletteCount);
        topology.staticLightPalette.assign(
            lightPalette.data(), lightPalette.data() + lightPaletteCount);
    }
    catch (...)
    {
        topology.staticVertices.clear();
        topology.staticBonePalette.clear();
        topology.staticLightPalette.clear();
        return false;
    }
    topology.staticBoneCount = source.numBones;
    topology.staticNormalCount = source.numNormals;
    topology.staticGeometryReady = !topology.staticVertices.empty();
    return topology.staticGeometryReady;
}

void ReleaseTopologyIndexBuffer(TopologyCacheEntry& entry)
{
    const std::uint32_t currentGeneration = worldvbo::ContextGeneration();
    if (entry.indexBuffer && entry.indexBufferGeneration == currentGeneration && g_deleteBuffers)
    {
        const unsigned id = entry.indexBuffer;
        __try { g_deleteBuffers(1, &id); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    if (entry.staticVertexBuffer &&
        entry.staticVertexBufferGeneration == currentGeneration &&
        g_deleteBuffers)
    {
        const unsigned id = entry.staticVertexBuffer;
        __try { g_deleteBuffers(1, &id); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    entry.indexBuffer = 0;
    entry.indexBufferGeneration = 0;
    entry.indexBufferValidatedGeneration = 0;
    entry.staticVertexBuffer = 0;
    entry.staticVertexBufferGeneration = 0;
    entry.staticVertexBufferValidatedGeneration = 0;
}

void ClearTopologyCache()
{
    // A cache eviction can delete the exact VBO/EBO names currently tracked by
    // the persistent GPU array scope.  Close that scope first so a recycled GL
    // object name can never be mistaken for the still-bound previous mesh.
    EndGpuProgramScope();
    for (auto& kv : g_topologyCache)
        ReleaseTopologyIndexBuffer(kv.second);
    g_topologyCache.clear();
}

const TopologyCacheEntry* GetTopology(const std::int16_t* stream,
                                      const std::uint8_t* mesh,
                                      bool strictByteCompare)
{
    if (g_topologyCache.size() > 4096u)
        ClearTopologyCache();
    const StudioModelRaw* studioModel = ReadCurrentStudioModelSeh();
    if (!studioModel)
        return nullptr;
    const TopologyCacheKey key{
        reinterpret_cast<std::uintptr_t>(stream),
        reinterpret_cast<std::uintptr_t>(mesh),
        reinterpret_cast<std::uintptr_t>(studioModel)
    };
    auto it = g_topologyCache.find(key);
    if (it != g_topologyCache.end())
    {
        if (CacheEntryStillExact(it->second, stream, mesh, strictByteCompare))
        {
            if (g_collectStats) ++g_predecodeHits;
            return &it->second;
        }
        ReleaseTopologyIndexBuffer(it->second);
        g_topologyCache.erase(it);
    }

    TopologyCacheEntry built{};
    if (!BuildTopologyCache(stream, mesh, studioModel, built))
        return nullptr;
    try
    {
        auto result = g_topologyCache.emplace(key, TopologyCacheEntry{});
        result.first->second = std::move(built);
        if (g_collectStats) ++g_predecodeBuilds;
        return &result.first->second;
    }
    catch (...)
    {
        return nullptr;
    }
}

int ReadIndexBufferMode()
{
    int mode = 0;
    __try { if (g_indexBufferMode) mode = static_cast<int>(g_indexBufferMode->value); }
    __except (EXCEPTION_EXECUTE_HANDLER) { mode = 0; }
    return (mode == 1 || mode == 2) ? mode : 0;
}

int ReadDrawRangeMode()
{
    int mode = 0;
    __try { if (g_drawRangeMode) mode = static_cast<int>(g_drawRangeMode->value); }
    __except (EXCEPTION_EXECUTE_HANDLER) { mode = 0; }
    return (mode == 1 || mode == 2) ? mode : 0;
}

int ReadVertexBufferMode()
{
    int mode = 0;
    __try { if (g_vertexBufferMode) mode = static_cast<int>(g_vertexBufferMode->value); }
    __except (EXCEPTION_EXECUTE_HANDLER) { mode = 0; }
    return mode == 1 ? 1 : 0;
}

int ReadGpuMode()
{
    int mode = 0;
    __try { if (g_gpuMode) mode = static_cast<int>(g_gpuMode->value); }
    __except (EXCEPTION_EXECUTE_HANDLER) { mode = 0; }
    return (mode == 1 || mode == 2) ? mode : 0;
}

bool EnsureStudioVertexBuffer()
{
    if (!g_genBuffers || !g_bufferData || !g_bindBuffer ||
        !worldvbo::ContextGenerationReady())
        return false;

    const std::uint32_t generation = worldvbo::ContextGeneration();
    if (g_studioVertexBufferGeneration != generation)
    {
        // A context generation change invalidates the old object name. Never
        // delete it through the new context, the old context already owned it.
        g_studioVertexBuffer = 0;
        g_studioVertexBufferGeneration = generation;
    }
    if (!g_studioVertexBuffer)
    {
        unsigned id = 0;
        g_genBuffers(1, &id);
        if (!id)
            return false;
        g_studioVertexBuffer = id;
    }
    return true;
}

bool EnsureStaticStudioVertexBuffer(const TopologyCacheEntry& topology)
{
    if (!EnsureStaticStudioGeometry(topology) ||
        !g_modelGenerationReady || !topology.ownerModel ||
        topology.ownerModelIndex < 0 ||
        topology.ownerModelIndex >= kKnownModelMax ||
        !worldvbo::ContextGenerationReady() ||
        !g_genBuffers || !g_deleteBuffers || !g_bufferData ||
        !g_getBufferParameteriv || !g_getIntegerv || !g_bindBuffer ||
        topology.staticVertices.empty())
        return false;

    const std::uint32_t generation = worldvbo::ContextGeneration();
    if (topology.staticVertexBuffer &&
        topology.staticVertexBufferGeneration == generation)
    {
        if (g_collectStats) ++g_gpuStaticHits;
        return true;
    }

    // Names from an older GL context are invalid in the new namespace. Forget
    // them without issuing a delete through the replacement context.
    if (topology.staticVertexBuffer)
    {
        topology.staticVertexBuffer = 0;
        topology.staticVertexBufferGeneration = 0;
        topology.staticVertexBufferValidatedGeneration = 0;
    }

    if (topology.staticVertices.size() >
        static_cast<std::size_t>(PTRDIFF_MAX) / sizeof(StaticStudioVertex))
        return false;
    const std::size_t byteCount =
        topology.staticVertices.size() * sizeof(StaticStudioVertex);
    if (byteCount > static_cast<std::size_t>(INT_MAX))
        return false;

    unsigned id = 0;
    int previousArrayBuffer = 0;
    bool storageExact = false;
    __try
    {
        g_getIntegerv(GL_ARRAY_BUFFER_BINDING, &previousArrayBuffer);
        g_genBuffers(1, &id);
        if (!id)
            return false;
        g_bindBuffer(GL_ARRAY_BUFFER, id);
        g_bufferData(GL_ARRAY_BUFFER,
                     static_cast<std::ptrdiff_t>(byteCount),
                     topology.staticVertices.data(), GL_STATIC_DRAW);
        int uploadedBytes = 0;
        g_getBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &uploadedBytes);
        storageExact = uploadedBytes == static_cast<int>(byteCount);
        g_bindBuffer(GL_ARRAY_BUFFER, static_cast<unsigned>(previousArrayBuffer));
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        if (g_bindBuffer)
        {
            __try { g_bindBuffer(GL_ARRAY_BUFFER, static_cast<unsigned>(previousArrayBuffer)); }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        if (id && g_deleteBuffers)
        {
            __try { g_deleteBuffers(1, &id); }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        return false;
    }

    if (!storageExact)
    {
        __try { g_deleteBuffers(1, &id); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        return false;
    }

    topology.staticVertexBuffer = id;
    topology.staticVertexBufferGeneration = generation;
    topology.staticVertexBufferValidatedGeneration = 0;
    if (g_collectStats) ++g_gpuStaticBuilds;
    return true;
}

bool ValidateDrawRangeExact(const TopologyCacheEntry& topology)
{
    if (topology.drawRangeValidated)
        return true;
    if (topology.renderTriangleIndices.empty() || topology.renderMinIndex > topology.renderMaxIndex)
        return false;
    ++g_drawRangeValidate;
    for (std::uint32_t index : topology.renderTriangleIndices)
    {
        if (index < topology.renderMinIndex || index > topology.renderMaxIndex)
        {
            ++g_drawRangeMismatch;
            return false;
        }
    }
    topology.drawRangeValidated = true;
    return true;
}

bool EnsureTopologyIndexBuffer(const TopologyCacheEntry& topology)
{
    if (!worldvbo::ContextGenerationReady() ||
        !g_genBuffers || !g_deleteBuffers || !g_bufferData || !g_getBufferParameteriv ||
        !g_getIntegerv || !g_bindBuffer ||
        topology.renderTriangleIndices.empty())
        return false;

    const std::uint32_t generation = worldvbo::ContextGeneration();
    if (topology.indexBuffer && topology.indexBufferGeneration == generation)
    {
        ++g_indexBufferHits;
        return true;
    }

    // A context teardown destroys the old GL object namespace. Never issue a
    // delete for an ID from an older generation, just forget and rebuild it.
    if (topology.indexBuffer)
    {
        topology.indexBuffer = 0;
        topology.indexBufferGeneration = 0;
        topology.indexBufferValidatedGeneration = 0;
    }

    if (topology.renderTriangleIndices.size() >
        static_cast<std::size_t>(PTRDIFF_MAX) / sizeof(std::uint32_t))
        return false;
    const std::size_t byteCount = topology.renderTriangleIndices.size() * sizeof(std::uint32_t);
    if (byteCount > static_cast<std::size_t>(INT_MAX))
        return false;

    unsigned id = 0;
    int previousElementBuffer = 0;
    bool storageExact = false;
    __try
    {
        g_getIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &previousElementBuffer);
        g_genBuffers(1, &id);
        if (!id)
            return false;
        g_bindBuffer(GL_ELEMENT_ARRAY_BUFFER, id);
        g_bufferData(GL_ELEMENT_ARRAY_BUFFER,
                     static_cast<std::ptrdiff_t>(byteCount),
                     topology.renderTriangleIndices.data(), GL_STATIC_DRAW);
        int uploadedBytes = 0;
        g_getBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &uploadedBytes);
        storageExact = uploadedBytes == static_cast<int>(byteCount);
        g_bindBuffer(GL_ELEMENT_ARRAY_BUFFER,
                     static_cast<unsigned>(previousElementBuffer));
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        if (g_bindBuffer)
        {
            __try
            {
                g_bindBuffer(GL_ELEMENT_ARRAY_BUFFER,
                             static_cast<unsigned>(previousElementBuffer));
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        if (id && g_deleteBuffers)
        {
            __try { g_deleteBuffers(1, &id); }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        return false;
    }

    if (!storageExact)
    {
        __try { g_deleteBuffers(1, &id); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        return false;
    }

    topology.indexBuffer = id;
    topology.indexBufferGeneration = generation;
    ++g_indexBufferBuilds;
    return true;
}

bool ReadBackIndexBufferExact(unsigned buffer,
                              std::size_t count,
                              std::uint32_t* destination)
{
    if (!buffer || !count || !destination || !g_bindBuffer ||
        !g_getBufferSubData || !g_getIntegerv)
        return false;
    int previousElementBuffer = 0;
    __try
    {
        g_getIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &previousElementBuffer);
        g_bindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer);
        g_getBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0,
                           static_cast<std::ptrdiff_t>(count * sizeof(std::uint32_t)),
                           destination);
        g_bindBuffer(GL_ELEMENT_ARRAY_BUFFER,
                     static_cast<unsigned>(previousElementBuffer));
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        __try
        {
            g_bindBuffer(GL_ELEMENT_ARRAY_BUFFER,
                         static_cast<unsigned>(previousElementBuffer));
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        return false;
    }
}

bool ValidateTopologyIndexBuffer(const TopologyCacheEntry& topology)
{
    const std::uint32_t generation = worldvbo::ContextGeneration();
    if (!topology.indexBuffer || topology.indexBufferGeneration != generation || !g_getBufferSubData)
        return false;
    if (topology.indexBufferValidatedGeneration == generation)
        return true;

    try
    {
        g_indexValidationScratch.resize(topology.renderTriangleIndices.size());
    }
    catch (...)
    {
        return false;
    }

    if (!ReadBackIndexBufferExact(topology.indexBuffer,
                                  topology.renderTriangleIndices.size(),
                                  g_indexValidationScratch.data()))
        return false;

    const bool equal = std::memcmp(g_indexValidationScratch.data(), topology.renderTriangleIndices.data(),
                                   topology.renderTriangleIndices.size() * sizeof(std::uint32_t)) == 0;

    ++g_indexBufferValidate;
    if (!equal)
    {
        ++g_indexBufferMismatch;
        return false;
    }
    topology.indexBufferValidatedGeneration = generation;
    return true;
}

bool ReadBackStaticStudioVertexBufferExact(unsigned buffer,
                                           std::size_t byteCount,
                                           void* destination)
{
    if (!buffer || !byteCount || !destination ||
        !g_getBufferSubData || !g_getIntegerv || !g_bindBuffer)
        return false;

    int previousArrayBuffer = 0;
    __try
    {
        g_getIntegerv(GL_ARRAY_BUFFER_BINDING, &previousArrayBuffer);
        g_bindBuffer(GL_ARRAY_BUFFER, buffer);
        g_getBufferSubData(GL_ARRAY_BUFFER, 0,
                           static_cast<std::ptrdiff_t>(byteCount),
                           destination);
        g_bindBuffer(GL_ARRAY_BUFFER, static_cast<unsigned>(previousArrayBuffer));
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        __try { g_bindBuffer(GL_ARRAY_BUFFER, static_cast<unsigned>(previousArrayBuffer)); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        return false;
    }
}

bool ValidateStaticStudioVertexBuffer(const TopologyCacheEntry& topology)
{
    const std::uint32_t generation = worldvbo::ContextGeneration();
    if (!topology.staticVertexBuffer ||
        topology.staticVertexBufferGeneration != generation ||
        !g_getBufferSubData || !g_getIntegerv || !g_bindBuffer ||
        topology.staticVertices.empty())
        return false;
    if (topology.staticVertexBufferValidatedGeneration == generation)
        return true;

    try
    {
        g_staticVertexValidationScratch.resize(topology.staticVertices.size());
    }
    catch (...)
    {
        return false;
    }

    const std::size_t byteCount =
        topology.staticVertices.size() * sizeof(StaticStudioVertex);
    if (!ReadBackStaticStudioVertexBufferExact(
            topology.staticVertexBuffer, byteCount,
            g_staticVertexValidationScratch.data()))
        return false;

    const bool equal =
        std::memcmp(g_staticVertexValidationScratch.data(),
                    topology.staticVertices.data(),
                    topology.staticVertices.size() * sizeof(StaticStudioVertex)) == 0;
    if (g_collectStats) ++g_gpuStaticValidate;
    if (!equal)
    {
        if (g_collectStats) ++g_gpuStaticMismatch;
        return false;
    }
    topology.staticVertexBufferValidatedGeneration = generation;
    return true;
}

bool GpuFixedStateCompatibleSeh(unsigned expectedProgram)
{
    if (!g_getIntegerv || !g_isEnabled || !g_activeTexture)
        return false;
    int activeTexture = static_cast<int>(GL_TEXTURE0);
    bool activeTextureKnown = false;
    __try
    {
        int currentProgram = 0;
        g_getIntegerv(GL_CURRENT_PROGRAM, &currentProgram);
        if (currentProgram != static_cast<int>(expectedProgram))
            return false;
        if (g_isEnabled(GL_LIGHTING) || g_isEnabled(GL_FOG) ||
            g_isEnabled(GL_COLOR_SUM))
            return false;
        for (unsigned i = 0; i < 6; ++i)
        {
            if (g_isEnabled(GL_CLIP_PLANE0 + i))
                return false;
        }

        int maxUnits = 0;
        g_getIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
        activeTextureKnown = true;
        g_getIntegerv(GL_MAX_TEXTURE_UNITS, &maxUnits);
        if (maxUnits <= 0 || maxUnits > 32 ||
            activeTexture < static_cast<int>(GL_TEXTURE0) ||
            activeTexture >= static_cast<int>(GL_TEXTURE0) + maxUnits)
            return false;

        bool compatible = true;
        for (int unit = 0; unit < maxUnits && compatible; ++unit)
        {
            g_activeTexture(GL_TEXTURE0 + static_cast<unsigned>(unit));
            if (g_isEnabled(GL_TEXTURE_GEN_S) || g_isEnabled(GL_TEXTURE_GEN_T) ||
                g_isEnabled(GL_TEXTURE_GEN_R) || g_isEnabled(GL_TEXTURE_GEN_Q))
            {
                compatible = false;
                break;
            }
            // The Studio shader publishes texture coordinates only for unit 0.
            // A live fixed-function texture unit above 0 would consume undefined
            // gl_TexCoord[] data and must therefore fail open to Gold.
            if (unit != 0 && g_isEnabled(GL_TEXTURE_2D))
            {
                compatible = false;
                break;
            }
        }
        g_activeTexture(static_cast<unsigned>(activeTexture));
        activeTextureKnown = false;
        return compatible;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        if (activeTextureKnown && g_activeTexture)
        {
            __try { g_activeTexture(static_cast<unsigned>(activeTexture)); }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        return false;
    }
}

bool StudioShadowWillBeSkippedSeh()
{
    if (!g_hwBase)
        return false;
    __try
    {
        // Mirror/reflection draws never enter the low-level Studio shadow path.
        if (*reinterpret_cast<const std::uint8_t*>(
                g_hwBase + kStudioShadowMirrorRva) != 0)
            return true;

        // These are the exact globals consumed by Gold's public
        // GL_StudioDrawShadow wrapper at hw+0x9D670.
        if (*reinterpret_cast<const std::uint32_t*>(
                g_hwBase + kStudioShadowsRawRva) == 0)
            return true;
        if (*reinterpret_cast<const std::int32_t*>(
                g_hwBase + kStudioSetupRenderModeRva) == 5)
            return true;

        const auto* renderModel =
            *reinterpret_cast<const std::uint8_t* const*>(
                g_hwBase + kStudioRenderModelPtrRva);
        if (!renderModel)
            return false;
        const std::uint32_t modelFlags =
            *reinterpret_cast<const std::uint32_t*>(renderModel + 0x50);
        return (modelFlags & 0x100u) != 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

void EndGpuProgramScope()
{
    EndGpuArrayScope();
    if (!g_gpuScopeActive)
        return;
    if (g_useProgram && g_getIntegerv)
    {
        __try
        {
            int currentProgram = 0;
            g_getIntegerv(GL_CURRENT_PROGRAM, &currentProgram);
            if (currentProgram == static_cast<int>(g_gpuProgram))
                g_useProgram(0);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }
    g_gpuScopeActive = false;
}

bool BeginGpuProgramScope()
{
    if (g_gpuScopeActive)
    {
        if (!g_getIntegerv)
            return false;
        __try
        {
            int currentProgram = 0;
            g_getIntegerv(GL_CURRENT_PROGRAM, &currentProgram);
            if (currentProgram == static_cast<int>(g_gpuProgram))
                return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
        EndGpuProgramScope();
        return false;
    }
    if (!EnsureGpuProgram() || !GpuFixedStateCompatibleSeh(0))
        return false;
    __try
    {
        g_useProgram(g_gpuProgram);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
    g_gpuScopeActive = true;
    return true;
}

bool FillAndUploadGpuDataSeh(const TopologyCacheEntry& topology,
                             float* rows,
                             int requiredRows,
                             int boneCount,
                             int lightCount,
                             float sScale,
                             float tScale,
                             DrawVertex* finalState)
{
    if (!rows || !finalState || !g_hwBase || !g_uniform4fv ||
        requiredRows <= 0 || requiredRows > g_gpuMaxRows ||
        boneCount <= 0 || boneCount > 128 || lightCount <= 0 ||
        g_gpuDataLocation < 0 || g_gpuParamsLocation < 0 ||
        topology.corners.empty())
        return false;

    __try
    {
        const float* boneTransform =
            reinterpret_cast<const float*>(g_hwBase + kBoneTransformRva);
        for (int i = 0; i < boneCount; ++i)
        {
            const unsigned bone = topology.staticBonePalette[i];
            if (bone >= static_cast<unsigned>(topology.staticBoneCount))
                return false;
            std::memcpy(rows + static_cast<std::size_t>(i) * 12u,
                        boneTransform + static_cast<std::size_t>(bone) * 12u,
                        12u * sizeof(float));
        }

        const int lightBase = boneCount * 3;
        const float* lightValues =
            reinterpret_cast<const float*>(g_hwBase + kPvLightValuesRva);
        for (int i = 0; i < lightCount; ++i)
        {
            const unsigned normal = topology.staticLightPalette[i];
            if (normal >= static_cast<unsigned>(topology.staticNormalCount))
                return false;
            const float* src =
                lightValues + static_cast<std::size_t>(normal) * 3u;
            float* dst =
                rows + static_cast<std::size_t>(lightBase + i) * 4u;
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            dst[3] = 0.0f;
        }

        float blend = 0.0f;
        std::memcpy(&blend, g_hwBase + kStudioBlendRva, sizeof(blend));

        const CachedCorner& last = topology.corners.back();
        if (last.norm >= static_cast<unsigned>(topology.staticNormalCount))
            return false;
        const float* lastLight =
            lightValues + static_cast<std::size_t>(last.norm) * 3u;
        finalState->st[0] = ScaleStudioCoord(last.studioS, sScale);
        finalState->st[1] = ScaleStudioCoord(last.studioT, tScale);
        finalState->rgba[0] = lastLight[0];
        finalState->rgba[1] = lastLight[1];
        finalState->rgba[2] = lastLight[2];
        finalState->rgba[3] = blend;

        const float params[4] = {
            sScale, tScale, blend, static_cast<float>(lightBase)
        };
        g_uniform4fv(g_gpuDataLocation, requiredRows, rows);
        g_uniform4fv(g_gpuParamsLocation, 1, params);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool PrepareGpuData(const TopologyCacheEntry& topology,
                    float sScale,
                    float tScale,
                    DrawVertex& finalState)
{
    const int boneCount = static_cast<int>(topology.staticBonePalette.size());
    const int lightCount = static_cast<int>(topology.staticLightPalette.size());
    if (boneCount <= 0 || boneCount > 128 || lightCount <= 0)
        return false;
    const int requiredRows = boneCount * 3 + lightCount;
    if (requiredRows <= 0 || requiredRows > g_gpuMaxRows)
        return false;
    try
    {
        const std::size_t requiredFloats =
            static_cast<std::size_t>(requiredRows) * 4u;
        if (g_gpuUniformRows.size() < requiredFloats)
            g_gpuUniformRows.resize(requiredFloats);
    }
    catch (...)
    {
        return false;
    }
    return FillAndUploadGpuDataSeh(
        topology, g_gpuUniformRows.data(), requiredRows,
        boneCount, lightCount, sScale, tScale, &finalState);
}

void EndGpuArrayScope()
{
    if (!g_gpuArrayScopeActive)
        return;

    if (g_popClientAttrib)
    {
        __try { g_popClientAttrib(); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    if (g_bindBuffer)
    {
        __try
        {
            g_bindBuffer(GL_ARRAY_BUFFER,
                         static_cast<unsigned>(g_gpuPreviousArrayBuffer));
            g_bindBuffer(GL_ELEMENT_ARRAY_BUFFER,
                         static_cast<unsigned>(g_gpuPreviousElementArrayBuffer));
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    g_gpuArrayScopeActive = false;
    g_gpuStaticArrayBuffer = 0;
    g_gpuElementBuffer = 0;
    g_gpuPreviousArrayBuffer = 0;
    g_gpuPreviousElementArrayBuffer = 0;
}

bool BeginGpuArrayScope()
{
    if (!g_getIntegerv || !g_pushClientAttrib ||
        !g_popClientAttrib || !g_bindBuffer || !g_vertexAttribPointer ||
        !g_enableVertexAttribArray)
        return false;

    if (!g_gpuArrayScopeActive)
    {
        bool pushed = false;
        __try
        {
            g_getIntegerv(GL_ARRAY_BUFFER_BINDING, &g_gpuPreviousArrayBuffer);
            g_getIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING,
                          &g_gpuPreviousElementArrayBuffer);
            g_pushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
            pushed = true;
            g_enableVertexAttribArray(kGpuAttribPosition);
            g_enableVertexAttribArray(kGpuAttribTexCoord);
            g_enableVertexAttribArray(kGpuAttribBone);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            if (pushed && g_popClientAttrib)
            {
                __try { g_popClientAttrib(); }
                __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
            if (g_bindBuffer)
            {
                __try
                {
                    g_bindBuffer(GL_ARRAY_BUFFER,
                                 static_cast<unsigned>(g_gpuPreviousArrayBuffer));
                    g_bindBuffer(GL_ELEMENT_ARRAY_BUFFER,
                                 static_cast<unsigned>(g_gpuPreviousElementArrayBuffer));
                }
                __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
            return false;
        }
        g_gpuArrayScopeActive = true;
        g_gpuStaticArrayBuffer = 0;
        g_gpuElementBuffer = 0;
        return true;
    }
    return true;
}

bool DrawGpuArraysSeh(unsigned staticVertexBuffer,
                      unsigned indexBuffer,
                      int indexCount)
{
    if (!staticVertexBuffer || !indexBuffer ||
        indexCount <= 0 || !g_bindBuffer || !g_vertexAttribPointer ||
        !g_drawElements || !BeginGpuArrayScope())
        return false;

    bool issuedDraw = false;
    __try
    {
        if (g_gpuStaticArrayBuffer != staticVertexBuffer)
        {
            g_bindBuffer(GL_ARRAY_BUFFER, staticVertexBuffer);
            g_vertexAttribPointer(kGpuAttribPosition, 3, GL_FLOAT,
                                 GL_FALSE_VALUE, sizeof(StaticStudioVertex),
                                 reinterpret_cast<const void*>(0));
            g_vertexAttribPointer(kGpuAttribTexCoord, 2, GL_SHORT,
                                 GL_FALSE_VALUE, sizeof(StaticStudioVertex),
                                 reinterpret_cast<const void*>(12));
            g_vertexAttribPointer(kGpuAttribBone, 2, GL_UNSIGNED_SHORT,
                                 GL_FALSE_VALUE, sizeof(StaticStudioVertex),
                                 reinterpret_cast<const void*>(16));
            g_gpuStaticArrayBuffer = staticVertexBuffer;
        }

        if (g_gpuElementBuffer != indexBuffer)
        {
            g_bindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
            g_gpuElementBuffer = indexBuffer;
        }

        issuedDraw = true;
        g_drawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        // Never replay after a draw may have reached the driver. The outer
        // Studio scope owns and restores the persistent array state.
        if (!issuedDraw)
            EndGpuArrayScope();
        return issuedDraw;
    }
}

bool DrawPredecodedGpu(const TopologyCacheEntry& topology,
                       float sScale,
                       float tScale)
{
    if (topology.renderTriangleIndices.empty() ||
        topology.renderTriangleIndices.size() > static_cast<std::size_t>(INT_MAX))
        return false;
    if (ReadNumStudioLightsSafe() != 0)
        return false;

    // A CPU fallback earlier in the same StudioDrawPoints deliberately keeps
    // the conventional client-array scope open. Close it before switching back
    // to the generic-attrib GPU path, otherwise one unsupported mesh would
    // poison every later eligible mesh in the entity draw.
    if (g_arrayScopeActive)
        EndArrayScopeImmediate();

    if (!EnsureStaticStudioVertexBuffer(topology))
    {
        if (g_collectStats) ++g_gpuFailStatic;
        return false;
    }
    if (!EnsureTopologyIndexBuffer(topology))
    {
        if (g_collectStats) ++g_gpuFailIndex;
        return false;
    }
    if (!BeginGpuProgramScope())
    {
        if (g_collectStats) ++g_gpuFailProgram;
        return false;
    }

    if (topology.renderMaxIndex >= topology.staticVertices.size() ||
        topology.staticBonePalette.empty() ||
        topology.staticLightPalette.empty() ||
        topology.corners.empty())
    {
        if (g_collectStats) ++g_gpuFailLimits;
        EndGpuProgramScope();
        return false;
    }

    const int boneCount = static_cast<int>(topology.staticBonePalette.size());
    const int lightCount = static_cast<int>(topology.staticLightPalette.size());
    const int requiredRows = boneCount * 3 + lightCount;
    if (boneCount <= 0 || boneCount > 128 ||
        lightCount <= 0 || requiredRows <= 0 || requiredRows > g_gpuMaxRows)
    {
        if (g_collectStats) ++g_gpuFailLimits;
        EndGpuProgramScope();
        return false;
    }

    DrawVertex finalState{};
    if (!PrepareGpuData(topology, sScale, tScale, finalState))
    {
        if (g_collectStats) ++g_gpuFailLights;
        EndGpuProgramScope();
        return false;
    }

    if (!DrawGpuArraysSeh(topology.staticVertexBuffer,
                          topology.indexBuffer,
                          static_cast<int>(topology.renderTriangleIndices.size())))
    {
        if (g_collectStats) ++g_gpuFailDraw;
        EndGpuProgramScope();
        return false;
    }

    QueueDeferredCurrentState(finalState, true);
    return true;
}

float ScaleStudioCoord(float raw, float scale)
{
    __m128 v = _mm_set_ss(raw);
    v = _mm_mul_ss(v, _mm_set_ss(scale));
    float out = 0.0f;
    _mm_store_ss(&out, v);
    return out;
}

float ScaleChromeCoord(std::int32_t raw, float scale)
{
    __m128i iv = _mm_cvtsi32_si128(raw);
    __m128 v = _mm_cvtepi32_ps(iv);
    v = _mm_mul_ss(v, _mm_set_ss(scale));
    float out = 0.0f;
    _mm_store_ss(&out, v);
    return out;
}

void InvokeLightLambert(const float* light,
                        const float* normal,
                        const float* lv,
                        float* resultRgb)
{
    __asm
    {
        sub esp, 32
        movups xmmword ptr [esp], xmm6
        movups xmmword ptr [esp + 16], xmm7
        push resultRgb
        push lv
        mov edx, normal
        mov ecx, light
        call dword ptr [g_lightLambert]
        add esp, 8
        movups xmm6, xmmword ptr [esp]
        movups xmm7, xmmword ptr [esp + 16]
        add esp, 32
    }
}

__declspec(align(16)) struct FxValidationState
{
    std::uint8_t bytes[512];
};

__declspec(noinline) bool SaveFxValidationState(FxValidationState* state)
{
    if (!state || (reinterpret_cast<std::uintptr_t>(state) & 15u) != 0)
        return false;
    __try
    {
        __asm
        {
            mov eax, state
            fxsave [eax]
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

__declspec(noinline) bool RestoreFxValidationState(const FxValidationState* state)
{
    if (!state || (reinterpret_cast<std::uintptr_t>(state) & 15u) != 0)
        return false;
    __try
    {
        __asm
        {
            mov eax, state
            fxrstor [eax]
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

int ReadNumStudioLightsSafe()
{
    __try
    {
        return *reinterpret_cast<const int*>(g_hwBase + kNumStudioLightsRva);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
    }
}

bool ReadForceFaceFlagsSafe(std::uint32_t& flags)
{
    __try
    {
        flags = *reinterpret_cast<const std::uint32_t*>(g_hwBase + kForceFaceFlagsRva);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool SnapshotLightWRaw(const CachedCorner* corners,
                       std::size_t count,
                       std::uint32_t* snapshot)
{
    if (!corners || !snapshot)
        return false;
    const auto* lightPos = reinterpret_cast<const std::uint32_t*>(g_hwBase + kLightPosRva);
    __try
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            const std::size_t base = static_cast<std::size_t>(corners[i].vert) * 12u;
            for (int light = 0; light < 3; ++light)
                snapshot[i * 3u + static_cast<std::size_t>(light)] =
                    lightPos[base + static_cast<std::size_t>(light) * 4u + 3u];
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool RestoreLightWRaw(const CachedCorner* corners,
                      std::size_t count,
                      const std::uint32_t* snapshot)
{
    if (!corners || !snapshot)
        return false;
    auto* lightPos = reinterpret_cast<std::uint32_t*>(g_hwBase + kLightPosRva);
    __try
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            const std::size_t base = static_cast<std::size_t>(corners[i].vert) * 12u;
            for (int light = 0; light < 3; ++light)
                lightPos[base + static_cast<std::size_t>(light) * 4u + 3u] =
                    snapshot[i * 3u + static_cast<std::size_t>(light)];
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool SnapshotLightW(const TopologyCacheEntry& topology,
                    std::vector<std::uint32_t>& snapshot)
{
    const int lights = ReadNumStudioLightsSafe();
    if (lights < 0 || lights > 3)
        return false;
    try
    {
        snapshot.resize(topology.corners.size() * 3u);
    }
    catch (...)
    {
        return false;
    }
    return SnapshotLightWRaw(topology.corners.data(), topology.corners.size(), snapshot.data());
}

bool RestoreLightW(const TopologyCacheEntry& topology,
                   const std::vector<std::uint32_t>& snapshot)
{
    if (snapshot.size() != topology.corners.size() * 3u)
        return false;
    return RestoreLightWRaw(topology.corners.data(), topology.corners.size(), snapshot.data());
}

bool FinishLightWValidationSetup(const TopologyCacheEntry& topology,
                                 bool generated)
{
    // Always restore the pre-generator lazy attenuation state, even if vertex
    // generation failed after touching one or more lightpos[].w values. Gold's
    // emitter must start from exactly the state it would have seen without the
    // validator.
    bool captured = false;
    if (generated)
        captured = SnapshotLightW(topology, g_predecodeLightWGenerated);
    const bool restored = RestoreLightW(topology, g_predecodeLightW);
    return generated && captured && restored;
}

void RecordCornerDedup(const TopologyCacheEntry& topology)
{
    if (!g_collectStats)
        return;
    g_cornerInputs += static_cast<std::uint64_t>(topology.corners.size());
    g_uniqueCornerOutputs += static_cast<std::uint64_t>(topology.uniqueCornerIndices.size());
}

void RecordPhaseSample(const TopologyCacheEntry& topology,
                       long long topologyTicks,
                       long long generateTicks,
                       long long submitTicks)
{
    if (topologyTicks < 0 || generateTicks < 0 || submitTicks < 0)
    {
        ++g_phaseMixed;
        return;
    }
    ++g_phaseMeshes;
    g_phaseCorners += static_cast<std::uint64_t>(topology.uniqueCorners.size());
    g_phaseIndices += static_cast<std::uint64_t>(topology.renderTriangleIndices.size());
    g_phaseTopologyTicks += static_cast<std::uint64_t>(topologyTicks);
    g_phaseGenerateTicks += static_cast<std::uint64_t>(generateTicks);
    g_phaseSubmitTicks += static_cast<std::uint64_t>(submitTicks);
}

bool GenerateStandardVertices(const TopologyCacheEntry& topology,
                              const float* studioNormals,
                              float sScale,
                              float tScale,
                              std::vector<DrawVertex>& out)
{
    if (!g_hwBase || !g_lightLambert || !studioNormals || topology.corners.empty() ||
        topology.uniqueCorners.empty())
        return false;
    const int numLights = ReadNumStudioLightsSafe();
    if (numLights < 0 || numLights > 3)
        return false;
    const bool zeroLights = numLights == 0;
    try
    {
        if (out.size() < topology.uniqueCorners.size())
            out.resize(topology.uniqueCorners.size());
    }
    catch (...)
    {
        return false;
    }

    const auto* aux = reinterpret_cast<const float*>(g_hwBase + kAuxVertsRva);
    const auto* lightValues = reinterpret_cast<const float*>(g_hwBase + kPvLightValuesRva);
    volatile float stableSScale = sScale;
    volatile float stableTScale = tScale;
    RecordCornerDedup(topology);

    if (zeroLights)
    {
        const float zeroSScale = sScale;
        const float zeroTScale = tScale;
        if (g_collectStats)
        {
            g_lambertSkipped += static_cast<std::uint64_t>(topology.corners.size());
            g_lambertZeroBypass += static_cast<std::uint64_t>(topology.lightPairCount);
        }
        std::uint32_t blendBits = 0;
        std::memcpy(&blendBits, g_hwBase + kStudioBlendRva, sizeof(blendBits));
        const CachedCorner* c = topology.uniqueCorners.data();
        const CachedCorner* const end = c + topology.uniqueCorners.size();
        DrawVertex* v = out.data();
        for (; c != end; ++c, ++v)
        {
            v->st[0] = ScaleStudioCoord(c->studioS, zeroSScale);
            v->st[1] = ScaleStudioCoord(c->studioT, zeroTScale);
            const float* fl = lightValues + static_cast<std::size_t>(c->norm) * 3u;
            const float* pos = aux + static_cast<std::size_t>(c->vert) * 3u;
            std::memcpy(v->xyz, pos, sizeof(v->xyz));
            std::memcpy(v->rgba, fl, 3u * sizeof(float));
            std::memcpy(&v->rgba[3], &blendBits, sizeof(blendBits));
        }
        return true;
    }
    const auto* lightPos = reinterpret_cast<const float*>(g_hwBase + kLightPosRva);
    const bool mirrorNormal = *reinterpret_cast<const std::uint8_t*>(g_hwBase + kMirrorNormalRva) != 0;
    if (g_collectStats)
        g_lambertSkipped += static_cast<std::uint64_t>(topology.corners.size() - topology.lightPairCount);
    for (std::size_t i = 0; i < topology.uniqueCorners.size(); ++i)
    {
        const CachedCorner& c = topology.uniqueCorners[i];
        DrawVertex& v = out[i];
        // Commit UVs to memory before entering Gold's LTCG-only Lambert ABI.
        // Keeping either value live in an XMM across that call is unsafe.
        v.st[0] = ScaleStudioCoord(c.studioS, stableSScale);
        v.st[1] = ScaleStudioCoord(c.studioT, stableTScale);
        if (c.lightFirst)
        {
            const float* srcNormal = studioNormals + static_cast<std::size_t>(c.norm) * 3u;
            float normal[3]{};
            if (mirrorNormal)
            {
                normal[0] = srcNormal[0];
                std::uint32_t y = 0;
                std::memcpy(&y, srcNormal + 1, sizeof(y));
                y ^= 0x80000000u;
                std::memcpy(normal + 1, &y, sizeof(y));
                normal[2] = srcNormal[2];
                srcNormal = normal;
            }
            InvokeLightLambert(lightPos + static_cast<std::size_t>(c.vert) * 12u,
                               srcNormal,
                               lightValues + static_cast<std::size_t>(c.norm) * 3u,
                               v.rgba);
            if (g_collectStats) ++g_lambertCalls;
            const float* pos = aux + static_cast<std::size_t>(c.vert) * 3u;
            std::memcpy(v.xyz, pos, sizeof(v.xyz));
        }
        else
        {
            const std::uint32_t firstSlot = topology.lightFirstRenderSlots[c.lightSlot];
            const DrawVertex& first = out[firstSlot];
            std::memcpy(v.xyz, first.xyz, sizeof(v.xyz));
            std::memcpy(v.rgba, first.rgba, 3u * sizeof(float));
        }
        // Gold reloads r_blend after R_LightLambert for every corner.
        v.rgba[3] = *reinterpret_cast<const float*>(g_hwBase + kStudioBlendRva);
    }
    return true;
}

bool MaterializeDeferredSkinForCpu()
{
    if (!studio_fastskin::DeferredSkinActive())
        return true;
    EndGpuProgramScope();
    return studio_fastskin::MaterializeDeferredSkin();
}

bool GenerateAltUvVertices(const TopologyCacheEntry& topology,
                           const float* studioNormals,
                           std::vector<DrawVertex>& out)
{
    if (!g_hwBase || !g_lightLambert || !studioNormals || topology.corners.empty() ||
        topology.uniqueCorners.empty())
        return false;
    const int numLights = ReadNumStudioLightsSafe();
    if (numLights < 0 || numLights > 3)
        return false;
    const bool zeroLights = numLights == 0;
    try
    {
        if (out.size() < topology.uniqueCorners.size())
            out.resize(topology.uniqueCorners.size());
    }
    catch (...)
    {
        return false;
    }

    const auto* aux = reinterpret_cast<const float*>(g_hwBase + kAuxVertsRva);
    const auto* lightValues = reinterpret_cast<const float*>(g_hwBase + kPvLightValuesRva);
    RecordCornerDedup(topology);

    if (zeroLights)
    {
        if (g_collectStats)
        {
            g_lambertSkipped += static_cast<std::uint64_t>(topology.corners.size());
            g_lambertZeroBypass += static_cast<std::uint64_t>(topology.lightPairCount);
        }
        std::uint32_t blendBits = 0;
        std::memcpy(&blendBits, g_hwBase + kStudioBlendRva, sizeof(blendBits));
        const CachedCorner* c = topology.uniqueCorners.data();
        const CachedCorner* const end = c + topology.uniqueCorners.size();
        DrawVertex* v = out.data();
        for (; c != end; ++c, ++v)
        {
            v->st[0] = c->altS;
            v->st[1] = c->altT;
            const float* fl = lightValues + static_cast<std::size_t>(c->norm) * 3u;
            const float* pos = aux + static_cast<std::size_t>(c->vert) * 3u;
            std::memcpy(v->xyz, pos, sizeof(v->xyz));
            std::memcpy(v->rgba, fl, 3u * sizeof(float));
            std::memcpy(&v->rgba[3], &blendBits, sizeof(blendBits));
        }
        return true;
    }
    const auto* lightPos = reinterpret_cast<const float*>(g_hwBase + kLightPosRva);
    const bool mirrorNormal = *reinterpret_cast<const std::uint8_t*>(g_hwBase + kMirrorNormalRva) != 0;
    if (g_collectStats)
        g_lambertSkipped += static_cast<std::uint64_t>(topology.corners.size() - topology.lightPairCount);
    for (std::size_t i = 0; i < topology.uniqueCorners.size(); ++i)
    {
        const CachedCorner& c = topology.uniqueCorners[i];
        DrawVertex& v = out[i];

        // 0x9AA20 uses Gold's custom half-like expansion directly, with no
        // width/height scale multiply. These bit-exact values were cached from
        // the immutable raw words when the topology entry was built.
        v.st[0] = c.altS;
        v.st[1] = c.altT;

        if (c.lightFirst)
        {
            const float* srcNormal = studioNormals + static_cast<std::size_t>(c.norm) * 3u;
            float normal[3]{};
            if (mirrorNormal)
            {
                normal[0] = srcNormal[0];
                std::uint32_t y = 0;
                std::memcpy(&y, srcNormal + 1, sizeof(y));
                y ^= 0x80000000u;
                std::memcpy(normal + 1, &y, sizeof(y));
                normal[2] = srcNormal[2];
                srcNormal = normal;
            }
            InvokeLightLambert(lightPos + static_cast<std::size_t>(c.vert) * 12u,
                               srcNormal,
                               lightValues + static_cast<std::size_t>(c.norm) * 3u,
                               v.rgba);
            if (g_collectStats) ++g_lambertCalls;
            const float* pos = aux + static_cast<std::size_t>(c.vert) * 3u;
            std::memcpy(v.xyz, pos, sizeof(v.xyz));
        }
        else
        {
            const std::uint32_t firstSlot = topology.lightFirstRenderSlots[c.lightSlot];
            const DrawVertex& first = out[firstSlot];
            std::memcpy(v.xyz, first.xyz, sizeof(v.xyz));
            std::memcpy(v.rgba, first.rgba, 3u * sizeof(float));
        }
        v.rgba[3] = *reinterpret_cast<const float*>(g_hwBase + kStudioBlendRva);
    }
    return true;
}

bool GenerateRegularChromeVertices(const TopologyCacheEntry& topology,
                                   const float* studioNormals,
                                   float sScale,
                                   float tScale,
                                   std::vector<DrawVertex>& out)
{
    if (!g_hwBase || !g_lightLambert || !studioNormals || topology.corners.empty() ||
        topology.uniqueCorners.empty())
        return false;
    const int numLights = ReadNumStudioLightsSafe();
    if (numLights < 0 || numLights > 3)
        return false;
    const bool zeroLights = numLights == 0;

    std::uint32_t forceFlags = 0;
    if (!ReadForceFaceFlagsSafe(forceFlags))
        return false;
    if ((forceFlags & 2u) != 0)
        return false;

    try
    {
        if (out.size() < topology.uniqueCorners.size())
            out.resize(topology.uniqueCorners.size());
    }
    catch (...)
    {
        return false;
    }

    const auto* aux = reinterpret_cast<const float*>(g_hwBase + kAuxVertsRva);
    const auto* lightValues = reinterpret_cast<const float*>(g_hwBase + kPvLightValuesRva);
    const auto* chrome = reinterpret_cast<const std::int32_t*>(g_hwBase + kChromeTableRva);
    volatile float stableSScale = sScale;
    volatile float stableTScale = tScale;
    RecordCornerDedup(topology);

    if (zeroLights)
    {
        const float zeroSScale = sScale;
        const float zeroTScale = tScale;
        if (g_collectStats)
        {
            g_lambertSkipped += static_cast<std::uint64_t>(topology.corners.size());
            g_lambertZeroBypass += static_cast<std::uint64_t>(topology.lightPairCount);
        }
        std::uint32_t blendBits = 0;
        std::memcpy(&blendBits, g_hwBase + kStudioBlendRva, sizeof(blendBits));
        const CachedCorner* c = topology.uniqueCorners.data();
        const CachedCorner* const end = c + topology.uniqueCorners.size();
        DrawVertex* v = out.data();
        for (; c != end; ++c, ++v)
        {
            const std::size_t chromeBase = static_cast<std::size_t>(c->norm) * 2u;
            v->st[0] = ScaleChromeCoord(chrome[chromeBase + 0u], zeroSScale);
            v->st[1] = ScaleChromeCoord(chrome[chromeBase + 1u], zeroTScale);
            const float* fl = lightValues + static_cast<std::size_t>(c->norm) * 3u;
            const float* pos = aux + static_cast<std::size_t>(c->vert) * 3u;
            std::memcpy(v->xyz, pos, sizeof(v->xyz));
            std::memcpy(v->rgba, fl, 3u * sizeof(float));
            std::memcpy(&v->rgba[3], &blendBits, sizeof(blendBits));
        }
        return true;
    }
    const auto* lightPos = reinterpret_cast<const float*>(g_hwBase + kLightPosRva);
    if (g_collectStats)
        g_lambertSkipped += static_cast<std::uint64_t>(topology.corners.size() - topology.lightPairCount);
    for (std::size_t i = 0; i < topology.uniqueCorners.size(); ++i)
    {
        const CachedCorner& c = topology.uniqueCorners[i];
        DrawVertex& v = out[i];

        // Cached streams reject normal indices above Gold's 0x3FFF limit, so
        // this is the regular chrome branch that emits UV + Lambert + Color4f.
        const std::size_t chromeBase = static_cast<std::size_t>(c.norm) * 2u;
        v.st[0] = ScaleChromeCoord(chrome[chromeBase + 0u], stableSScale);
        v.st[1] = ScaleChromeCoord(chrome[chromeBase + 1u], stableTScale);

        if (c.lightFirst)
        {
            const float* normal = studioNormals + static_cast<std::size_t>(c.norm) * 3u;
            InvokeLightLambert(lightPos + static_cast<std::size_t>(c.vert) * 12u,
                               normal,
                               lightValues + static_cast<std::size_t>(c.norm) * 3u,
                               v.rgba);
            if (g_collectStats) ++g_lambertCalls;
            const float* pos = aux + static_cast<std::size_t>(c.vert) * 3u;
            std::memcpy(v.xyz, pos, sizeof(v.xyz));
        }
        else
        {
            const std::uint32_t firstSlot = topology.lightFirstRenderSlots[c.lightSlot];
            const DrawVertex& first = out[firstSlot];
            std::memcpy(v.xyz, first.xyz, sizeof(v.xyz));
            std::memcpy(v.rgba, first.rgba, 3u * sizeof(float));
        }
        v.rgba[3] = *reinterpret_cast<const float*>(g_hwBase + kStudioBlendRva);
    }
    return true;
}

bool GenerateForcedChromeVertices(const TopologyCacheEntry& topology,
                                  float sScale,
                                  float tScale,
                                  std::vector<DrawVertex>& out,
                                  bool captureColor)
{
    if (!g_hwBase || (captureColor && !g_getFloatv) ||
        topology.corners.empty() || topology.uniqueCorners.empty())
        return false;

    try
    {
        if (out.size() < topology.uniqueCorners.size())
            out.resize(topology.uniqueCorners.size());
    }
    catch (...)
    {
        return false;
    }

    float currentColor[4]{};
    if (captureColor)
        g_getFloatv(GL_CURRENT_COLOR, currentColor);
    const auto* aux = reinterpret_cast<const float*>(g_hwBase + kAuxVertsRva);
    const auto* chrome = reinterpret_cast<const std::int32_t*>(g_hwBase + kChromeTableRva);
    const auto* firstNormal = reinterpret_cast<const std::int32_t*>(g_hwBase + kFirstNormalByVertexRva);
    const float stableSScale = sScale;
    const float stableTScale = tScale;
    RecordCornerDedup(topology);

    for (std::size_t i = 0; i < topology.uniqueCorners.size(); ++i)
    {
        const CachedCorner& c = topology.uniqueCorners[i];
        DrawVertex& v = out[i];

        const std::int32_t selectedNormal = firstNormal[c.vert];
        if (selectedNormal >= 0 && selectedNormal <= 0x3FFF)
        {
            const std::size_t chromeBase = static_cast<std::size_t>(selectedNormal) * 2u;
            v.st[0] = ScaleChromeCoord(chrome[chromeBase + 0u], stableSScale);
            v.st[1] = ScaleChromeCoord(chrome[chromeBase + 1u], stableTScale);
        }
        else
        {
            v.st[0] = ScaleStudioCoord(c.studioS, stableSScale);
            v.st[1] = ScaleStudioCoord(c.studioT, stableTScale);
        }

        const float* pos = aux + static_cast<std::size_t>(c.vert) * 3u;
        v.xyz[0] = pos[0]; v.xyz[1] = pos[1]; v.xyz[2] = pos[2];
        if (captureColor)
            std::memcpy(v.rgba, currentColor, sizeof(v.rgba));
    }
    return true;
}

void ResetPrimitive()
{
    g_capturing = false;
    g_passthrough = false;
    g_haveTex = false;
    g_haveColor = false;
    g_exactPattern = true;
    g_primitiveMode = 0;
    g_vertices.clear();
}

bool BeginArrayScopeRaw(const void* identity,
                        int stride,
                        const float* xyz,
                        const float* rgba,
                        const float* st,
                        bool useColor,
                        int texCoordSize = 2)
{
    if (!identity || !xyz || !st || (useColor && !rgba) || stride <= 0 ||
        (texCoordSize != 2 && texCoordSize != 3))
        return false;

    if (g_arrayScopeRejected)
        return false;

    if (g_arrayScopeActive)
    {
        if (g_arrayUsingVertexVbo)
        {
            // A predecode VBO draw can be followed by a fail-open captured
            // emitter in the same StudioDrawPoints. CPU array pointers are
            // offsets while ARRAY_BUFFER is nonzero, so switch sources before
            // any client-memory fallback is allowed to repoint them.
            g_bindBuffer(GL_ARRAY_BUFFER, 0);
            g_arrayUsingVertexVbo = false;
            g_arrayPointerBase = nullptr;
            g_arrayColorPointerBase = nullptr;
            g_arrayPointerStride = 0;
            g_arrayColorPointerStride = 0;
            g_arrayTexCoordSize = 0;
        }
        // Gold's StudioDrawPoints kernel and the three exact emitters never
        // touch client-array state. Keep the pushed client state open across
        // meshes in this same outer StudioDrawPoints call and only repoint the
        // arrays for the current predecoded vertex buffer.
        if (identity != g_arrayPointerBase || stride != g_arrayPointerStride ||
            texCoordSize != g_arrayTexCoordSize)
        {
            g_vertexPointer(3, GL_FLOAT, stride, xyz);
            g_texCoordPointer(texCoordSize, GL_FLOAT, stride, st);
            g_arrayPointerBase = identity;
            g_arrayPointerStride = stride;
            g_arrayTexCoordSize = texCoordSize;
        }
        if (useColor && (identity != g_arrayColorPointerBase || stride != g_arrayColorPointerStride))
        {
            g_colorPointer(4, GL_FLOAT, stride, rgba);
            g_arrayColorPointerBase = identity;
            g_arrayColorPointerStride = stride;
        }
        if (useColor != g_arrayColorEnabled)
        {
            if ((useColor && !g_enableClientState) || (!useColor && !g_disableClientState))
                return false;
            if (useColor) g_enableClientState(GL_COLOR_ARRAY);
            else g_disableClientState(GL_COLOR_ARRAY);
            g_arrayColorEnabled = useColor;
        }
        return true;
    }

    // Cold entry proves every function the persistent scope may need later,
    // including a possible color-array transition on a later forced-chrome mesh.
    if (!g_drawArrays || !g_enableClientState || !g_disableClientState ||
        !g_vertexPointer || !g_texCoordPointer || !g_colorPointer ||
        !g_pushClientAttrib || !g_popClientAttrib || !g_getIntegerv ||
        !g_clientActiveTexture || !g_bindBuffer || !g_isEnabled)
        return false;

    // Stock immediate mode ignores client arrays. glDrawArrays does not. Only
    // enter the fast path when every unrelated core client array is proven
    // disabled, including texcoord arrays on nonzero texture units. Otherwise
    // replay stock immediate mode without modifying the foreign state.
    if (g_isEnabled(GL_NORMAL_ARRAY) || g_isEnabled(GL_INDEX_ARRAY) ||
        g_isEnabled(GL_EDGE_FLAG_ARRAY) || g_isEnabled(GL_FOG_COORDINATE_ARRAY) ||
        g_isEnabled(GL_SECONDARY_COLOR_ARRAY))
    {
        g_arrayScopeRejected = true;
        return false;
    }

    int checkClientTexture = static_cast<int>(GL_TEXTURE0);
    int maxTextureUnits = 0;
    g_getIntegerv(GL_CLIENT_ACTIVE_TEXTURE, &checkClientTexture);
    g_getIntegerv(GL_MAX_TEXTURE_UNITS, &maxTextureUnits);
    if (maxTextureUnits < 1 || maxTextureUnits > 32)
    {
        g_arrayScopeRejected = true;
        return false;
    }
    for (int unit = 1; unit < maxTextureUnits; ++unit)
    {
        g_clientActiveTexture(GL_TEXTURE0 + static_cast<unsigned>(unit));
        if (g_isEnabled(GL_TEXTURE_COORD_ARRAY))
        {
            g_clientActiveTexture(static_cast<unsigned>(checkClientTexture));
            g_arrayScopeRejected = true;
            return false;
        }
    }
    g_clientActiveTexture(static_cast<unsigned>(checkClientTexture));

    g_getIntegerv(GL_ARRAY_BUFFER_BINDING, &g_previousArrayBuffer);
    g_getIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &g_previousElementArrayBuffer);
    g_getIntegerv(GL_CLIENT_ACTIVE_TEXTURE, &g_previousClientTexture);
    g_pushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    g_bindBuffer(GL_ARRAY_BUFFER, 0);
    g_bindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    g_clientActiveTexture(GL_TEXTURE0);
    g_enableClientState(GL_VERTEX_ARRAY);
    if (useColor) g_enableClientState(GL_COLOR_ARRAY);
    else g_disableClientState(GL_COLOR_ARRAY);
    g_enableClientState(GL_TEXTURE_COORD_ARRAY);

    g_vertexPointer(3, GL_FLOAT, stride, xyz);
    if (useColor) g_colorPointer(4, GL_FLOAT, stride, rgba);
    g_texCoordPointer(texCoordSize, GL_FLOAT, stride, st);
    g_arrayPointerBase = identity;
    g_arrayColorPointerBase = useColor ? identity : nullptr;
    g_arrayPointerStride = stride;
    g_arrayColorPointerStride = useColor ? stride : 0;
    g_arrayTexCoordSize = texCoordSize;
    g_arrayColorEnabled = useColor;
    g_arrayScopeActive = true;
    return true;
}

void FlushDeferredCurrentState()
{
    // Array draws do not update OpenGL's immediate-mode current attributes.
    // Consecutive predecoded Studio meshes overwrite both attributes before
    // using them, so production can defer these two driver calls.  Flush at
    // every boundary where Gold/foreign code can observe or inherit them.
    if (g_deferredTexValid)
    {
        if (g_texCoord2f)
            g_texCoord2f(g_deferredTex[0], g_deferredTex[1]);
        g_deferredTexValid = false;
    }
    if (g_deferredColorValid)
    {
        if (g_color4f)
            g_color4f(g_deferredColor[0], g_deferredColor[1],
                      g_deferredColor[2], g_deferredColor[3]);
        g_deferredColorValid = false;
    }
}

void QueueDeferredCurrentState(const DrawVertex& last, bool useColor)
{
    std::memcpy(g_deferredTex, last.st, sizeof(g_deferredTex));
    g_deferredTexValid = true;
    if (useColor)
    {
        std::memcpy(g_deferredColor, last.rgba, sizeof(g_deferredColor));
        g_deferredColorValid = true;
    }
}

bool BeginArrayScope(const Vertex* base, bool useColor = true)
{
    if (!base)
        return false;
    return BeginArrayScopeRaw(base, sizeof(Vertex), base[0].xyz, base[0].rgba, base[0].st, useColor);
}

bool BeginArrayScope(const DrawVertex* base, bool useColor = true)
{
    if (!base)
        return false;
    return BeginArrayScopeRaw(base, sizeof(DrawVertex), base[0].xyz, base[0].rgba, base[0].st, useColor);
}

void EndArrayScopeImmediate()
{
    if (!g_arrayScopeActive)
        return;
    g_popClientAttrib();
    g_bindBuffer(GL_ARRAY_BUFFER, static_cast<unsigned>(g_previousArrayBuffer));
    g_bindBuffer(GL_ELEMENT_ARRAY_BUFFER, static_cast<unsigned>(g_previousElementArrayBuffer));
    g_clientActiveTexture(static_cast<unsigned>(g_previousClientTexture));
    g_arrayScopeActive = false;
    g_arrayPointerBase = nullptr;
    g_arrayColorPointerBase = nullptr;
    g_arrayPointerStride = 0;
    g_arrayColorPointerStride = 0;
    g_arrayTexCoordSize = 0;
    g_arrayColorEnabled = false;
    g_arrayUsingVertexVbo = false;
}

void EndArrayScope()
{
    if (!g_arrayScopeActive)
        return;
    // Production predecode may emit several meshes inside one StudioDrawPoints
    // call. Keep one exact saved client-state scope until the outermost call
    // returns instead of paying Get/Push/Pop for every mesh.  Explicit CPU->GPU
    // transitions use EndArrayScopeImmediate() above.
    if (g_scopeDepth > 0 && g_activeMode == 1)
        return;
    EndArrayScopeImmediate();
}

bool BeginArrayScopeVbo(const DrawVertex* base, bool useColor)
{
    if (!base || !EnsureStudioVertexBuffer())
        return false;

    // Reuse the exact cold-entry proof/state save from the client-array path.
    // On the first mesh it briefly defines CPU pointers, then switches them to
    // byte offsets into our private VBO. Subsequent meshes keep those offsets.
    if (!g_arrayScopeActive && !BeginArrayScope(base, useColor))
        return false;

    if (!g_arrayUsingVertexVbo)
    {
        g_bindBuffer(GL_ARRAY_BUFFER, g_studioVertexBuffer);
        g_vertexPointer(3, GL_FLOAT, sizeof(DrawVertex), reinterpret_cast<const void*>(0));
        if (useColor)
            g_colorPointer(4, GL_FLOAT, sizeof(DrawVertex), reinterpret_cast<const void*>(12));
        g_texCoordPointer(2, GL_FLOAT, sizeof(DrawVertex), reinterpret_cast<const void*>(28));
        g_arrayPointerBase = nullptr;
        g_arrayColorPointerBase = nullptr;
        g_arrayPointerStride = sizeof(DrawVertex);
        g_arrayColorPointerStride = useColor ? sizeof(DrawVertex) : 0;
        g_arrayUsingVertexVbo = true;
    }
    else if (useColor)
    {
        // Forced chrome can disable color between ordinary meshes. The pointer
        // itself remains offset 12, but refresh it when re-enabling color so a
        // foreign/fallback transition cannot leave a CPU pointer behind.
        g_colorPointer(4, GL_FLOAT, sizeof(DrawVertex), reinterpret_cast<const void*>(12));
    }

    if (useColor != g_arrayColorEnabled)
    {
        if (useColor) g_enableClientState(GL_COLOR_ARRAY);
        else g_disableClientState(GL_COLOR_ARRAY);
        g_arrayColorEnabled = useColor;
    }
    return true;
}

void ReplayEmitter()
{
    for (const PrimitiveRange& p : g_emitterPrimitives)
    {
        worldvbo::StudioImmediateBegin(p.mode);
        const std::size_t end = p.first + p.count;
        for (std::size_t i = p.first; i < end; ++i)
        {
            const Vertex& v = g_emitterRawVertices[i];
            if (v.hasTex) g_texCoord2f(v.st[0], v.st[1]);
            if (v.hasColor) g_color4f(v.rgba[0], v.rgba[1], v.rgba[2], v.rgba[3]);
            g_vertex3f(v.xyz[0], v.xyz[1], v.xyz[2]);
        }
        worldvbo::StudioImmediateEnd();
    }
    ++g_meshReplays;
    g_replayed += static_cast<std::uint64_t>(g_emitterPrimitives.size());
}

void AppendTriangle(const Vertex& a, const Vertex& b, const Vertex& c)
{
    g_emitterTriangles.push_back(a);
    g_emitterTriangles.push_back(b);
    g_emitterTriangles.push_back(c);
}

bool AppendPrimitiveTriangles(unsigned mode, const std::vector<Vertex>& v)
{
    if (v.size() < 3) return true;
    if (mode == GL_TRIANGLE_FAN)
    {
        for (std::size_t i = 1; i + 1 < v.size(); ++i)
            AppendTriangle(v[0], v[i], v[i + 1]);
        return true;
    }
    if (mode == GL_TRIANGLE_STRIP)
    {
        for (std::size_t i = 0; i + 2 < v.size(); ++i)
        {
            if ((i & 1u) == 0)
                AppendTriangle(v[i], v[i + 1], v[i + 2]);
            else
                AppendTriangle(v[i + 1], v[i], v[i + 2]);
        }
        return true;
    }
    return false;
}

bool DrawEmitterBatch()
{
    if (g_emitterTriangles.empty() || !g_drawArrays)
        return false;
    if (!BeginArrayScope(g_emitterTriangles.data()))
        return false;
    g_drawArrays(GL_TRIANGLES, 0, static_cast<int>(g_emitterTriangles.size()));
    EndArrayScope();

    // Array-fed attributes do not become current state. Reproduce the state
    // left behind by the final immediate-mode corner of the Gold emitter.
    if (!g_emitterRawVertices.empty())
    {
        const Vertex& last = g_emitterRawVertices.back();
        if (last.hasTex)
            g_texCoord2f(last.st[0], last.st[1]);
        if (g_effectiveColorKnown)
            g_color4f(g_effectiveColor[0], g_effectiveColor[1],
                      g_effectiveColor[2], g_effectiveColor[3]);
    }
    ++g_meshBatches;
    g_batched += static_cast<std::uint64_t>(g_emitterPrimitives.size());
    g_verticesBatched += static_cast<std::uint64_t>(g_emitterTriangles.size());
    return true;
}

bool VertexBitsEqual(const Vertex& a, const Vertex& b)
{
    return std::memcmp(a.xyz, b.xyz, sizeof(a.xyz)) == 0 &&
           std::memcmp(a.rgba, b.rgba, sizeof(a.rgba)) == 0 &&
           std::memcmp(a.st, b.st, sizeof(a.st)) == 0 &&
           a.hasTex == b.hasTex && a.hasColor == b.hasColor;
}

bool ExpandPredecodedForValidation(const TopologyCacheEntry& topology,
                                   const std::vector<DrawVertex>& uniqueVertices,
                                   std::vector<Vertex>& expanded,
                                   bool useColor)
{
    if (uniqueVertices.size() < topology.uniqueCornerIndices.size() || topology.corners.empty())
        return false;
    try
    {
        expanded.resize(topology.corners.size());
    }
    catch (...)
    {
        return false;
    }
    for (std::size_t i = 0; i < topology.corners.size(); ++i)
    {
        const std::uint32_t slot = topology.corners[i].renderSlot;
        if (slot >= uniqueVertices.size())
            return false;
        const DrawVertex& src = uniqueVertices[slot];
        Vertex& dst = expanded[i];
        std::memcpy(dst.xyz, src.xyz, sizeof(dst.xyz));
        std::memcpy(dst.rgba, src.rgba, sizeof(dst.rgba));
        std::memcpy(dst.st, src.st, sizeof(dst.st));
        dst.hasTex = true;
        dst.hasColor = useColor;
    }
    return true;
}

bool DrawPredecodedIndexed(const TopologyCacheEntry& topology,
                           const std::vector<DrawVertex>& vertices,
                           bool useColor = true,
                           bool samplePhase = false)
{
    if (vertices.empty() || vertices.size() < topology.uniqueCornerIndices.size() ||
        topology.corners.empty() || topology.renderTriangleIndices.empty() || !g_drawElements ||
        topology.renderTriangleIndices.size() > static_cast<std::size_t>(INT_MAX))
        return false;
    const std::uint32_t lastSlot = topology.corners.back().renderSlot;
    if (lastSlot >= vertices.size())
        return false;
    const long long submitSetupStart = samplePhase ? prof::Now() : 0;

    // Forced chrome intentionally does not emit glColor4f in Gold and thus
    // inherits the current color from the previous mesh.  Materialize any
    // deferred state before disabling the color array for that draw.
    if (!useColor)
        FlushDeferredCurrentState();

    const int indexBufferMode = g_scopeIndexBufferMode;
    const bool useVertexVbo = g_scopeVertexBufferMode == 1 && indexBufferMode == 0 &&
                              topology.renderMaxIndex < vertices.size();
    if (useVertexVbo)
    {
        if (!BeginArrayScopeVbo(vertices.data(), useColor))
            return false;
        const std::size_t usedVertices = static_cast<std::size_t>(topology.renderMaxIndex) + 1u;
        g_bufferData(GL_ARRAY_BUFFER,
                     static_cast<std::ptrdiff_t>(usedVertices * sizeof(DrawVertex)),
                     vertices.data(), GL_STREAM_DRAW);
    }
    else if (!BeginArrayScope(vertices.data(), useColor))
    {
        return false;
    }

    const bool haveIndexBuffer = indexBufferMode != 0 && EnsureTopologyIndexBuffer(topology);
    const bool validatedIndexBuffer = indexBufferMode == 2 && haveIndexBuffer &&
                                      ValidateTopologyIndexBuffer(topology);
    const bool useIndexBuffer = indexBufferMode == 1 && haveIndexBuffer;
    if (indexBufferMode != 0 && !haveIndexBuffer)
    {
        if (g_collectStats) ++g_indexBufferFallbacks;
    }
    if (indexBufferMode == 2 && haveIndexBuffer && !validatedIndexBuffer)
    {
        if (g_collectStats) ++g_indexBufferFallbacks;
    }

    const int drawRangeMode = g_scopeDrawRangeMode;
    const bool rangeAvailable = g_drawRangeElements != nullptr;
    const bool rangeValidated = drawRangeMode == 2 && rangeAvailable &&
                                ValidateDrawRangeExact(topology);
    if (drawRangeMode != 0 && !rangeAvailable)
    {
        if (g_collectStats) ++g_drawRangeFallbacks;
    }
    if (drawRangeMode == 2 && rangeAvailable && !rangeValidated)
    {
        if (g_collectStats) ++g_drawRangeFallbacks;
    }

    const long long submitDrawStart = samplePhase ? prof::Now() : 0;
    if (useIndexBuffer)
    {
        g_bindBuffer(GL_ELEMENT_ARRAY_BUFFER, topology.indexBuffer);
        g_drawElements(GL_TRIANGLES,
                       static_cast<int>(topology.renderTriangleIndices.size()),
                       GL_UNSIGNED_INT,
                       nullptr);
        // The persistent client-array scope intentionally keeps element-array
        // binding zero between Studio meshes, the outer scope restores the
        // caller's original binding when StudioDrawPoints returns.
        g_bindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }
    else if (drawRangeMode == 1 && rangeAvailable)
    {
        g_drawRangeElements(GL_TRIANGLES,
                            topology.renderMinIndex,
                            topology.renderMaxIndex,
                            static_cast<int>(topology.renderTriangleIndices.size()),
                            GL_UNSIGNED_INT,
                            topology.renderTriangleIndices.data());
        if (g_collectStats) ++g_drawRangeCalls;
    }
    else
    {
        g_drawElements(GL_TRIANGLES,
                       static_cast<int>(topology.renderTriangleIndices.size()),
                       GL_UNSIGNED_INT,
                       topology.renderTriangleIndices.data());
    }
    const long long submitDrawEnd = samplePhase ? prof::Now() : 0;
    EndArrayScope();

    // glDrawElements, like glDrawArrays, does not update immediate-mode current
    // attributes. Gold's emitter leaves the final corner's texcoord/color live.
    // Keep that state logically pending across consecutive predecoded meshes,
    // it is flushed before any path that can inherit/observe it and at the
    // outer StudioDrawPoints boundary.
    const DrawVertex& last = vertices[lastSlot];
    QueueDeferredCurrentState(last, useColor);
    const long long submitRestoreEnd = samplePhase ? prof::Now() : 0;

    if (samplePhase && submitDrawStart >= submitSetupStart &&
        submitDrawEnd >= submitDrawStart && submitRestoreEnd >= submitDrawEnd)
    {
        g_phaseSubmitSetupTicks += static_cast<std::uint64_t>(submitDrawStart - submitSetupStart);
        g_phaseSubmitDrawTicks += static_cast<std::uint64_t>(submitDrawEnd - submitDrawStart);
        g_phaseSubmitRestoreTicks += static_cast<std::uint64_t>(submitRestoreEnd - submitDrawEnd);
    }

    if (g_collectStats)
    {
        const std::uint64_t prims = static_cast<std::uint64_t>(topology.primitives.size());
        g_primitives += prims;
        g_exactPrimitives += prims;
        g_batched += prims;
        ++g_meshBatches;
        g_verticesCaptured += static_cast<std::uint64_t>(topology.corners.size());
        g_verticesBatched += static_cast<std::uint64_t>(topology.renderTriangleIndices.size());
    }
    return true;
}

int ReadPredecodeMode()
{
    int mode = 0;
    __try { if (g_predecodeMode) mode = static_cast<int>(g_predecodeMode->value); }
    __except (EXCEPTION_EXECUTE_HANDLER) { mode = 0; }
    return mode;
}

int __cdecl TryPredecodedStandard(const std::int16_t* stream,
                                  const std::uint8_t* mesh,
                                  const float* studioNormals,
                                  float sScale,
                                  float tScale)
{
    g_predecodeValidationPending = false;
    g_predecodeValidationEntry = nullptr;
    g_predecodeValidationKind = 0;
    g_predecodeValidateLightW = false;
    if (g_collectStats) ++g_predecodeStdAttempts;
    const int mode = g_scopePredecodeMode;
    if (mode != 1 && mode != 2)
    {
        MaterializeDeferredSkinForCpu();
        return 0;
    }

    const bool samplePhase = g_phaseSampleActive && mode == 1;
    const long long topologyStart = samplePhase ? prof::Now() : 0;
    const TopologyCacheEntry* topology = GetTopology(stream, mesh, mode == 2);
    const long long topologyEnd = samplePhase ? prof::Now() : 0;
    if (!topology)
    {
        MaterializeDeferredSkinForCpu();
        if (samplePhase) ++g_phaseMixed;
        if (g_collectStats) ++g_predecodeFallback;
        return 0;
    }
    if (g_scopeGpuMode == 2)
    {
        const bool staticReady =
            EnsureStaticStudioVertexBuffer(*topology) &&
            ValidateStaticStudioVertexBuffer(*topology);
        if (!staticReady && g_collectStats)
            ++g_gpuStaticFallback;
    }
    if (mode == 2)
    {
        if (!SnapshotLightW(*topology, g_predecodeLightW))
        {
            if (g_collectStats) ++g_predecodeFallback;
            return 0;
        }
        FxValidationState fpState{};
        if (!SaveFxValidationState(&fpState))
        {
            if (g_collectStats) ++g_predecodeFallback;
            return 0;
        }
        const bool generated = GenerateStandardVertices(*topology, studioNormals, sScale, tScale,
                                                        g_predecodedVertices);
        const bool expanded = generated && ExpandPredecodedForValidation(*topology,
                                                                         g_predecodedVertices,
                                                                         g_predecodeValidation,
                                                                         true);
        const bool validationReady = FinishLightWValidationSetup(*topology, generated) && expanded;
        const bool fpRestored = RestoreFxValidationState(&fpState);
        if (!validationReady || !fpRestored)
        {
            if (g_collectStats) ++g_predecodeFallback;
            return 0;
        }
        g_predecodeValidateLightW = true;
        g_predecodeValidationEntry = topology;
        g_predecodeValidationPending = true;
        g_predecodeValidationKind = 1;
        return 0; // Gold emitter remains authoritative while validating.
    }

    const bool deferredSkin = studio_fastskin::DeferredSkinActive();
    long long generateStart = samplePhase ? prof::Now() : 0;
    long long generateEnd = generateStart;
    long long submitStart = samplePhase ? prof::Now() : 0;
    long long submitEnd = submitStart;
    bool generated = false;
    bool drawn = false;
    if (g_scopeGpuMode == 1)
    {
        if (g_collectStats) ++g_gpuAttempts;
        drawn = DrawPredecodedGpu(*topology, sScale, tScale);
        if (samplePhase) submitEnd = prof::Now();
        if (g_collectStats)
        {
            if (drawn) ++g_gpuDraws;
            else ++g_gpuFallbacks;
        }
        // A successful static/uniform GPU draw requires no per-corner CPU
        // vertex generation at all.
        generated = drawn;
    }
    if (!drawn)
    {
        EndGpuProgramScope();
        if (samplePhase) generateStart = prof::Now();
        if (deferredSkin)
        {
            if (MaterializeDeferredSkinForCpu())
            {
                generated = GenerateStandardVertices(*topology, studioNormals,
                                                     sScale, tScale,
                                                     g_predecodedVertices);
            }
            else
            {
                generated = false;
            }
        }
        else
        {
            generated = GenerateStandardVertices(*topology, studioNormals,
                                                 sScale, tScale,
                                                 g_predecodedVertices);
        }
        if (samplePhase)
        {
            generateEnd = prof::Now();
            submitStart = generateEnd;
        }
        drawn = generated &&
                DrawPredecodedIndexed(*topology, g_predecodedVertices, true,
                                      samplePhase);
        if (samplePhase) submitEnd = prof::Now();
    }
    if (!generated || !drawn)
    {
        MaterializeDeferredSkinForCpu();
        if (samplePhase) ++g_phaseMixed;
        if (g_collectStats) ++g_predecodeFallback;
        return 0;
    }

    if (samplePhase)
        RecordPhaseSample(*topology,
                          topologyEnd - topologyStart,
                          generateEnd - generateStart,
                          submitEnd - submitStart);

    if (g_collectStats)
    {
        ++g_predecodeFast;
        ++g_predecodeStdFast;
    }
    return 1;
}

int __cdecl TryPredecodedAltUv(const std::int16_t* stream,
                               const std::uint8_t* mesh,
                               const float* studioNormals)
{
    EndGpuProgramScope();
    if (!MaterializeDeferredSkinForCpu())
        return 0;
    g_predecodeValidationPending = false;
    g_predecodeValidationEntry = nullptr;
    g_predecodeValidationKind = 0;
    g_predecodeValidateLightW = false;
    if (g_collectStats) ++g_predecodeAltAttempts;
    const int mode = g_scopePredecodeMode;
    if (mode != 1 && mode != 2)
        return 0;

    const bool samplePhase = g_phaseSampleActive && mode == 1;
    const long long topologyStart = samplePhase ? prof::Now() : 0;
    const TopologyCacheEntry* topology = GetTopology(stream, mesh, mode == 2);
    const long long topologyEnd = samplePhase ? prof::Now() : 0;
    if (!topology)
    {
        if (samplePhase) ++g_phaseMixed;
        if (g_collectStats) ++g_predecodeFallback;
        return 0;
    }

    if (mode == 2)
    {
        if (!SnapshotLightW(*topology, g_predecodeLightW))
        {
            if (g_collectStats) ++g_predecodeFallback;
            return 0;
        }
        FxValidationState fpState{};
        if (!SaveFxValidationState(&fpState))
        {
            if (g_collectStats) ++g_predecodeFallback;
            return 0;
        }
        const bool generated = GenerateAltUvVertices(*topology, studioNormals, g_predecodedVertices);
        const bool expanded = generated && ExpandPredecodedForValidation(*topology,
                                                                         g_predecodedVertices,
                                                                         g_predecodeValidation,
                                                                         true);
        const bool validationReady = FinishLightWValidationSetup(*topology, generated) && expanded;
        const bool fpRestored = RestoreFxValidationState(&fpState);
        if (!validationReady || !fpRestored)
        {
            if (g_collectStats) ++g_predecodeFallback;
            return 0;
        }
        g_predecodeValidateLightW = true;
        g_predecodeValidationEntry = topology;
        g_predecodeValidationPending = true;
        g_predecodeValidationKind = 2;
        return 0;
    }

    const long long generateStart = samplePhase ? prof::Now() : 0;
    const bool generated = GenerateAltUvVertices(*topology, studioNormals, g_predecodedVertices);
    const long long generateEnd = samplePhase ? prof::Now() : 0;
    const long long submitStart = samplePhase ? prof::Now() : 0;
    const bool drawn = generated && DrawPredecodedIndexed(*topology, g_predecodedVertices, true, samplePhase);
    const long long submitEnd = samplePhase ? prof::Now() : 0;
    if (!generated || !drawn)
    {
        if (samplePhase) ++g_phaseMixed;
        if (g_collectStats) ++g_predecodeFallback;
        return 0;
    }

    if (samplePhase)
        RecordPhaseSample(*topology,
                          topologyEnd - topologyStart,
                          generateEnd - generateStart,
                          submitEnd - submitStart);

    if (g_collectStats)
    {
        ++g_predecodeFast;
        ++g_predecodeAltFast;
    }
    return 1;
}

int __cdecl TryPredecodedChrome(const std::int16_t* stream,
                                const std::uint8_t* mesh,
                                const float* studioNormals,
                                float sScale,
                                float tScale)
{
    EndGpuProgramScope();
    if (!MaterializeDeferredSkinForCpu())
        return 0;
    g_predecodeValidationPending = false;
    g_predecodeValidationEntry = nullptr;
    g_predecodeValidationKind = 0;
    g_predecodeValidateLightW = false;
    if (g_collectStats) ++g_predecodeChromeAttempts;
    const int mode = g_scopePredecodeMode;
    if (mode != 1 && mode != 2)
        return 0;

    std::uint32_t forceFlags = 0;
    if (!ReadForceFaceFlagsSafe(forceFlags))
    {
        if (g_collectStats) ++g_predecodeFallback;
        return 0;
    }
    const bool samplePhase = g_phaseSampleActive && mode == 1;
    const long long topologyStart = samplePhase ? prof::Now() : 0;
    const TopologyCacheEntry* topology = GetTopology(stream, mesh, mode == 2);
    const long long topologyEnd = samplePhase ? prof::Now() : 0;
    if (!topology)
    {
        if (samplePhase) ++g_phaseMixed;
        if (g_collectStats) ++g_predecodeFallback;
        return 0;
    }

    const bool forcedChrome = (forceFlags & 2u) != 0;

    if (mode == 2)
    {
        bool generated = false;
        FxValidationState fpState{};
        bool fpSaved = false;
        if (forcedChrome)
        {
            fpSaved = SaveFxValidationState(&fpState);
            if (fpSaved)
            {
                generated = GenerateForcedChromeVertices(*topology, sScale, tScale,
                                                         g_predecodedVertices, true);
                if (generated)
                    generated = ExpandPredecodedForValidation(*topology, g_predecodedVertices,
                                                              g_predecodeValidation, false);
            }
        }
        else
        {
            if (SnapshotLightW(*topology, g_predecodeLightW))
            {
                fpSaved = SaveFxValidationState(&fpState);
                if (fpSaved)
                {
                    generated = GenerateRegularChromeVertices(*topology, studioNormals, sScale, tScale,
                                                              g_predecodedVertices);
                    const bool expanded = generated && ExpandPredecodedForValidation(*topology,
                                                                                     g_predecodedVertices,
                                                                                     g_predecodeValidation,
                                                                                     true);
                    generated = FinishLightWValidationSetup(*topology, generated) && expanded;
                    g_predecodeValidateLightW = generated;
                }
            }
        }
        const bool fpRestored = fpSaved && RestoreFxValidationState(&fpState);
        generated = generated && fpRestored;
        if (!generated)
        {
            if (g_collectStats)
            {
                ++g_predecodeFallback;
                if (forcedChrome) ++g_predecodeChromeForcedFallback;
            }
            return 0;
        }
        g_predecodeValidationEntry = topology;
        g_predecodeValidationPending = true;
        g_predecodeValidationKind = forcedChrome ? 4 : 3;
        return 0;
    }

    bool generated = false;
    bool drawn = false;
    long long generateStart = samplePhase ? prof::Now() : 0;
    long long generateEnd = generateStart;
    long long submitStart = generateStart;
    long long submitEnd = generateStart;
    if (forcedChrome)
    {
        generated = GenerateForcedChromeVertices(*topology, sScale, tScale,
                                                 g_predecodedVertices, false);
        generateEnd = samplePhase ? prof::Now() : 0;
        submitStart = samplePhase ? prof::Now() : 0;
        if (generated)
            drawn = DrawPredecodedIndexed(*topology, g_predecodedVertices, false, samplePhase);
        submitEnd = samplePhase ? prof::Now() : 0;
    }
    else
    {
        generated = GenerateRegularChromeVertices(*topology, studioNormals, sScale, tScale,
                                                  g_predecodedVertices);
        generateEnd = samplePhase ? prof::Now() : 0;
        submitStart = samplePhase ? prof::Now() : 0;
        if (generated)
            drawn = DrawPredecodedIndexed(*topology, g_predecodedVertices, true, samplePhase);
        submitEnd = samplePhase ? prof::Now() : 0;
    }
    if (!generated || !drawn)
    {
        if (samplePhase) ++g_phaseMixed;
        if (g_collectStats)
        {
            ++g_predecodeFallback;
            if (forcedChrome) ++g_predecodeChromeForcedFallback;
        }
        return 0;
    }

    if (samplePhase)
        RecordPhaseSample(*topology,
                          topologyEnd - topologyStart,
                          generateEnd - generateStart,
                          submitEnd - submitStart);

    if (g_collectStats)
    {
        ++g_predecodeFast;
        ++g_predecodeChromeFast;
        if (forcedChrome) ++g_predecodeChromeForcedFast;
    }
    return 1;
}

void __cdecl ValidatePredecodedStandardAfterGold()
{
    if (!g_predecodeValidationPending)
        return;
    g_predecodeValidationPending = false;
    const TopologyCacheEntry* topology = g_predecodeValidationEntry;
    g_predecodeValidationEntry = nullptr;
    const bool validateLightW = g_predecodeValidateLightW;
    g_predecodeValidateLightW = false;
    ++g_predecodeValidate;
    if (g_predecodeValidationKind == 1) ++g_predecodeStdValidate;
    else if (g_predecodeValidationKind == 2) ++g_predecodeAltValidate;
    else if (g_predecodeValidationKind == 3) ++g_predecodeChromeValidate;
    else if (g_predecodeValidationKind == 4) ++g_predecodeChromeForcedValidate;
    g_predecodeValidationKind = 0;

    bool lightWEqual = true;
    std::size_t lightWMismatch = static_cast<std::size_t>(-1);
    if (validateLightW)
    {
        if (!topology || !SnapshotLightW(*topology, g_predecodeLightWGold) ||
            g_predecodeLightWGenerated.size() != g_predecodeLightWGold.size())
        {
            lightWEqual = false;
        }
        else
        {
            for (std::size_t i = 0; i < g_predecodeLightWGenerated.size(); ++i)
            {
                if (g_predecodeLightWGenerated[i] != g_predecodeLightWGold[i])
                {
                    lightWEqual = false;
                    lightWMismatch = i;
                    break;
                }
            }
        }
    }

    bool equal = topology != nullptr &&
                 topology->corners.size() == g_predecodeValidation.size() &&
                 g_emitterRawVertices.size() == g_predecodeValidation.size() &&
                 g_emitterPrimitives.size() == topology->primitives.size() &&
                 lightWEqual;
    if (equal)
    {
        for (std::size_t i = 0; i < topology->primitives.size(); ++i)
        {
            const TopologyPrimitive& a = topology->primitives[i];
            const PrimitiveRange& b = g_emitterPrimitives[i];
            if (a.mode != b.mode || a.first != b.first || a.count != b.count)
            {
                equal = false;
                break;
            }
        }
    }
    if (equal)
    {
        for (std::size_t i = 0; i < g_predecodeValidation.size(); ++i)
        {
            if (!VertexBitsEqual(g_predecodeValidation[i], g_emitterRawVertices[i]))
            {
                equal = false;
                break;
            }
        }
    }
    if (equal)
    {
        // Also prove the cached strip/fan -> triangle winding against the
        // existing, independently-built capture path.
        if (topology->triangleIndices.size() != g_emitterTriangles.size())
            equal = false;
        else
        {
            for (std::size_t i = 0; i < topology->triangleIndices.size(); ++i)
            {
                const std::uint32_t source = topology->triangleIndices[i];
                if (source >= g_emitterRawVertices.size() ||
                    !VertexBitsEqual(g_emitterRawVertices[source], g_emitterTriangles[i]))
                {
                    equal = false;
                    break;
                }
            }
        }
    }
    if (!equal && !g_predecodeMismatchLogged)
    {
        g_predecodeMismatchLogged = true;
        rendererlog::Line("studiopredecode mismatch: corners=%llu generated=%llu raw=%llu primCached=%llu primRaw=%llu idx=%llu tris=%llu",
                   static_cast<unsigned long long>(topology ? topology->corners.size() : 0),
                   static_cast<unsigned long long>(g_predecodeValidation.size()),
                   static_cast<unsigned long long>(g_emitterRawVertices.size()),
                   static_cast<unsigned long long>(topology ? topology->primitives.size() : 0),
                   static_cast<unsigned long long>(g_emitterPrimitives.size()),
                   static_cast<unsigned long long>(topology ? topology->triangleIndices.size() : 0),
                   static_cast<unsigned long long>(g_emitterTriangles.size()));

        if (!lightWEqual)
        {
            if (topology && lightWMismatch != static_cast<std::size_t>(-1))
            {
                const std::size_t cornerIndex = lightWMismatch / 3u;
                const unsigned lightIndex = static_cast<unsigned>(lightWMismatch % 3u);
                const unsigned vert = cornerIndex < topology->corners.size()
                    ? static_cast<unsigned>(topology->corners[cornerIndex].vert) : 0xFFFFFFFFu;
                rendererlog::Line("studiopredecode first light W diff corner=%llu vert=%u light=%u generated=%08X gold=%08X",
                           static_cast<unsigned long long>(cornerIndex), vert, lightIndex,
                           g_predecodeLightWGenerated[lightWMismatch],
                           g_predecodeLightWGold[lightWMismatch]);
            }
            else
            {
                rendererlog::Line("studiopredecode light W validation unavailable/size mismatch generated=%llu gold=%llu",
                           static_cast<unsigned long long>(g_predecodeLightWGenerated.size()),
                           static_cast<unsigned long long>(g_predecodeLightWGold.size()));
            }
        }

        if (topology)
        {
            const std::size_t pn = topology->primitives.size() < g_emitterPrimitives.size()
                ? topology->primitives.size() : g_emitterPrimitives.size();
            for (std::size_t i = 0; i < pn; ++i)
            {
                const TopologyPrimitive& a = topology->primitives[i];
                const PrimitiveRange& b = g_emitterPrimitives[i];
                if (a.mode != b.mode || a.first != b.first || a.count != b.count)
                {
                    rendererlog::Line("studiopredecode first primitive diff i=%llu cache=(%u,%u,%u) gold=(%u,%llu,%llu)",
                               static_cast<unsigned long long>(i), a.mode, a.first, a.count,
                               b.mode, static_cast<unsigned long long>(b.first),
                               static_cast<unsigned long long>(b.count));
                    break;
                }
            }
        }

        const std::size_t vn = g_predecodeValidation.size() < g_emitterRawVertices.size()
            ? g_predecodeValidation.size() : g_emitterRawVertices.size();
        for (std::size_t i = 0; i < vn; ++i)
        {
            const Vertex& a = g_predecodeValidation[i];
            const Vertex& b = g_emitterRawVertices[i];
            if (!VertexBitsEqual(a, b))
            {
                std::uint32_t ax[9]{}, bx[9]{};
                std::memcpy(&ax[0], a.xyz, sizeof(a.xyz));
                std::memcpy(&ax[3], a.rgba, sizeof(a.rgba));
                std::memcpy(&ax[7], a.st, sizeof(a.st));
                std::memcpy(&bx[0], b.xyz, sizeof(b.xyz));
                std::memcpy(&bx[3], b.rgba, sizeof(b.rgba));
                std::memcpy(&bx[7], b.st, sizeof(b.st));
                rendererlog::Line("studiopredecode first vertex diff i=%llu xyz %08X/%08X %08X/%08X %08X/%08X rgba %08X/%08X %08X/%08X %08X/%08X %08X/%08X st %08X/%08X %08X/%08X flags %d/%d %d/%d",
                           static_cast<unsigned long long>(i),
                           ax[0], bx[0], ax[1], bx[1], ax[2], bx[2],
                           ax[3], bx[3], ax[4], bx[4], ax[5], bx[5], ax[6], bx[6],
                           ax[7], bx[7], ax[8], bx[8],
                           a.hasTex ? 1 : 0, b.hasTex ? 1 : 0,
                           a.hasColor ? 1 : 0, b.hasColor ? 1 : 0);
                break;
            }
        }
    }
    if (!equal)
        ++g_predecodeMismatch;
}

void __cdecl BeginEmitterCapture()
{
    if (!ScopeActive() || !g_emitHooksReady || (g_activeMode != 1 && g_activeMode != 2))
        return;
    if (g_activeMode == 1)
    {
        EndGpuProgramScope();
        MaterializeDeferredSkinForCpu();
        FlushDeferredCurrentState();
    }
    g_emitterActive = true;
    g_emitterFailed = false;
    g_effectiveColorKnown = false;
    g_emitterRawVertices.clear();
    g_emitterTriangles.clear();
    g_emitterPrimitives.clear();
    ResetPrimitive();
}

void __cdecl EndEmitterCapture()
{
    if (!g_emitterActive)
        return;

    if (g_capturing)
    {
        // The exact Gold emitters always balance Begin/End. Treat a missing End
        // as an unsafe mesh and replay whatever was fully captured.
        g_emitterFailed = true;
        ResetPrimitive();
    }

    if (g_activeMode == 1)
    {
        if (g_emitterFailed || !DrawEmitterBatch())
        {
            ReplayEmitter();
            g_fallback += static_cast<std::uint64_t>(g_emitterPrimitives.size());
        }
    }

    g_emitterActive = false;
    g_emitterFailed = false;
    g_emitterRawVertices.clear();
    g_emitterTriangles.clear();
    g_emitterPrimitives.clear();
}

void LogStats()
{
    if (g_drawPointsCalls != 1 && (g_drawPointsCalls & 0x7FFu) != 0) return;
    rendererlog::Line("studiobatch: calls=%llu prim=%llu exact=%llu batched=%llu replay=%llu meshBatch=%llu meshReplay=%llu fallback=%llu verts=%llu captured=%llu preBuild=%llu preHit=%llu genHit=%llu genBump=%llu byteChk=%llu owner=%llu/%llu preFast=%llu preFallback=%llu preVal=%llu preMis=%llu preStd=%llu/%llu/%llu preAlt=%llu/%llu/%llu preChrome=%llu/%llu/%llu forced=%llu/%llu/%llu lambert=%llu skipped=%llu zeroLambert=%llu cornerIn=%llu cornerUnique=%llu ibo=%llu/%llu/%llu val=%llu/%llu range=%llu/%llu valR=%llu/%llu gpuStatic=%llu/%llu val=%llu/%llu fallback=%llu gpu=%llu/%llu/%llu",
               static_cast<unsigned long long>(g_drawPointsCalls),
               static_cast<unsigned long long>(g_primitives),
               static_cast<unsigned long long>(g_exactPrimitives),
               static_cast<unsigned long long>(g_batched),
               static_cast<unsigned long long>(g_replayed),
               static_cast<unsigned long long>(g_meshBatches),
               static_cast<unsigned long long>(g_meshReplays),
               static_cast<unsigned long long>(g_fallback),
               static_cast<unsigned long long>(g_verticesBatched),
               static_cast<unsigned long long>(g_verticesCaptured),
               static_cast<unsigned long long>(g_predecodeBuilds),
               static_cast<unsigned long long>(g_predecodeHits),
               static_cast<unsigned long long>(g_predecodeGenerationHits),
               static_cast<unsigned long long>(g_studioGenerationBumps.load(std::memory_order_relaxed)),
               static_cast<unsigned long long>(g_predecodeByteChecks),
               static_cast<unsigned long long>(g_topologyOwnerResolved),
               static_cast<unsigned long long>(g_topologyOwnerFallback),
               static_cast<unsigned long long>(g_predecodeFast),
               static_cast<unsigned long long>(g_predecodeFallback),
               static_cast<unsigned long long>(g_predecodeValidate),
               static_cast<unsigned long long>(g_predecodeMismatch),
               static_cast<unsigned long long>(g_predecodeStdAttempts),
               static_cast<unsigned long long>(g_predecodeStdFast),
               static_cast<unsigned long long>(g_predecodeStdValidate),
               static_cast<unsigned long long>(g_predecodeAltAttempts),
               static_cast<unsigned long long>(g_predecodeAltFast),
               static_cast<unsigned long long>(g_predecodeAltValidate),
               static_cast<unsigned long long>(g_predecodeChromeAttempts),
               static_cast<unsigned long long>(g_predecodeChromeFast),
               static_cast<unsigned long long>(g_predecodeChromeValidate),
               static_cast<unsigned long long>(g_predecodeChromeForcedFast),
               static_cast<unsigned long long>(g_predecodeChromeForcedValidate),
               static_cast<unsigned long long>(g_predecodeChromeForcedFallback),
               static_cast<unsigned long long>(g_lambertCalls),
               static_cast<unsigned long long>(g_lambertSkipped),
               static_cast<unsigned long long>(g_lambertZeroBypass),
               static_cast<unsigned long long>(g_cornerInputs),
               static_cast<unsigned long long>(g_uniqueCornerOutputs),
               static_cast<unsigned long long>(g_indexBufferBuilds),
               static_cast<unsigned long long>(g_indexBufferHits),
               static_cast<unsigned long long>(g_indexBufferFallbacks),
               static_cast<unsigned long long>(g_indexBufferValidate),
               static_cast<unsigned long long>(g_indexBufferMismatch),
               static_cast<unsigned long long>(g_drawRangeCalls),
               static_cast<unsigned long long>(g_drawRangeFallbacks),
               static_cast<unsigned long long>(g_drawRangeValidate),
               static_cast<unsigned long long>(g_drawRangeMismatch),
               static_cast<unsigned long long>(g_gpuStaticBuilds),
               static_cast<unsigned long long>(g_gpuStaticHits),
               static_cast<unsigned long long>(g_gpuStaticValidate),
               static_cast<unsigned long long>(g_gpuStaticMismatch),
               static_cast<unsigned long long>(g_gpuStaticFallback),
               static_cast<unsigned long long>(g_gpuAttempts),
               static_cast<unsigned long long>(g_gpuDraws),
               static_cast<unsigned long long>(g_gpuFallbacks));
    rendererlog::Line(
        "studio gpu fallback: static=%llu index=%llu program=%llu limits=%llu data=%llu draw=%llu",
        static_cast<unsigned long long>(g_gpuFailStatic),
        static_cast<unsigned long long>(g_gpuFailIndex),
        static_cast<unsigned long long>(g_gpuFailProgram),
        static_cast<unsigned long long>(g_gpuFailLimits),
        static_cast<unsigned long long>(g_gpuFailLights),
        static_cast<unsigned long long>(g_gpuFailDraw));
}

void LogPhaseStats()
{
    if (!g_phaseMeshes || (g_drawPointsCalls & 0x7FFu) != 0)
        return;
    LARGE_INTEGER fq{};
    QueryPerformanceFrequency(&fq);
    if (!fq.QuadPart)
        return;
    const double scale = 1.0e6 / static_cast<double>(fq.QuadPart) /
                         static_cast<double>(g_phaseMeshes);
    rendererlog::Line("studiophase: meshes=%llu mixed=%llu corners=%llu indices=%llu topo_us=%.3f gen_us=%.3f submit_us=%.3f setup_us=%.3f draw_us=%.3f restore_us=%.3f",
               static_cast<unsigned long long>(g_phaseMeshes),
               static_cast<unsigned long long>(g_phaseMixed),
               static_cast<unsigned long long>(g_phaseCorners),
               static_cast<unsigned long long>(g_phaseIndices),
               static_cast<double>(g_phaseTopologyTicks) * scale,
               static_cast<double>(g_phaseGenerateTicks) * scale,
               static_cast<double>(g_phaseSubmitTicks) * scale,
               static_cast<double>(g_phaseSubmitSetupTicks) * scale,
               static_cast<double>(g_phaseSubmitDrawTicks) * scale,
               static_cast<double>(g_phaseSubmitRestoreTicks) * scale);
}

void LogAbStats()
{
    const std::uint64_t total = g_abStockCalls + g_abBatchCalls;
    if (total == 0 || (total & 0x7FFu) != 0)
        return;
    LARGE_INTEGER fq{};
    QueryPerformanceFrequency(&fq);
    if (!fq.QuadPart) return;
    const double stockUs = g_abStockCalls
        ? 1.0e6 * static_cast<double>(g_abStockTicks) /
          static_cast<double>(fq.QuadPart) / static_cast<double>(g_abStockCalls) : 0.0;
    const double batchUs = g_abBatchCalls
        ? 1.0e6 * static_cast<double>(g_abBatchTicks) /
          static_cast<double>(fq.QuadPart) / static_cast<double>(g_abBatchCalls) : 0.0;
    rendererlog::Line("studiobatch_ab: stock_us=%.3f batch_us=%.3f stockN=%llu batchN=%llu",
               stockUs, batchUs,
               static_cast<unsigned long long>(g_abStockCalls),
               static_cast<unsigned long long>(g_abBatchCalls));
}

bool RefreshEngineSlot()
{
    if (!g_clientBase || !g_hwBase || !g_originalDrawPoints)
        return false;
    auto** slot = reinterpret_cast<void**>(g_clientBase + kEngineStudioDrawPointsSlotRva);
    void* replacement = reinterpret_cast<void*>(&StudioDrawPoints_Hook);
    void* expected = reinterpret_cast<void*>(g_originalDrawPoints);
    __try
    {
        if (*slot == replacement)
            return true;
        if (*slot == expected)
        {
            if (PatchPointer(slot, expected, replacement))
            {
                rendererlog::Line("studiobatch: restored StudioDrawPoints slot after engine table refresh");
                g_slotWarned = false;
                return true;
            }
            return false;
        }
        if (!g_slotWarned)
        {
            rendererlog::Line("studiobatch: StudioDrawPoints slot changed unexpectedly (%p), leaving untouched", *slot);
            g_slotWarned = true;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
    return false;
}

void WINAPI TexCoord2f_Hook(float s, float t)
{
    // Once TryBegin has suppressed the real glBegin, this capture is a
    // transaction.  Do not abandon it merely because the outer scope/mode was
    // changed asynchronously before glEnd, doing so would forward vertices to
    // GL without a matching real glBegin.
    if (!g_capturing || g_passthrough)
    {
        // A real setter supersedes any logically deferred value.
        g_deferredTexValid = false;
        if (g_texCoord2f) g_texCoord2f(s, t);
        return;
    }
    if (g_haveTex || g_haveColor)
        g_exactPattern = false;
    g_pendingTex[0] = s;
    g_pendingTex[1] = t;
    g_haveTex = true;
}

void WINAPI Color4f_Hook(float r, float g, float b, float a)
{
    if (!g_capturing || g_passthrough)
    {
        g_deferredColorValid = false;
        if (g_color4f) g_color4f(r, g, b, a);
        return;
    }
    if (!g_haveTex || g_haveColor)
        g_exactPattern = false;
    g_pendingColor[0] = r;
    g_pendingColor[1] = g;
    g_pendingColor[2] = b;
    g_pendingColor[3] = a;
    g_haveColor = true;
}

void WINAPI Vertex3f_Hook(float x, float y, float z)
{
    if (!g_capturing || g_passthrough)
    {
        if (g_vertex3f) g_vertex3f(x, y, z);
        return;
    }
    if (!g_haveTex)
        g_exactPattern = false;

    if (g_haveColor)
    {
        std::memcpy(g_effectiveColor, g_pendingColor, sizeof(g_effectiveColor));
        g_effectiveColorKnown = true;
    }
    else if (!g_effectiveColorKnown)
    {
        if (g_getFloatv)
        {
            g_getFloatv(GL_CURRENT_COLOR, g_effectiveColor);
            g_effectiveColorKnown = true;
        }
        else
        {
            g_exactPattern = false;
        }
    }

    Vertex v{};
    v.xyz[0] = x; v.xyz[1] = y; v.xyz[2] = z;
    std::memcpy(v.st, g_pendingTex, sizeof(v.st));
    std::memcpy(v.rgba, g_effectiveColor, sizeof(v.rgba));
    v.hasTex = g_haveTex;
    v.hasColor = g_haveColor;
    if (g_haveTex)
        std::memcpy(g_lastTex, g_pendingTex, sizeof(g_lastTex));
    g_vertices.push_back(v);
    g_haveTex = false;
    g_haveColor = false;
}

bool RefreshQglHooks()
{
    if (!g_hwBase) return false;
    auto** texSlot = reinterpret_cast<void**>(g_hwBase + kQglTexCoord2fRva);
    auto** colorSlot = reinterpret_cast<void**>(g_hwBase + kQglColor4fRva);
    auto** vertexSlot = reinterpret_cast<void**>(g_hwBase + kQglVertex3fRva);
    if (!*texSlot || !*colorSlot || !*vertexSlot) return false;

    if (*texSlot != reinterpret_cast<void*>(&TexCoord2f_Hook))
    {
        g_texCoord2f = reinterpret_cast<GlTexCoord2fFn>(*texSlot);
        if (!PatchPointer(texSlot, reinterpret_cast<void*>(g_texCoord2f), reinterpret_cast<void*>(&TexCoord2f_Hook)))
            return false;
    }
    if (*colorSlot != reinterpret_cast<void*>(&Color4f_Hook))
    {
        g_color4f = reinterpret_cast<GlColor4fFn>(*colorSlot);
        if (!PatchPointer(colorSlot, reinterpret_cast<void*>(g_color4f), reinterpret_cast<void*>(&Color4f_Hook)))
            return false;
    }
    if (*vertexSlot != reinterpret_cast<void*>(&Vertex3f_Hook))
    {
        g_vertex3f = reinterpret_cast<GlVertex3fFn>(*vertexSlot);
        if (!PatchPointer(vertexSlot, reinterpret_cast<void*>(g_vertex3f), reinterpret_cast<void*>(&Vertex3f_Hook)))
            return false;
    }

    g_drawArrays = ReadQgl<GlDrawArraysFn>(kQglDrawArraysRva);
    g_drawElements = ReadQgl<GlDrawElementsFn>(kQglDrawElementsRva);
    const GlDrawRangeElementsFn qglDrawRange = ReadQgl<GlDrawRangeElementsFn>(kQglDrawRangeElementsRva);
    g_enableClientState = ReadQgl<GlEnableClientStateFn>(kQglEnableClientStateRva);
    g_disableClientState = ReadQgl<GlDisableClientStateFn>(kQglDisableClientStateRva);
    g_vertexPointer = ReadQgl<GlVertexPointerFn>(kQglVertexPointerRva);
    g_texCoordPointer = ReadQgl<GlTexCoordPointerFn>(kQglTexCoordPointerRva);
    g_colorPointer = ReadQgl<GlColorPointerFn>(kQglColorPointerRva);
    g_pushClientAttrib = ReadQgl<GlPushClientAttribFn>(kQglPushClientAttribRva);
    g_popClientAttrib = ReadQgl<GlPopClientAttribFn>(kQglPopClientAttribRva);
    g_getIntegerv = ReadQgl<GlGetIntegervFn>(kQglGetIntegervRva);
    g_activeTexture = ReadQgl<GlActiveTextureFn>(kQglActiveTextureRva);
    g_clientActiveTexture = ReadQgl<GlClientActiveTextureFn>(kQglClientActiveTextureRva);
    g_bindBuffer = ReadQgl<GlBindBufferFn>(kQglBindBufferRva);
    g_deleteBuffers = ReadQgl<GlDeleteBuffersFn>(kQglDeleteBuffersRva);
    g_genBuffers = ReadQgl<GlGenBuffersFn>(kQglGenBuffersRva);
    g_bufferData = ReadQgl<GlBufferDataFn>(kQglBufferDataRva);
    const std::uint32_t contextGeneration = worldvbo::ContextGeneration();
    if (qglDrawRange)
    {
        g_drawRangeElements = qglDrawRange;
        g_drawRangeGeneration = contextGeneration;
    }
    else if (worldvbo::ContextGenerationReady() && g_drawRangeGeneration != contextGeneration)
    {
        g_drawRangeElements = nullptr;
        __try
        {
            auto getProc = *reinterpret_cast<SdlGetProcAddressFn*>(g_hwBase + kSdlGetProcAddressIatRva);
            if (getProc)
            {
                g_drawRangeElements = reinterpret_cast<GlDrawRangeElementsFn>(getProc("glDrawRangeElements"));
                if (!g_drawRangeElements)
                    g_drawRangeElements = reinterpret_cast<GlDrawRangeElementsFn>(getProc("glDrawRangeElementsEXT"));
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            g_drawRangeElements = nullptr;
        }
        g_drawRangeGeneration = contextGeneration;
    }
    else if (!worldvbo::ContextGenerationReady())
    {
        // The direct SDL proc is context-owned. Without the exact context
        // destroy generation hook we cannot prove it remains valid across a
        // video-mode/context recreation, so fail open to glDrawElements.
        g_drawRangeElements = nullptr;
        g_drawRangeGeneration = 0;
    }
    if (g_getBufferSubDataGeneration != contextGeneration)
    {
        g_getBufferSubData = nullptr;
        g_getBufferParameteriv = nullptr;
        __try
        {
            auto getProc = *reinterpret_cast<SdlGetProcAddressFn*>(g_hwBase + kSdlGetProcAddressIatRva);
            if (getProc)
            {
                g_getBufferSubData = reinterpret_cast<GlGetBufferSubDataFn>(getProc("glGetBufferSubData"));
                g_getBufferParameteriv = reinterpret_cast<GlGetBufferParameterivFn>(
                    getProc("glGetBufferParameteriv"));
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            g_getBufferSubData = nullptr;
            g_getBufferParameteriv = nullptr;
        }
        g_getBufferSubDataGeneration = contextGeneration;
    }
    g_isEnabled = ReadQgl<GlIsEnabledFn>(kQglIsEnabledRva);
    g_getFloatv = ReadQgl<GlGetFloatvFn>(kQglGetFloatvRva);
    return g_texCoord2f && g_color4f && g_vertex3f && g_drawArrays && g_getFloatv;
}

void __cdecl StudioDrawPoints_Dispatch()
{
    if (!g_originalDrawPoints) return;
    void* const slotCaller = _ReturnAddress();
    if (g_scopeDepth > 0 && g_activeMode == 1)
    {
        MaterializeDeferredSkinForCpu();
        EndGpuProgramScope();
        FlushDeferredCurrentState();
    }
    studio_fastlighting::BeginDraw();
    studio_fastchrome::BeginDraw();
    int mode = 0;
    __try { if (g_mode) mode = static_cast<int>(g_mode->value); }
    __except (EXCEPTION_EXECUTE_HANDLER) { mode = 0; }
    if (mode != 1 && mode != 2 && mode != 3)
    {
        g_originalDrawPoints();
        return;
    }
    if (!g_emitHooksReady || !worldvbo::StudioImmediateReady() || !RefreshQglHooks())
    {
        ++g_fallback;
        g_originalDrawPoints();
        return;
    }

    ++g_drawPointsCalls;
    const bool outermost = g_scopeDepth == 0;
    if (outermost)
    {
        g_scopePredecodeMode = ReadPredecodeMode();
        g_scopeIndexBufferMode = ReadIndexBufferMode();
        g_scopeDrawRangeMode = ReadDrawRangeMode();
        g_scopeVertexBufferMode = ReadVertexBufferMode();
        g_scopeGpuMode = ReadGpuMode();
        // Detailed counters/logging are diagnostic-only. Production mode1 with
        // r_profile=0 skips them entirely, validator/A-B modes keep their
        // full oracle telemetry even when profiling is otherwise disabled.
        g_collectStats = prof::Active() || mode == 2 || mode == 3 ||
                         g_scopePredecodeMode == 2 || g_scopeIndexBufferMode == 2 ||
                         g_scopeDrawRangeMode == 2 || g_scopeGpuMode == 2;
        // Phase timing is diagnostic only and piggybacks on r_profile.
        // One outer draw in 256 keeps QPC overhead far below the measured work.
        g_phaseSampleActive = mode == 1 && prof::Active() &&
                              (g_drawPointsCalls & 0xFFu) == 0;
    }
    const bool abMode = mode == 3;
    bool abBatch = false;
    if (abMode)
    {
        // Multiplicative hash prevents a stable entity ordering from pinning a
        // particular model to one side of the A/B across frames.
        const std::uint32_t h = static_cast<std::uint32_t>(++g_abSequence * 2654435761ull);
        abBatch = (h & 0x80000000u) != 0;
    }
    const DWORD thread = GetCurrentThreadId();
    if (g_scopeDepth == 0)
    {
        // A previous abnormal exit must never leak client-array ownership into
        // a later StudioDrawPoints call. In the normal path this is already
        // inactive here.
        studio_fastskin::ClearDeferredSkin();
        EndGpuProgramScope();
        FlushDeferredCurrentState();
        EndArrayScope();
        g_arrayScopeRejected = false;
        g_scopeThread = thread;
        g_activeMode = abMode ? (abBatch ? 1 : 0) : mode;
    }
    ++g_scopeDepth;
    LARGE_INTEGER ab0{}, ab1{};
    if (abMode) QueryPerformanceCounter(&ab0);
    if (g_activeMode == 1)
    {
        g_retainedDirectBridgeCaller = slotCaller;
        InterlockedExchange(&g_retainedDirectBridgeActive, 1);
    }
    g_originalDrawPoints();
    if (g_activeMode == 1)
    {
        InterlockedExchange(&g_retainedDirectBridgeActive, 0);
        g_retainedDirectBridgeCaller = nullptr;
    }
    if (abMode)
    {
        QueryPerformanceCounter(&ab1);
        if (ab1.QuadPart >= ab0.QuadPart)
        {
            const std::uint64_t dt = static_cast<std::uint64_t>(ab1.QuadPart - ab0.QuadPart);
            if (abBatch) { g_abBatchTicks += dt; ++g_abBatchCalls; }
            else { g_abStockTicks += dt; ++g_abStockCalls; }
        }
    }
    const bool finishedOutermost = --g_scopeDepth == 0;
    if (finishedOutermost)
    {
        // Reaching this boundary with a still-deferred skin means every mesh in
        // the draw was emitted by the static GPU path. GpuSkinDeferAllowed()
        // proved that Gold's immediately-following shadow wrapper will skip, and
        // no other post-DrawPoints consumer reads auxverts in this hardware path.
        studio_fastskin::ClearDeferredSkin();
        EndGpuProgramScope();
        FlushDeferredCurrentState();
        EndArrayScope();
        g_arrayScopeRejected = false;
        g_scopeThread = 0;
        g_activeMode = 0;
        g_scopePredecodeMode = 0;
        g_scopeIndexBufferMode = 0;
        g_scopeDrawRangeMode = 0;
        g_scopeVertexBufferMode = 0;
        g_scopeGpuMode = 0;
        ResetPrimitive();
    }
    else
    {
        // Native Gold does not recurse here, but a foreign hook can.  A nested
        // stock StudioDrawPoints leaves its final immediate attributes current
        // before returning to its caller, so materialize our logically pending
        // nested result at the same boundary before the outer draw resumes.
        MaterializeDeferredSkinForCpu();
        EndGpuProgramScope();
        FlushDeferredCurrentState();
    }
    if (g_collectStats)
        LogStats();
    if (finishedOutermost)
    {
        LogPhaseStats();
        g_phaseSampleActive = false;
        g_collectStats = false;
    }
    if (abMode) LogAbStats();
}

__declspec(naked) void StudioDrawPoints_Hook()
{
    __asm
    {
        cmp dword ptr [g_retainedRendererActive], 0
        jne stock
        mov eax, dword ptr [g_mode]
        test eax, eax
        jz stock
        cmp dword ptr [eax + 0Ch], 03F800000h
        je active
        cmp dword ptr [eax + 0Ch], 040000000h
        je active
        cmp dword ptr [eax + 0Ch], 040400000h
        je active
    stock:
        jmp dword ptr [g_originalDrawPoints]
    active:
        jmp StudioDrawPoints_Dispatch
    }
}

__declspec(naked) void EmitStandard_Hook()
{
    __asm
    {
        cmp dword ptr [g_activeMode], 1
        je predecode
        cmp dword ptr [g_activeMode], 2
        je active
    stock:
        jmp dword ptr [g_emitStandard]
    predecode:
        sub esp, 20
        mov dword ptr [esp], ecx
        lea eax, [edi - 8]
        mov dword ptr [esp + 4], eax
        mov dword ptr [esp + 8], edx
        movss dword ptr [esp + 12], xmm2
        movss dword ptr [esp + 16], xmm3
        push dword ptr [esp + 16]
        push dword ptr [esp + 16]
        push dword ptr [esp + 16]
        push dword ptr [esp + 16]
        push dword ptr [esp + 16]
        call TryPredecodedStandard
        add esp, 20
        test eax, eax
        jnz predecoded
        mov ecx, dword ptr [esp]
        mov edx, dword ptr [esp + 8]
        movss xmm2, dword ptr [esp + 12]
        movss xmm3, dword ptr [esp + 16]
        add esp, 20
        jmp active
    predecoded:
        add esp, 20
        ret
    active:
        sub esp, 32
        movups xmmword ptr [esp], xmm2
        movups xmmword ptr [esp + 16], xmm3
        pushad
        call BeginEmitterCapture
        popad
        movups xmm2, xmmword ptr [esp]
        movups xmm3, xmmword ptr [esp + 16]
        add esp, 32
        call dword ptr [g_emitStandard]
        pushad
        call ValidatePredecodedStandardAfterGold
        call EndEmitterCapture
        popad
        ret
    }
}

__declspec(naked) void EmitAltUv_Hook()
{
    __asm
    {
        cmp dword ptr [g_activeMode], 1
        je predecode
        cmp dword ptr [g_activeMode], 2
        je active
    stock:
        jmp dword ptr [g_emitAltUv]
    predecode:
        sub esp, 12
        mov dword ptr [esp], ecx
        lea eax, [edi - 8]
        mov dword ptr [esp + 4], eax
        mov dword ptr [esp + 8], edx
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        call TryPredecodedAltUv
        add esp, 12
        test eax, eax
        jnz predecoded
        mov ecx, dword ptr [esp]
        mov edx, dword ptr [esp + 8]
        add esp, 12
        jmp active
    predecoded:
        add esp, 12
        ret
    active:
        sub esp, 32
        movups xmmword ptr [esp], xmm2
        movups xmmword ptr [esp + 16], xmm3
        pushad
        call BeginEmitterCapture
        popad
        movups xmm2, xmmword ptr [esp]
        movups xmm3, xmmword ptr [esp + 16]
        add esp, 32
        call dword ptr [g_emitAltUv]
        pushad
        call ValidatePredecodedStandardAfterGold
        call EndEmitterCapture
        popad
        ret
    }
}

__declspec(naked) void EmitChrome_Hook()
{
    __asm
    {
        cmp dword ptr [g_activeMode], 1
        je predecode
        cmp dword ptr [g_activeMode], 2
        je active
    stock:
        jmp dword ptr [g_emitChrome]
    predecode:
        sub esp, 20
        mov dword ptr [esp], ecx
        lea eax, [edi - 8]
        mov dword ptr [esp + 4], eax
        mov dword ptr [esp + 8], edx
        movss dword ptr [esp + 12], xmm2
        movss dword ptr [esp + 16], xmm3
        push dword ptr [esp + 16]
        push dword ptr [esp + 16]
        push dword ptr [esp + 16]
        push dword ptr [esp + 16]
        push dword ptr [esp + 16]
        call TryPredecodedChrome
        add esp, 20
        test eax, eax
        jnz predecoded
        mov ecx, dword ptr [esp]
        mov edx, dword ptr [esp + 8]
        movss xmm2, dword ptr [esp + 12]
        movss xmm3, dword ptr [esp + 16]
        add esp, 20
        jmp active
    predecoded:
        add esp, 20
        ret
    active:
        sub esp, 32
        movups xmmword ptr [esp], xmm2
        movups xmmword ptr [esp + 16], xmm3
        pushad
        call BeginEmitterCapture
        popad
        movups xmm2, xmmword ptr [esp]
        movups xmm3, xmmword ptr [esp + 16]
        add esp, 32
        call dword ptr [g_emitChrome]
        pushad
        call ValidatePredecodedStandardAfterGold
        call EndEmitterCapture
        popad
        ret
    }
}
} // namespace

bool GpuSkinDeferAllowed()
{
    int batchMode = 0;
    __try { if (g_mode) batchMode = static_cast<int>(g_mode->value); }
    __except (EXCEPTION_EXECUTE_HANDLER) { batchMode = 0; }

    // Never defer in validators/A-B or nested foreign recursion. The source
    // pointers captured by fastskin are owned by this exact outer DrawPoints.
    if (batchMode != 1 || g_scopeDepth != 1 || g_activeMode != 1 ||
        g_scopePredecodeMode != 1 || g_scopeGpuMode != 1 ||
        g_arrayScopeActive || g_gpuScopeActive ||
        !g_modelGenerationReady || !worldvbo::ContextGenerationReady())
        return false;

    // Initial deferred path is intentionally the dominant Standard/zero-light
    // case. Alt-UV/chrome/elight paths materialize CPU auxverts instead.
    if (ReadNumStudioLightsSafe() != 0)
        return false;
    __try
    {
        if (*reinterpret_cast<const std::uint32_t*>(
                g_hwBase + kForceFaceFlagsRva) != 0)
            return false;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }

    if (!ReadCurrentStudioModelSeh() || !StudioShadowWillBeSkippedSeh())
        return false;

    // Do not skip the CPU skin loop unless the exact GL state/program needed
    // for the eventual static draw is already viable. Any later mesh-specific
    // failure still materializes FastSkin before falling back to Gold.
    return EnsureGpuProgram();
}

bool QueryModelIdentity(const void* studioHeader, const void** ownerModel,
                        int* ownerIndex, std::uint32_t* generation)
{
    if (!studioHeader || !ownerModel || !ownerIndex || !generation ||
        !g_modelGenerationReady)
        return false;

    const std::uint8_t* model = nullptr;
    int index = -1;
    std::uint32_t value = 0;
    if (!ResolveStudioModelOwner(static_cast<const std::uint8_t*>(studioHeader),
                                 model, index, value))
        return false;

    *ownerModel = model;
    *ownerIndex = index;
    *generation = value;
    return true;
}

bool ValidateModelIdentity(const void* studioHeader, const void* ownerModel,
                           int ownerIndex, std::uint32_t generation)
{
    if (!studioHeader || !ownerModel || ownerIndex < 0 ||
        ownerIndex >= kKnownModelMax || !g_modelGenerationReady ||
        !g_hwBase)
        return false;
    __try
    {
        const int count =
            *reinterpret_cast<const int*>(g_hwBase + kKnownModelCountRva);
        if (count < 0 || count > kKnownModelMax || ownerIndex >= count)
            return false;
        const auto* known =
            reinterpret_cast<const std::uint8_t* const*>(
                g_hwBase + kKnownModelTableRva);
        const auto* model =
            static_cast<const std::uint8_t*>(ownerModel);
        if (known[ownerIndex] != model ||
            *reinterpret_cast<const std::int32_t*>(model + 0x44) != 3 ||
            *reinterpret_cast<const std::uint8_t* const*>(
                model + 0x184) != studioHeader)
            return false;
        return g_knownModelGenerations[ownerIndex].load(
                   std::memory_order_relaxed) == generation;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool ShadowWillBeSkipped()
{
    return StudioShadowWillBeSkippedSeh();
}

void SetRetainedRendererActive(bool active)
{
    InterlockedExchange(
        &g_retainedRendererActive,
        active ? 1L : 0L);
}

bool RetainedDirectScopeAllowed()
{
    if (!g_clientBase ||
        InterlockedCompareExchange(
            &g_retainedDirectBridgeActive, 0, 0) == 0)
        return false;
    if (g_scopeDepth <= 0 || g_activeMode != 1 ||
        g_scopeThread != GetCurrentThreadId())
        return false;
    return g_retainedDirectBridgeCaller ==
           g_clientBase + kClientNormalDrawReturnRva;
}

// world_vbo calls these from its qglBegin/qglEnd hooks. We only claim the call
// while inside the exact engine StudioDrawPoints callback.
bool TryBegin(unsigned mode)
{
    if (!ScopeActive() || !g_emitterActive || g_capturing || g_passthrough)
        return false;
    if (mode != GL_TRIANGLE_STRIP && mode != GL_TRIANGLE_FAN)
        return false;
    ResetPrimitive();
    g_capturing = true;
    g_primitiveMode = mode;
    ++g_primitives;
    return true;
}

bool TryEnd()
{
    // TryBegin already owns/suppressed the matching real Begin.  Once that has
    // happened we must complete the transaction even if the outer scope/cvar
    // changed before End, returning false here would execute a real glEnd with
    // no real glBegin.
    if (!g_capturing && !g_passthrough)
        return false;

    // The three inspected Gold emitters all issue exactly texcoord, color,
    // vertex for each valid corner. Anything else is conservatively replayed
    // by returning false only when no calls were suppressed, once captured we
    // must finish the primitive ourselves.
    const bool eligible = g_capturing && !g_haveTex && !g_haveColor && g_exactPattern &&
                          (g_primitiveMode == GL_TRIANGLE_STRIP || g_primitiveMode == GL_TRIANGLE_FAN);
    g_verticesCaptured += static_cast<std::uint64_t>(g_vertices.size());
    if (eligible)
        ++g_exactPrimitives;

    if (g_activeMode == 1 && g_emitterActive)
    {
        PrimitiveRange p{};
        p.mode = g_primitiveMode;
        p.first = g_emitterRawVertices.size();
        p.count = g_vertices.size();
        g_emitterPrimitives.push_back(p);
        g_emitterRawVertices.insert(g_emitterRawVertices.end(), g_vertices.begin(), g_vertices.end());
        if (!eligible || !AppendPrimitiveTriangles(g_primitiveMode, g_vertices))
            g_emitterFailed = true;
    }
    else
    {
        // Mode 2 is the exact capture/replay validator. It suppresses the
        // original immediate primitive, then emits the captured event sequence
        // through world_vbo's saved true Begin/End pointers.
        worldvbo::StudioImmediateBegin(g_primitiveMode);
        for (const Vertex& v : g_vertices)
        {
            if (v.hasTex) g_texCoord2f(v.st[0], v.st[1]);
            if (v.hasColor) g_color4f(v.rgba[0], v.rgba[1], v.rgba[2], v.rgba[3]);
            g_vertex3f(v.xyz[0], v.xyz[1], v.xyz[2]);
        }
        if (g_haveTex) g_texCoord2f(g_pendingTex[0], g_pendingTex[1]);
        if (g_haveColor) g_color4f(g_pendingColor[0], g_pendingColor[1], g_pendingColor[2], g_pendingColor[3]);
        worldvbo::StudioImmediateEnd();
        ++g_replayed;
    }
    ResetPrimitive();
    return true;
}

bool Install(HMODULE client, HMODULE hw, cl_enginefunc_t* engine)
{
    if (!ClientMatches(client) || !hwbuild::MatchesTarget(hw) || !engine || !engine->pfnRegisterVariable)
    {
        rendererlog::Line("studiobatch: exact client/hw/engine unavailable, disabled");
        return false;
    }
    g_clientBase = reinterpret_cast<std::uint8_t*>(client);
    g_hwBase = reinterpret_cast<std::uint8_t*>(hw);
    // A tri-command count is a signed 16-bit value. Reserving its full
    // positive domain guarantees the qgl callbacks never allocate/reallocate
    // while inside an active glBegin/glEnd capture.
    g_vertices.reserve(32768);
    g_emitterRawVertices.reserve(131072);
    g_emitterTriangles.reserve(262144);
    g_emitterPrimitives.reserve(32768);
    g_predecodedVertices.reserve(131072);
    g_predecodeValidation.reserve(131072);
    for (auto& generation : g_knownModelGenerations)
        generation.store(1u, std::memory_order_relaxed);
    __try { g_mode = engine->pfnRegisterVariable("r_studio_batch", "1", 0); }
    __except (EXCEPTION_EXECUTE_HANDLER) { g_mode = nullptr; }
    __try { g_predecodeMode = engine->pfnRegisterVariable("r_studio_predecode", "1", 0); }
    __except (EXCEPTION_EXECUTE_HANDLER) { g_predecodeMode = nullptr; }
    __try { g_indexBufferMode = engine->pfnRegisterVariable("r_studio_indexbuffer", "0", 0); }
    __except (EXCEPTION_EXECUTE_HANDLER) { g_indexBufferMode = nullptr; }
    __try { g_drawRangeMode = engine->pfnRegisterVariable("r_studio_drawrange", "1", 0); }
    __except (EXCEPTION_EXECUTE_HANDLER) { g_drawRangeMode = nullptr; }
    __try { g_vertexBufferMode = engine->pfnRegisterVariable("r_studio_vertexbuffer", "0", 0); }
    __except (EXCEPTION_EXECUTE_HANDLER) { g_vertexBufferMode = nullptr; }
    __try { g_gpuMode = engine->pfnRegisterVariable("r_studio_gpu", "0", 0); }
    __except (EXCEPTION_EXECUTE_HANDLER) { g_gpuMode = nullptr; }
    g_lightLambert = g_hwBase + kLightLambertRva;

    g_modelGenerationReady = InstallModelGenerationHook();
    rendererlog::Line("studiobatch: Studio cache-free generation invalidation %s (hw+0x%X), topology hits %s",
               g_modelGenerationReady ? "installed" : "unavailable",
               static_cast<unsigned>(kCacheFreeRva),
               g_modelGenerationReady ? "skip raw memcmp in mode1" : "retain raw memcmp");

    auto** slot = reinterpret_cast<void**>(g_clientBase + kEngineStudioDrawPointsSlotRva);
    void* expected = g_hwBase + kHwStudioDrawPointsRva;
    __try
    {
        if (*slot != expected)
        {
            rendererlog::Line("studiobatch: StudioDrawPoints slot mismatch (%p expected %p), disabled", *slot, expected);
            return false;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }

    g_originalDrawPoints = reinterpret_cast<StudioDrawPointsFn>(expected);
    if (!PatchPointer(slot, expected, reinterpret_cast<void*>(&StudioDrawPoints_Hook)))
    {
        g_originalDrawPoints = nullptr;
        rendererlog::Line("studiobatch: unable to patch StudioDrawPoints engine slot");
        return false;
    }

    g_emitStandard = g_hwBase + kEmitStandardRva;
    g_emitAltUv = g_hwBase + kEmitAltUvRva;
    g_emitChrome = g_hwBase + kEmitChromeRva;
    const bool chromeOk = PatchRelativeCall(g_hwBase + kEmitChromeCallRva,
                                            g_emitChrome, reinterpret_cast<void*>(&EmitChrome_Hook));
    const bool altOk = PatchRelativeCall(g_hwBase + kEmitAltUvCallRva,
                                         g_emitAltUv, reinterpret_cast<void*>(&EmitAltUv_Hook));
    const bool standardOk = PatchRelativeCall(g_hwBase + kEmitStandardCallRva,
                                              g_emitStandard, reinterpret_cast<void*>(&EmitStandard_Hook));
    g_emitHooksReady = chromeOk && altOk && standardOk;
    if (!g_emitHooksReady)
    {
        rendererlog::Line("studiobatch: emitter callsite patch incomplete (std=%s alt=%s chrome=%s), fast path disabled",
                   standardOk ? "ok" : "fail", altOk ? "ok" : "fail", chromeOk ? "ok" : "fail");
    }
    else
    {
        rendererlog::Line("studiobatch: hooked StudioDrawPoints + 3 mesh emitters (mode 0 stock, 1 mesh triangles, 2 capture/replay)");
    }
    return true;
}

void UpdateFrame()
{
    RefreshEngineSlot();
}
} // namespace studio_drawbatch
