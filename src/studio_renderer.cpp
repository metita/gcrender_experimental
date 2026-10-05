#include "studio_renderer.h"
#include "studio_drawbatch.h"
#include "studio.h"
#include "world_vbo.h"
#include <meshoptimizer.h>
#include "hw_build.h"
#include "log.h"
#include "profile.h"
#include "sprite_vbo.h"

#include <windows.h>
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <intrin.h>
#include <limits>
#include <unordered_map>
#include <vector>

namespace studio_renderer
{
namespace
{
constexpr std::uintptr_t kStudioHdrPtrRva = 0x029F5888u;
constexpr std::uintptr_t kCurrentStudioModelRva = 0x029839BCu;
constexpr std::uintptr_t kCurrentEntityRva = 0x0078F470u;
constexpr std::uintptr_t kEngineStudioSetupLightingSlotRva = 0x001B25A8u;
constexpr std::uintptr_t kInnerDrawPointsRva = 0x0009B0A0u;
constexpr std::uintptr_t kInnerDrawPointsNormalCallRva = 0x0009B7A1u;
constexpr std::uintptr_t kDrawBrushModelRva = 0x00075620u;
constexpr std::uintptr_t kDrawSpriteModelRva = 0x0006FA60u;
constexpr std::uintptr_t kDrawBrushModelCallRva = 0x00070301u;
constexpr std::uintptr_t kDrawSpriteModelCallRva = 0x000703EDu;
constexpr std::uintptr_t kSolidDrawModelReturnRva = 0x000702FCu;
constexpr std::uintptr_t kClientNormalDrawReturnRva = 0x000B0A0Cu;
constexpr std::uintptr_t kClientStudioRenderShadowCallRva = 0x000AFE42u;
constexpr std::uintptr_t kClientStudioRenderShadowSlotRva = 0x001B25FCu;
constexpr std::uintptr_t kBoneTransformRva = 0x029F3A58u;
constexpr std::uintptr_t kBoneLightVectorRva = 0x029F5288u;
constexpr std::uintptr_t kStudioAmbientRva = 0x02891740u;
constexpr std::uintptr_t kStudioShadeRva = 0x0289173Cu;
constexpr std::uintptr_t kStudioColorMixRva = 0x029F224Cu;
constexpr std::uintptr_t kStudioBlendRva = 0x029F588Cu;
constexpr std::uintptr_t kForceFaceFlagsRva = 0x029839B0u;
constexpr std::uintptr_t kCurrentMeshFlagsRva = 0x029839ACu;
constexpr std::uintptr_t kStudioSetupRenderModeRva = 0x027EF524u;
constexpr std::uintptr_t kNumStudioLightsRva = 0x02952B98u;
constexpr std::uintptr_t kDebugStudioModeRva = 0x03339698u;
constexpr std::uintptr_t kStudioTriangleCounterRva = 0x0078F950u;
constexpr std::uintptr_t kLightGammaTableRva = 0x02AF61E8u;
constexpr std::uintptr_t kBoneLightAgeRva = 0x02891768u;
constexpr std::uintptr_t kStudioStampRva = 0x0288D5B4u;
constexpr std::uintptr_t kMirrorNormalRva = 0x027EF521u;
constexpr std::uintptr_t kMirrorPredicateRva = 0x000948B0u;
constexpr std::uintptr_t kRendererTypeRva = 0x02CB4888u;
constexpr std::uintptr_t kQglCullFaceRva = 0x027E37E0u;
constexpr std::uintptr_t kQglDisableRva = 0x027E3DF8u;
constexpr std::uintptr_t kQglEnableRva = 0x027E3DFCu;
constexpr std::uintptr_t kQglIsEnabledRva = 0x027E3DF4u;
constexpr std::uintptr_t kQglTexCoord2fRva = 0x027E3778u;
constexpr std::uintptr_t kQglActiveTextureRva = 0x027E3784u;
constexpr std::uintptr_t kQglColor4fRva = 0x027E3DD4u;
constexpr std::uintptr_t kQglDrawElementsRva = 0x027E3CE8u;
constexpr std::uintptr_t kQglGetFloatvRva = 0x027E37DCu;
constexpr std::uintptr_t kQglAlphaFuncRva = 0x027E3818u;
constexpr std::uintptr_t kQglDepthMaskRva = 0x027E376Cu;
constexpr std::uintptr_t kQglBlendFuncRva = 0x027E3DE8u;
constexpr std::uintptr_t kQglShadeModelRva = 0x027E37CCu;
constexpr std::uintptr_t kQglTexEnviRva = 0x027E3744u;
constexpr std::uintptr_t kQglPushClientAttribRva = 0x027E3B04u;
constexpr std::uintptr_t kQglPopClientAttribRva = 0x027E3B14u;
constexpr std::uintptr_t kQglDisableClientStateRva = 0x027E3CF0u;
constexpr std::uintptr_t kQglClientActiveTextureRva = 0x027E386Cu;
constexpr std::uintptr_t kSdlGetProcAddressIatRva = 0x001FA330u;
constexpr std::uintptr_t kTextureResolverRva = 0x00098AE0u;
constexpr std::uintptr_t kStudioLightingRva = 0x00096B50u;
constexpr std::uintptr_t kStudioChromeRva = 0x00098080u;
constexpr std::uintptr_t kChromeTableRva = 0x029639A8u;
constexpr std::uintptr_t kQglBindBufferRva = 0x027E3DB8u;
constexpr std::uintptr_t kQglDeleteBuffersRva = 0x027E3DC8u;
constexpr std::uintptr_t kQglGenBuffersRva = 0x027E3DCCu;
constexpr std::uintptr_t kQglBufferDataRva = 0x027E3DB4u;
constexpr std::uintptr_t kQglGetIntegervRva = 0x027E3DD0u;
constexpr std::uintptr_t kGoldCurrentTmuRva = 0x027E35E0u;
// Solid R_DrawSpriteModel inputs, same addresses as sprite_vbo's capture gate.
constexpr std::uintptr_t kSpriteTextureMatrixRva = 0x0078F944u;
constexpr std::uintptr_t kSpriteBlendRva = 0x0333A008u;
constexpr std::uintptr_t kQglDepthFuncRva = 0x027E37D8u;
// Gold-visible globals written by StudioRenderShadow (hw+0x9D7A0) through the
// triangle API: tri render mode, tri color (r,g,b,a stored downwards from
// +0x58B0) and the VGUI2 current-texture reset done by tri Begin.
constexpr std::uintptr_t kTriRenderModeRva = 0x029F58D0u;
constexpr std::uintptr_t kTriColorRva = 0x029F58A4u;
constexpr std::uintptr_t kVguiCurrentTextureRva = 0x0333B850u;

constexpr unsigned GL_ARRAY_BUFFER = 0x8892u;
constexpr unsigned GL_ELEMENT_ARRAY_BUFFER = 0x8893u;
constexpr unsigned GL_UNIFORM_BUFFER = 0x8A11u;
constexpr unsigned GL_ARRAY_BUFFER_BINDING = 0x8894u;
constexpr unsigned GL_ELEMENT_ARRAY_BUFFER_BINDING = 0x8895u;
constexpr unsigned GL_UNIFORM_BUFFER_BINDING = 0x8A28u;
constexpr unsigned GL_UNIFORM_BUFFER_START = 0x8A29u;
constexpr unsigned GL_UNIFORM_BUFFER_SIZE = 0x8A2Au;
constexpr unsigned GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT = 0x8A34u;
constexpr unsigned GL_MAX_UNIFORM_BUFFER_BINDINGS = 0x8A2Fu;
constexpr unsigned GL_MAX_UNIFORM_BLOCK_SIZE = 0x8A30u;
constexpr unsigned GL_STATIC_DRAW = 0x88E4u;
constexpr unsigned GL_STREAM_DRAW = 0x88E0u;
constexpr unsigned GL_TRIANGLES = 0x0004u;
constexpr unsigned GL_FLOAT = 0x1406u;
constexpr unsigned GL_SHORT = 0x1402u;
constexpr unsigned GL_UNSIGNED_BYTE = 0x1401u;
constexpr unsigned GL_UNSIGNED_SHORT = 0x1403u;
constexpr unsigned GL_UNSIGNED_INT = 0x1405u;
constexpr unsigned GL_CURRENT_PROGRAM = 0x8B8Du;
constexpr unsigned GL_MAX_VERTEX_ATTRIBS = 0x8869u;
constexpr unsigned GL_MAX_VERTEX_UNIFORM_COMPONENTS = 0x8B4Au;
constexpr unsigned GL_VERTEX_ATTRIB_ARRAY_ENABLED = 0x8622u;
constexpr unsigned GL_VERTEX_SHADER = 0x8B31u;
constexpr unsigned GL_COMPILE_STATUS = 0x8B81u;
constexpr unsigned GL_LINK_STATUS = 0x8B82u;
constexpr unsigned GL_INFO_LOG_LENGTH = 0x8B84u;
constexpr unsigned GL_FRONT = 0x0404u;
constexpr unsigned GL_CULL_FACE = 0x0B44u;
constexpr unsigned GL_CLIENT_VERTEX_ARRAY_BIT = 0x00000002u;
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
constexpr unsigned GL_TEXTURE_BINDING_2D = 0x8069u;
constexpr unsigned GL_MAX_TEXTURE_UNITS = 0x84E2u;
constexpr unsigned GL_VERTEX_ARRAY_BINDING = 0x85B5u;
constexpr unsigned GL_MODELVIEW_MATRIX = 0x0BA6u;
constexpr unsigned GL_PROJECTION_MATRIX = 0x0BA7u;
constexpr unsigned GL_TEXTURE_MATRIX = 0x0BA8u;
constexpr unsigned GL_CULL_FACE_MODE = 0x0B45u;
constexpr unsigned GL_ALPHA_TEST = 0x0BC0u;
constexpr unsigned GL_BLEND = 0x0BE2u;
constexpr unsigned GL_ALPHA_TEST_FUNC = 0x0BC1u;
constexpr unsigned GL_ALPHA_TEST_REF = 0x0BC2u;
constexpr unsigned GL_DEPTH_WRITEMASK = 0x0B72u;
constexpr unsigned GL_SHADE_MODEL = 0x0B54u;
constexpr unsigned GL_TEXTURE_ENV = 0x2300u;
constexpr unsigned GL_TEXTURE_ENV_MODE = 0x2200u;
constexpr unsigned GL_MODULATE = 0x2100u;
constexpr unsigned GL_REPLACE = 0x1E01u;
constexpr unsigned GL_GREATER = 0x0204u;
constexpr unsigned GL_NOTEQUAL = 0x0205u;
constexpr unsigned GL_ONE = 1u;
constexpr unsigned GL_FLAT = 0x1D00u;
constexpr unsigned GL_SMOOTH = 0x1D01u;
constexpr unsigned GL_MAP_WRITE_BIT = 0x0002u;
constexpr unsigned GL_MAP_INVALIDATE_RANGE_BIT = 0x0004u;
constexpr unsigned GL_MAP_UNSYNCHRONIZED_BIT = 0x0020u;
constexpr unsigned GL_MAP_INVALIDATE_BUFFER_BIT = 0x0008u;
constexpr unsigned GL_MAP_FLUSH_EXPLICIT_BIT = 0x0010u;
constexpr unsigned GL_MAP_PERSISTENT_BIT = 0x0040u;
constexpr unsigned GL_MAP_COHERENT_BIT = 0x0080u;
constexpr unsigned GL_SYNC_GPU_COMMANDS_COMPLETE = 0x9117u;
constexpr unsigned GL_SYNC_FLUSH_COMMANDS_BIT = 0x0001u;
constexpr unsigned GL_ALREADY_SIGNALED = 0x911Au;
constexpr unsigned GL_CONDITION_SATISFIED = 0x911Cu;
constexpr unsigned GL_MAJOR_VERSION = 0x821Bu;
constexpr unsigned GL_MINOR_VERSION = 0x821Cu;
constexpr unsigned GL_EXTENSIONS = 0x1F03u;
constexpr unsigned GL_DEPTH_TEST = 0x0B71u;
constexpr unsigned GL_DEPTH_FUNC = 0x0B74u;
constexpr unsigned GL_FOG = 0x0B60u;
constexpr unsigned GL_STENCIL_TEST = 0x0B90u;
constexpr unsigned GL_POLYGON_OFFSET_FILL = 0x8037u;
constexpr unsigned GL_BLEND_DST = 0x0BE0u;
constexpr unsigned GL_BLEND_SRC = 0x0BE1u;
constexpr unsigned GL_CURRENT_COLOR = 0x0B00u;
constexpr unsigned GL_CURRENT_TEXTURE_COORDS = 0x0B03u;
constexpr unsigned GL_MATRIX_MODE = 0x0BA0u;
constexpr unsigned GL_MODELVIEW = 0x1700u;
constexpr unsigned GL_PROJECTION = 0x1701u;
constexpr unsigned GL_TEXTURE = 0x1702u;
constexpr unsigned GL_INVALID_INDEX = 0xFFFFFFFFu;
constexpr unsigned char GL_FALSE_VALUE = 0;

constexpr unsigned kAttribPosition = 8;
constexpr unsigned kAttribNormal = 9;
constexpr unsigned kAttribRawST = 10;
constexpr unsigned kAttribPositionBone = 11;
constexpr unsigned kAttribNormalBone = 12;

using GlBindBufferFn = void (WINAPI*)(unsigned target, unsigned buffer);
using GlDeleteBuffersFn = void (WINAPI*)(int count, const unsigned* buffers);
using GlGenBuffersFn = void (WINAPI*)(int count, unsigned* buffers);
using GlBufferDataFn = void (WINAPI*)(unsigned target, std::ptrdiff_t size,
                                     const void* data, unsigned usage);
using GlBufferSubDataFn = void (WINAPI*)(unsigned target, std::ptrdiff_t offset,
                                        std::ptrdiff_t size, const void* data);
using GlGetIntegervFn = void (WINAPI*)(unsigned pname, int* params);
using GlGetIntegeriVFn = void (WINAPI*)(unsigned pname, unsigned index, int* params);
using GlGetInteger64iVFn = void (WINAPI*)(unsigned pname, unsigned index,
                                         long long* params);
using GlGetFloatvFn = void (WINAPI*)(unsigned pname, float* params);
using GlTexCoord2fFn = void (WINAPI*)(float s, float t);
using GlActiveTextureFn = void (WINAPI*)(unsigned texture);
using GlColor4fFn = void (WINAPI*)(float r, float g, float b, float a);
using GlAlphaFuncFn = void (WINAPI*)(unsigned func, float ref);
using GlDepthMaskFn = void (WINAPI*)(unsigned char flag);
using GlBlendFuncFn = void (WINAPI*)(unsigned src, unsigned dst);
using GlShadeModelFn = void (WINAPI*)(unsigned mode);
using GlTexEnviFn = void (WINAPI*)(unsigned target, unsigned pname, int param);
using GlGetTexEnvivFn = void (WINAPI*)(unsigned target, unsigned pname, int* params);
using GlDrawElementsFn = void (WINAPI*)(unsigned mode, int count, unsigned type,
                                       const void* indices);
using GlDrawElementsInstancedFn = void (WINAPI*)(
    unsigned mode, int count, unsigned type,
    const void* indices, int instanceCount);
using GlCullFaceFn = void (WINAPI*)(unsigned mode);
using GlDepthFuncFn = void (WINAPI*)(unsigned func);
using GlMatrixModeFn = void (WINAPI*)(unsigned mode);
using GlPushMatrixFn = void (WINAPI*)();
using GlPopMatrixFn = void (WINAPI*)();
using GlLoadMatrixfFn = void (WINAPI*)(const float* matrix);
using GlTexCoord4fvFn = void (WINAPI*)(const float* coords);
// Client StudioRenderShadow provider: (sprite index, p1, p2, p3, p4).
using RenderShadowFn = void (__cdecl*)(int sprite, float* p1, float* p2,
                                       float* p3, float* p4);
using GlEnableFn = void (WINAPI*)(unsigned cap);
using GlDisableFn = void (WINAPI*)(unsigned cap);
using GlIsEnabledFn = unsigned char (WINAPI*)(unsigned cap);
using GlPushClientAttribFn = void (WINAPI*)(unsigned mask);
using GlPopClientAttribFn = void (WINAPI*)();
using GlDisableClientStateFn = void (WINAPI*)(unsigned array);
using GlClientActiveTextureFn = void (WINAPI*)(unsigned texture);
using SdlGetProcAddressFn = void* (__cdecl*)(const char* name);
using GlCreateShaderFn = unsigned (WINAPI*)(unsigned type);
using GlShaderSourceFn = void (WINAPI*)(unsigned shader, int count,
                                       const char* const* strings,
                                       const int* lengths);
using GlCompileShaderFn = void (WINAPI*)(unsigned shader);
using GlGetShaderivFn = void (WINAPI*)(unsigned shader, unsigned pname, int* value);
using GlGetShaderInfoLogFn = void (WINAPI*)(unsigned shader, int maxLength,
                                           int* length, char* log);
using GlDeleteShaderFn = void (WINAPI*)(unsigned shader);
using GlCreateProgramFn = unsigned (WINAPI*)();
using GlAttachShaderFn = void (WINAPI*)(unsigned program, unsigned shader);
using GlBindAttribLocationFn = void (WINAPI*)(unsigned program, unsigned index,
                                             const char* name);
using GlLinkProgramFn = void (WINAPI*)(unsigned program);
using GlGetProgramivFn = void (WINAPI*)(unsigned program, unsigned pname, int* value);
using GlGetProgramInfoLogFn = void (WINAPI*)(unsigned program, int maxLength,
                                            int* length, char* log);
using GlUseProgramFn = void (WINAPI*)(unsigned program);
using GlDeleteProgramFn = void (WINAPI*)(unsigned program);
using GlGetUniformLocationFn = int (WINAPI*)(unsigned program, const char* name);
using GlUniform4fvFn = void (WINAPI*)(int location, int count, const float* value);
using GlUniform3fvFn = void (WINAPI*)(int location, int count, const float* value);
using GlUniformMatrix4fvFn = void (WINAPI*)(int location, int count,
                                           unsigned char transpose,
                                           const float* value);
using GlVertexAttribPointerFn = void (WINAPI*)(unsigned index, int size,
                                              unsigned type,
                                              unsigned char normalized,
                                              int stride,
                                              const void* pointer);
using GlEnableVertexAttribArrayFn = void (WINAPI*)(unsigned index);
using GlDisableVertexAttribArrayFn = void (WINAPI*)(unsigned index);
using GlGetVertexAttribivFn = void (WINAPI*)(unsigned index, unsigned pname,
                                            int* value);
using GlGetUniformBlockIndexFn = unsigned (WINAPI*)(unsigned program,
                                                    const char* blockName);
using GlUniformBlockBindingFn = void (WINAPI*)(unsigned program,
                                              unsigned blockIndex,
                                              unsigned blockBinding);
using GlBindBufferRangeFn = void (WINAPI*)(unsigned target, unsigned index,
                                          unsigned buffer,
                                          std::ptrdiff_t offset,
                                          std::ptrdiff_t size);
using GlBindBufferBaseFn = void (WINAPI*)(unsigned target, unsigned index,
                                         unsigned buffer);
using GlMapBufferRangeFn = void* (WINAPI*)(unsigned target,
                                          std::ptrdiff_t offset,
                                          std::ptrdiff_t length,
                                          unsigned access);
using GlFlushMappedBufferRangeFn = void (WINAPI*)(unsigned target,
                                                  std::ptrdiff_t offset,
                                                  std::ptrdiff_t length);
using GlUnmapBufferFn = unsigned char (WINAPI*)(unsigned target);
using GlBufferStorageFn = void (WINAPI*)(unsigned target, std::ptrdiff_t size,
                                        const void* data, unsigned flags);
using GlFenceSyncFn = void* (WINAPI*)(unsigned condition, unsigned flags);
using GlClientWaitSyncFn = unsigned (WINAPI*)(void* sync, unsigned flags,
                                             unsigned long long timeout);
using GlDeleteSyncFn = void (WINAPI*)(void* sync);
using GlGetStringFn = const unsigned char* (WINAPI*)(unsigned name);
using GlGenVertexArraysFn = void (WINAPI*)(int count, unsigned* arrays);
using GlDeleteVertexArraysFn = void (WINAPI*)(int count, const unsigned* arrays);
using GlBindVertexArrayFn = void (WINAPI*)(unsigned array);
struct StudioHeaderRaw;
using TextureResolverFn = void (__cdecl*)(void* studioHeader, int textureSlot);
using TextureHeaderFn = StudioHeaderRaw* (__cdecl*)();
using GoldBindFn = void (__fastcall*)(int textureUnit, int textureId);
using MirrorPredicateFn = unsigned char (__cdecl*)();
using InnerDrawPointsFn = void (__cdecl*)();
using SolidBarrierFn = void (__cdecl*)();

struct AlightRaw
{
    int ambientlight;
    int shadelight;
    float color[3];
    float* plightvec;
};
static_assert(sizeof(AlightRaw) == 24, "alight_t layout mismatch");
using StudioSetupLightingFn = void (__cdecl*)(AlightRaw* lighting);

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
static_assert(sizeof(StudioHeaderRaw) == 244, "Studio header layout mismatch");

struct StudioBodypartRaw
{
    char name[64];
    int nummodels;
    int base;
    int modelindex;
};
static_assert(sizeof(StudioBodypartRaw) == 76, "Studio bodypart layout mismatch");

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
static_assert(sizeof(StudioModelRaw) == 112, "Studio model layout mismatch");

struct StudioMeshRaw
{
    int numtris;
    int triindex;
    int skinref;
    int numnorms;
    int normindex;
};
static_assert(sizeof(StudioMeshRaw) == 20, "Studio mesh layout mismatch");

struct StudioTextureRaw
{
    char name[64];
    int flags;
    int width;
    int height;
    int glId;
};
static_assert(sizeof(StudioTextureRaw) == 80, "Studio texture layout mismatch");

// Keep source normals as float32 and preserve the two independent Studio bone
// streams. csgl3 quantizes normals and uses one bone for both, Gold does not
// require that simplification, and visual equivalence matters more here.
struct RetainedVertex
{
    float xyz[3];
    float normal[3];
    std::int16_t rawST[2];
    // Gold-space bone ids are kept for stock-compatible state restoration and
    // the legacy-uniform shader fallback. The final two bytes are the same
    // bones remapped into the selected submodel's compact UBO palette.
    std::uint8_t positionBone;
    std::uint8_t normalBone;
    std::uint8_t palettePositionBone;
    std::uint8_t paletteNormalBone;
};
static_assert(sizeof(RetainedVertex) == 32, "Retained Studio vertex must be 32 bytes");
static_assert(offsetof(RetainedVertex, normal) == 12, "normal offset mismatch");
static_assert(offsetof(RetainedVertex, rawST) == 24, "raw ST offset mismatch");
static_assert(offsetof(RetainedVertex, positionBone) == 28, "bone offset mismatch");
static_assert(offsetof(RetainedVertex, palettePositionBone) == 30,
              "palette bone offset mismatch");

struct RetainedSubmesh
{
    std::uint32_t firstIndex = 0;
    std::uint32_t optimizedFirstIndex = 0;
    std::uint32_t indexCount = 0;
    int skinref = 0;
    int triangleCount = 0;
    std::uint32_t lastVertex = 0;
};

struct RetainedSubmodel
{
    const StudioModelRaw* source = nullptr;
    std::vector<RetainedSubmesh> meshes;
    // BuildSubmodel owns a private CornerKey map, so every retained vertex for
    // one Studio submodel is appended as one contiguous model-wide range.
    // Keep that range explicit for sparse dynamic CHROME UV uploads.
    std::size_t firstVertex = 0;
    std::size_t vertexCount = 0;
    std::uint64_t usedPositionBones[2]{};
    std::uint8_t bonePalette[128]{};
    std::uint8_t bonePaletteCount = 0;
};

struct RetainedCache
{
    const std::uint8_t* header = nullptr;
    std::uint32_t headerLength = 0;
    const void* ownerModel = nullptr;
    int ownerIndex = -1;
    std::uint32_t ownerGeneration = 0;

    // One CPU-side model-wide geometry image. GPU VBO/IBO ownership is added
    // by the direct renderer and follows this same exact generation identity.
    std::vector<RetainedVertex> vertices;
    // Original Studio normal ordinal for each retained corner. Keeping this as
    // side metadata preserves the compact 32-byte static GPU vertex while
    // allowing exact stock CHROME coordinates to be mapped back to corners.
    std::vector<std::uint16_t> normalIndices;
    std::vector<std::uint32_t> indices;
    std::vector<std::uint16_t> indices16;
    std::vector<RetainedSubmodel> submodels;
    std::unordered_map<const StudioModelRaw*, std::size_t> submodelLookup;
    std::uint32_t meshoptRanges = 0;
    std::uint64_t meshoptVerticesBefore = 0;
    std::uint64_t meshoptVerticesAfter = 0;

    unsigned vertexBuffer = 0;
    unsigned indexBuffer = 0;
    bool index16 = false;
    unsigned chromeUvBuffer = 0;
    std::size_t chromeUvBufferBytes = 0;
    unsigned vertexArray = 0;
    bool vertexArrayUsesPalette = false;
    std::uint32_t contextGeneration = 0;
};

struct CornerKey
{
    std::uint16_t vertex;
    std::uint16_t normal;
    std::int16_t s;
    std::int16_t t;

    bool operator==(const CornerKey& other) const
    {
        return vertex == other.vertex && normal == other.normal &&
               s == other.s && t == other.t;
    }
};

struct CornerHash
{
    std::size_t operator()(const CornerKey& key) const
    {
        std::uint64_t value =
            static_cast<std::uint64_t>(key.vertex) |
            (static_cast<std::uint64_t>(key.normal) << 16) |
            (static_cast<std::uint64_t>(static_cast<std::uint16_t>(key.s)) << 32) |
            (static_cast<std::uint64_t>(static_cast<std::uint16_t>(key.t)) << 48);
        value ^= value >> 33;
        value *= 0xff51afd7ed558ccdULL;
        value ^= value >> 33;
        return static_cast<std::size_t>(value);
    }
};

std::uint8_t* g_hwBase = nullptr;
std::uint8_t* g_clientBase = nullptr;
cvar_t* g_mode = nullptr;
cvar_t* g_nonPlayerMode = nullptr;
cvar_t* g_meshoptMode = nullptr;
cvar_t* g_instancingMode = nullptr;
cvar_t* g_shadowDeferMode = nullptr;
cl_enginefunc_t* g_entryEngine = nullptr;
std::unordered_map<const std::uint8_t*, RetainedCache> g_caches;
std::unordered_map<const StudioHeaderRaw*, std::uint64_t>
    g_negativeCacheRetry;
constexpr std::uint64_t kNegativeCacheRetryAttempts = 4096u;
GlBindBufferFn g_bindBuffer = nullptr;
GlDeleteBuffersFn g_deleteBuffers = nullptr;
GlGenBuffersFn g_genBuffers = nullptr;
GlBufferDataFn g_bufferData = nullptr;
GlBufferSubDataFn g_bufferSubData = nullptr;
GlGetIntegervFn g_getIntegerv = nullptr;
GlGetFloatvFn g_getFloatv = nullptr;
GlTexCoord2fFn g_texCoord2f = nullptr;
GlActiveTextureFn g_activeTexture = nullptr;
GlColor4fFn g_color4f = nullptr;
GlAlphaFuncFn g_alphaFunc = nullptr;
GlDepthMaskFn g_depthMask = nullptr;
GlBlendFuncFn g_blendFunc = nullptr;
GlShadeModelFn g_shadeModel = nullptr;
GlTexEnviFn g_texEnvi = nullptr;
GlGetTexEnvivFn g_getTexEnviv = nullptr;
GlDrawElementsFn g_drawElements = nullptr;
GlDrawElementsInstancedFn g_drawElementsInstanced = nullptr;
GlCullFaceFn g_cullFace = nullptr;
GlDepthFuncFn g_depthFunc = nullptr;
GlMatrixModeFn g_matrixMode = nullptr;
GlPushMatrixFn g_pushMatrix = nullptr;
GlPopMatrixFn g_popMatrix = nullptr;
GlLoadMatrixfFn g_loadMatrixf = nullptr;
GlTexCoord4fvFn g_texCoord4fv = nullptr;
GlEnableFn g_enable = nullptr;
GlDisableFn g_disable = nullptr;
GlIsEnabledFn g_isEnabled = nullptr;
GlPushClientAttribFn g_pushClientAttrib = nullptr;
GlPopClientAttribFn g_popClientAttrib = nullptr;
GlDisableClientStateFn g_disableClientState = nullptr;
GlClientActiveTextureFn g_clientActiveTexture = nullptr;
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
GlUniform3fvFn g_uniform3fv = nullptr;
GlUniformMatrix4fvFn g_uniformMatrix4fv = nullptr;
GlVertexAttribPointerFn g_vertexAttribPointer = nullptr;
GlEnableVertexAttribArrayFn g_enableVertexAttribArray = nullptr;
GlDisableVertexAttribArrayFn g_disableVertexAttribArray = nullptr;
GlGetVertexAttribivFn g_getVertexAttribiv = nullptr;
GlGetIntegeriVFn g_getIntegeriV = nullptr;
GlGetInteger64iVFn g_getInteger64iV = nullptr;
GlGetUniformBlockIndexFn g_getUniformBlockIndex = nullptr;
GlUniformBlockBindingFn g_uniformBlockBinding = nullptr;
GlBindBufferRangeFn g_bindBufferRange = nullptr;
GlBindBufferBaseFn g_bindBufferBase = nullptr;
GlMapBufferRangeFn g_mapBufferRange = nullptr;
GlFlushMappedBufferRangeFn g_flushMappedBufferRange = nullptr;
GlUnmapBufferFn g_unmapBuffer = nullptr;
GlBufferStorageFn g_bufferStorage = nullptr;
GlFenceSyncFn g_fenceSync = nullptr;
GlClientWaitSyncFn g_clientWaitSync = nullptr;
GlDeleteSyncFn g_deleteSync = nullptr;
GlGetStringFn g_getString = nullptr;
GlGenVertexArraysFn g_genVertexArrays = nullptr;
GlDeleteVertexArraysFn g_deleteVertexArrays = nullptr;
GlBindVertexArrayFn g_bindVertexArray = nullptr;
StudioSetupLightingFn g_originalSetupLighting = nullptr;
bool g_setupLightingHookReady = false;
TextureResolverFn g_textureResolver = nullptr;
TextureHeaderFn g_textureHeader = nullptr;
GoldBindFn g_goldBind = nullptr;
MirrorPredicateFn g_mirrorPredicate = nullptr;
InnerDrawPointsFn g_innerDrawPoints = nullptr;
void* g_studioLighting = nullptr;
void* g_studioChrome = nullptr;
bool g_innerCallHooked = false;
int g_solidEntityPassDepth = 0;
bool g_deferredBarriersReady = false;
thread_local int g_nativeOpaqueDrawModelDepth = 0;
thread_local bool g_directProgramOwned = false;
std::uint64_t g_forcedProgramRestores = 0;
SolidBarrierFn g_originalDrawBrushModel = nullptr;
SolidBarrierFn g_originalDrawSpriteModel = nullptr;
void** g_clientStudioRenderShadowSlot = nullptr;
bool g_clientStudioShadowBarrierReady = false;

unsigned g_program = 0;
std::uint32_t g_programGeneration = 0;
int g_bonesLocation = -1;
int g_lightVectorsLocation = -1;
int g_gammaFit0Location = -1;
int g_gammaFit1Location = -1;
int g_paramsLocation = -1;
int g_colorBlendLocation = -1;
int g_modelViewLocation = -1;
int g_projectionLocation = -1;
int g_textureMatrixLocation = -1;
unsigned g_instancedProgram = 0;
int g_instancedGammaFit0Location = -1;
int g_instancedGammaFit1Location = -1;
int g_instancedParamsLocation = -1;
int g_instancedColorBlendLocation = -1;
int g_instancedModelViewLocation = -1;
int g_instancedProjectionLocation = -1;
int g_instancedTextureMatrixLocation = -1;
int g_instancedLayoutLocation = -1;
unsigned g_instancedBoneBlockIndex = GL_INVALID_INDEX;
unsigned g_instancedCapacity = 0;
std::size_t g_instancedBlockBytes = 0;
bool g_instancedProgramFailed = false;
std::uint32_t g_instancedGammaFitRevision = 0;
cl_enginefunc_t* g_engine = nullptr;

struct GammaCurveFit
{
    float wh = 0.0f;
    float wsat = 1.0f;
    float p[3]{};
    float q[3]{};
};

GammaCurveFit g_gammaFit{};
float g_gammaFit0[4]{};
float g_gammaFit1[4]{};
float g_gammaCvar = -1.0f;
float g_lightGammaCvar = -1.0f;
float g_brightnessCvar = -9999.0f;
bool g_gammaFitKnown = false;
std::uint32_t g_gammaFitRevision = 0;
std::uint32_t g_gammaFitProgramRevision = 0;
std::uint32_t g_stateValidationGeneration = 0;
int g_cachedMaxTextureUnits = 0;
bool g_useUniformBuffer = false;
unsigned g_boneBlockIndex = GL_INVALID_INDEX;
unsigned g_boneBlockBinding = 0;

constexpr int kUniformBufferCount = 3;
// 4 MB per ring buffer = 512 bone blocks of 8 KB. The former 512 KB held only
// 64 commands per frame, after which crowded scenes fell back to stock.
constexpr std::size_t kUniformBufferBytes = 1u << 22;
constexpr std::size_t kBoneBlockBytes =
    384u * sizeof(float) * 4u +
    128u * sizeof(float) * 4u;
static_assert(kBoneBlockBytes == 8192u, "studio bone block size mismatch");
// One std140 StudioBoneData record: row0, row1, row2, light.
constexpr std::size_t kBoneRecordBytes = 4u * sizeof(float) * 4u;
constexpr std::size_t kBoneRecordFloats = kBoneRecordBytes / sizeof(float);
// Instanced draws read N compact per-entity palettes from one bound range.
// The declared block is capped at 64 KB, the common GL_MAX_UNIFORM_BLOCK_SIZE.
constexpr std::size_t kMaxInstancedBlockBytes = 64u * 1024u;
constexpr unsigned kInstanceCap = 32;

// Every frame region is addressed as (buffer, base + offset) with offsets in
// [0, kUniformBufferBytes). Orphan mode owns one buffer per frame (base 0),
// persistent mode carves the three regions out of one mapped buffer.
struct UniformBufferFrame
{
    unsigned buffer = 0;
    std::size_t base = 0;
    // Persistent mode: GPU fence for the last frame that wrote this region.
    void* fence = nullptr;
};

UniformBufferFrame g_uniformBuffers[kUniformBufferCount]{};
int g_uniformBufferFrame = 0;
std::size_t g_uniformBufferOffset = 0;
// GL_ARB_buffer_storage ring: one immutable buffer of
// kUniformBufferCount * kUniformBufferBytes, mapped write/persistent/coherent
// for the lifetime of the GL context. Bone blocks are written straight into
// the current frame region, a fence per region guards its reuse three frames
// later. Null when the orphan + map/subdata path is active.
std::uint8_t* g_uniformPersistentBase = nullptr;
std::uint64_t g_uniformPersistentWrites = 0;
std::uint64_t g_uniformFenceWaits = 0;
int g_uniformBufferAlignment = 256;
int g_uniformBlockMaxSize = 0;
std::uint32_t g_uniformBufferGeneration = 0;
bool g_uniformBufferReady = false;
// The reserved indexed point is picked by InitializeUniformBuffers as a point
// nobody had bound, and only programs created here reference it through
// glUniformBlockBinding. Every Studio draw rebinds the exact range it reads,
// so the point is module-owned: hot paths neither query nor restore it, they
// only record the last range here. The generic GL_UNIFORM_BUFFER target is
// likewise written only by this module and is returned to the value observed
// when the ring buffers were created instead of being read back every use.
std::size_t g_lastUniformBlockOffset = 0;
std::size_t g_lastUniformBlockSize = 0;
unsigned g_lastUniformBlockBuffer = 0;
int g_genericUniformRestore = 0;
alignas(16) float g_boneBlockScratch[kBoneBlockBytes / sizeof(float)]{};

struct EntityUniformKey
{
    std::uint32_t contextGeneration = 0;
    int studioStamp = 0;
    const void* entity = nullptr;
    const void* header = nullptr;
    const void* submodel = nullptr;
    int boneCount = 0;
};

EntityUniformKey g_lastEntityUniformKey{};
bool g_entityUniformKeyValid = false;
float g_lastColorBlend[4]{};
bool g_colorBlendUniformValid = false;
float g_lastParams[4]{};
bool g_paramsUniformValid = false;

std::uint64_t g_directAttempts = 0;
std::uint64_t g_directDraws = 0;
std::uint64_t g_directFallbacks = 0;
std::uint64_t g_nonPlayerAttempts = 0;
std::uint64_t g_nonPlayerDeferredDraws = 0;
std::uint64_t g_chromePrepasses = 0;
std::uint64_t g_chromeNormalCalls = 0;

enum class DirectFallbackReason : unsigned
{
    Caller,
    GlobalState,
    NegativeCache,
    CacheResolve,
    Submodel,
    TextureHeader,
    Skin,
    Material,
    MaterialState,
    ProgramGpu,
    BoneStamp,
    GlState,
    Execute,
    Count
};

std::uint64_t g_directFallbackReason[
    static_cast<unsigned>(DirectFallbackReason::Count)]{};

bool DirectFallback(DirectFallbackReason reason)
{
    ++g_directFallbacks;
    if (prof::Active())
        ++g_directFallbackReason[static_cast<unsigned>(reason)];
    return false;
}

// Profiling breakdown of DirectFallbackReason::Material, one entry per
// return site in TryDirectDraw and ResolveDirectMaterial.
enum class MaterialReject : unsigned
{
    Reserve,
    HeaderCounts,
    SkinTable,
    TextureSlot,
    TextureArray,
    AltUv,
    TextureSize,
    IndexCount,
    LastVertex,
    FlatLight,
    Store,
    ChromePrepare,
    ChromeUpload,
    Count
};

constexpr const char* kMaterialRejectNames[
    static_cast<unsigned>(MaterialReject::Count)] = {
    "reserve", "header", "skintable", "slot", "textures", "altuv",
    "size", "indices", "lastvertex", "flatlight", "store",
    "chromePrep", "chromeUpload"
};

std::uint64_t g_materialReject[
    static_cast<unsigned>(MaterialReject::Count)]{};

bool MaterialFallback(MaterialReject reason)
{
    if (prof::Active())
        ++g_materialReject[static_cast<unsigned>(reason)];
    return DirectFallback(DirectFallbackReason::Material);
}

// Profiling breakdown of every RecordDeferredDirectDraw refusal, split by
// player and non-player. A non-player refusal is DirectFallbackReason::Execute
// (deferred-only rule), a player refusal takes the immediate retained path.
enum class RecordReject : unsigned
{
    Scope,
    ReplayReady,
    VertexArray,
    Capacity,
    Prepared,
    Texture,
    Remap,
    Additive,
    Matrices,
    MatrixSplit,
    CullState,
    ChromePrepare,
    ChromeCapacity,
    Store,
    Count
};

constexpr const char* kRecordRejectNames[
    static_cast<unsigned>(RecordReject::Count)] = {
    "scope", "replay", "vao", "capacity", "prepared", "texture", "remap",
    "additive", "matrices", "matrixSplit", "cull", "chromePrep",
    "chromeCap", "store"
};

std::uint64_t g_recordReject[2][
    static_cast<unsigned>(RecordReject::Count)]{};
std::uint64_t g_executeNonPlayerRecord = 0;
std::uint64_t g_executeImmediate = 0;
std::uint64_t g_deferredChromeRecords = 0;
std::uint64_t g_deferredMaskedRecords = 0;

enum class GlobalRejectReason : unsigned
{
    ReadGlobals,
    NotPlayer,
    RenderMode,
    SetupRenderMode,
    GlowShell,
    ForceFlags,
    StudioLights,
    DebugMode,
    Skin,
    BoneCount,
    Shadow,
    Count
};

std::uint64_t g_globalRejectReason[
    static_cast<unsigned>(GlobalRejectReason::Count)]{};
std::uint64_t g_setupModeRejectValues[16]{};
std::uint64_t g_setupModeRejectOther = 0;

// Profiling breakdown of GlobalRejectReason::NotPlayer. DrawModelScope is
// further split by the first DrawModelEntryCompatible check that failed.
enum class NonPlayerReject : unsigned
{
    OutsidePass,
    DrawModelScope,
    GlowShell,
    Other,
    Count
};

enum class DrawModelEntryReject : unsigned
{
    Flags,
    Mode,
    Caller,
    DrawEntities,
    Entity,
    RenderMode,
    GlowShell,
    ForceFlags,
    Count
};

std::uint64_t g_nonPlayerReject[
    static_cast<unsigned>(NonPlayerReject::Count)]{};
std::uint64_t g_drawModelEntryReject[
    static_cast<unsigned>(DrawModelEntryReject::Count)]{};

bool DrawModelEntryFallback(DrawModelEntryReject reason)
{
    if (prof::Active())
        ++g_drawModelEntryReject[static_cast<unsigned>(reason)];
    return false;
}

bool GlobalFallback(GlobalRejectReason reason)
{
    if (prof::Active())
        ++g_globalRejectReason[static_cast<unsigned>(reason)];
    return DirectFallback(DirectFallbackReason::GlobalState);
}

struct CacheResolveFailureStats
{
    std::uint64_t readState = 0;
    std::uint64_t cachedSubmodel = 0;
    std::uint64_t identity = 0;
    std::uint64_t build = 0;
    std::uint64_t emplace = 0;
    std::uint64_t finalSubmodel = 0;
};

CacheResolveFailureStats g_cacheResolveFailure{};
std::uint64_t g_buildFailureLogCount = 0;

struct DirectTimingStats
{
    std::uint64_t stateCalls = 0;
    std::uint64_t uniformCalls = 0;
    std::uint64_t uboUploads = 0;
    std::uint64_t uboBinds = 0;
    std::uint64_t materialCalls = 0;
    std::uint64_t drawCalls = 0;
    std::uint64_t restoreCalls = 0;
    std::uint64_t directAttemptCalls = 0;
    std::uint64_t fallbackAttemptCalls = 0;
    long long stateTicks = 0;
    long long uniformTicks = 0;
    long long uboUploadTicks = 0;
    long long uboBindTicks = 0;
    long long materialTicks = 0;
    long long drawTicks = 0;
    long long restoreTicks = 0;
    long long directAttemptTicks = 0;
    long long fallbackAttemptTicks = 0;
    // Deferred-run flushes reached from DirectKernelDispatch (immediate,
    // matrix-split and fallback barriers). They submit other entities'
    // packets and replay deferred client shadows, so they are excluded from
    // the per-draw attempt times above and reported on their own.
    std::uint64_t dispatchFlushes = 0;
    long long dispatchFlushTicks = 0;
};

DirectTimingStats g_directTiming{};
// Monotonic sum of profiled flush time, sampled around each dispatch.
long long g_profiledFlushTicks = 0;
std::uint64_t g_profiledFlushCount = 0;

double TimingMicrosPerCall(long long ticks, std::uint64_t calls)
{
    if (!ticks || !calls)
        return 0.0;
    LARGE_INTEGER frequency{};
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
        return 0.0;
    return
        static_cast<double>(ticks) * 1000000.0 /
        static_cast<double>(frequency.QuadPart) /
        static_cast<double>(calls);
}

struct LightingCapture
{
    const void* entity = nullptr;
    const void* header = nullptr;
    int ambientlight = 0;
    int shadelight = 0;
    float color[3]{};
    float lightvec[3]{};
    std::uint32_t serial = 0;
    bool valid = false;
};

LightingCapture g_lighting{};
std::uint32_t g_lightingSerial = 0;

struct DirectGlobals
{
    const StudioHeaderRaw* header = nullptr;
    const StudioModelRaw* currentModel = nullptr;
    std::uint8_t* entity = nullptr;
    const void* entityModel = nullptr;
    int player = 0;
    int skin = 0;
    int renderMode = 0;
    int setupRenderMode = 0;
    int renderFx = 0;
    int moveType = 0;
    int aiment = 0;
    int boneCount = 0;
    int studioStamp = 0;
    int numStudioLights = 0;
    int debugStudioMode = 0;
    std::uint32_t forceFlags = 0;
    float ambient = 0.0f;
    float shade = 0.0f;
    float colorMix[3]{};
    float blend = 1.0f;
};

bool PatchPointer(void** slot, void* expected, void* replacement)
{
    if (!slot || !expected || !replacement)
        return false;
    __try
    {
        if (*slot != expected)
            return false;
        DWORD oldProtect = 0;
        if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProtect))
            return false;
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

bool PointerInsideHw(const void* pointer)
{
    const auto value = reinterpret_cast<std::uintptr_t>(pointer);
    const auto base = reinterpret_cast<std::uintptr_t>(g_hwBase);
    return value >= base && value < base + hwbuild::kSizeOfImage;
}

bool CaptureLightingInput(const AlightRaw* input, LightingCapture& capture)
{
    if (!input || !g_hwBase)
        return false;
    __try
    {
        const void* entity =
            *reinterpret_cast<void* const*>(g_hwBase + kCurrentEntityRva);
        const void* header =
            *reinterpret_cast<void* const*>(g_hwBase + kStudioHdrPtrRva);
        if (!entity || !header || !input->plightvec)
            return false;
        capture.entity = entity;
        capture.header = header;
        capture.ambientlight = input->ambientlight;
        capture.shadelight = input->shadelight;
        std::memcpy(capture.color, input->color, sizeof(capture.color));
        std::memcpy(capture.lightvec, input->plightvec, sizeof(capture.lightvec));
        capture.serial = ++g_lightingSerial;
        capture.valid = true;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        capture = {};
        return false;
    }
}

void __cdecl StudioSetupLighting_Hook(AlightRaw* lighting)
{
    LightingCapture pending{};
    const bool captured = CaptureLightingInput(lighting, pending);
    if (g_originalSetupLighting)
        g_originalSetupLighting(lighting);
    if (captured)
        g_lighting = pending;
    else
        g_lighting = {};
}

bool RefreshSetupLightingHook()
{
    if (!g_clientBase || !g_hwBase)
        return false;
    auto** slot = reinterpret_cast<void**>(
        g_clientBase + kEngineStudioSetupLightingSlotRva);
    __try
    {
        if (*slot == reinterpret_cast<void*>(&StudioSetupLighting_Hook))
        {
            g_setupLightingHookReady = g_originalSetupLighting != nullptr;
            return g_setupLightingHookReady;
        }
        if (!g_originalSetupLighting)
        {
            if (!PointerInsideHw(*slot))
                return false;
            g_originalSetupLighting =
                reinterpret_cast<StudioSetupLightingFn>(*slot);
        }
        if (*slot != reinterpret_cast<void*>(g_originalSetupLighting))
        {
            g_setupLightingHookReady = false;
            g_lighting = {};
            return false;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        g_setupLightingHookReady = false;
        g_lighting = {};
        return false;
    }

    g_setupLightingHookReady = PatchPointer(
        slot, reinterpret_cast<void*>(g_originalSetupLighting),
        reinterpret_cast<void*>(&StudioSetupLighting_Hook));
    return g_setupLightingHookReady;
}

void RemoveSetupLightingHook()
{
    if (!g_clientBase || !g_originalSetupLighting)
    {
        g_setupLightingHookReady = false;
        g_lighting = {};
        return;
    }
    auto** slot = reinterpret_cast<void**>(
        g_clientBase + kEngineStudioSetupLightingSlotRva);
    __try
    {
        if (*slot == reinterpret_cast<void*>(&StudioSetupLighting_Hook))
        {
            PatchPointer(slot,
                         reinterpret_cast<void*>(&StudioSetupLighting_Hook),
                         reinterpret_cast<void*>(g_originalSetupLighting));
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    g_setupLightingHookReady = false;
    g_lighting = {};
}

template <typename T>
T ReadQgl(std::uintptr_t rva)
{
    if (!g_hwBase)
        return nullptr;
    __try { return reinterpret_cast<T>(*reinterpret_cast<void**>(g_hwBase + rva)); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

void* GetGlProc(const char* name);

bool RefreshGlFunctions()
{
    g_bindBuffer = ReadQgl<GlBindBufferFn>(kQglBindBufferRva);
    g_deleteBuffers = ReadQgl<GlDeleteBuffersFn>(kQglDeleteBuffersRva);
    g_genBuffers = ReadQgl<GlGenBuffersFn>(kQglGenBuffersRva);
    g_bufferData = ReadQgl<GlBufferDataFn>(kQglBufferDataRva);
    g_bufferSubData =
        reinterpret_cast<GlBufferSubDataFn>(GetGlProc("glBufferSubData"));
    g_getIntegerv = ReadQgl<GlGetIntegervFn>(kQglGetIntegervRva);
    g_getFloatv = ReadQgl<GlGetFloatvFn>(kQglGetFloatvRva);
    g_texCoord2f = ReadQgl<GlTexCoord2fFn>(kQglTexCoord2fRva);
    g_activeTexture = ReadQgl<GlActiveTextureFn>(kQglActiveTextureRva);
    g_color4f = ReadQgl<GlColor4fFn>(kQglColor4fRva);
    g_alphaFunc = ReadQgl<GlAlphaFuncFn>(kQglAlphaFuncRva);
    g_depthMask = ReadQgl<GlDepthMaskFn>(kQglDepthMaskRva);
    g_blendFunc = ReadQgl<GlBlendFuncFn>(kQglBlendFuncRva);
    g_shadeModel = ReadQgl<GlShadeModelFn>(kQglShadeModelRva);
    g_texEnvi = ReadQgl<GlTexEnviFn>(kQglTexEnviRva);
    g_getTexEnviv =
        reinterpret_cast<GlGetTexEnvivFn>(GetGlProc("glGetTexEnviv"));
    g_drawElements = ReadQgl<GlDrawElementsFn>(kQglDrawElementsRva);
    g_drawElementsInstanced =
        reinterpret_cast<GlDrawElementsInstancedFn>(
            GetGlProc("glDrawElementsInstanced"));
    if (!g_drawElementsInstanced)
    {
        g_drawElementsInstanced =
            reinterpret_cast<GlDrawElementsInstancedFn>(
                GetGlProc("glDrawElementsInstancedARB"));
    }
    g_cullFace = ReadQgl<GlCullFaceFn>(kQglCullFaceRva);
    // Optional, used only by the deferred client shadow replay.
    g_depthFunc = ReadQgl<GlDepthFuncFn>(kQglDepthFuncRva);
    g_matrixMode =
        reinterpret_cast<GlMatrixModeFn>(GetGlProc("glMatrixMode"));
    g_pushMatrix =
        reinterpret_cast<GlPushMatrixFn>(GetGlProc("glPushMatrix"));
    g_popMatrix =
        reinterpret_cast<GlPopMatrixFn>(GetGlProc("glPopMatrix"));
    g_loadMatrixf =
        reinterpret_cast<GlLoadMatrixfFn>(GetGlProc("glLoadMatrixf"));
    g_texCoord4fv =
        reinterpret_cast<GlTexCoord4fvFn>(GetGlProc("glTexCoord4fv"));
    g_enable = ReadQgl<GlEnableFn>(kQglEnableRva);
    g_disable = ReadQgl<GlDisableFn>(kQglDisableRva);
    g_isEnabled = ReadQgl<GlIsEnabledFn>(kQglIsEnabledRva);
    g_pushClientAttrib =
        ReadQgl<GlPushClientAttribFn>(kQglPushClientAttribRva);
    g_popClientAttrib =
        ReadQgl<GlPopClientAttribFn>(kQglPopClientAttribRva);
    g_disableClientState =
        ReadQgl<GlDisableClientStateFn>(kQglDisableClientStateRva);
    g_clientActiveTexture =
        ReadQgl<GlClientActiveTextureFn>(kQglClientActiveTextureRva);
    return g_bindBuffer && g_deleteBuffers && g_genBuffers &&
           g_bufferData && g_bufferSubData && g_getIntegerv && g_getFloatv &&
           g_texCoord2f && g_activeTexture && g_color4f && g_alphaFunc &&
           g_depthMask && g_blendFunc && g_shadeModel && g_texEnvi &&
           g_getTexEnviv && g_drawElements &&
           g_cullFace && g_enable && g_disable && g_isEnabled &&
           g_pushClientAttrib && g_popClientAttrib &&
           g_disableClientState && g_clientActiveTexture;
}

void* GetGlProc(const char* name)
{
    if (!g_hwBase || !name)
        return nullptr;
    __try
    {
        const auto getProc =
            *reinterpret_cast<SdlGetProcAddressFn*>(
                g_hwBase + kSdlGetProcAddressIatRva);
        return getProc ? getProc(name) : nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
}

bool RefreshShaderFunctions()
{
    g_createShader =
        reinterpret_cast<GlCreateShaderFn>(GetGlProc("glCreateShader"));
    g_shaderSource =
        reinterpret_cast<GlShaderSourceFn>(GetGlProc("glShaderSource"));
    g_compileShader =
        reinterpret_cast<GlCompileShaderFn>(GetGlProc("glCompileShader"));
    g_getShaderiv =
        reinterpret_cast<GlGetShaderivFn>(GetGlProc("glGetShaderiv"));
    g_getShaderInfoLog =
        reinterpret_cast<GlGetShaderInfoLogFn>(GetGlProc("glGetShaderInfoLog"));
    g_deleteShader =
        reinterpret_cast<GlDeleteShaderFn>(GetGlProc("glDeleteShader"));
    g_createProgram =
        reinterpret_cast<GlCreateProgramFn>(GetGlProc("glCreateProgram"));
    g_attachShader =
        reinterpret_cast<GlAttachShaderFn>(GetGlProc("glAttachShader"));
    g_bindAttribLocation =
        reinterpret_cast<GlBindAttribLocationFn>(GetGlProc("glBindAttribLocation"));
    g_linkProgram =
        reinterpret_cast<GlLinkProgramFn>(GetGlProc("glLinkProgram"));
    g_getProgramiv =
        reinterpret_cast<GlGetProgramivFn>(GetGlProc("glGetProgramiv"));
    g_getProgramInfoLog =
        reinterpret_cast<GlGetProgramInfoLogFn>(GetGlProc("glGetProgramInfoLog"));
    g_useProgram =
        reinterpret_cast<GlUseProgramFn>(GetGlProc("glUseProgram"));
    g_deleteProgram =
        reinterpret_cast<GlDeleteProgramFn>(GetGlProc("glDeleteProgram"));
    g_getUniformLocation =
        reinterpret_cast<GlGetUniformLocationFn>(GetGlProc("glGetUniformLocation"));
    g_uniform4fv =
        reinterpret_cast<GlUniform4fvFn>(GetGlProc("glUniform4fv"));
    g_uniform3fv =
        reinterpret_cast<GlUniform3fvFn>(GetGlProc("glUniform3fv"));
    g_uniformMatrix4fv =
        reinterpret_cast<GlUniformMatrix4fvFn>(
            GetGlProc("glUniformMatrix4fv"));
    g_vertexAttribPointer =
        reinterpret_cast<GlVertexAttribPointerFn>(GetGlProc("glVertexAttribPointer"));
    g_enableVertexAttribArray =
        reinterpret_cast<GlEnableVertexAttribArrayFn>(
            GetGlProc("glEnableVertexAttribArray"));
    g_disableVertexAttribArray =
        reinterpret_cast<GlDisableVertexAttribArrayFn>(
            GetGlProc("glDisableVertexAttribArray"));
    g_getVertexAttribiv =
        reinterpret_cast<GlGetVertexAttribivFn>(GetGlProc("glGetVertexAttribiv"));
    g_getIntegeriV =
        reinterpret_cast<GlGetIntegeriVFn>(GetGlProc("glGetIntegeri_v"));
    g_getInteger64iV =
        reinterpret_cast<GlGetInteger64iVFn>(
            GetGlProc("glGetInteger64i_v"));
    g_getUniformBlockIndex =
        reinterpret_cast<GlGetUniformBlockIndexFn>(
            GetGlProc("glGetUniformBlockIndex"));
    g_uniformBlockBinding =
        reinterpret_cast<GlUniformBlockBindingFn>(
            GetGlProc("glUniformBlockBinding"));
    g_bindBufferRange =
        reinterpret_cast<GlBindBufferRangeFn>(
            GetGlProc("glBindBufferRange"));
    g_bindBufferBase =
        reinterpret_cast<GlBindBufferBaseFn>(
            GetGlProc("glBindBufferBase"));
    g_mapBufferRange =
        reinterpret_cast<GlMapBufferRangeFn>(
            GetGlProc("glMapBufferRange"));
    g_flushMappedBufferRange =
        reinterpret_cast<GlFlushMappedBufferRangeFn>(
            GetGlProc("glFlushMappedBufferRange"));
    g_unmapBuffer =
        reinterpret_cast<GlUnmapBufferFn>(
            GetGlProc("glUnmapBuffer"));
    g_bufferStorage =
        reinterpret_cast<GlBufferStorageFn>(
            GetGlProc("glBufferStorage"));
    g_fenceSync =
        reinterpret_cast<GlFenceSyncFn>(GetGlProc("glFenceSync"));
    g_clientWaitSync =
        reinterpret_cast<GlClientWaitSyncFn>(
            GetGlProc("glClientWaitSync"));
    g_deleteSync =
        reinterpret_cast<GlDeleteSyncFn>(GetGlProc("glDeleteSync"));
    if (HMODULE gl = GetModuleHandleA("opengl32.dll"))
    {
        g_getString = reinterpret_cast<GlGetStringFn>(
            GetProcAddress(gl, "glGetString"));
    }
    g_genVertexArrays =
        reinterpret_cast<GlGenVertexArraysFn>(GetGlProc("glGenVertexArrays"));
    g_deleteVertexArrays =
        reinterpret_cast<GlDeleteVertexArraysFn>(GetGlProc("glDeleteVertexArrays"));
    g_bindVertexArray =
        reinterpret_cast<GlBindVertexArrayFn>(GetGlProc("glBindVertexArray"));

    return g_createShader && g_shaderSource && g_compileShader &&
           g_getShaderiv && g_deleteShader && g_createProgram &&
           g_attachShader && g_bindAttribLocation && g_linkProgram &&
           g_getProgramiv && g_useProgram && g_deleteProgram &&
           g_getUniformLocation && g_uniform4fv && g_uniform3fv &&
           g_uniformMatrix4fv &&
           g_vertexAttribPointer && g_enableVertexAttribArray &&
           g_disableVertexAttribArray && g_getVertexAttribiv;
}

bool ReadCurrentStudioState(const StudioHeaderRaw*& header,
                            const StudioModelRaw*& currentModel)
{
    header = nullptr;
    currentModel = nullptr;
    if (!g_hwBase)
        return false;
    __try
    {
        header = *reinterpret_cast<const StudioHeaderRaw* const*>(
            g_hwBase + kStudioHdrPtrRva);
        currentModel = *reinterpret_cast<const StudioModelRaw* const*>(
            g_hwBase + kCurrentStudioModelRva);
        return header != nullptr && currentModel != nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        header = nullptr;
        currentModel = nullptr;
        return false;
    }
}

int ReadMode()
{
    if (!g_mode)
        return 0;
    __try { return static_cast<int>(g_mode->value); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

int ReadNonPlayerMode()
{
    if (!g_nonPlayerMode)
        return 0;
    __try { return static_cast<int>(g_nonPlayerMode->value); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

int ReadMeshoptMode()
{
    if (!g_meshoptMode)
        return 0;
    __try { return static_cast<int>(g_meshoptMode->value); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

int ReadInstancingMode()
{
    if (!g_instancingMode)
        return 0;
    __try { return static_cast<int>(g_instancingMode->value); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

int ReadShadowDeferMode()
{
    if (!g_shadowDeferMode)
        return 0;
    __try { return static_cast<int>(g_shadowDeferMode->value); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

bool ReadExactRDrawEntitiesNormal()
{
    if (!g_entryEngine || !g_entryEngine->pfnGetCvarFloat)
        return false;
    __try
    {
        return g_entryEngine->pfnGetCvarFloat("r_drawentities") == 1.0f;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool DrawModelEntryCompatible(int flags, void* caller)
{
    if (flags != 3)
        return DrawModelEntryFallback(DrawModelEntryReject::Flags);
    if (ReadMode() != 1 ||
        ReadNonPlayerMode() != 1 ||
        !g_deferredBarriersReady ||
        !g_clientStudioShadowBarrierReady ||
        g_solidEntityPassDepth <= 0 ||
        !g_hwBase)
        return DrawModelEntryFallback(DrawModelEntryReject::Mode);
    if (caller != g_hwBase + kSolidDrawModelReturnRva)
        return DrawModelEntryFallback(DrawModelEntryReject::Caller);
    if (!ReadExactRDrawEntitiesNormal())
        return DrawModelEntryFallback(DrawModelEntryReject::DrawEntities);

    DrawModelEntryReject reject = DrawModelEntryReject::Count;
    __try
    {
        std::uint8_t* const entity =
            *reinterpret_cast<std::uint8_t**>(
                g_hwBase + kCurrentEntityRva);
        const std::uint8_t* const model =
            entity
                ? *reinterpret_cast<std::uint8_t* const*>(
                      entity + 0xB94)
                : nullptr;
        // Inside Gold's DrawPoints only renderfx 19 (glow shell: normal
        // inflation, forced CHROME, ONE/ONE second pass) changes the output.
        // Every other renderfx (kRenderFxDeadPlayer corpses, NoDissipation,
        // LightMultiplier, ...) only feeds client/lighting code that runs
        // before DrawPoints and is captured there like for players.
        // MOVETYPE_FOLLOW/aiment entities reach DrawPoints with Gold's
        // already-merged bone transforms and setup lighting, which the
        // recorder copies at that point exactly like any other entity. Their
        // parent's setup-only DrawModel/DrawPlayer(0) emits nothing.
        if (!entity ||
            *reinterpret_cast<const int*>(entity + 4) != 0 ||
            !model ||
            *reinterpret_cast<const int*>(model + 0x44) != 3)
            reject = DrawModelEntryReject::Entity;
        else if (*reinterpret_cast<const int*>(entity + 0x2F8) != 0)
            reject = DrawModelEntryReject::RenderMode;
        else if (*reinterpret_cast<const int*>(entity + 0x304) == 19)
            reject = DrawModelEntryReject::GlowShell;
        else if (*reinterpret_cast<const std::uint32_t*>(
                     g_hwBase + kForceFaceFlagsRva) != 0)
            reject = DrawModelEntryReject::ForceFlags;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        reject = DrawModelEntryReject::Entity;
    }
    if (reject != DrawModelEntryReject::Count)
        return DrawModelEntryFallback(reject);
    return true;
}

bool ReleaseOwnedDirectProgram()
{
    if (!g_directProgramOwned)
        return true;
    if (!g_useProgram)
        return false;
    __try
    {
        g_useProgram(0);
        g_directProgramOwned = false;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

StudioHeaderRaw* ResolveTextureHeaderSafe()
{
    if (!g_textureHeader)
        return nullptr;
    __try { return g_textureHeader(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

bool ReadDirectGlobals(DirectGlobals& out)
{
    out = {};
    if (!g_hwBase)
        return false;
    __try
    {
        out.header =
            *reinterpret_cast<const StudioHeaderRaw* const*>(
                g_hwBase + kStudioHdrPtrRva);
        out.currentModel =
            *reinterpret_cast<const StudioModelRaw* const*>(
                g_hwBase + kCurrentStudioModelRva);
        out.entity =
            *reinterpret_cast<std::uint8_t**>(
                g_hwBase + kCurrentEntityRva);
        if (!out.header || !out.currentModel || !out.entity)
            return false;

        out.entityModel =
            *reinterpret_cast<const void* const*>(
                out.entity + 0xB94);
        out.player = *reinterpret_cast<const int*>(out.entity + 4);
        out.skin = static_cast<int>(
            *reinterpret_cast<const std::int16_t*>(out.entity + 0x2E8));
        out.renderMode =
            *reinterpret_cast<const int*>(out.entity + 0x2F8);
        out.renderFx =
            *reinterpret_cast<const int*>(out.entity + 0x304);
        out.moveType =
            *reinterpret_cast<const int*>(out.entity + 0x308);
        out.aiment =
            *reinterpret_cast<const int*>(out.entity + 0x344);
        out.setupRenderMode =
            *reinterpret_cast<const int*>(
                g_hwBase + kStudioSetupRenderModeRva);
        out.boneCount = out.header->numbones;
        out.studioStamp =
            *reinterpret_cast<const int*>(
                g_hwBase + kStudioStampRva);
        out.numStudioLights =
            *reinterpret_cast<const int*>(
                g_hwBase + kNumStudioLightsRva);
        out.debugStudioMode =
            *reinterpret_cast<const int*>(
                g_hwBase + kDebugStudioModeRva);
        out.forceFlags =
            *reinterpret_cast<const std::uint32_t*>(
                g_hwBase + kForceFaceFlagsRva);
        out.ambient = static_cast<float>(
            *reinterpret_cast<const int*>(
                g_hwBase + kStudioAmbientRva));
        out.shade =
            *reinterpret_cast<const float*>(
                g_hwBase + kStudioShadeRva);
        std::memcpy(
            out.colorMix,
            g_hwBase + kStudioColorMixRva,
            sizeof(out.colorMix));
        out.blend =
            *reinterpret_cast<const float*>(
                g_hwBase + kStudioBlendRva);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        out = {};
        return false;
    }
}

double Det3(const double m[3][3])
{
    return
        m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
        m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
        m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

void Solve3x3(
    const double a[3][3],
    const double b[3],
    double out[3])
{
    const double invDet = 1.0 / Det3(a);
    for (int column = 0; column < 3; ++column)
    {
        double m[3][3];
        std::memcpy(m, a, sizeof(m));
        for (int row = 0; row < 3; ++row)
            m[row][column] = b[row];
        out[column] = Det3(m) * invDet;
    }
}

void SnapGammaNoise(double values[3], double span)
{
    constexpr double epsilon = 1.0e-7;
    double scale = span;
    for (int i = 0; i < 3; ++i)
    {
        if (std::fabs(values[i]) * scale < epsilon)
            values[i] = 0.0;
        scale *= span;
    }
}

GammaCurveFit FitGammaCurve(
    float gamma,
    float lightgamma,
    float brightness)
{
    GammaCurveFit result{};
    if (!(gamma > 0.0f) || !(lightgamma > 0.0f))
        return result;

    const double L = static_cast<double>(lightgamma);
    const double G = 1.0 / static_cast<double>(gamma);
    double hinge = 0.125;
    double scale = 1.0;
    if (brightness > 1.0f)
    {
        hinge = 0.05;
        scale = static_cast<double>(brightness);
    }
    else if (brightness > 0.0f)
    {
        const double b = static_cast<double>(brightness);
        hinge = 0.125 - b * b * 0.075;
    }

    const double wh = std::pow(hinge / scale, 0.5 / L);
    const double wsat = std::pow(scale, -0.5 / L);
    const double upperSpan = wsat - wh;
    const double lowerCoeff =
        std::pow(0.125 * scale / hinge, G);
    const double lowerExponent = 2.0 * L * G;
    const double nodes[3] = {0.25, 0.75, 1.0};

    double a[3][3]{};
    double rhs[3]{};
    double p[3]{};
    double q[3]{};
    for (int i = 0; i < 3; ++i)
    {
        const double w = wh * nodes[i];
        a[i][0] = w;
        a[i][1] = w * w;
        a[i][2] = w * w * w;
        rhs[i] = lowerCoeff * std::pow(w, lowerExponent);
    }
    Solve3x3(a, rhs, p);
    SnapGammaNoise(p, wh);

    for (int i = 0; i < 3; ++i)
    {
        const double t = upperSpan * nodes[i];
        const double w = i == 2 ? wsat : wh + t;
        const double high =
            i == 2
                ? 1.0
                : std::pow(
                      0.125 +
                          ((scale * std::pow(w, 2.0 * L) - hinge) /
                           (1.0 - hinge)) *
                              0.875,
                      G);
        a[i][0] = t;
        a[i][1] = t * t;
        a[i][2] = t * t * t;
        rhs[i] =
            high -
            w * (p[0] + w * (p[1] + w * p[2]));
    }
    Solve3x3(a, rhs, q);
    SnapGammaNoise(q, upperSpan);

    result.wh = static_cast<float>(wh);
    result.wsat = static_cast<float>(wsat);
    for (int i = 0; i < 3; ++i)
    {
        result.p[i] = static_cast<float>(p[i]);
        result.q[i] = static_cast<float>(q[i]);
    }
    return result;
}

float ReadEngineCvar(const char* name, float fallback)
{
    if (!g_engine || !g_engine->pfnGetCvarFloat)
        return fallback;
    __try { return g_engine->pfnGetCvarFloat(name); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return fallback; }
}

void UpdateGammaFit()
{
    const float gamma = ReadEngineCvar("gamma", 2.5f);
    const float lightgamma = ReadEngineCvar("lightgamma", 2.5f);
    const float brightness = ReadEngineCvar("brightness", 0.0f);
    if (g_gammaFitKnown &&
        gamma == g_gammaCvar &&
        lightgamma == g_lightGammaCvar &&
        brightness == g_brightnessCvar)
        return;

    const GammaCurveFit fit =
        FitGammaCurve(gamma, lightgamma, brightness);
    if (!(fit.wsat > 0.0f))
        return;

    g_gammaFit = fit;
    g_gammaFit0[0] = fit.wh;
    g_gammaFit0[1] = fit.wsat;
    g_gammaFit0[2] = fit.p[0];
    g_gammaFit0[3] = fit.p[1];
    g_gammaFit1[0] = fit.p[2];
    g_gammaFit1[1] = fit.q[0];
    g_gammaFit1[2] = fit.q[1];
    g_gammaFit1[3] = fit.q[2];
    g_gammaCvar = gamma;
    g_lightGammaCvar = lightgamma;
    g_brightnessCvar = brightness;
    g_gammaFitKnown = true;
    ++g_gammaFitRevision;
    if (g_gammaFitRevision == 0)
        g_gammaFitRevision = 1;
}

void ResetProgramForContext(std::uint32_t generation)
{
    g_program = 0;
    g_instancedProgram = 0;
    g_programGeneration = generation;
    g_bonesLocation = -1;
    g_lightVectorsLocation = -1;
    g_gammaFit0Location = -1;
    g_gammaFit1Location = -1;
    g_paramsLocation = -1;
    g_colorBlendLocation = -1;
    g_modelViewLocation = -1;
    g_projectionLocation = -1;
    g_textureMatrixLocation = -1;
    g_instancedGammaFit0Location = -1;
    g_instancedGammaFit1Location = -1;
    g_instancedParamsLocation = -1;
    g_instancedColorBlendLocation = -1;
    g_instancedModelViewLocation = -1;
    g_instancedProjectionLocation = -1;
    g_instancedTextureMatrixLocation = -1;
    g_instancedLayoutLocation = -1;
    g_instancedBoneBlockIndex = GL_INVALID_INDEX;
    g_instancedCapacity = 0;
    g_instancedBlockBytes = 0;
    g_instancedProgramFailed = false;
    g_instancedGammaFitRevision = 0;
    g_gammaFitProgramRevision = 0;
    g_entityUniformKeyValid = false;
    g_colorBlendUniformValid = false;
    g_paramsUniformValid = false;
    g_useUniformBuffer = false;
    g_boneBlockIndex = GL_INVALID_INDEX;
    g_boneBlockBinding = 0;
    // The previous context owned the buffers, mapping and fences, so they
    // are dropped rather than deleted through the new context.
    for (UniformBufferFrame& frame : g_uniformBuffers)
        frame = {};
    g_uniformPersistentBase = nullptr;
    g_uniformBufferFrame = 0;
    g_uniformBufferOffset = 0;
    g_uniformBufferAlignment = 256;
    g_uniformBlockMaxSize = 0;
    g_uniformBufferGeneration = generation;
    g_uniformBufferReady = false;
    g_lastUniformBlockOffset = 0;
    g_lastUniformBlockSize = 0;
    g_lastUniformBlockBuffer = 0;
    g_genericUniformRestore = 0;
}

std::size_t AlignUp(std::size_t value, std::size_t alignment)
{
    if (alignment <= 1)
        return value;
    const std::size_t mask = alignment - 1;
    if ((alignment & mask) != 0)
        return ((value + alignment - 1) / alignment) * alignment;
    return (value + mask) & ~mask;
}

enum class IndexedUboKind : unsigned char
{
    None,
    Base,
    Range
};

struct IndexedUboBinding
{
    IndexedUboKind kind = IndexedUboKind::None;
    unsigned buffer = 0;
    long long start = 0;
    long long size = 0;
};

bool ReadIndexedUboBinding(unsigned binding,
                           IndexedUboBinding& out)
{
    out = {};
    if (!g_getIntegeriV || !g_getInteger64iV)
        return false;
    int buffer = 0;
    long long start = 0;
    long long size = 0;
    __try
    {
        g_getIntegeriV(
            GL_UNIFORM_BUFFER_BINDING,
            binding,
            &buffer);
        if (buffer != 0)
        {
            g_getInteger64iV(
                GL_UNIFORM_BUFFER_START,
                binding,
                &start);
            g_getInteger64iV(
                GL_UNIFORM_BUFFER_SIZE,
                binding,
                &size);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
    if (buffer == 0)
        return true;
    if (buffer < 0 || start < 0 || size < 0)
        return false;

    out.buffer = static_cast<unsigned>(buffer);
    out.start = start;
    out.size = size;
    if (start == 0 && size == 0)
    {
        out.kind = IndexedUboKind::Base;
        return true;
    }
    if (size <= 0)
        return false;
    out.kind = IndexedUboKind::Range;
    return true;
}

bool ReadGenericUniformBinding(int& buffer)
{
    buffer = 0;
    if (!g_getIntegerv)
        return false;
    __try
    {
        g_getIntegerv(GL_UNIFORM_BUFFER_BINDING, &buffer);
        return buffer >= 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        buffer = 0;
        return false;
    }
}

bool RestoreGenericUniformBinding(int buffer)
{
    if (!g_bindBuffer || buffer < 0)
        return false;
    __try
    {
        g_bindBuffer(
            GL_UNIFORM_BUFFER,
            static_cast<unsigned>(buffer));
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool RestoreKnownGenericUniformBinding()
{
    return RestoreGenericUniformBinding(g_genericUniformRestore);
}

void NoteOwnedUniformRange(unsigned buffer,
                           std::size_t offset,
                           std::size_t size)
{
    g_lastUniformBlockBuffer = buffer;
    g_lastUniformBlockOffset = offset;
    g_lastUniformBlockSize = size;
}

void ClearUniformBindingOwnership()
{
    g_lastUniformBlockBuffer = 0;
    g_lastUniformBlockOffset = 0;
    g_lastUniformBlockSize = 0;
}

bool UniformBufferApiReady()
{
    return g_getIntegeriV &&
           g_getInteger64iV &&
           g_getUniformBlockIndex &&
           g_uniformBlockBinding &&
           g_bindBufferRange &&
           g_bindBufferBase &&
           g_bufferSubData &&
           g_bindBuffer &&
           g_bufferData &&
           g_genBuffers &&
           g_getIntegerv;
}

bool HasExtensionToken(const char* extensions, const char* name)
{
    if (!extensions || !name || !*name)
        return false;
    const std::size_t length = std::strlen(name);
    for (const char* at = std::strstr(extensions, name); at;
         at = std::strstr(at + 1, name))
    {
        const bool startOk = at == extensions || at[-1] == ' ';
        const char end = at[length];
        if (startOk && (end == '\0' || end == ' '))
            return true;
    }
    return false;
}

// GL 4.4 core or GL_ARB_buffer_storage, plus ARB_sync for the region fences.
bool PersistentUniformApiReady()
{
    if (!g_bufferStorage || !g_mapBufferRange || !g_fenceSync ||
        !g_clientWaitSync || !g_deleteSync || !g_genBuffers ||
        !g_bindBuffer || !g_getIntegerv)
        return false;
    int major = 0;
    int minor = 0;
    __try
    {
        g_getIntegerv(GL_MAJOR_VERSION, &major);
        g_getIntegerv(GL_MINOR_VERSION, &minor);
        if (major > 4 || (major == 4 && minor >= 4))
            return true;
        if (!g_getString)
            return false;
        return HasExtensionToken(
            reinterpret_cast<const char*>(g_getString(GL_EXTENSIONS)),
            "GL_ARB_buffer_storage");
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// Allocates and maps the persistent ring. Leaves the new buffer bound to the
// generic GL_UNIFORM_BUFFER target, the caller restores it.
bool CreatePersistentUniformRing(unsigned& buffer, std::uint8_t*& base)
{
    buffer = 0;
    base = nullptr;
    const std::size_t totalBytes =
        kUniformBufferBytes * static_cast<std::size_t>(kUniformBufferCount);
    const unsigned flags =
        GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
    __try
    {
        g_genBuffers(1, &buffer);
        if (buffer)
        {
            g_bindBuffer(GL_UNIFORM_BUFFER, buffer);
            g_bufferStorage(
                GL_UNIFORM_BUFFER,
                static_cast<std::ptrdiff_t>(totalBytes),
                nullptr,
                flags);
            // A failed glBufferStorage leaves no storage, the map then
            // fails and returns null.
            base = static_cast<std::uint8_t*>(
                g_mapBufferRange(
                    GL_UNIFORM_BUFFER,
                    0,
                    static_cast<std::ptrdiff_t>(totalBytes),
                    flags));
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        base = nullptr;
    }
    if (base)
        return true;
    if (buffer && g_deleteBuffers)
    {
        __try { g_deleteBuffers(1, &buffer); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    buffer = 0;
    return false;
}

bool CreateOrphanUniformBuffers(unsigned (&buffers)[kUniformBufferCount])
{
    for (unsigned& buffer : buffers)
        buffer = 0;
    bool setupOk = false;
    __try
    {
        g_genBuffers(kUniformBufferCount, buffers);
        setupOk = true;
        for (int i = 0; i < kUniformBufferCount; ++i)
        {
            if (!buffers[i])
            {
                setupOk = false;
                break;
            }
        }
        for (int i = 0; setupOk && i < kUniformBufferCount; ++i)
        {
            g_bindBuffer(GL_UNIFORM_BUFFER, buffers[i]);
            g_bufferData(
                GL_UNIFORM_BUFFER,
                static_cast<std::ptrdiff_t>(kUniformBufferBytes),
                nullptr,
                GL_STREAM_DRAW);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        setupOk = false;
    }
    return setupOk;
}

void DeleteUniformBufferNames(const unsigned* buffers, int count)
{
    if (!g_deleteBuffers)
        return;
    for (int i = 0; i < count; ++i)
    {
        if (!buffers[i])
            continue;
        // Repeated names (the persistent ring) are deleted once.
        bool repeated = false;
        for (int j = 0; j < i; ++j)
            repeated = repeated || buffers[j] == buffers[i];
        if (repeated)
            continue;
        __try { g_deleteBuffers(1, &buffers[i]); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
}

// Same-context re-initialization (a later program setup step failed and
// EnsureDirectProgram retries) must not leak the ring of the previous attempt.
// Deleting the persistent buffer also unmaps it. Buffers of an older context
// are dropped by ResetProgramForContext instead.
void ReleaseUniformBuffersForContext()
{
    if (!g_uniformBufferReady ||
        !worldvbo::ContextGenerationReady() ||
        g_uniformBufferGeneration != worldvbo::ContextGeneration())
        return;
    unsigned names[kUniformBufferCount]{};
    for (int i = 0; i < kUniformBufferCount; ++i)
    {
        UniformBufferFrame& frame = g_uniformBuffers[i];
        names[i] = frame.buffer;
        if (frame.fence && g_deleteSync)
        {
            __try { g_deleteSync(frame.fence); }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        frame = {};
    }
    DeleteUniformBufferNames(names, kUniformBufferCount);
    g_uniformPersistentBase = nullptr;
    g_uniformBufferReady = false;
    ClearUniformBindingOwnership();
}

bool InitializeUniformBuffers(unsigned program)
{
    if (!program || !UniformBufferApiReady() ||
        !worldvbo::ContextGenerationReady())
        return false;

    int maxBlockSize = 0;
    int maxBindings = 0;
    int alignment = 0;
    __try
    {
        g_getIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &maxBlockSize);
        g_getIntegerv(GL_MAX_UNIFORM_BUFFER_BINDINGS, &maxBindings);
        g_getIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &alignment);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
    if (maxBlockSize < static_cast<int>(kBoneBlockBytes) ||
        maxBindings <= 0 || alignment <= 0)
        return false;

    const unsigned block =
        g_getUniformBlockIndex(program, "StudioBoneConstants");
    if (block == GL_INVALID_INDEX)
        return false;

    ReleaseUniformBuffersForContext();

    // Reserve an actually unused indexed point, scanning high-to-low so we
    // stay out of the way of conventional low-numbered plugin bindings.
    unsigned binding = UINT_MAX;
    for (int candidate = maxBindings - 1; candidate >= 0; --candidate)
    {
        IndexedUboBinding indexed{};
        if (!ReadIndexedUboBinding(
                static_cast<unsigned>(candidate), indexed))
            return false;
        if (indexed.kind == IndexedUboKind::None)
        {
            binding = static_cast<unsigned>(candidate);
            break;
        }
    }
    if (binding == UINT_MAX)
        return false;

    int previousGenericBinding = 0;
    if (!ReadGenericUniformBinding(previousGenericBinding))
        return false;
    unsigned buffers[kUniformBufferCount]{};
    std::uint8_t* persistentBase = nullptr;
    // Region bases are multiples of kUniformBufferBytes. Every bound range
    // offset (region base + aligned offset) is therefore a multiple of
    // GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT only when it divides the region size.
    const bool persistentAligned =
        kUniformBufferBytes % static_cast<std::size_t>(alignment) == 0u;
    const bool persistentApi =
        persistentAligned && PersistentUniformApiReady();
    bool setupOk = false;
    if (persistentApi)
    {
        unsigned ring = 0;
        if (CreatePersistentUniformRing(ring, persistentBase))
        {
            for (unsigned& buffer : buffers)
                buffer = ring;
            setupOk = true;
        }
    }
    if (!setupOk)
    {
        persistentBase = nullptr;
        setupOk = CreateOrphanUniformBuffers(buffers);
    }
    if (setupOk)
    {
        __try { g_uniformBlockBinding(program, block, binding); }
        __except (EXCEPTION_EXECUTE_HANDLER) { setupOk = false; }
    }
    for (int i = 0; setupOk && i < kUniformBufferCount; ++i)
        setupOk = buffers[i] != 0;

    const bool genericRestored =
        RestoreGenericUniformBinding(previousGenericBinding);
    if (!setupOk || !genericRestored)
    {
        DeleteUniformBufferNames(buffers, kUniformBufferCount);
        if (!genericRestored)
            (void)RestoreGenericUniformBinding(previousGenericBinding);
        return false;
    }

    for (int i = 0; i < kUniformBufferCount; ++i)
    {
        UniformBufferFrame& frame = g_uniformBuffers[i];
        frame.buffer = buffers[i];
        frame.base = persistentBase
            ? static_cast<std::size_t>(i) * kUniformBufferBytes
            : 0u;
        frame.fence = nullptr;
    }

    g_uniformPersistentBase = persistentBase;
    g_uniformBufferAlignment = alignment;
    g_uniformBlockMaxSize = maxBlockSize;
    g_uniformBufferFrame = 0;
    g_uniformBufferOffset = 0;
    g_uniformBufferGeneration = worldvbo::ContextGeneration();
    g_uniformBufferReady = true;
    g_boneBlockIndex = block;
    g_boneBlockBinding = binding;
    g_lastUniformBlockOffset = 0;
    g_lastUniformBlockSize = 0;
    g_lastUniformBlockBuffer = 0;
    g_genericUniformRestore = previousGenericBinding;
    g_entityUniformKeyValid = false;
    rendererlog::Line(
        "studio renderer: UBO constants ready block=%dKB alignment=%d binding=%u",
        maxBlockSize / 1024, alignment, binding);
    if (persistentBase)
    {
        rendererlog::Line(
            "studio renderer: UBO upload mode=persistent "
            "(buffer_storage coherent ring %ux%uKB, fence per frame region)",
            static_cast<unsigned>(kUniformBufferCount),
            static_cast<unsigned>(kUniformBufferBytes / 1024u));
    }
    else
    {
        rendererlog::Line(
            "studio renderer: UBO upload mode=orphan (map/subdata, %s)",
            !persistentAligned
                ? "alignment does not divide the frame region"
                : persistentApi
                      ? "persistent allocation failed"
                      : "buffer_storage unavailable");
    }
    return true;
}

// Persistent mode: the region about to be reused was last written three
// frames ago. Its fence has normally signaled long since, the blocking wait
// is a safety net for a GPU queue running more than two frames behind.
bool WaitUniformRegionFence(UniformBufferFrame& frame)
{
    if (!frame.fence)
        return true;
    unsigned status = 0;
    __try
    {
        status = g_clientWaitSync(frame.fence, 0, 0);
        if (status != GL_ALREADY_SIGNALED &&
            status != GL_CONDITION_SATISFIED)
        {
            if (prof::Active())
                ++g_uniformFenceWaits;
            for (int attempt = 0; attempt < 4; ++attempt)
            {
                status = g_clientWaitSync(
                    frame.fence,
                    GL_SYNC_FLUSH_COMMANDS_BIT,
                    250000000ull);
                if (status == GL_ALREADY_SIGNALED ||
                    status == GL_CONDITION_SATISFIED)
                    break;
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
    if (status != GL_ALREADY_SIGNALED &&
        status != GL_CONDITION_SATISFIED)
        return false;
    __try { g_deleteSync(frame.fence); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    frame.fence = nullptr;
    return true;
}

bool RotatePersistentUniformFrame(int nextFrame)
{
    UniformBufferFrame& next = g_uniformBuffers[nextFrame];
    // On a failed wait the current region keeps filling linearly. Ranges are
    // never rewritten within a frame, so staying on it is safe, the rotation
    // is retried on the next frame.
    if (!WaitUniformRegionFence(next))
        return false;
    UniformBufferFrame& current =
        g_uniformBuffers[g_uniformBufferFrame];
    if (current.fence)
    {
        __try { g_deleteSync(current.fence); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        current.fence = nullptr;
    }
    __try
    {
        current.fence =
            g_fenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        current.fence = nullptr;
    }
    if (!current.fence)
        return false;
    g_uniformBufferFrame = nextFrame;
    g_uniformBufferOffset = 0;
    g_entityUniformKeyValid = false;
    return true;
}

bool RotateUniformBufferFrame()
{
    if (!g_useUniformBuffer || !g_uniformBufferReady ||
        !worldvbo::ContextGenerationReady() ||
        g_uniformBufferGeneration != worldvbo::ContextGeneration())
        return false;

    const int nextFrame =
        (g_uniformBufferFrame + 1) % kUniformBufferCount;
    if (g_uniformPersistentBase)
        return RotatePersistentUniformFrame(nextFrame);
    UniformBufferFrame& frame =
        g_uniformBuffers[nextFrame];
    if (!frame.buffer)
        return false;

    bool orphaned = false;
    __try
    {
        g_bindBuffer(GL_UNIFORM_BUFFER, frame.buffer);
        // Orphan once per frame. Subsequent entity constants are linear writes
        // into this fresh storage, older frames live in the other two buffers.
        g_bufferData(
            GL_UNIFORM_BUFFER,
            static_cast<std::ptrdiff_t>(kUniformBufferBytes),
            nullptr,
            GL_STREAM_DRAW);
        orphaned = true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        orphaned = false;
    }
    const bool genericRestored =
        RestoreKnownGenericUniformBinding();
    if (!orphaned || !genericRestored)
        return false;
    g_uniformBufferFrame = nextFrame;
    g_uniformBufferOffset = 0;
    g_entityUniformKeyValid = false;
    return true;
}

void ReleaseUniformBufferBinding()
{
    if (!g_useUniformBuffer || !g_uniformBufferReady ||
        !g_bindBufferBase || !g_bindBuffer ||
        g_lastUniformBlockBuffer == 0)
        return;

    bool released = false;
    __try
    {
        g_bindBufferBase(
            GL_UNIFORM_BUFFER,
            g_boneBlockBinding,
            0);
        released = true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        released = false;
    }
    const bool genericRestored =
        RestoreKnownGenericUniformBinding();
    if (released && genericRestored)
        ClearUniformBindingOwnership();
}

unsigned CompileDirectProgram(bool useUniformBuffer)
{
    if (!RefreshShaderFunctions())
        return 0;

    static const char* headerLegacy =
        "#version 120\n";
    static const char* headerUbo =
        "#version 140\n"
        "#extension GL_ARB_compatibility : require\n";
    static const char* declarationsLegacy =
        "attribute vec3 aPosition;\n"
        "attribute vec3 aNormal;\n"
        "attribute vec2 aRawST;\n"
        "attribute float aPositionBone;\n"
        "attribute float aNormalBone;\n"
        "uniform vec4 uBones[384];\n"
        "uniform vec3 uLightVectors[128];\n"
        "#define BONE_ROW0(i) uBones[(i) * 3 + 0]\n"
        "#define BONE_ROW1(i) uBones[(i) * 3 + 1]\n"
        "#define BONE_ROW2(i) uBones[(i) * 3 + 2]\n"
        "#define LIGHT_VECTOR(i) uLightVectors[i]\n"
        "#define APPLY_POSITION(v) (gl_ModelViewProjectionMatrix * (v))\n"
        "#define APPLY_TEX(v) (gl_TextureMatrix[0] * (v))\n"
        "uniform vec4 uGammaFit0;\n"
        "uniform vec4 uGammaFit1;\n"
        "uniform vec4 uParams;\n"
        "uniform vec4 uColorBlend;\n";
    static const char* declarationsUbo =
        "in vec3 aPosition;\n"
        "in vec3 aNormal;\n"
        "in vec2 aRawST;\n"
        "in float aPositionBone;\n"
        "in float aNormalBone;\n"
        "struct StudioBoneData {\n"
        "  vec4 row0;\n"
        "  vec4 row1;\n"
        "  vec4 row2;\n"
        "  vec4 light;\n"
        "};\n"
        "layout(std140) uniform StudioBoneConstants {\n"
        "  StudioBoneData uBoneData[128];\n"
        "};\n"
        "#define BONE_ROW0(i) uBoneData[i].row0\n"
        "#define BONE_ROW1(i) uBoneData[i].row1\n"
        "#define BONE_ROW2(i) uBoneData[i].row2\n"
        "#define LIGHT_VECTOR(i) uBoneData[i].light.xyz\n"
        "uniform mat4 uModelView;\n"
        "uniform mat4 uProjection;\n"
        "uniform mat4 uTextureMatrix;\n"
        "#define APPLY_POSITION(v) (uProjection * uModelView * (v))\n"
        "#define APPLY_TEX(v) (uTextureMatrix * (v))\n"
        "uniform vec4 uGammaFit0;\n"
        "uniform vec4 uGammaFit1;\n"
        "uniform vec4 uParams;\n"
        "uniform vec4 uColorBlend;\n"
        ;
    static const char* body =
        "float gammaValue(int idx) {\n"
        "  float x = float(idx) * (1.0 / 1023.0);\n"
        "  float w = min(sqrt(max(x, 0.0)), uGammaFit0.y);\n"
        "  float p = w * (uGammaFit0.z + w * (uGammaFit0.w + w * uGammaFit1.x));\n"
        "  float dt = max(w - uGammaFit0.x, 0.0);\n"
        "  float q = dt * (uGammaFit1.y + dt * (uGammaFit1.z + dt * uGammaFit1.w));\n"
        "  return clamp(p + q, 0.0, 1.0);\n"
        "}\n"
        "void main() {\n"
        "  int bi = int(aPositionBone);\n"
        "  vec4 r0 = BONE_ROW0(bi);\n"
        "  vec4 r1 = BONE_ROW1(bi);\n"
        "  vec4 r2 = BONE_ROW2(bi);\n"
        "  vec3 p;\n"
        "  p.x = ((aPosition.y * r0.y + aPosition.x * r0.x) + aPosition.z * r0.z) + r0.w;\n"
        "  p.y = ((aPosition.y * r1.y + aPosition.x * r1.x) + aPosition.z * r1.z) + r1.w;\n"
        "  p.z = ((aPosition.y * r2.y + aPosition.x * r2.x) + aPosition.z * r2.z) + r2.w;\n"
        "  float light;\n"
        "  if (uParams.x < 0.0) {\n"
        "    light = uParams.z;\n"
        "  } else {\n"
        "    vec3 lv = LIGHT_VECTOR(int(aNormalBone));\n"
        "    float lightCos = (aNormal.x * lv.x + aNormal.y * lv.y) + aNormal.z * lv.z;\n"
        "    lightCos = min(1.0, lightCos);\n"
        "    float illum = uParams.z + uParams.w;\n"
        "    float t = (lightCos + 0.49532413482666016) / 1.4953241348266602;\n"
        "    if (t > 0.0) illum = illum - uParams.w * t;\n"
        "    int gi = int(floor(clamp(illum, 0.0, 255.0) * 4.0));\n"
        "    if (gi < 0) gi = 0;\n"
        "    if (gi > 1023) gi = 1023;\n"
        "    light = gammaValue(gi);\n"
        "  }\n"
        "  vec4 c = vec4(uColorBlend.rgb * light, uColorBlend.a);\n"
        "  vec2 st = aRawST * vec2(abs(uParams.x), uParams.y);\n"
        "  gl_Position = APPLY_POSITION(vec4(p, 1.0));\n"
        "  gl_TexCoord[0] = APPLY_TEX(vec4(st, 0.0, 1.0));\n"
        "  gl_FrontColor = c;\n"
        "  gl_BackColor = c;\n"
        "}\n";
    const char* sourceParts[3] = {
        useUniformBuffer ? headerUbo : headerLegacy,
        useUniformBuffer ? declarationsUbo : declarationsLegacy,
        body
    };

    unsigned shader = 0;
    unsigned program = 0;
    __try
    {
        shader = g_createShader(GL_VERTEX_SHADER);
        if (!shader)
            return 0;
        g_shaderSource(shader, 3, sourceParts, nullptr);
        g_compileShader(shader);
        int compiled = 0;
        g_getShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (!compiled)
        {
            if (g_getShaderInfoLog)
            {
                char log[1024]{};
                int length = 0;
                g_getShaderInfoLog(
                    shader, static_cast<int>(sizeof(log) - 1), &length, log);
                rendererlog::Line(
                    "studio renderer: vertex shader compile failed: %s", log);
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
        g_bindAttribLocation(program, kAttribPosition, "aPosition");
        g_bindAttribLocation(program, kAttribNormal, "aNormal");
        g_bindAttribLocation(program, kAttribRawST, "aRawST");
        g_bindAttribLocation(program, kAttribPositionBone, "aPositionBone");
        g_bindAttribLocation(program, kAttribNormalBone, "aNormalBone");
        g_linkProgram(program);
        int linked = 0;
        g_getProgramiv(program, GL_LINK_STATUS, &linked);
        g_deleteShader(shader);
        shader = 0;
        if (!linked)
        {
            if (g_getProgramInfoLog)
            {
                char log[1024]{};
                int length = 0;
                g_getProgramInfoLog(
                    program, static_cast<int>(sizeof(log) - 1), &length, log);
                rendererlog::Line(
                    "studio renderer: program link failed: %s", log);
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

unsigned CompileInstancedProgram(unsigned boneRecords, unsigned capacity)
{
    if (!RefreshShaderFunctions() ||
        boneRecords < 128u || capacity < 2u || capacity > kInstanceCap)
        return 0;

    // Each instance reads its own compact submodel palette. The CPU stages
    // instance i at record i * uInstanceLayout.x of one bound range, so N
    // entities sharing a submodel and material draw with one call per mesh.
    char header[192]{};
    std::snprintf(
        header, sizeof(header),
        "#version 140\n"
        "#extension GL_ARB_compatibility : require\n"
        "#define BONE_RECORDS %u\n"
        "#define INSTANCE_CAP %u\n",
        boneRecords, capacity);
    static const char* declarations =
        "in vec3 aPosition;\n"
        "in vec3 aNormal;\n"
        "in vec2 aRawST;\n"
        "in float aPositionBone;\n"
        "in float aNormalBone;\n"
        "struct StudioBoneData {\n"
        "  vec4 row0;\n"
        "  vec4 row1;\n"
        "  vec4 row2;\n"
        "  vec4 light;\n"
        "};\n"
        "layout(std140) uniform StudioBoneConstants {\n"
        "  StudioBoneData uBoneData[BONE_RECORDS];\n"
        "};\n"
        "uniform mat4 uModelView;\n"
        "uniform mat4 uProjection;\n"
        "uniform mat4 uTextureMatrix;\n"
        "uniform vec4 uGammaFit0;\n"
        "uniform vec4 uGammaFit1;\n"
        "uniform vec4 uInstanceLayout;\n"
        "uniform vec4 uInstanceParams[INSTANCE_CAP];\n"
        "uniform vec4 uInstanceColorBlend[INSTANCE_CAP];\n";
    static const char* body =
        "float gammaValue(int idx) {\n"
        "  float x = float(idx) * (1.0 / 1023.0);\n"
        "  float w = min(sqrt(max(x, 0.0)), uGammaFit0.y);\n"
        "  float p = w * (uGammaFit0.z + w * (uGammaFit0.w + w * uGammaFit1.x));\n"
        "  float dt = max(w - uGammaFit0.x, 0.0);\n"
        "  float q = dt * (uGammaFit1.y + dt * (uGammaFit1.z + dt * uGammaFit1.w));\n"
        "  return clamp(p + q, 0.0, 1.0);\n"
        "}\n"
        "void main() {\n"
        "  int iid = gl_InstanceID;\n"
        "  int base = iid * int(uInstanceLayout.x);\n"
        "  vec4 params = uInstanceParams[iid];\n"
        "  vec4 colorBlend = uInstanceColorBlend[iid];\n"
        "  int bi = int(aPositionBone);\n"
        "  StudioBoneData pb = uBoneData[base + bi];\n"
        "  vec3 p;\n"
        "  p.x = ((aPosition.y * pb.row0.y + aPosition.x * pb.row0.x) + aPosition.z * pb.row0.z) + pb.row0.w;\n"
        "  p.y = ((aPosition.y * pb.row1.y + aPosition.x * pb.row1.x) + aPosition.z * pb.row1.z) + pb.row1.w;\n"
        "  p.z = ((aPosition.y * pb.row2.y + aPosition.x * pb.row2.x) + aPosition.z * pb.row2.z) + pb.row2.w;\n"
        "  float light;\n"
        "  if (params.x < 0.0) {\n"
        "    light = params.z;\n"
        "  } else {\n"
        "    vec3 lv = uBoneData[base + int(aNormalBone)].light.xyz;\n"
        "    float lightCos = (aNormal.x * lv.x + aNormal.y * lv.y) + aNormal.z * lv.z;\n"
        "    lightCos = min(1.0, lightCos);\n"
        "    float illum = params.z + params.w;\n"
        "    float t = (lightCos + 0.49532413482666016) / 1.4953241348266602;\n"
        "    if (t > 0.0) illum = illum - params.w * t;\n"
        "    int gi = int(floor(clamp(illum, 0.0, 255.0) * 4.0));\n"
        "    if (gi < 0) gi = 0;\n"
        "    if (gi > 1023) gi = 1023;\n"
        "    light = gammaValue(gi);\n"
        "  }\n"
        "  vec4 c = vec4(colorBlend.rgb * light, colorBlend.a);\n"
        "  vec2 st = aRawST * vec2(abs(params.x), params.y);\n"
        "  gl_Position = uProjection * uModelView * vec4(p, 1.0);\n"
        "  gl_TexCoord[0] = uTextureMatrix * vec4(st, 0.0, 1.0);\n"
        "  gl_FrontColor = c;\n"
        "  gl_BackColor = c;\n"
        "}\n";

    const char* sourceParts[3] = {header, declarations, body};
    unsigned shader = 0;
    unsigned program = 0;
    __try
    {
        shader = g_createShader(GL_VERTEX_SHADER);
        if (!shader)
            return 0;
        g_shaderSource(shader, 3, sourceParts, nullptr);
        g_compileShader(shader);
        int compiled = 0;
        g_getShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (!compiled)
        {
            if (g_getShaderInfoLog)
            {
                char log[1024]{};
                int length = 0;
                g_getShaderInfoLog(
                    shader, static_cast<int>(sizeof(log) - 1),
                    &length, log);
                rendererlog::Line(
                    "studio renderer: instanced vertex shader compile failed: %s",
                    log);
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
        g_bindAttribLocation(program, kAttribPosition, "aPosition");
        g_bindAttribLocation(program, kAttribNormal, "aNormal");
        g_bindAttribLocation(program, kAttribRawST, "aRawST");
        g_bindAttribLocation(program, kAttribPositionBone, "aPositionBone");
        g_bindAttribLocation(program, kAttribNormalBone, "aNormalBone");
        g_linkProgram(program);
        int linked = 0;
        g_getProgramiv(program, GL_LINK_STATUS, &linked);
        g_deleteShader(shader);
        shader = 0;
        if (!linked)
        {
            if (g_getProgramInfoLog)
            {
                char log[1024]{};
                int length = 0;
                g_getProgramInfoLog(
                    program, static_cast<int>(sizeof(log) - 1),
                    &length, log);
                rendererlog::Line(
                    "studio renderer: instanced program link failed: %s",
                    log);
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

bool EnsureInstancedProgram()
{
    if (ReadInstancingMode() != 1 ||
        !g_useUniformBuffer || !g_uniformBufferReady ||
        !g_drawElementsInstanced || !g_uniformBlockBinding ||
        !g_getUniformBlockIndex || g_uniformBlockMaxSize <= 0)
        return false;
    if (g_instancedProgram)
        return g_instancedCapacity >= 2u && g_instancedBlockBytes > 0;
    // A failed compile/link is latched per GL context instead of being
    // retried by every deferred flush.
    if (g_instancedProgramFailed)
        return false;

    std::size_t blockBytes =
        (std::min)(static_cast<std::size_t>(g_uniformBlockMaxSize),
                   kMaxInstancedBlockBytes);
    blockBytes -= blockBytes % kBoneRecordBytes;
    if (blockBytes < kBoneBlockBytes ||
        blockBytes > kUniformBufferBytes)
    {
        g_instancedProgramFailed = true;
        return false;
    }
    const unsigned boneRecords =
        static_cast<unsigned>(blockBytes / kBoneRecordBytes);
    const unsigned capacity = kInstanceCap;

    const unsigned program =
        CompileInstancedProgram(boneRecords, capacity);
    if (!program)
    {
        g_instancedProgramFailed = true;
        return false;
    }

    unsigned block = GL_INVALID_INDEX;
    int gammaFit0 = -1;
    int gammaFit1 = -1;
    int layout = -1;
    int params = -1;
    int colorBlend = -1;
    int modelView = -1;
    int projection = -1;
    int textureMatrix = -1;
    bool ok = false;
    __try
    {
        block = g_getUniformBlockIndex(
            program, "StudioBoneConstants");
        gammaFit0 = g_getUniformLocation(program, "uGammaFit0");
        gammaFit1 = g_getUniformLocation(program, "uGammaFit1");
        layout = g_getUniformLocation(program, "uInstanceLayout");
        params = g_getUniformLocation(program, "uInstanceParams[0]");
        colorBlend =
            g_getUniformLocation(program, "uInstanceColorBlend[0]");
        modelView = g_getUniformLocation(program, "uModelView");
        projection = g_getUniformLocation(program, "uProjection");
        textureMatrix =
            g_getUniformLocation(program, "uTextureMatrix");
        if (block != GL_INVALID_INDEX &&
            gammaFit0 >= 0 && gammaFit1 >= 0 && layout >= 0 &&
            params >= 0 && colorBlend >= 0 &&
            modelView >= 0 && projection >= 0 &&
            textureMatrix >= 0)
        {
            g_uniformBlockBinding(
                program, block, g_boneBlockBinding);
            ok = true;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        ok = false;
    }
    if (!ok)
    {
        __try { g_deleteProgram(program); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        g_instancedProgramFailed = true;
        return false;
    }

    g_instancedProgram = program;
    g_instancedGammaFit0Location = gammaFit0;
    g_instancedGammaFit1Location = gammaFit1;
    g_instancedLayoutLocation = layout;
    g_instancedParamsLocation = params;
    g_instancedColorBlendLocation = colorBlend;
    g_instancedModelViewLocation = modelView;
    g_instancedProjectionLocation = projection;
    g_instancedTextureMatrixLocation = textureMatrix;
    g_instancedBoneBlockIndex = block;
    g_instancedCapacity = capacity;
    g_instancedBlockBytes = blockBytes;
    g_instancedGammaFitRevision = 0;
    rendererlog::Line(
        "studio renderer: instancing program ready capacity=%u records=%u block=%uKB",
        capacity, boneRecords,
        static_cast<unsigned>(blockBytes / 1024u));
    return true;
}

bool EnsureDirectProgram()
{
    if (!worldvbo::ContextGenerationReady())
        return false;
    const std::uint32_t generation = worldvbo::ContextGeneration();
    if (g_programGeneration != generation)
    {
        ResetProgramForContext(generation);
        g_stateValidationGeneration = 0;
        g_cachedMaxTextureUnits = 0;
    }
    if (g_program)
        return (g_useUniformBuffer ||
                (g_bonesLocation >= 0 &&
                 g_lightVectorsLocation >= 0)) &&
               g_gammaFit0Location >= 0 &&
               g_gammaFit1Location >= 0 &&
               g_paramsLocation >= 0 &&
               g_colorBlendLocation >= 0 &&
               (!g_useUniformBuffer ||
                (g_modelViewLocation >= 0 &&
                 g_projectionLocation >= 0 &&
                 g_textureMatrixLocation >= 0));

    if (!RefreshGlFunctions() || !RefreshShaderFunctions())
        return false;

    int maxUniformComponents = 0;
    int maxAttribs = 0;
    __try
    {
        g_getIntegerv(
            GL_MAX_VERTEX_UNIFORM_COMPONENTS, &maxUniformComponents);
        g_getIntegerv(GL_MAX_VERTEX_ATTRIBS, &maxAttribs);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }

    if (maxAttribs <= static_cast<int>(kAttribNormalBone))
        return false;

    bool useUniformBuffer = false;
    unsigned program = 0;
    if (UniformBufferApiReady() && maxUniformComponents >= 1280)
    {
        program = CompileDirectProgram(true);
        if (program && InitializeUniformBuffers(program))
        {
            useUniformBuffer = true;
        }
        else if (program)
        {
            __try { g_deleteProgram(program); }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
            program = 0;
        }
    }

    // Legacy fallback keeps the previous large default-uniform arrays.
    if (!program)
    {
        // 384 vec4 bone rows + 128 vec3 light vectors + 256 vec4 gamma rows
        // plus two vec4 parameter blocks = 2952 scalar components.
        if (maxUniformComponents < 3072)
            return false;
        program = CompileDirectProgram(false);
        useUniformBuffer = false;
    }
    if (!program)
        return false;

    int bones = -1;
    int lights = -1;
    int gammaFit0 = -1;
    int gammaFit1 = -1;
    int params = -1;
    int colorBlend = -1;
    int modelView = -1;
    int projection = -1;
    int textureMatrix = -1;
    __try
    {
        if (!useUniformBuffer)
        {
            bones = g_getUniformLocation(program, "uBones");
            lights = g_getUniformLocation(program, "uLightVectors");
        }
        gammaFit0 = g_getUniformLocation(program, "uGammaFit0");
        gammaFit1 = g_getUniformLocation(program, "uGammaFit1");
        params = g_getUniformLocation(program, "uParams");
        colorBlend = g_getUniformLocation(program, "uColorBlend");
        if (useUniformBuffer)
        {
            modelView = g_getUniformLocation(program, "uModelView");
            projection = g_getUniformLocation(program, "uProjection");
            textureMatrix =
                g_getUniformLocation(program, "uTextureMatrix");
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        bones = lights = gammaFit0 = gammaFit1 = params = colorBlend = -1;
        modelView = projection = textureMatrix = -1;
    }
    if ((!useUniformBuffer && (bones < 0 || lights < 0)) ||
        gammaFit0 < 0 || gammaFit1 < 0 ||
        params < 0 || colorBlend < 0 ||
        (useUniformBuffer &&
         (modelView < 0 || projection < 0 || textureMatrix < 0)))
    {
        __try { g_deleteProgram(program); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        return false;
    }

    g_program = program;
    g_useUniformBuffer = useUniformBuffer;
    g_bonesLocation = bones;
    g_lightVectorsLocation = lights;
    g_gammaFit0Location = gammaFit0;
    g_gammaFit1Location = gammaFit1;
    g_paramsLocation = params;
    g_colorBlendLocation = colorBlend;
    g_modelViewLocation = modelView;
    g_projectionLocation = projection;
    g_textureMatrixLocation = textureMatrix;
    g_gammaFitProgramRevision = 0;
    rendererlog::Line(
        "studio renderer: direct program ready "
        "(uniformComponents=%d attribs=%d constants=%s)",
        maxUniformComponents, maxAttribs,
        g_useUniformBuffer ? "UBO" : "legacy-uniforms");
    return true;
}

template <typename T>
const T* HeaderArray(const StudioHeaderRaw* header, int offset, int count);

struct DirectMaterial
{
    const StudioTextureRaw* texture = nullptr;
    int textureSlot = -1;
    unsigned flags = 0;
    float sScale = 0.0f;
    float tScale = 0.0f;
    float flatLight = 1.0f;
    // The mesh lighting scalar does not depend on the vertex normal: Gold's
    // FLATSHADE meshes and its non-player FULLBRIGHT shortcut. The shader
    // then uses flatLight, sampled from the stock R_StudioLighting.
    bool constantLight = false;
    bool directBind = false;
};

struct PreparedDirectMesh
{
    const RetainedSubmesh* mesh = nullptr;
    DirectMaterial material{};
};

std::vector<PreparedDirectMesh> g_preparedMeshes;

std::uint32_t SelectDrawFirstIndex(const RetainedSubmesh& mesh,
                                   const DirectMaterial& material)
{
    if (ReadMeshoptMode() == 1 &&
        (material.flags & (0x20u | 0x40u)) == 0u &&
        mesh.optimizedFirstIndex != mesh.firstIndex)
        return mesh.optimizedFirstIndex;
    return mesh.firstIndex;
}

unsigned RetainedIndexType(const RetainedCache& cache)
{
    return cache.index16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
}

std::size_t RetainedIndexBytes(const RetainedCache& cache)
{
    return cache.index16 ? sizeof(std::uint16_t) : sizeof(std::uint32_t);
}

struct ChromeCoord
{
    int s = 0;
    int t = 0;
    bool valid = false;
};

struct ChromeUv
{
    float s = 0.0f;
    float t = 0.0f;
};

std::vector<ChromeCoord> g_chromeCoords;
std::vector<ChromeUv> g_chromeUvScratch;

struct DirectMatrices
{
    float modelView[16]{};
    float projection[16]{};
    float texture[16]{};
};

struct DeferredMeshDraw
{
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    unsigned glId = 0;
    unsigned flags = 0;
    float sScale = 0.0f;
    float tScale = 0.0f;
    float flatLight = 1.0f;
    bool constantLight = false;
};

struct DeferredStudioCommand
{
    RetainedCache* cache = nullptr;
    const RetainedSubmodel* submodel = nullptr;
    std::size_t meshFirst = 0;
    std::size_t meshCount = 0;
    std::size_t boneFloatFirst = 0;
    std::size_t boneFloatCount = 0;
    std::size_t uniformOffset = 0;
    float colorBlend[4]{};
    float ambient = 0.0f;
    float shade = 0.0f;
    unsigned char mirror = 0;
    int rendererType = 1;
    std::size_t bucket = 0;
    // Number of client shadows deferred before this packet was recorded.
    // Packets of different segments are never reordered across a shadow.
    std::size_t segment = 0;
    // CHROME packets: this command's stock chrome UVs for the submodel's
    // vertex range, g_deferredChromeUvs[chromeUvFirst, + chromeUvCount),
    // staged by the flush into the chrome stream buffer. chromeStreamBase is
    // the attribute offset for vertex 0 of the retained cache, so the data
    // lands at chromeStreamBase + firstVertex * sizeof(ChromeUv).
    std::size_t chromeUvFirst = 0;
    std::size_t chromeUvCount = 0;
    std::size_t chromeFirstVertex = 0;
    std::size_t chromeStreamBase = 0;
    bool hasChrome = false;
    bool hasMasked = false;
};

// One flush draw group. count == 1 replays a command through the ordinary
// program, count >= 2 draws that many bucketed commands with one instanced
// call per mesh. Members are g_deferredOrder[orderFirst, orderFirst + count).
struct DeferredDrawGroup
{
    std::size_t orderFirst = 0;
    std::size_t count = 0;
    std::size_t uniformOffset = 0;
    std::size_t recordsPerInstance = 0;
};

// Fixed-function state a client shadow consumes without setting it itself.
// StudioRenderShadow sets blend, blend func, TMU0 env/binding, shade model,
// depth mask and color through the triangle API; everything below is
// inherited from the caller and is reproduced at replay.
struct ShadowEnv
{
    unsigned char cull = 0;
    unsigned char depthTest = 0;
    unsigned char alphaTest = 0;
    unsigned char fog = 0;
    unsigned char stencil = 0;
    unsigned char polygonOffset = 0;
    int cullMode = 0;
    int depthFunc = 0;
    int alphaFunc = 0;
    float alphaRef = 0.0f;
};

// One client StudioRenderShadow call taken out of the solid entity pass and
// replayed by the flush exactly between the packets it originally separated.
struct DeferredShadow
{
    RenderShadowFn provider = nullptr;
    int sprite = 0;
    float points[4][3]{};
    ShadowEnv env{};
};

std::vector<DeferredStudioCommand> g_deferredCommands;
std::vector<DeferredShadow> g_deferredShadows;
// Deferred CHROME coordinates of the pending run and the size of the stream
// image the flush uploads (padding included, see chromeStreamBase).
std::vector<ChromeUv> g_deferredChromeUvs;
std::size_t g_deferredChromeStreamBytes = 0;
bool g_deferredRunHasMasked = false;
// One stream buffer, orphaned and refilled by every flush that carries CHROME
// packets. Bound to GL_ARRAY_BUFFER only inside the flush.
constexpr std::size_t kChromeStreamMaxBytes = 4u << 20;
unsigned g_chromeStreamBuffer = 0;
std::uint32_t g_chromeStreamGeneration = 0;
std::vector<DeferredMeshDraw> g_deferredMeshes;
std::vector<float> g_deferredBoneFloats;
std::vector<std::size_t> g_deferredOrder;
std::vector<std::size_t> g_deferredBucketHeads;
std::vector<DeferredDrawGroup> g_deferredGroups;
DirectMatrices g_deferredRunMatrices{};
bool g_deferredRunMatricesValid = false;
const std::uint8_t* g_deferredLastEntity = nullptr;
// TMU0 texture Gold-visibly bound by the latest recorded packet. A flush
// rebinds it through Gold's own bind so GL and Gold's texture cache end the
// flush exactly where the last recorded DrawPoints left them.
unsigned g_deferredEntryTexture = 0;
bool g_deferredEntryTextureValid = false;
bool g_deferredFlushInProgress = false;
std::uint64_t g_deferredRecorded = 0;
std::uint64_t g_deferredFlushed = 0;
std::uint64_t g_deferredMappedUploads = 0;
std::uint64_t g_deferredSubDataUploads = 0;
std::uint64_t g_deferredColorUniformSkips = 0;
std::uint64_t g_deferredParamsUniformSkips = 0;
std::uint64_t g_instancedDrawCalls = 0;
std::uint64_t g_instancedEntities = 0;
std::uint64_t g_instancedSavedDraws = 0;

// Every reason a pending deferred run can be submitted. Barriers that are
// order-independent for opaque depth-written packets are counted as skips.
enum class FlushTrigger : unsigned
{
    PassBegin,
    PassEnd,
    Brush,
    Sprite,
    Shadow,
    Glow,
    DrawModel,
    DrawPlayer,
    Fallback,
    Immediate,
    MatrixSplit,
    Count
};

constexpr unsigned kFlushTriggerCount =
    static_cast<unsigned>(FlushTrigger::Count);
constexpr const char* kFlushTriggerNames[kFlushTriggerCount] = {
    "passBegin", "passEnd", "brush", "sprite", "shadow", "glow",
    "drawModel", "drawPlayer", "fallback", "immediate", "matrix"
};

// Profiling-only flush statistics, reset after every periodic report.
struct FlushProfileStats
{
    std::uint64_t siteFlushes[kFlushTriggerCount]{};
    std::uint64_t siteSkips[kFlushTriggerCount]{};
    std::uint64_t frames = 0;
    std::uint64_t flushes = 0;
    std::uint64_t records = 0;
    std::uint64_t maxRecords = 0;
    std::uint64_t groups = 0;
    std::uint64_t instancedGroups = 0;
    std::uint64_t matrixVerifies = 0;
    std::uint64_t matrixMismatches = 0;
    std::uint64_t shadowsDeferred = 0;
    std::uint64_t shadowsReplayed = 0;
    std::uint64_t shadowEnvFixes = 0;
    std::uint64_t shadowMatrixLoads = 0;
    std::uint64_t shadowReplayFailures = 0;
    std::uint64_t chromeRecords = 0;
    std::uint64_t maskedRecords = 0;
    std::uint64_t chromeStreamBytes = 0;
    long long ticks = 0;
    long long chargedTicks = 0;
    long long shadowReplayTicks = 0;
};

FlushProfileStats g_flushProfile{};
std::uint32_t g_matrixVerifySequence = 0;
bool g_matrixMismatchLogged = false;

// Matrices captured once per barrier-delimited stretch of the solid entity
// pass. Inside that stretch only client Studio code and Gold's world-space
// Studio kernel run between two DrawPoints calls. Neither touches the GL
// modelview, projection or texture matrix: Studio vertices are transformed by
// the CPU bone matrices, which already contain the entity origin and angles,
// and every stock entity draw relies on entering with the pass view matrix.
// Brush, sprite and shadow draws (R_RotateForEntity push/pop and foreign
// providers) are deferred-flush barriers and invalidate this snapshot.
DirectMatrices g_passMatrices{};
bool g_passMatricesValid = false;
std::uint32_t g_passMatricesGeneration = 0;

bool DeferredCommandsShareInstanceKey(
    const DeferredStudioCommand& first,
    const DeferredStudioCommand& second)
{
    // CHROME and MASKED packets carry per-packet UV sources or alpha-test
    // state the instanced program does not replay, they stay single.
    if (first.hasChrome || second.hasChrome ||
        first.hasMasked || second.hasMasked ||
        !first.cache || first.cache != second.cache ||
        !first.submodel || first.submodel != second.submodel ||
        first.rendererType != second.rendererType ||
        first.mirror != second.mirror ||
        first.segment != second.segment ||
        first.meshCount != second.meshCount ||
        first.boneFloatCount != second.boneFloatCount ||
        first.meshFirst > g_deferredMeshes.size() ||
        first.meshCount > g_deferredMeshes.size() - first.meshFirst ||
        second.meshFirst > g_deferredMeshes.size() ||
        second.meshCount > g_deferredMeshes.size() - second.meshFirst)
        return false;

    for (std::size_t meshIndex = 0;
         meshIndex < first.meshCount; ++meshIndex)
    {
        const DeferredMeshDraw& a =
            g_deferredMeshes[first.meshFirst + meshIndex];
        const DeferredMeshDraw& b =
            g_deferredMeshes[second.meshFirst + meshIndex];
        if (a.firstIndex != b.firstIndex ||
            a.indexCount != b.indexCount ||
            a.glId != b.glId ||
            a.flags != b.flags ||
            a.constantLight != b.constantLight ||
            a.sScale != b.sScale ||
            a.tScale != b.tScale)
            return false;
    }
    return true;
}

// Gold keeps its selected TMU in a private shadow that its own GL_Bind and
// texture-env code depend on (gl_state's Studio texenv filter reads the same
// value). Reading it avoids a synchronous GL_ACTIVE_TEXTURE query.
bool ReadGoldActiveTexture(int& texture)
{
    texture = static_cast<int>(GL_TEXTURE0);
    if (!g_hwBase)
        return false;
    int unit = -1;
    __try
    {
        unit = *reinterpret_cast<const int*>(
            g_hwBase + kGoldCurrentTmuRva);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
    if (unit < 0 || unit >= 32)
        return false;
    texture = static_cast<int>(GL_TEXTURE0) + unit;
    return true;
}

bool ReadActiveTexture(int& texture)
{
    if (ReadGoldActiveTexture(texture))
        return true;
    texture = static_cast<int>(GL_TEXTURE0);
    if (!g_getIntegerv)
        return false;
    __try
    {
        g_getIntegerv(GL_ACTIVE_TEXTURE, &texture);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        texture = static_cast<int>(GL_TEXTURE0);
        return false;
    }
}

bool CaptureDirectMatrices(DirectMatrices& out)
{
    if (!g_getFloatv || !g_getIntegerv || !g_activeTexture)
        return false;
    int previousActiveTexture = static_cast<int>(GL_TEXTURE0);
    if (!ReadActiveTexture(previousActiveTexture))
        return false;
    __try
    {
        g_getFloatv(GL_MODELVIEW_MATRIX, out.modelView);
        g_getFloatv(GL_PROJECTION_MATRIX, out.projection);
        if (previousActiveTexture != static_cast<int>(GL_TEXTURE0))
            g_activeTexture(GL_TEXTURE0);
        g_getFloatv(GL_TEXTURE_MATRIX, out.texture);
        if (previousActiveTexture != static_cast<int>(GL_TEXTURE0))
            g_activeTexture(static_cast<unsigned>(previousActiveTexture));
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        __try
        {
            if (previousActiveTexture != static_cast<int>(GL_TEXTURE0))
                g_activeTexture(static_cast<unsigned>(previousActiveTexture));
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        return false;
    }
}

bool PassSnapshotsAllowed()
{
    return g_solidEntityPassDepth > 0 &&
           g_deferredBarriersReady &&
           g_clientStudioShadowBarrierReady &&
           worldvbo::ContextGenerationReady();
}

void InvalidatePassSnapshots()
{
    g_passMatricesValid = false;
    studio_drawbatch::InvalidateClientArrayProof();
}

// Returns the pass matrix snapshot, capturing it on first use after a
// barrier. Outside a barrier-guarded solid pass this is a plain capture.
bool AcquireDirectMatrices(DirectMatrices& out)
{
    if (!PassSnapshotsAllowed())
        return CaptureDirectMatrices(out);
    const std::uint32_t generation = worldvbo::ContextGeneration();
    if (g_passMatricesValid &&
        g_passMatricesGeneration == generation)
    {
        out = g_passMatrices;
        return true;
    }
    if (!CaptureDirectMatrices(out))
    {
        g_passMatricesValid = false;
        return false;
    }
    g_passMatrices = out;
    g_passMatricesGeneration = generation;
    g_passMatricesValid = true;
    return true;
}

bool UploadDirectMatrices(const DirectMatrices& matrices)
{
    if (!g_useUniformBuffer)
        return true;
    if (!g_uniformMatrix4fv ||
        g_modelViewLocation < 0 ||
        g_projectionLocation < 0 ||
        g_textureMatrixLocation < 0)
        return false;
    __try
    {
        g_uniformMatrix4fv(
            g_modelViewLocation, 1, GL_FALSE_VALUE,
            matrices.modelView);
        g_uniformMatrix4fv(
            g_projectionLocation, 1, GL_FALSE_VALUE,
            matrices.projection);
        g_uniformMatrix4fv(
            g_textureMatrixLocation, 1, GL_FALSE_VALUE,
            matrices.texture);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// Gold's texture resolver (hw+0x98AE0) only leaves the descriptor's own glId
// bound for an entity with index > 0 when the name is neither "DM_Base.bmp"
// (_stricmp) nor a parsable remap name, which needs V_strnicmp(name,
// "Remap", 5) == 0 and a length of exactly 18 or 22. Any name that could pass
// either test keeps the per-mesh resolver call. This is a superset of the
// real remap names: case folding never changes the length or turns another
// first byte into D/d or R/r.
bool TextureMayRemap(const StudioTextureRaw& texture)
{
    const unsigned char first =
        static_cast<unsigned char>(texture.name[0]);
    std::size_t length = 0;
    while (length < sizeof(texture.name) && texture.name[length] != '\0')
        ++length;
    if (first == 'D' || first == 'd')
        return length == 11u;
    if (first == 'R' || first == 'r')
        return length == 18u || length == 22u;
    return false;
}

bool ResolveDirectMaterial(const StudioHeaderRaw* header,
                           int skinFamily,
                           const RetainedSubmesh& mesh,
                           DirectMaterial& out,
                           MaterialReject& reject)
{
    out = {};
    reject = MaterialReject::HeaderCounts;
    if (!header ||
        header->numtextures <= 0 || header->numtextures > 4096 ||
        header->textureindex <= 0 ||
        header->numskinref <= 0 || header->numskinref > 4096 ||
        header->numskinfamilies <= 0 || header->numskinfamilies > 256 ||
        skinFamily < 0 || skinFamily >= header->numskinfamilies ||
        mesh.skinref < 0 || mesh.skinref >= header->numskinref)
        return false;

    reject = MaterialReject::SkinTable;
    const std::size_t skinCount =
        static_cast<std::size_t>(header->numskinref) *
        static_cast<std::size_t>(header->numskinfamilies);
    if (skinCount > static_cast<std::size_t>(INT_MAX))
        return false;
    const auto* skinTable =
        HeaderArray<std::int16_t>(
            header, header->skinindex, static_cast<int>(skinCount));
    if (!skinTable)
        return false;

    reject = MaterialReject::TextureSlot;
    const std::size_t skinOffset =
        static_cast<std::size_t>(skinFamily) *
            static_cast<std::size_t>(header->numskinref) +
        static_cast<std::size_t>(mesh.skinref);
    const int textureSlot =
        static_cast<int>(skinTable[skinOffset]);
    if (textureSlot < 0 || textureSlot >= header->numtextures)
        return false;

    reject = MaterialReject::TextureArray;
    const StudioTextureRaw* textures =
        HeaderArray<StudioTextureRaw>(
            header, header->textureindex, header->numtextures);
    if (!textures)
        return false;
    const StudioTextureRaw& texture = textures[textureSlot];
    const unsigned flags = static_cast<unsigned>(texture.flags);
    // Retained Standard owns ordinary/FULLBRIGHT/FLATSHADE, CHROME, MASKED
    // and ADDITIVE. CHROME UVs are supplied by the exact stock CPU prepass.
    // Inside DrawPoints Gold reads only bits 0x01/0x04 (R_StudioLighting
    // hw+0x96B50), 0x02/0x20/0x40 and the sign bit (emitter selection and
    // material state, hw+0x9AE80). The sign bit selects the AltUV emitter
    // and stays Gold-owned. Every other bit (NOMIPS, ALPHA and the private
    // bits some compilers set, e.g. 0x800/0x1000) only affects texture upload
    // and does not change the draw.
    reject = MaterialReject::AltUv;
    if ((flags & 0x80000000u) != 0u)
        return false;
    reject = MaterialReject::TextureSize;
    if (texture.width <= 0 || texture.height <= 0)
        return false;

    out.texture = &texture;
    out.textureSlot = textureSlot;
    out.flags = flags;
    out.sScale = 1.0f / static_cast<float>(texture.width);
    out.tScale = 1.0f / static_cast<float>(texture.height);
    if ((flags & 0x02u) != 0u)
    {
        // Gold's CHROME emitter multiplies the ordinary inverse texture scale
        // by exactly 1/1024 before converting its integer chrome table to float.
        out.sScale *= (1.0f / 1024.0f);
        out.tScale *= (1.0f / 1024.0f);
    }
    // The former first-letter test sent every D*/R* name (receiver.bmp,
    // reticle.bmp, DE_slide1.bmp, ...) through the resolver, which also kept
    // those meshes out of the deferred recorder.
    out.directBind = !TextureMayRemap(texture);
    return true;
}

bool MaskedEntryStateReady()
{
    if (!g_getIntegerv || !g_isEnabled || !g_alphaFunc ||
        !g_depthMask || !g_enable || !g_disable)
        return false;
    int depthWrite = 0;
    __try
    {
        g_getIntegerv(GL_DEPTH_WRITEMASK, &depthWrite);
        return g_isEnabled(GL_ALPHA_TEST) == 0 && depthWrite != 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// Opaque retained draws (recorded or immediate) must start from Gold's
// opaque state. If blending is on, stock would blend this draw, so the exact
// answer is to let Gold draw it.
bool OpaqueEntryBlendOff()
{
    if (!g_isEnabled)
        return false;
    __try
    {
        return g_isEnabled(GL_BLEND) == 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool AdditiveEntryStateReady()
{
    if (!g_getIntegerv || !g_isEnabled || !g_depthMask ||
        !g_blendFunc || !g_shadeModel || !g_enable || !g_disable)
        return false;
    int depthWrite = 0;
    __try
    {
        g_getIntegerv(GL_DEPTH_WRITEMASK, &depthWrite);
        return g_isEnabled(GL_BLEND) == 0 &&
               depthWrite != 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// Barrier order-independence.
//
// Pending deferred packets are opaque: depth-tested, depth-written, blend
// forced off by the flush (MASKED meshes additionally alpha-tested, which
// only discards fragments). Any other draw that is itself opaque, depth-tested
// and depth-writing produces the same color and depth per pixel whether it
// reaches GL before or after them, so the barrier in front of it can be
// skipped. Only exact equal-depth ties between different entities could
// differ, the tolerance the instancing reorder already accepts. Blended,
// additive, non-depth-writing or non-depth-tested draws depend on what is
// already in the framebuffer and still flush first.
//
// The flush itself queries and restores every GL state it changes (program,
// VAO, TMU0 binding and env, shade model, blend, depth write, UBO targets),
// so a later flush leaves the stock draw sequence untouched. GL matrices are
// re-verified on the next record after any skipped barrier, see
// g_deferredRunMatricesVerify.
bool g_deferredRunMatricesVerify = false;

bool DepthOrderedOpaqueGlState()
{
    if (!g_isEnabled || !g_getIntegerv)
        return false;
    int depthWrite = 0;
    __try
    {
        if (g_isEnabled(GL_BLEND) != 0 ||
            g_isEnabled(GL_DEPTH_TEST) == 0)
            return false;
        g_getIntegerv(GL_DEPTH_WRITEMASK, &depthWrite);
        return depthWrite != 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool ReadCurrentEntityRender(int& modelType, int& renderMode, int& renderFx)
{
    modelType = -1;
    renderMode = -1;
    renderFx = -1;
    if (!g_hwBase)
        return false;
    __try
    {
        const std::uint8_t* const entity =
            *reinterpret_cast<const std::uint8_t* const*>(
                g_hwBase + kCurrentEntityRva);
        if (!entity)
            return false;
        const std::uint8_t* const model =
            *reinterpret_cast<const std::uint8_t* const*>(
                entity + 0xB94);
        if (!model)
            return false;
        modelType = *reinterpret_cast<const int*>(model + 0x44);
        renderMode = *reinterpret_cast<const int*>(entity + 0x2F8);
        renderFx = *reinterpret_cast<const int*>(entity + 0x304);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// Solid-pass brush entity (mod_brush, kRenderNormal, no fx). Stock
// R_DrawBrushModel draws opaque or alpha-tested surfaces with depth writes
// inside R_RotateForEntity's push/pop. Its decals blend, but they are
// coplanar with the brush's own depth-written surfaces: a packet in front
// covers brush and decal in either order, a packet behind is rejected by the
// brush depth in either order.
bool BrushDrawOrderIndependent()
{
    int modelType = -1;
    int renderMode = -1;
    int renderFx = -1;
    return g_solidEntityPassDepth > 0 &&
           ReadCurrentEntityRender(modelType, renderMode, renderFx) &&
           modelType == 0 && renderMode == 0 && renderFx == 0 &&
           DepthOrderedOpaqueGlState();
}

// Solid-pass sprite with kRenderNormal and the sprite blend cvar at 0: stock
// R_DrawSpriteModel disables GL_BLEND and enables GL_ALPHA_TEST before its
// quad and inherits depth test/write, which must already be on. The texture
// matrix variant stays a barrier.
bool SpriteDrawOrderIndependent()
{
    int modelType = -1;
    int renderMode = -1;
    int renderFx = -1;
    if (g_solidEntityPassDepth <= 0 ||
        !ReadCurrentEntityRender(modelType, renderMode, renderFx) ||
        modelType != 1 || renderMode != 0 || renderFx != 0)
        return false;
    __try
    {
        if (*reinterpret_cast<const int*>(
                g_hwBase + kSpriteTextureMatrixRva) != 0 ||
            *reinterpret_cast<const int*>(
                g_hwBase + kSpriteBlendRva) != 0)
            return false;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
    return DepthOrderedOpaqueGlState();
}

// Stock StudioDrawModel from the exact solid-pass callsite for a kRenderNormal
// studio entity without fx or forced face flags. Its framebuffer output is
// the hooked DrawPoints (each one re-evaluated by the fallback barrier) and
// the client shadow (its own barrier, flushed or deferred in order, so the
// shadow hook must be live). The glow-shell branch keeps its boundary flush
// in gl_state.
bool StockDrawModelOrderIndependent(int flags, void* caller)
{
    if (flags != 3 ||
        ReadMode() != 1 ||
        !g_deferredBarriersReady ||
        !g_clientStudioShadowBarrierReady ||
        g_solidEntityPassDepth <= 0 ||
        !g_hwBase ||
        caller != g_hwBase + kSolidDrawModelReturnRva ||
        !ReadExactRDrawEntitiesNormal())
        return false;
    int modelType = -1;
    int renderMode = -1;
    int renderFx = -1;
    if (!ReadCurrentEntityRender(modelType, renderMode, renderFx) ||
        modelType != 3 || renderMode != 0 || renderFx != 0)
        return false;
    __try
    {
        return *reinterpret_cast<const std::uint32_t*>(
                   g_hwBase + kForceFaceFlagsRva) == 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// True when no mesh of the current submodel resolves to an ADDITIVE texture
// for the entity's skin family, mirroring Gold's skin selection.
bool CurrentSubmodelWithoutAdditive(const DirectGlobals& state)
{
    const StudioHeaderRaw* textureHeader = ResolveTextureHeaderSafe();
    if (!textureHeader || !state.header || !state.currentModel ||
        state.skin < 0)
        return false;
    __try
    {
        if (textureHeader->id != 0x54534449 ||
            textureHeader->version != 10 ||
            textureHeader->numtextures <= 0 ||
            textureHeader->numtextures > 4096 ||
            textureHeader->numskinref <= 0 ||
            textureHeader->numskinref > 4096 ||
            textureHeader->numskinfamilies <= 0 ||
            textureHeader->numskinfamilies > 256)
            return false;
        const StudioModelRaw* const model = state.currentModel;
        if (model->nummesh < 0 || model->nummesh > 4096)
            return false;
        if (model->nummesh == 0)
            return true;
        const StudioMeshRaw* const meshes =
            HeaderArray<StudioMeshRaw>(
                state.header, model->meshindex, model->nummesh);
        const int skinRefs = textureHeader->numskinref;
        const std::int16_t* const skinTable =
            HeaderArray<std::int16_t>(
                textureHeader, textureHeader->skinindex,
                skinRefs * textureHeader->numskinfamilies);
        const StudioTextureRaw* const textures =
            HeaderArray<StudioTextureRaw>(
                textureHeader, textureHeader->textureindex,
                textureHeader->numtextures);
        if (!meshes || !skinTable || !textures)
            return false;
        const int family =
            state.skin < textureHeader->numskinfamilies ? state.skin : 0;
        for (int i = 0; i < model->nummesh; ++i)
        {
            const int skinref = meshes[i].skinref;
            if (skinref < 0 || skinref >= skinRefs)
                return false;
            const int slot = skinTable[family * skinRefs + skinref];
            if (slot < 0 || slot >= textureHeader->numtextures ||
                (static_cast<unsigned>(textures[slot].flags) & 0x20u) != 0u)
                return false;
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// Stock DrawPoints about to run for the current entity: kRenderNormal, no
// setup render mode, no fx (glow shell), no forced face flags, ordinary debug
// mode, opaque depth-tested and depth-written GL state at entry, and no
// ADDITIVE mesh (stock enables ONE/ONE blending without depth writes for
// those). MASKED meshes are alpha-tested with depth writes and CHROME meshes
// are opaque, both stay order-independent.
bool StockDrawPointsOrderIndependent()
{
    DirectGlobals state{};
    if (!ReadDirectGlobals(state) ||
        state.renderMode != 0 ||
        state.setupRenderMode != 0 ||
        state.renderFx != 0 ||
        state.forceFlags != 0 ||
        state.debugStudioMode >= 2)
        return false;
    return DepthOrderedOpaqueGlState() &&
           CurrentSubmodelWithoutAdditive(state);
}

bool BeginMaskedMaterial()
{
    if (!g_enable || !g_alphaFunc || !g_depthMask)
        return false;
    bool ok = true;
    __try { g_enable(GL_ALPHA_TEST); }
    __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    __try { g_alphaFunc(GL_GREATER, 0.5f); }
    __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    __try { g_depthMask(1); }
    __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    if (!ok)
    {
        // Keep a failed partial transition from leaking into Gold fallback.
        // This is also Gold's normal post-MASKED state.
        __try { g_alphaFunc(GL_NOTEQUAL, 0.0f); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        __try { g_disable(GL_ALPHA_TEST); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        __try { g_depthMask(1); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    return ok;
}

bool EndMaskedMaterial()
{
    if (!g_alphaFunc || !g_disable || !g_depthMask)
        return false;
    bool ok = true;
    __try { g_alphaFunc(GL_NOTEQUAL, 0.0f); }
    __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    __try { g_disable(GL_ALPHA_TEST); }
    __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    __try { g_depthMask(1); }
    __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    return ok;
}

bool BeginAdditiveMaterial()
{
    if (!g_getIntegerv || !g_blendFunc || !g_enable ||
        !g_disable || !g_depthMask || !g_shadeModel)
        return false;
    int entryShade = 0;
    __try { g_getIntegerv(GL_SHADE_MODEL, &entryShade); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    bool ok = true;
    __try { g_blendFunc(GL_ONE, GL_ONE); }
    __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    __try { g_enable(GL_BLEND); }
    __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    __try { g_depthMask(0); }
    __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    __try { g_shadeModel(GL_SMOOTH); }
    __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    if (!ok)
    {
        __try { g_disable(GL_BLEND); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        __try { g_depthMask(1); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        __try { g_shadeModel(static_cast<unsigned>(entryShade)); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    return ok;
}

bool EndAdditiveMaterial()
{
    if (!g_disable || !g_depthMask || !g_shadeModel)
        return false;
    bool ok = true;
    __try { g_disable(GL_BLEND); }
    __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    __try { g_depthMask(1); }
    __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    __try { g_shadeModel(GL_FLAT); }
    __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    return ok;
}

bool ReadAttribDisabled(unsigned index)
{
    if (!g_getVertexAttribiv)
        return false;
    int enabled = 1;
    __try
    {
        g_getVertexAttribiv(
            index, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &enabled);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
    return enabled == 0;
}

bool DirectGlStateReady(bool useVertexArray,
                        int& previousVertexArray,
                        int& previousArrayBuffer,
                        int& previousElementBuffer,
                        int& maxTextureUnits)
{
    previousVertexArray = 0;
    previousArrayBuffer = 0;
    previousElementBuffer = 0;
    maxTextureUnits = 0;
    if (!g_getIntegerv || !g_getVertexAttribiv)
        return false;

    if (!worldvbo::ContextGenerationReady())
        return false;
    const std::uint32_t generation =
        worldvbo::ContextGeneration();

    int currentProgram = -1;
    int activeTexture = -1;
    int textureEnvMode = -1;
    if (!ReadActiveTexture(activeTexture))
        return false;
    __try
    {
        g_getIntegerv(GL_CURRENT_PROGRAM, &currentProgram);
        if (!g_getTexEnviv)
            return false;
        g_getTexEnviv(
            GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE,
            &textureEnvMode);
        if (useVertexArray)
        {
            g_getIntegerv(
                GL_VERTEX_ARRAY_BINDING, &previousVertexArray);
        }
        else
        {
        g_getIntegerv(
            GL_ARRAY_BUFFER_BINDING, &previousArrayBuffer);
        g_getIntegerv(
            GL_ELEMENT_ARRAY_BUFFER_BINDING, &previousElementBuffer);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
    // Gold's ordinary Studio path enters on TMU0 with MODULATE.  The retained
    // shader must not silently inherit a world/effect texture stage left on a
    // different unit or env mode, fail open to stock instead.
    if (currentProgram != 0 ||
        activeTexture != static_cast<int>(GL_TEXTURE0) ||
        textureEnvMode != static_cast<int>(GL_MODULATE))
        return false;

    if (useVertexArray)
        return g_bindVertexArray != nullptr;

    if (g_stateValidationGeneration != generation)
    {
        int units = 0;
        __try
        {
            g_getIntegerv(GL_MAX_TEXTURE_UNITS, &units);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        if (units <= 0 || units > 32 ||
            !ReadAttribDisabled(kAttribPosition) ||
            !ReadAttribDisabled(kAttribNormal) ||
            !ReadAttribDisabled(kAttribRawST) ||
            !ReadAttribDisabled(kAttribPositionBone) ||
            !ReadAttribDisabled(kAttribNormalBone))
            return false;
        g_cachedMaxTextureUnits = units;
        g_stateValidationGeneration = generation;
    }
    maxTextureUnits = g_cachedMaxTextureUnits;
    return maxTextureUnits > 0;
}

bool BeginDirectArrays(const RetainedCache& cache,
                       int maxTextureUnits)
{
    if (!g_pushClientAttrib || !g_popClientAttrib ||
        !g_disableClientState || !g_clientActiveTexture ||
        !g_bindBuffer || !g_vertexAttribPointer ||
        !g_enableVertexAttribArray ||
        !cache.vertexBuffer || !cache.indexBuffer)
        return false;

    bool pushed = false;
    __try
    {
        g_pushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
        pushed = true;
        g_disableClientState(GL_VERTEX_ARRAY);
        g_disableClientState(GL_NORMAL_ARRAY);
        g_disableClientState(GL_COLOR_ARRAY);
        g_disableClientState(GL_INDEX_ARRAY);
        g_disableClientState(GL_EDGE_FLAG_ARRAY);
        g_disableClientState(GL_FOG_COORDINATE_ARRAY);
        g_disableClientState(GL_SECONDARY_COLOR_ARRAY);
        for (int unit = 0; unit < maxTextureUnits; ++unit)
        {
            g_clientActiveTexture(GL_TEXTURE0 + static_cast<unsigned>(unit));
            g_disableClientState(GL_TEXTURE_COORD_ARRAY);
        }
        g_clientActiveTexture(GL_TEXTURE0);

        g_bindBuffer(GL_ARRAY_BUFFER, cache.vertexBuffer);
        g_bindBuffer(GL_ELEMENT_ARRAY_BUFFER, cache.indexBuffer);
        const std::size_t positionBoneOffset =
            g_useUniformBuffer
                ? offsetof(RetainedVertex, palettePositionBone)
                : offsetof(RetainedVertex, positionBone);
        const std::size_t normalBoneOffset =
            g_useUniformBuffer
                ? offsetof(RetainedVertex, paletteNormalBone)
                : offsetof(RetainedVertex, normalBone);

        g_vertexAttribPointer(
            kAttribPosition, 3, GL_FLOAT, GL_FALSE_VALUE,
            sizeof(RetainedVertex),
            reinterpret_cast<const void*>(
                offsetof(RetainedVertex, xyz)));
        g_vertexAttribPointer(
            kAttribNormal, 3, GL_FLOAT, GL_FALSE_VALUE,
            sizeof(RetainedVertex),
            reinterpret_cast<const void*>(
                offsetof(RetainedVertex, normal)));
        g_vertexAttribPointer(
            kAttribRawST, 2, GL_SHORT, GL_FALSE_VALUE,
            sizeof(RetainedVertex),
            reinterpret_cast<const void*>(
                offsetof(RetainedVertex, rawST)));
        g_vertexAttribPointer(
            kAttribPositionBone, 1, GL_UNSIGNED_BYTE, GL_FALSE_VALUE,
            sizeof(RetainedVertex),
            reinterpret_cast<const void*>(
                positionBoneOffset));
        g_vertexAttribPointer(
            kAttribNormalBone, 1, GL_UNSIGNED_BYTE, GL_FALSE_VALUE,
            sizeof(RetainedVertex),
            reinterpret_cast<const void*>(
                normalBoneOffset));

        g_enableVertexAttribArray(kAttribPosition);
        g_enableVertexAttribArray(kAttribNormal);
        g_enableVertexAttribArray(kAttribRawST);
        g_enableVertexAttribArray(kAttribPositionBone);
        g_enableVertexAttribArray(kAttribNormalBone);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        if (pushed && g_popClientAttrib)
        {
            __try { g_popClientAttrib(); }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        return false;
    }
}

void EndDirectArrays(int previousArrayBuffer,
                     int previousElementBuffer)
{
    if (g_disableVertexAttribArray)
    {
        __try
        {
            g_disableVertexAttribArray(kAttribPosition);
            g_disableVertexAttribArray(kAttribNormal);
            g_disableVertexAttribArray(kAttribRawST);
            g_disableVertexAttribArray(kAttribPositionBone);
            g_disableVertexAttribArray(kAttribNormalBone);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    if (g_popClientAttrib)
    {
        __try { g_popClientAttrib(); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    if (g_bindBuffer)
    {
        __try
        {
            g_bindBuffer(
                GL_ARRAY_BUFFER,
                static_cast<unsigned>(previousArrayBuffer));
            g_bindBuffer(
                GL_ELEMENT_ARRAY_BUFFER,
                static_cast<unsigned>(previousElementBuffer));
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
}

bool ConfigureRawStAttribute(const RetainedCache& cache, bool chrome)
{
    if (!g_getIntegerv || !g_bindBuffer || !g_vertexAttribPointer ||
        !cache.vertexBuffer || (chrome && !cache.chromeUvBuffer))
        return false;

    int previousArray = 0;
    bool ok = false;
    __try
    {
        g_getIntegerv(GL_ARRAY_BUFFER_BINDING, &previousArray);
        if (chrome)
        {
            g_bindBuffer(GL_ARRAY_BUFFER, cache.chromeUvBuffer);
            g_vertexAttribPointer(
                kAttribRawST, 2, GL_FLOAT, GL_FALSE_VALUE,
                sizeof(ChromeUv), nullptr);
        }
        else
        {
            g_bindBuffer(GL_ARRAY_BUFFER, cache.vertexBuffer);
            g_vertexAttribPointer(
                kAttribRawST, 2, GL_SHORT, GL_FALSE_VALUE,
                sizeof(RetainedVertex),
                reinterpret_cast<const void*>(
                    offsetof(RetainedVertex, rawST)));
        }
        ok = true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        ok = false;
    }

    __try
    {
        g_bindBuffer(
            GL_ARRAY_BUFFER,
            static_cast<unsigned>(previousArray));
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        ok = false;
    }

    if (!ok && chrome)
    {
        // A partially applied CHROME pointer must not escape into fallback or
        // the next retained mesh. Best-effort restore the static raw-ST source.
        __try
        {
            g_bindBuffer(GL_ARRAY_BUFFER, cache.vertexBuffer);
            g_vertexAttribPointer(
                kAttribRawST, 2, GL_SHORT, GL_FALSE_VALUE,
                sizeof(RetainedVertex),
                reinterpret_cast<const void*>(
                    offsetof(RetainedVertex, rawST)));
            g_bindBuffer(
                GL_ARRAY_BUFFER,
                static_cast<unsigned>(previousArray));
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    return ok;
}

void InvokeStockLighting(float* output,
                         int bone,
                         int flags,
                         const float* normal)
{
    void* target = g_studioLighting;
    __asm
    {
        push normal
        push flags
        mov edx, bone
        mov ecx, output
        call target
        add esp, 8
    }
}

bool SampleStockLighting(float& output,
                         int bone,
                         int flags,
                         const float* normal)
{
    if (!g_studioLighting || !normal)
        return false;
    output = 1.0f;
    __try
    {
        InvokeStockLighting(&output, bone, flags, normal);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        output = 1.0f;
        return false;
    }
}

void InvokeStockChrome(int* output,
                       int bone,
                       const float* normal)
{
    void* target = g_studioChrome;
    __asm
    {
        push normal
        mov edx, bone
        mov ecx, output
        call target
        add esp, 4
    }
}

bool SampleStockChrome(int output[2],
                       int bone,
                       const float* normal)
{
    if (!g_studioChrome || !output || !normal)
        return false;
    output[0] = 0;
    output[1] = 0;
    __try
    {
        InvokeStockChrome(output, bone, normal);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        output[0] = 0;
        output[1] = 0;
        return false;
    }
}

bool SpanValid(const StudioHeaderRaw* header, int offset, std::size_t bytes)
{
    if (!header || offset < 0 ||
        header->length < static_cast<int>(sizeof(StudioHeaderRaw)))
        return false;
    const std::size_t length = static_cast<std::size_t>(header->length);
    const std::size_t start = static_cast<std::size_t>(offset);
    return start <= length && bytes <= length - start;
}

template <typename T>
const T* HeaderArray(const StudioHeaderRaw* header, int offset, int count)
{
    if (count < 0)
        return nullptr;
    const std::size_t n = static_cast<std::size_t>(count);
    if (n > SIZE_MAX / sizeof(T) ||
        !SpanValid(header, offset, n * sizeof(T)))
        return nullptr;
    return reinterpret_cast<const T*>(
        reinterpret_cast<const std::uint8_t*>(header) + offset);
}

bool AppendTriangle(RetainedCache& cache,
                    std::uint32_t a, std::uint32_t b, std::uint32_t c)
{
    try
    {
        cache.indices.push_back(a);
        cache.indices.push_back(b);
        cache.indices.push_back(c);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

enum class BuildSubmodelFailure : unsigned
{
    None,
    Counts,
    Arrays,
    Reserve,
    StampBone,
    CornerIndex,
    CornerBone,
    Palette,
    VertexStore,
    MeshHeader,
    TriBounds,
    TriCount,
    PrimitiveAlloc,
    CornerResolve,
    AppendTriangle,
    EmptyMesh,
    MeshStore,
    EmptyOutput
};

BuildSubmodelFailure g_lastBuildSubmodelFailure = BuildSubmodelFailure::None;

bool BuildSubmodelFailureIsResourceFailure(BuildSubmodelFailure reason)
{
    // These failures mean the retained cache itself could not grow. Continuing
    // to build other body models under the same allocation pressure would not
    // be a reliable fail-open strategy, so abort the whole cache in that case.
    switch (reason)
    {
    case BuildSubmodelFailure::Reserve:
    case BuildSubmodelFailure::VertexStore:
    case BuildSubmodelFailure::PrimitiveAlloc:
    case BuildSubmodelFailure::AppendTriangle:
    case BuildSubmodelFailure::MeshStore:
        return true;
    default:
        return false;
    }
}

bool BuildSubmodel(const StudioHeaderRaw* header,
                   const StudioModelRaw* model,
                   RetainedCache& cache,
                   RetainedSubmodel& output)
{
    g_lastBuildSubmodelFailure = BuildSubmodelFailure::None;
    auto fail = [](BuildSubmodelFailure reason) -> bool
    {
        g_lastBuildSubmodelFailure = reason;
        return false;
    };
    if (!header || !model)
        return fail(BuildSubmodelFailure::Counts);

    output.source = model;
    output.firstVertex = cache.vertices.size();

    // Valid Gold Studio bodygroups commonly carry a literal "blank" model
    // with no meshes/verts/normals to represent "draw nothing" for that slot.
    // Keep it in submodelLookup instead of rejecting the entire MDL cache.
    if (model->nummesh == 0 && model->numverts == 0 && model->numnorms == 0)
    {
        output.vertexCount = 0;
        return true;
    }

    if (model->nummesh <= 0 || model->nummesh > 4096 ||
        model->numverts <= 0 || model->numverts > 65535 ||
        model->numnorms <= 0 || model->numnorms > 65535)
        return fail(BuildSubmodelFailure::Counts);

    const StudioMeshRaw* meshes =
        HeaderArray<StudioMeshRaw>(header, model->meshindex, model->nummesh);
    const float* positions =
        HeaderArray<float>(header, model->vertindex, model->numverts * 3);
    const float* normals =
        HeaderArray<float>(header, model->normindex, model->numnorms * 3);
    const std::uint8_t* vertexBones =
        HeaderArray<std::uint8_t>(header, model->vertinfoindex, model->numverts);
    const std::uint8_t* normalBones =
        HeaderArray<std::uint8_t>(header, model->norminfoindex, model->numnorms);
    if (!meshes || !positions || !normals || !vertexBones || !normalBones)
        return fail(BuildSubmodelFailure::Arrays);

    std::unordered_map<CornerKey, std::uint32_t, CornerHash> unique;
    std::array<std::uint8_t, 128> paletteMap{};
    paletteMap.fill(0xFFu);
    try
    {
        unique.reserve(static_cast<std::size_t>(model->numverts) * 2u);
        output.meshes.reserve(static_cast<std::size_t>(model->nummesh));
    }
    catch (...)
    {
        return fail(BuildSubmodelFailure::Reserve);
    }

    // Gold's inner DrawPoints walks every model vertex up to its 0x4000 hard
    // limit before triangle emission and stamps the owning bone's light age,
    // including vertices that no triangle command references. Keep that exact
    // side effect separate from the retained-corner geometry/palette mask.
    const int stampVertexCount =
        model->numverts < 0x4000 ? model->numverts : 0x4000;
    for (int vertex = 0; vertex < stampVertexCount; ++vertex)
    {
        const unsigned bone = vertexBones[vertex];
        if (bone >= 128u || bone >= static_cast<unsigned>(header->numbones))
            return fail(BuildSubmodelFailure::StampBone);
        output.usedPositionBones[bone >> 6] |=
            (1ull << (bone & 63u));
    }

    auto resolvePaletteBone = [&](unsigned bone,
                                  std::uint8_t& localBone) -> bool
    {
        if (bone >= 128u || bone >= static_cast<unsigned>(header->numbones))
            return false;
        std::uint8_t mapped = paletteMap[bone];
        if (mapped == 0xFFu)
        {
            if (output.bonePaletteCount >= 128u)
                return false;
            mapped = output.bonePaletteCount++;
            output.bonePalette[mapped] = static_cast<std::uint8_t>(bone);
            paletteMap[bone] = mapped;
        }
        localBone = mapped;
        return true;
    };

    auto resolveCorner = [&](const std::int16_t* record,
                             std::uint32_t& slot) -> bool
    {
        const unsigned vertex = static_cast<std::uint16_t>(record[0]);
        const unsigned normal = static_cast<std::uint16_t>(record[1]);
        if (vertex >= static_cast<unsigned>(model->numverts) ||
            normal >= static_cast<unsigned>(model->numnorms) ||
            vertex >= 0x4000u || normal >= 0x4000u)
            return fail(BuildSubmodelFailure::CornerIndex);
        if (vertexBones[vertex] >= header->numbones ||
            normalBones[normal] >= header->numbones)
            return fail(BuildSubmodelFailure::CornerBone);

        const CornerKey key{
            static_cast<std::uint16_t>(vertex),
            static_cast<std::uint16_t>(normal),
            record[2], record[3]
        };
        const auto found = unique.find(key);
        if (found != unique.end())
        {
            slot = found->second;
            return true;
        }

        if (cache.vertices.size() >= static_cast<std::size_t>(UINT32_MAX))
            return fail(BuildSubmodelFailure::VertexStore);
        RetainedVertex value{};
        std::memcpy(value.xyz,
                    positions + static_cast<std::size_t>(vertex) * 3u,
                    sizeof(value.xyz));
        std::memcpy(value.normal,
                    normals + static_cast<std::size_t>(normal) * 3u,
                    sizeof(value.normal));
        value.rawST[0] = record[2];
        value.rawST[1] = record[3];
        value.positionBone = vertexBones[vertex];
        value.normalBone = normalBones[normal];
        if (!resolvePaletteBone(
                value.positionBone, value.palettePositionBone) ||
            !resolvePaletteBone(
                value.normalBone, value.paletteNormalBone))
            return fail(BuildSubmodelFailure::Palette);

        slot = static_cast<std::uint32_t>(cache.vertices.size());
        try
        {
            cache.vertices.push_back(value);
            cache.normalIndices.push_back(
                static_cast<std::uint16_t>(normal));
            unique.emplace(key, slot);
        }
        catch (...)
        {
            return fail(BuildSubmodelFailure::VertexStore);
        }
        return true;
    };

    const auto* headerBytes = reinterpret_cast<const std::uint8_t*>(header);
    const auto* headerEnd = headerBytes + header->length;
    for (int meshIndex = 0; meshIndex < model->nummesh; ++meshIndex)
    {
        const StudioMeshRaw& mesh = meshes[meshIndex];
        const int skinref =
            static_cast<int>(static_cast<std::int16_t>(mesh.skinref & 0xFFFF));
        if (mesh.triindex < 0 || skinref < 0 || mesh.numtris < 0 ||
            !SpanValid(header, mesh.triindex, sizeof(std::int16_t)))
            return fail(BuildSubmodelFailure::MeshHeader);

        const std::size_t firstIndex = cache.indices.size();
        std::uint32_t lastVertex = 0;
        bool haveLastVertex = false;
        const std::uint8_t* cursor = headerBytes + mesh.triindex;
        for (;;)
        {
            if (cursor + sizeof(std::int16_t) > headerEnd)
                return fail(BuildSubmodelFailure::TriBounds);
            std::int16_t command = 0;
            std::memcpy(&command, cursor, sizeof(command));
            cursor += sizeof(command);
            if (command == 0)
                break;

            const int count = command < 0 ? -static_cast<int>(command)
                                          : static_cast<int>(command);
            if (count < 3 || count > 32767)
                return fail(BuildSubmodelFailure::TriCount);
            const std::size_t commandBytes =
                static_cast<std::size_t>(count) * 4u * sizeof(std::int16_t);
            if (cursor > headerEnd ||
                commandBytes > static_cast<std::size_t>(headerEnd - cursor))
                return fail(BuildSubmodelFailure::TriBounds);

            const auto* records =
                reinterpret_cast<const std::int16_t*>(cursor);
            std::vector<std::uint32_t> primitive;
            try { primitive.resize(static_cast<std::size_t>(count)); }
            catch (...) { return fail(BuildSubmodelFailure::PrimitiveAlloc); }
            for (int i = 0; i < count; ++i)
            {
                if (!resolveCorner(
                        records + static_cast<std::size_t>(i) * 4u,
                        primitive[static_cast<std::size_t>(i)]))
                    return fail(g_lastBuildSubmodelFailure == BuildSubmodelFailure::None
                                    ? BuildSubmodelFailure::CornerResolve
                                    : g_lastBuildSubmodelFailure);
            }
            lastVertex = primitive.back();
            haveLastVertex = true;

            if (command < 0)
            {
                // Gold negative commands are triangle fans.
                for (int i = 1; i + 1 < count; ++i)
                {
                    if (!AppendTriangle(
                            cache, primitive[0],
                            primitive[static_cast<std::size_t>(i)],
                            primitive[static_cast<std::size_t>(i + 1)]))
                        return fail(BuildSubmodelFailure::AppendTriangle);
                }
            }
            else
            {
                // Preserve Gold strip winding exactly.
                for (int i = 0; i + 2 < count; ++i)
                {
                    const std::uint32_t a =
                        primitive[static_cast<std::size_t>(i)];
                    const std::uint32_t b =
                        primitive[static_cast<std::size_t>(i + 1)];
                    const std::uint32_t c =
                        primitive[static_cast<std::size_t>(i + 2)];
                    const bool ok = (i & 1) == 0
                        ? AppendTriangle(cache, a, b, c)
                        : AppendTriangle(cache, b, a, c);
                    if (!ok)
                        return fail(BuildSubmodelFailure::AppendTriangle);
                }
            }
            cursor += commandBytes;
        }

        const std::size_t indexCount = cache.indices.size() - firstIndex;
        if (!haveLastVertex || firstIndex > UINT32_MAX || indexCount > UINT32_MAX)
            return fail(BuildSubmodelFailure::EmptyMesh);
        try
        {
            output.meshes.push_back({
                static_cast<std::uint32_t>(firstIndex),
                static_cast<std::uint32_t>(firstIndex),
                static_cast<std::uint32_t>(indexCount),
                skinref,
                mesh.numtris,
                lastVertex
            });
        }
        catch (...)
        {
            return fail(BuildSubmodelFailure::MeshStore);
        }
    }
    if (output.meshes.empty() || output.bonePaletteCount == 0)
        return fail(BuildSubmodelFailure::EmptyOutput);
    output.vertexCount = cache.vertices.size() - output.firstVertex;
    return true;
}

bool BuildMeshoptRanges(RetainedCache& cache)
{
    const std::size_t originalIndexCount = cache.indices.size();
    cache.meshoptRanges = 0;
    cache.meshoptVerticesBefore = 0;
    cache.meshoptVerticesAfter = 0;

    for (RetainedSubmodel& submodel : cache.submodels)
        for (RetainedSubmesh& mesh : submodel.meshes)
            mesh.optimizedFirstIndex = mesh.firstIndex;

    if (originalIndexCount == 0 || cache.vertices.empty() ||
        originalIndexCount > static_cast<std::size_t>(UINT32_MAX) ||
        originalIndexCount > SIZE_MAX / 2u)
        return false;

    try
    {
        // The high mirror contains one same-sized range for EVERY submesh.
        // Improved ranges use meshoptimizer order, neutral/regressive ranges
        // copy Gold order verbatim. This preserves high-mirror contiguity, so
        // existing adjacent-material coalescing cannot be lost merely because
        // one neighboring mesh had no cache improvement.
        cache.indices.reserve(originalIndexCount * 2u);
        std::vector<std::uint32_t> optimized;

        for (RetainedSubmodel& submodel : cache.submodels)
        {
            for (RetainedSubmesh& mesh : submodel.meshes)
            {
                const std::size_t first =
                    static_cast<std::size_t>(mesh.firstIndex);
                const std::size_t count =
                    static_cast<std::size_t>(mesh.indexCount);
                if (count < 3u || (count % 3u) != 0u ||
                    first > originalIndexCount ||
                    count > originalIndexCount - first)
                    continue;

                optimized.resize(count);
                const std::uint32_t* source =
                    cache.indices.data() + first;
                meshopt_optimizeVertexCache(
                    optimized.data(), source, count,
                    cache.vertices.size());

                // Keep only ranges that improve the conservative FIFO cache
                // model. The analyzer is not a hardware oracle, but rejecting
                // neutral/regressive results makes this optimization fail open.
                const meshopt_VertexCacheStatistics before =
                    meshopt_analyzeVertexCache(
                        source, count, cache.vertices.size(),
                        16, 32, 32);
                const meshopt_VertexCacheStatistics after =
                    meshopt_analyzeVertexCache(
                        optimized.data(), count, cache.vertices.size(),
                        16, 32, 32);
                if (cache.indices.size() >
                    static_cast<std::size_t>(UINT32_MAX) - count)
                    continue;

                const std::uint32_t optimizedFirst =
                    static_cast<std::uint32_t>(cache.indices.size());
                if (after.vertices_transformed <
                    before.vertices_transformed)
                {
                    cache.indices.insert(
                        cache.indices.end(),
                        optimized.begin(), optimized.end());
                    ++cache.meshoptRanges;
                    cache.meshoptVerticesBefore +=
                        before.vertices_transformed;
                    cache.meshoptVerticesAfter +=
                        after.vertices_transformed;
                }
                else
                {
                    cache.indices.insert(
                        cache.indices.end(),
                        source, source + count);
                }
                mesh.optimizedFirstIndex = optimizedFirst;
            }
        }
        return true;
    }
    catch (...)
    {
        cache.indices.resize(originalIndexCount);
        cache.meshoptRanges = 0;
        cache.meshoptVerticesBefore = 0;
        cache.meshoptVerticesAfter = 0;
        for (RetainedSubmodel& submodel : cache.submodels)
            for (RetainedSubmesh& mesh : submodel.meshes)
                mesh.optimizedFirstIndex = mesh.firstIndex;
        return false;
    }
}

bool BuildCache(const StudioHeaderRaw* header, RetainedCache& cache)
{
    if (!header || header->id != 0x54534449 || header->version != 10 ||
        header->length < static_cast<int>(sizeof(*header)) ||
        header->length > 128 * 1024 * 1024 ||
        header->numbones <= 0 || header->numbones > 128 ||
        header->numbodyparts <= 0 || header->numbodyparts > 4096)
        return false;

    const void* ownerModel = nullptr;
    int ownerIndex = -1;
    std::uint32_t ownerGeneration = 0;
    if (!studio_drawbatch::QueryModelIdentity(
            header, &ownerModel, &ownerIndex, &ownerGeneration))
        return false;

    const StudioBodypartRaw* bodyparts =
        HeaderArray<StudioBodypartRaw>(
            header, header->bodypartindex, header->numbodyparts);
    if (!bodyparts)
        return false;

    cache = {};
    cache.header = reinterpret_cast<const std::uint8_t*>(header);
    cache.headerLength = static_cast<std::uint32_t>(header->length);
    cache.ownerModel = ownerModel;
    cache.ownerIndex = ownerIndex;
    cache.ownerGeneration = ownerGeneration;

    try
    {
        for (int body = 0; body < header->numbodyparts; ++body)
        {
            const StudioBodypartRaw& part = bodyparts[body];
            if (part.nummodels <= 0 || part.nummodels > 4096)
                return false;
            const StudioModelRaw* models =
                HeaderArray<StudioModelRaw>(
                    header, part.modelindex, part.nummodels);
            if (!models)
                return false;
            for (int model = 0; model < part.nummodels; ++model)
            {
                const std::size_t vertexCheckpoint = cache.vertices.size();
                const std::size_t normalIndexCheckpoint =
                    cache.normalIndices.size();
                const std::size_t indexCheckpoint = cache.indices.size();
                RetainedSubmodel retained{};
                if (!BuildSubmodel(header, &models[model], cache, retained))
                {
                    // BuildSubmodel appends into model-wide shared geometry.
                    // Roll back the failed body model so a geometry-specific
                    // retained limitation cannot poison otherwise compatible
                    // alternates in the same MDL. A lookup miss for this exact
                    // model will later fail open to Gold's stock DrawPoints.
                    cache.vertices.resize(vertexCheckpoint);
                    cache.normalIndices.resize(normalIndexCheckpoint);
                    cache.indices.resize(indexCheckpoint);
                    if (prof::Active() && g_buildFailureLogCount < 128)
                    {
                        ++g_buildFailureLogCount;
                        rendererlog::Line(
                            "studio renderer: BuildSubmodel skip header='%.64s' model='%.64s' reason=%u meshes=%d verts=%d norms=%d bones=%d",
                            header->name,
                            models[model].name,
                            static_cast<unsigned>(g_lastBuildSubmodelFailure),
                            models[model].nummesh,
                            models[model].numverts,
                            models[model].numnorms,
                            header->numbones);
                    }
                    if (BuildSubmodelFailureIsResourceFailure(
                            g_lastBuildSubmodelFailure))
                        return false;
                    continue;
                }
                const std::size_t slot = cache.submodels.size();
                cache.submodels.push_back(std::move(retained));
                cache.submodelLookup.emplace(&models[model], slot);
            }
        }
    }
    catch (...)
    {
        return false;
    }

    if (cache.vertices.empty() ||
        cache.normalIndices.size() != cache.vertices.size() ||
        cache.indices.empty() ||
        cache.submodels.empty())
        return false;

    // Mesh optimization is an optional acceleration image layered on top of
    // the authoritative Gold-order geometry. Allocation/analyzer failure never
    // invalidates an otherwise compatible retained cache.
    (void)BuildMeshoptRanges(cache);
    cache.index16 =
        cache.vertices.size() <=
            static_cast<std::size_t>(
                (std::numeric_limits<std::uint16_t>::max)()) + 1u &&
        std::all_of(
            cache.indices.begin(), cache.indices.end(),
            [](std::uint32_t index)
            {
                return index <=
                    static_cast<std::uint32_t>(
                        (std::numeric_limits<std::uint16_t>::max)());
            });
    cache.indices16.clear();
    if (cache.index16)
    {
        try
        {
            cache.indices16.reserve(cache.indices.size());
            for (std::uint32_t index : cache.indices)
                cache.indices16.push_back(
                    static_cast<std::uint16_t>(index));
        }
        catch (...)
        {
            cache.indices16.clear();
            cache.index16 = false;
        }
    }
    return true;
}

bool EnsureChromeUvBuffer(RetainedCache& cache)
{
    if (!g_getIntegerv || !g_genBuffers || !g_deleteBuffers ||
        !g_bindBuffer || !g_bufferData || cache.vertices.empty())
        return false;
    if (cache.vertices.size() >
        static_cast<std::size_t>(PTRDIFF_MAX) / sizeof(ChromeUv))
        return false;
    const std::size_t requiredBytes =
        cache.vertices.size() * sizeof(ChromeUv);
    if (cache.chromeUvBuffer &&
        cache.chromeUvBufferBytes == requiredBytes)
        return true;

    int previousArray = 0;
    unsigned buffer = 0;
    bool generated = false;
    bool ok = false;
    __try
    {
        g_getIntegerv(GL_ARRAY_BUFFER_BINDING, &previousArray);
        if (cache.chromeUvBuffer)
        {
            const unsigned stale = cache.chromeUvBuffer;
            g_deleteBuffers(1, &stale);
            cache.chromeUvBuffer = 0;
            cache.chromeUvBufferBytes = 0;
        }
        g_genBuffers(1, &buffer);
        if (!buffer)
            __leave;
        generated = true;
        g_bindBuffer(GL_ARRAY_BUFFER, buffer);
        g_bufferData(
            GL_ARRAY_BUFFER,
            static_cast<std::ptrdiff_t>(requiredBytes),
            nullptr,
            GL_STREAM_DRAW);
        ok = true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        ok = false;
    }

    __try
    {
        g_bindBuffer(
            GL_ARRAY_BUFFER,
            static_cast<unsigned>(previousArray));
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        ok = false;
    }

    if (!ok)
    {
        if (generated && buffer)
        {
            __try { g_deleteBuffers(1, &buffer); }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        return false;
    }

    cache.chromeUvBuffer = buffer;
    cache.chromeUvBufferBytes = requiredBytes;
    return true;
}

bool PrepareChromeCoordinates(const DirectGlobals& state,
                              const RetainedCache& cache,
                              const RetainedSubmodel& submodel)
{
    const StudioModelRaw* model = submodel.source;
    if (!g_studioChrome || !state.header || !model ||
        model->nummesh <= 0 || model->numnorms <= 0 ||
        g_preparedMeshes.size() != static_cast<std::size_t>(model->nummesh) ||
        cache.normalIndices.size() != cache.vertices.size() ||
        submodel.firstVertex > cache.vertices.size() ||
        submodel.vertexCount > cache.vertices.size() - submodel.firstVertex)
        return false;

    const StudioMeshRaw* meshes = HeaderArray<StudioMeshRaw>(
        state.header, model->meshindex, model->nummesh);
    const float* normals = HeaderArray<float>(
        state.header, model->normindex, model->numnorms * 3);
    const std::uint8_t* normalBones = HeaderArray<std::uint8_t>(
        state.header, model->norminfoindex, model->numnorms);
    if (!meshes || !normals || !normalBones)
        return false;

    try
    {
        g_chromeCoords.assign(
            static_cast<std::size_t>(model->numnorms), ChromeCoord{});
        g_chromeUvScratch.assign(submodel.vertexCount, ChromeUv{});
    }
    catch (...)
    {
        return false;
    }

    std::size_t normalOrdinal = 0;
    bool anyChrome = false;
    for (int meshIndex = 0; meshIndex < model->nummesh; ++meshIndex)
    {
        const StudioMeshRaw& rawMesh = meshes[meshIndex];
        if (rawMesh.numnorms < 0)
            return false;
        const std::size_t count =
            static_cast<std::size_t>(rawMesh.numnorms);
        if (normalOrdinal > static_cast<std::size_t>(model->numnorms) ||
            count > static_cast<std::size_t>(model->numnorms) - normalOrdinal)
            return false;

        const unsigned flags =
            g_preparedMeshes[static_cast<std::size_t>(meshIndex)].material.flags;
        if ((flags & 0x02u) != 0u)
        {
            anyChrome = true;
            for (std::size_t i = 0; i < count; ++i)
            {
                const std::size_t normalIndex = normalOrdinal + i;
                const unsigned bone = normalBones[normalIndex];
                if (bone >= 128u ||
                    bone >= static_cast<unsigned>(state.boneCount))
                    return false;
                int* output = reinterpret_cast<int*>(
                    g_hwBase + kChromeTableRva) + normalIndex * 2u;
                if (!SampleStockChrome(
                        output,
                        static_cast<int>(bone),
                        normals + normalIndex * 3u))
                    return false;
                ++g_chromeNormalCalls;
                ChromeCoord& coord = g_chromeCoords[normalIndex];
                coord.s = output[0];
                coord.t = output[1];
                coord.valid = true;
            }
        }
        normalOrdinal += count;
    }
    if (!anyChrome)
        return false;
    ++g_chromePrepasses;

    const std::size_t vertexEnd =
        submodel.firstVertex + submodel.vertexCount;
    for (const PreparedDirectMesh& prepared : g_preparedMeshes)
    {
        if (!prepared.mesh || (prepared.material.flags & 0x02u) == 0u)
            continue;
        const RetainedSubmesh& mesh = *prepared.mesh;
        const std::size_t first = static_cast<std::size_t>(mesh.firstIndex);
        const std::size_t count = static_cast<std::size_t>(mesh.indexCount);
        if (first > cache.indices.size() ||
            count > cache.indices.size() - first)
            return false;
        for (std::size_t i = 0; i < count; ++i)
        {
            const std::size_t vertexIndex =
                static_cast<std::size_t>(cache.indices[first + i]);
            if (vertexIndex < submodel.firstVertex ||
                vertexIndex >= vertexEnd ||
                vertexIndex >= cache.normalIndices.size())
                return false;
            const std::size_t normalIndex =
                static_cast<std::size_t>(cache.normalIndices[vertexIndex]);
            if (normalIndex >= g_chromeCoords.size() ||
                !g_chromeCoords[normalIndex].valid)
                return false;
            ChromeUv& uv =
                g_chromeUvScratch[vertexIndex - submodel.firstVertex];
            uv.s = static_cast<float>(g_chromeCoords[normalIndex].s);
            uv.t = static_cast<float>(g_chromeCoords[normalIndex].t);
        }
    }
    return true;
}

bool UploadChromeCoordinates(RetainedCache& cache,
                             const RetainedSubmodel& submodel)
{
    if (!cache.chromeUvBuffer || !g_getIntegerv || !g_bindBuffer ||
        !g_bufferSubData ||
        g_chromeUvScratch.size() != submodel.vertexCount)
        return false;
    if (submodel.firstVertex > SIZE_MAX / sizeof(ChromeUv) ||
        submodel.vertexCount > SIZE_MAX / sizeof(ChromeUv))
        return false;
    const std::size_t offset = submodel.firstVertex * sizeof(ChromeUv);
    const std::size_t bytes = submodel.vertexCount * sizeof(ChromeUv);
    if (offset > cache.chromeUvBufferBytes ||
        bytes > cache.chromeUvBufferBytes - offset ||
        offset > static_cast<std::size_t>(PTRDIFF_MAX) ||
        bytes > static_cast<std::size_t>(PTRDIFF_MAX))
        return false;

    int previousArray = 0;
    bool ok = false;
    __try
    {
        g_getIntegerv(GL_ARRAY_BUFFER_BINDING, &previousArray);
        g_bindBuffer(GL_ARRAY_BUFFER, cache.chromeUvBuffer);
        g_bufferSubData(
            GL_ARRAY_BUFFER,
            static_cast<std::ptrdiff_t>(offset),
            static_cast<std::ptrdiff_t>(bytes),
            g_chromeUvScratch.data());
        ok = true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        ok = false;
    }
    __try
    {
        g_bindBuffer(
            GL_ARRAY_BUFFER,
            static_cast<unsigned>(previousArray));
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        ok = false;
    }
    return ok;
}

void ForgetGpu(RetainedCache& cache, bool deleteObjects)
{
    if (deleteObjects && g_deleteBuffers)
    {
        if (cache.vertexArray && g_deleteVertexArrays)
        {
            const unsigned id = cache.vertexArray;
            __try { g_deleteVertexArrays(1, &id); }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        if (cache.vertexBuffer)
        {
            const unsigned id = cache.vertexBuffer;
            __try { g_deleteBuffers(1, &id); }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        if (cache.indexBuffer)
        {
            const unsigned id = cache.indexBuffer;
            __try { g_deleteBuffers(1, &id); }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        if (cache.chromeUvBuffer)
        {
            const unsigned id = cache.chromeUvBuffer;
            __try { g_deleteBuffers(1, &id); }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
    }
    cache.vertexArray = 0;
    cache.vertexArrayUsesPalette = false;
    cache.vertexBuffer = 0;
    cache.indexBuffer = 0;
    cache.chromeUvBuffer = 0;
    cache.chromeUvBufferBytes = 0;
    cache.contextGeneration = 0;
}

bool EnsureCacheVertexArray(RetainedCache& cache)
{
    if (cache.vertexArray &&
        cache.vertexArrayUsesPalette == g_useUniformBuffer)
        return true;
    if (!g_genVertexArrays || !g_deleteVertexArrays ||
        !g_bindVertexArray || !g_getIntegerv ||
        !g_bindBuffer || !g_vertexAttribPointer ||
        !g_enableVertexAttribArray ||
        !cache.vertexBuffer || !cache.indexBuffer)
        return false;

    if (cache.vertexArray)
    {
        const unsigned stale = cache.vertexArray;
        __try { g_deleteVertexArrays(1, &stale); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        cache.vertexArray = 0;
        cache.vertexArrayUsesPalette = false;
    }

    int previousVao = 0;
    int previousArrayBuffer = 0;
    unsigned vao = 0;
    bool created = false;
    __try
    {
        g_getIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVao);
        g_getIntegerv(
            GL_ARRAY_BUFFER_BINDING, &previousArrayBuffer);
        g_genVertexArrays(1, &vao);
        if (!vao)
            __leave;
        created = true;
        g_bindVertexArray(vao);
        g_bindBuffer(GL_ARRAY_BUFFER, cache.vertexBuffer);
        g_bindBuffer(GL_ELEMENT_ARRAY_BUFFER, cache.indexBuffer);
        const std::size_t positionBoneOffset =
            g_useUniformBuffer
                ? offsetof(RetainedVertex, palettePositionBone)
                : offsetof(RetainedVertex, positionBone);
        const std::size_t normalBoneOffset =
            g_useUniformBuffer
                ? offsetof(RetainedVertex, paletteNormalBone)
                : offsetof(RetainedVertex, normalBone);

        g_vertexAttribPointer(
            kAttribPosition, 3, GL_FLOAT, GL_FALSE_VALUE,
            sizeof(RetainedVertex),
            reinterpret_cast<const void*>(
                offsetof(RetainedVertex, xyz)));
        g_vertexAttribPointer(
            kAttribNormal, 3, GL_FLOAT, GL_FALSE_VALUE,
            sizeof(RetainedVertex),
            reinterpret_cast<const void*>(
                offsetof(RetainedVertex, normal)));
        g_vertexAttribPointer(
            kAttribRawST, 2, GL_SHORT, GL_FALSE_VALUE,
            sizeof(RetainedVertex),
            reinterpret_cast<const void*>(
                offsetof(RetainedVertex, rawST)));
        g_vertexAttribPointer(
            kAttribPositionBone, 1, GL_UNSIGNED_BYTE,
            GL_FALSE_VALUE, sizeof(RetainedVertex),
            reinterpret_cast<const void*>(
                positionBoneOffset));
        g_vertexAttribPointer(
            kAttribNormalBone, 1, GL_UNSIGNED_BYTE,
            GL_FALSE_VALUE, sizeof(RetainedVertex),
            reinterpret_cast<const void*>(
                normalBoneOffset));
        g_enableVertexAttribArray(kAttribPosition);
        g_enableVertexAttribArray(kAttribNormal);
        g_enableVertexAttribArray(kAttribRawST);
        g_enableVertexAttribArray(kAttribPositionBone);
        g_enableVertexAttribArray(kAttribNormalBone);

        g_bindVertexArray(static_cast<unsigned>(previousVao));
        g_bindBuffer(
            GL_ARRAY_BUFFER,
            static_cast<unsigned>(previousArrayBuffer));
        cache.vertexArray = vao;
        cache.vertexArrayUsesPalette = g_useUniformBuffer;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }

    __try
    {
        g_bindVertexArray(static_cast<unsigned>(previousVao));
        g_bindBuffer(
            GL_ARRAY_BUFFER,
            static_cast<unsigned>(previousArrayBuffer));
        if (created && vao)
            g_deleteVertexArrays(1, &vao);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}

bool UploadGpu(RetainedCache& cache)
{
    if (cache.vertices.empty() || cache.indices.empty() ||
        !worldvbo::ContextGenerationReady())
        return false;

    const std::uint32_t contextGeneration = worldvbo::ContextGeneration();
    if (cache.vertexBuffer && cache.indexBuffer &&
        cache.contextGeneration == contextGeneration)
    {
        // VAO is an optional fast state container. Its absence is not a
        // correctness failure, the legacy scoped-array path remains available.
        (void)EnsureCacheVertexArray(cache);
        return true;
    }

    if (!RefreshGlFunctions())
        return false;

    if (cache.contextGeneration != 0 &&
        cache.contextGeneration != contextGeneration)
    {
        // The old context owned these names and already destroyed them.
        ForgetGpu(cache, false);
    }
    else if (cache.vertexBuffer || cache.indexBuffer)
    {
        ForgetGpu(cache, true);
    }

    int previousArray = 0;
    int previousElement = 0;
    unsigned vertexBuffer = 0;
    unsigned indexBuffer = 0;
    if (cache.index16 &&
        cache.indices16.size() != cache.indices.size())
        return false;
    bool bindingsKnown = false;
    __try
    {
        g_getIntegerv(GL_ARRAY_BUFFER_BINDING, &previousArray);
        g_getIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &previousElement);
        bindingsKnown = true;

        g_genBuffers(1, &vertexBuffer);
        g_genBuffers(1, &indexBuffer);
        if (!vertexBuffer || !indexBuffer)
            __leave;

        g_bindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
        g_bufferData(
            GL_ARRAY_BUFFER,
            static_cast<std::ptrdiff_t>(
                cache.vertices.size() * sizeof(RetainedVertex)),
            cache.vertices.data(), GL_STATIC_DRAW);

        g_bindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
        g_bufferData(
            GL_ELEMENT_ARRAY_BUFFER,
            static_cast<std::ptrdiff_t>(
                cache.indices.size() * RetainedIndexBytes(cache)),
            cache.index16
                ? static_cast<const void*>(cache.indices16.data())
                : static_cast<const void*>(cache.indices.data()),
            GL_STATIC_DRAW);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        vertexBuffer = 0;
        indexBuffer = 0;
    }

    if (bindingsKnown)
    {
        __try
        {
            g_bindBuffer(GL_ARRAY_BUFFER, static_cast<unsigned>(previousArray));
            g_bindBuffer(GL_ELEMENT_ARRAY_BUFFER,
                         static_cast<unsigned>(previousElement));
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    if (!vertexBuffer || !indexBuffer)
    {
        // If only one allocation succeeded, clean it up in the live context.
        if (g_deleteBuffers)
        {
            if (vertexBuffer)
            {
                __try { g_deleteBuffers(1, &vertexBuffer); }
                __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
            if (indexBuffer)
            {
                __try { g_deleteBuffers(1, &indexBuffer); }
                __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
        }
        return false;
    }

    cache.vertexBuffer = vertexBuffer;
    cache.indexBuffer = indexBuffer;
    cache.contextGeneration = contextGeneration;
    (void)EnsureCacheVertexArray(cache);
    return true;
}

RetainedCache* ResolveCurrentCache()
{
    const StudioHeaderRaw* header = nullptr;
    const StudioModelRaw* currentModel = nullptr;
    if (!ReadCurrentStudioState(header, currentModel))
    {
        if (prof::Active()) ++g_cacheResolveFailure.readState;
        return nullptr;
    }

    const auto key = reinterpret_cast<const std::uint8_t*>(header);
    auto it = g_caches.find(key);
    if (it != g_caches.end() &&
        it->second.headerLength ==
            static_cast<std::uint32_t>(header->length) &&
        studio_drawbatch::ValidateModelIdentity(
            header,
            it->second.ownerModel,
            it->second.ownerIndex,
            it->second.ownerGeneration))
    {
        if (it->second.submodelLookup.find(currentModel) ==
            it->second.submodelLookup.end())
        {
            if (prof::Active()) ++g_cacheResolveFailure.cachedSubmodel;
            return nullptr;
        }
        return &it->second;
    }

    const void* ownerModel = nullptr;
    int ownerIndex = -1;
    std::uint32_t ownerGeneration = 0;
    if (!studio_drawbatch::QueryModelIdentity(
            header, &ownerModel, &ownerIndex, &ownerGeneration))
    {
        if (prof::Active()) ++g_cacheResolveFailure.identity;
        return nullptr;
    }

    const bool stale =
        it == g_caches.end() ||
        it->second.ownerModel != ownerModel ||
        it->second.ownerIndex != ownerIndex ||
        it->second.ownerGeneration != ownerGeneration ||
        it->second.headerLength != static_cast<std::uint32_t>(header->length);
    if (stale)
    {
        if (it != g_caches.end())
        {
            const bool sameContext =
                it->second.contextGeneration != 0 &&
                it->second.contextGeneration == worldvbo::ContextGeneration();
            ForgetGpu(it->second, sameContext);
            g_caches.erase(it);
        }
        RetainedCache fresh{};
        if (!BuildCache(header, fresh))
        {
            if (prof::Active()) ++g_cacheResolveFailure.build;
            return nullptr;
        }
        try
        {
            it = g_caches.emplace(key, std::move(fresh)).first;
        }
        catch (...)
        {
            if (prof::Active()) ++g_cacheResolveFailure.emplace;
            return nullptr;
        }
        rendererlog::Line(
            "studio renderer: retained model built bodymodels=%u vertices=%u indices=%u meshoptRanges=%u transformed=%llu->%llu",
            static_cast<unsigned>(it->second.submodels.size()),
            static_cast<unsigned>(it->second.vertices.size()),
            static_cast<unsigned>(it->second.indices.size()),
            static_cast<unsigned>(it->second.meshoptRanges),
            static_cast<unsigned long long>(
                it->second.meshoptVerticesBefore),
            static_cast<unsigned long long>(
                it->second.meshoptVerticesAfter));
    }

    if (it->second.submodelLookup.find(currentModel) ==
        it->second.submodelLookup.end())
    {
        if (prof::Active()) ++g_cacheResolveFailure.finalSubmodel;
        return nullptr;
    }
    return &it->second;
}

bool ReadRendererType(int& rendererType)
{
    rendererType = 1;
    if (!g_hwBase)
        return false;
    __try
    {
        rendererType =
            *reinterpret_cast<const int*>(
                g_hwBase + kRendererTypeRva);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        rendererType = 1;
        return false;
    }
}

bool BeginGoldCull(unsigned char& mirror, int& rendererType)
{
    mirror = 0;
    rendererType = 1;
    if (!g_mirrorPredicate || !g_hwBase ||
        !g_cullFace || !g_disable)
        return false;
    if (!ReadRendererType(rendererType))
        return false;

    __try
    {
        mirror = g_mirrorPredicate();
        *reinterpret_cast<unsigned char*>(
            g_hwBase + kMirrorNormalRva) = mirror;
        if (rendererType != 1)
        {
            if (mirror)
                g_disable(GL_CULL_FACE);
            else
                g_cullFace(GL_FRONT);
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

void EndGoldCull(int rendererType)
{
    if (rendererType == 1 || !g_enable)
        return;
    __try { g_enable(GL_CULL_FACE); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

bool WriteMeshAccounting(unsigned flags, int triangles)
{
    if (!g_hwBase || triangles < 0)
        return false;
    __try
    {
        *reinterpret_cast<unsigned*>(
            g_hwBase + kCurrentMeshFlagsRva) = flags;
        *reinterpret_cast<int*>(
            g_hwBase + kStudioTriangleCounterRva) += triangles;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool StampUsedPositionBones(const RetainedSubmodel& submodel)
{
    if (!g_hwBase)
        return false;
    __try
    {
        const int stamp =
            *reinterpret_cast<const int*>(
                g_hwBase + kStudioStampRva);
        auto* ages =
            reinterpret_cast<int*>(
                g_hwBase + kBoneLightAgeRva);
        for (unsigned word = 0; word < 2; ++word)
        {
            std::uint64_t bits =
                submodel.usedPositionBones[word];
            while (bits)
            {
                unsigned bit = 0;
#if defined(_MSC_VER)
                unsigned long index = 0;
#if defined(_M_IX86)
                const std::uint32_t lo =
                    static_cast<std::uint32_t>(bits);
                if (lo)
                {
                    _BitScanForward(&index, lo);
                    bit = static_cast<unsigned>(index);
                }
                else
                {
                    _BitScanForward(
                        &index,
                        static_cast<std::uint32_t>(bits >> 32));
                    bit = 32u + static_cast<unsigned>(index);
                }
#else
                _BitScanForward64(&index, bits);
                bit = static_cast<unsigned>(index);
#endif
#else
                bit = static_cast<unsigned>(__builtin_ctzll(bits));
#endif
                const unsigned bone = word * 64u + bit;
                if (bone < 128u)
                    ages[bone] = stamp;
                bits &= bits - 1u;
            }
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool PackUniformBoneData(const DirectGlobals& state,
                         const RetainedSubmodel& submodel,
                         float* destination,
                         std::size_t destinationFloats,
                         std::size_t& packedFloats)
{
    packedFloats = 0;
    if (!destination || !g_hwBase ||
        state.boneCount <= 0 || state.boneCount > 128 ||
        submodel.bonePaletteCount == 0 ||
        submodel.bonePaletteCount > 128)
        return false;
    const std::size_t paletteCount =
        static_cast<std::size_t>(submodel.bonePaletteCount);
    const std::size_t requiredFloats = paletteCount * 16u;
    if (requiredFloats > destinationFloats)
        return false;

    const float* bones =
        reinterpret_cast<const float*>(
            g_hwBase + kBoneTransformRva);
    const float* lightVectors =
        reinterpret_cast<const float*>(
            g_hwBase + kBoneLightVectorRva);
    __try
    {
        for (std::size_t localBone = 0;
             localBone < paletteCount; ++localBone)
        {
            const unsigned sourceBone =
                static_cast<unsigned>(
                    submodel.bonePalette[localBone]);
            if (sourceBone >= static_cast<unsigned>(state.boneCount))
                return false;
            float* packed = destination + localBone * 16u;
            std::memcpy(
                packed,
                bones + static_cast<std::size_t>(sourceBone) * 12u,
                12u * sizeof(float));
            packed[12] =
                lightVectors[
                    static_cast<std::size_t>(sourceBone) * 3u + 0u];
            packed[13] =
                lightVectors[
                    static_cast<std::size_t>(sourceBone) * 3u + 1u];
            packed[14] =
                lightVectors[
                    static_cast<std::size_t>(sourceBone) * 3u + 2u];
            packed[15] = 0.0f;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
    packedFloats = requiredFloats;
    return true;
}

bool UploadUniformBufferEntity(
    const DirectGlobals& state,
    const RetainedSubmodel& submodel,
    const EntityUniformKey& key)
{
    if (!g_useUniformBuffer || !g_uniformBufferReady ||
        !g_bindBuffer || !g_bufferSubData || !g_bindBufferRange ||
        !g_hwBase || state.boneCount <= 0 || state.boneCount > 128 ||
        submodel.bonePaletteCount == 0 ||
        submodel.bonePaletteCount > 128)
        return false;

    UniformBufferFrame& frame =
        g_uniformBuffers[g_uniformBufferFrame];
    if (!frame.buffer)
        return false;

    const std::size_t aligned =
        AlignUp(
            g_uniformBufferOffset,
            static_cast<std::size_t>(
                g_uniformBufferAlignment));
    if (aligned > kUniformBufferBytes ||
        kBoneBlockBytes > kUniformBufferBytes - aligned)
        return false;

    // std140 layout is an interleaved 64-byte record per local palette bone:
    //   vec4 row0, row1, row2, light.
    // The linked block remains 8192 bytes (128 records) for a single shader,
    // but only the compact prefix actually referenced by this submodel is
    // transferred. Typical player submodels use a small fraction of 128 bones.
    const bool collectTiming = prof::Active();
    const std::size_t rangeOffset = frame.base + aligned;
    if (g_uniformPersistentBase)
    {
        // Persistent ring: pack straight into the coherent mapping. The range
        // is past every offset already handed out this frame and its region's
        // fence was retired at rotation, so the GPU cannot be reading it.
        const long long uploadStart =
            collectTiming ? prof::Now() : 0;
        std::size_t packedFloats = 0;
        if (!PackUniformBoneData(
                state, submodel,
                reinterpret_cast<float*>(
                    g_uniformPersistentBase + rangeOffset),
                kBoneBlockBytes / sizeof(float), packedFloats))
            return false;
        if (collectTiming)
        {
            g_directTiming.uboUploadTicks +=
                prof::Now() - uploadStart;
            ++g_directTiming.uboUploads;
            ++g_uniformPersistentWrites;
        }
    }

    bool uploaded = false;
    bool rangeBound = false;
    if (!g_uniformPersistentBase)
    {
        bool staged = false;
        std::size_t packedFloats = 0;
        if (!PackUniformBoneData(
                state, submodel, g_boneBlockScratch,
                kBoneBlockBytes / sizeof(float), packedFloats))
            return false;
        const std::size_t uploadBytes =
            packedFloats * sizeof(float);
        __try
        {
            g_bindBuffer(GL_UNIFORM_BUFFER, frame.buffer);
            const long long uploadStart =
                collectTiming ? prof::Now() : 0;
            g_bufferSubData(
                GL_UNIFORM_BUFFER,
                static_cast<std::ptrdiff_t>(aligned),
                static_cast<std::ptrdiff_t>(uploadBytes),
                g_boneBlockScratch);
            if (collectTiming)
            {
                g_directTiming.uboUploadTicks +=
                    prof::Now() - uploadStart;
                ++g_directTiming.uboUploads;
            }
            staged = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            staged = false;
        }
        if (!staged)
        {
            (void)RestoreKnownGenericUniformBinding();
            return false;
        }
    }
    __try
    {
        const long long bindStart =
            collectTiming ? prof::Now() : 0;
        g_bindBufferRange(
            GL_UNIFORM_BUFFER,
            g_boneBlockBinding,
            frame.buffer,
            static_cast<std::ptrdiff_t>(rangeOffset),
            static_cast<std::ptrdiff_t>(kBoneBlockBytes));
        rangeBound = true;
        if (collectTiming)
        {
            g_directTiming.uboBindTicks +=
                prof::Now() - bindStart;
            ++g_directTiming.uboBinds;
        }
        uploaded = true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        uploaded = false;
    }

    // The reserved indexed point is module-owned (see g_lastUniformBlock*),
    // a failed upload leaves nothing foreign to restore there.
    if (rangeBound)
        NoteOwnedUniformRange(frame.buffer, rangeOffset, kBoneBlockBytes);
    const bool genericRestored =
        RestoreKnownGenericUniformBinding();
    if (!uploaded || !genericRestored)
        return false;

    g_uniformBufferOffset = aligned + kBoneBlockBytes;
    g_lastEntityUniformKey = key;
    g_entityUniformKeyValid = true;
    return true;
}

bool UploadEntityUniforms(const DirectGlobals& state,
                          const RetainedSubmodel& submodel)
{
    if (!g_useProgram || !g_uniform4fv ||
        (!g_useUniformBuffer && !g_uniform3fv) ||
        !g_hwBase || !g_program || state.boneCount <= 0 ||
        state.boneCount > 128 || !g_gammaFitKnown)
        return false;

    __try
    {
        g_useProgram(g_program);
        g_directProgramOwned = true;
        if (g_gammaFitProgramRevision != g_gammaFitRevision)
        {
            g_uniform4fv(
                g_gammaFit0Location, 1, g_gammaFit0);
            g_uniform4fv(
                g_gammaFit1Location, 1, g_gammaFit1);
            g_gammaFitProgramRevision = g_gammaFitRevision;
        }

        const EntityUniformKey key{
            worldvbo::ContextGeneration(),
            state.studioStamp,
            state.entity,
            state.header,
            submodel.source,
            state.boneCount
        };
        const bool sameEntityUniforms =
            g_entityUniformKeyValid &&
            g_lastEntityUniformKey.contextGeneration == key.contextGeneration &&
            g_lastEntityUniformKey.studioStamp == key.studioStamp &&
            g_lastEntityUniformKey.entity == key.entity &&
            g_lastEntityUniformKey.header == key.header &&
            (!g_useUniformBuffer ||
             g_lastEntityUniformKey.submodel == key.submodel) &&
            g_lastEntityUniformKey.boneCount == key.boneCount;
        if (!sameEntityUniforms)
        {
            if (g_useUniformBuffer)
            {
                if (!UploadUniformBufferEntity(state, submodel, key))
                {
                    // UploadEntityUniforms owns g_program from this point.
                    // Any ordinary UBO rejection must restore Gold's known
                    // fixed-function entry program before falling back.
                    (void)ReleaseOwnedDirectProgram();
                    return false;
                }
            }
            else
            {
                g_uniform4fv(
                    g_bonesLocation,
                    state.boneCount * 3,
                    reinterpret_cast<const float*>(
                        g_hwBase + kBoneTransformRva));
                g_uniform3fv(
                    g_lightVectorsLocation,
                    state.boneCount,
                    reinterpret_cast<const float*>(
                        g_hwBase + kBoneLightVectorRva));
                g_lastEntityUniformKey = key;
                g_entityUniformKeyValid = true;
            }
        }

        const float colorBlend[4] = {
            state.colorMix[0],
            state.colorMix[1],
            state.colorMix[2],
            state.blend
        };
        if (!g_colorBlendUniformValid ||
            std::memcmp(
                g_lastColorBlend, colorBlend,
                sizeof(colorBlend)) != 0)
        {
            g_uniform4fv(
                g_colorBlendLocation, 1, colorBlend);
            std::memcpy(
                g_lastColorBlend, colorBlend,
                sizeof(colorBlend));
            g_colorBlendUniformValid = true;
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        (void)ReleaseOwnedDirectProgram();
        return false;
    }
}

bool SetMaterialParams(const DirectGlobals& state,
                       const DirectMaterial& material)
{
    if (!g_uniform4fv)
        return false;
    const bool flatShade = material.constantLight;
    const float params[4] = {
        flatShade ? -material.sScale : material.sScale,
        material.tScale,
        flatShade ? material.flatLight : state.ambient,
        flatShade ? 0.0f : state.shade
    };
    if (g_paramsUniformValid &&
        std::memcmp(g_lastParams, params, sizeof(params)) == 0)
        return true;
    __try
    {
        g_uniform4fv(g_paramsLocation, 1, params);
        std::memcpy(g_lastParams, params, sizeof(params));
        g_paramsUniformValid = true;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool RestoreFinalImmediateState(const DirectGlobals& state,
                                const RetainedCache& cache,
                                const RetainedSubmesh& lastMesh,
                                const DirectMaterial& lastMaterial)
{
    if (!g_texCoord2f || !g_color4f || !g_studioLighting ||
        lastMesh.lastVertex >= cache.vertices.size())
        return false;
    const RetainedVertex& last =
        cache.vertices[lastMesh.lastVertex];

    float scalar = 1.0f;
    InvokeStockLighting(
        &scalar,
        static_cast<int>(last.normalBone),
        static_cast<int>(lastMaterial.flags),
        last.normal);

    float s = 0.0f;
    float t = 0.0f;
    if ((lastMaterial.flags & 0x02u) != 0u)
    {
        if (lastMesh.lastVertex >= cache.normalIndices.size())
            return false;
        const std::size_t normalIndex =
            static_cast<std::size_t>(
                cache.normalIndices[lastMesh.lastVertex]);
        if (normalIndex >= g_chromeCoords.size() ||
            !g_chromeCoords[normalIndex].valid)
            return false;
        s = static_cast<float>(g_chromeCoords[normalIndex].s) *
            lastMaterial.sScale;
        t = static_cast<float>(g_chromeCoords[normalIndex].t) *
            lastMaterial.tScale;
    }
    else
    {
        s = static_cast<float>(last.rawST[0]) * lastMaterial.sScale;
        t = static_cast<float>(last.rawST[1]) * lastMaterial.tScale;
    }
    __try
    {
        g_texCoord2f(s, t);
        g_color4f(
            state.colorMix[0] * scalar,
            state.colorMix[1] * scalar,
            state.colorMix[2] * scalar,
            state.blend);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool ExecuteDirectDraw(const DirectGlobals& state,
                       RetainedCache& cache,
                       const RetainedSubmodel& submodel,
                       StudioHeaderRaw* textureHeader,
                       int previousVertexArray,
                       int previousArrayBuffer,
                       int previousElementBuffer,
                       int maxTextureUnits)
{
    if (!g_textureResolver || !g_goldBind || !g_drawElements ||
        submodel.meshes.empty() || g_preparedMeshes.size() != submodel.meshes.size())
        return false;

    DirectMaterial lastMaterial{};
    const RetainedSubmesh* lastMesh = nullptr;
    unsigned char mirror = 0;
    int rendererType = 1;
    bool arraysBegun = false;
    bool programActive = false;
    bool cullBegun = false;
    bool maskedStateActive = false;
    bool additiveStateActive = false;
    bool chromeAttribActive = false;
    bool drewAny = false;
    unsigned boundDirectTexture = UINT_MAX;
    const bool usingVertexArray =
        cache.vertexArray != 0 && g_bindVertexArray != nullptr;
    DirectMatrices matrices{};
    if (g_useUniformBuffer && !AcquireDirectMatrices(matrices))
        return false;

    const bool collectTiming = prof::Active();
    const long long uniformsStart =
        collectTiming ? prof::Now() : 0;
    if (!UploadEntityUniforms(state, submodel))
        return false;
    programActive = true;
    if (!UploadDirectMatrices(matrices))
    {
        (void)ReleaseOwnedDirectProgram();
        programActive = false;
        return false;
    }
    if (collectTiming)
    {
        g_directTiming.uniformTicks +=
            prof::Now() - uniformsStart;
        ++g_directTiming.uniformCalls;
    }
    if (usingVertexArray)
    {
        __try { g_bindVertexArray(cache.vertexArray); }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            (void)ReleaseOwnedDirectProgram();
            return false;
        }
    }
    else if (!BeginDirectArrays(cache, maxTextureUnits))
    {
        (void)ReleaseOwnedDirectProgram();
        return false;
    }
    else
    {
        arraysBegun = true;
    }
    if (!BeginGoldCull(mirror, rendererType))
    {
        if (usingVertexArray)
        {
            __try
            {
                g_bindVertexArray(
                    static_cast<unsigned>(previousVertexArray));
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        else
        {
            EndDirectArrays(previousArrayBuffer, previousElementBuffer);
        }
        (void)ReleaseOwnedDirectProgram();
        return false;
    }
    cullBegun = true;

    __try
    {
        for (std::size_t preparedIndex = 0;
             preparedIndex < g_preparedMeshes.size();)
        {
            const PreparedDirectMesh& prepared =
                g_preparedMeshes[preparedIndex];
            if (!prepared.mesh)
                __leave;
            const RetainedSubmesh& mesh = *prepared.mesh;
            const DirectMaterial& material = prepared.material;
            if (!WriteMeshAccounting(
                    material.flags, mesh.triangleCount))
                __leave;

            const bool masked =
                (material.flags & 0x40u) != 0u;
            const bool additive =
                (material.flags & 0x20u) != 0u;
            const bool chrome =
                (material.flags & 0x02u) != 0u;
            if (masked)
            {
                // Gold enters masked state before texture resolution/binding.
                if (!BeginMaskedMaterial())
                    __leave;
                maskedStateActive = true;
            }
            if (additive)
            {
                // Gold applies ADDITIVE after MASKED setup and before texture
                // resolution: ONE/ONE blend, depth writes off, smooth shade.
                if (!BeginAdditiveMaterial())
                    __leave;
                additiveStateActive = true;
            }

            const long long materialStart =
                collectTiming ? prof::Now() : 0;
            if (material.directBind)
            {
                if (boundDirectTexture != material.texture->glId)
                {
                    g_goldBind(0, material.texture->glId);
                    boundDirectTexture = material.texture->glId;
                }
            }
            else
            {
                g_textureResolver(textureHeader, material.textureSlot);
                // Gold's resolver may bind a remap/DM_Base texture whose object
                // differs from the descriptor's base glId. Resynchronise the
                // direct-bind shadow before the next ordinary material.
                boundDirectTexture = UINT_MAX;
            }
            if (!SetMaterialParams(state, material))
                __leave;
            if (collectTiming)
            {
                g_directTiming.materialTicks +=
                    prof::Now() - materialStart;
                ++g_directTiming.materialCalls;
            }

            std::uint32_t drawFirst =
                SelectDrawFirstIndex(mesh, material);
            std::uint32_t drawCount = mesh.indexCount;
            std::size_t lastPrepared = preparedIndex;
            // Only coalesce the proven direct-bind case. Remap/DM_Base meshes
            // retain one exact Gold resolver call per mesh.
            if (material.directBind && !masked && !additive && !chrome)
            {
                while (lastPrepared + 1 < g_preparedMeshes.size())
                {
                    const PreparedDirectMesh& next =
                        g_preparedMeshes[lastPrepared + 1];
                    if (!next.mesh || !next.material.directBind ||
                        next.material.texture->glId != material.texture->glId ||
                        next.material.flags != material.flags ||
                        next.material.sScale != material.sScale ||
                        next.material.tScale != material.tScale ||
                        next.material.flatLight != material.flatLight ||
                        SelectDrawFirstIndex(
                            *next.mesh, next.material) !=
                            drawFirst + drawCount ||
                        drawCount > UINT32_MAX - next.mesh->indexCount)
                        break;
                    if (!WriteMeshAccounting(
                            next.material.flags, next.mesh->triangleCount))
                        __leave;
                    drawCount += next.mesh->indexCount;
                    ++lastPrepared;
                }
            }

            const long long drawStart =
                collectTiming ? prof::Now() : 0;
            if (chrome)
            {
                if (!ConfigureRawStAttribute(cache, true))
                    __leave;
                chromeAttribActive = true;
            }
            g_drawElements(
                GL_TRIANGLES,
                static_cast<int>(drawCount),
                RetainedIndexType(cache),
                reinterpret_cast<const void*>(
                    static_cast<std::uintptr_t>(
                        drawFirst) * RetainedIndexBytes(cache)));

            // From this point on, Gold cannot safely be replayed for this
            // DrawPoints call: geometry has already reached GL. Commit the
            // submitted mesh before any MASKED/ADDITIVE cleanup that can
            // itself fail through a foreign qgl hook. This prevents a
            // cleanup failure from returning false and double-drawing the
            // same mesh through the stock path.
            drewAny = true;
            lastMaterial =
                g_preparedMeshes[lastPrepared].material;
            lastMesh =
                g_preparedMeshes[lastPrepared].mesh;
            if (masked)
            {
                if (!EndMaskedMaterial())
                    __leave;
                maskedStateActive = false;
            }
            if (additive)
            {
                if (!EndAdditiveMaterial())
                    __leave;
                additiveStateActive = false;
            }
            // Restore our private generic attribute only after Gold-visible
            // MASKED/ADDITIVE cleanup, preserving the stock post-emitter call
            // order for any foreign qgl hooks.
            if (chrome)
            {
                if (!ConfigureRawStAttribute(cache, false))
                    __leave;
                chromeAttribActive = false;
            }
            if (collectTiming)
            {
                g_directTiming.drawTicks +=
                    prof::Now() - drawStart;
                ++g_directTiming.drawCalls;
            }
            preparedIndex = lastPrepared + 1;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        // A GL/foreign exception after the first direct submission cannot safely
        // replay Gold without double drawing. Cleanup below and claim the draw.
    }

    if (maskedStateActive)
    {
        (void)EndMaskedMaterial();
        maskedStateActive = false;
    }
    if (additiveStateActive)
    {
        (void)EndAdditiveMaterial();
        additiveStateActive = false;
    }
    if (chromeAttribActive)
    {
        (void)ConfigureRawStAttribute(cache, false);
        chromeAttribActive = false;
    }

    if (cullBegun)
        EndGoldCull(rendererType);
    if (programActive && g_useProgram)
    {
        (void)ReleaseOwnedDirectProgram();
    }
    if (usingVertexArray)
    {
        __try
        {
            g_bindVertexArray(
                static_cast<unsigned>(previousVertexArray));
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    else if (arraysBegun)
        EndDirectArrays(previousArrayBuffer, previousElementBuffer);

    if (!drewAny || !lastMesh)
        return false;
    const long long restoreStart =
        collectTiming ? prof::Now() : 0;
    (void)RestoreFinalImmediateState(
        state, cache, *lastMesh, lastMaterial);
    if (collectTiming)
    {
        g_directTiming.restoreTicks +=
            prof::Now() - restoreStart;
        ++g_directTiming.restoreCalls;
    }
    return true;
}

bool DeferredCapacityAvailable()
{
    if (!g_uniformBufferReady ||
        g_uniformBufferAlignment <= 0)
        return false;
    const std::size_t alignment =
        static_cast<std::size_t>(g_uniformBufferAlignment);
    const std::size_t first =
        AlignUp(g_uniformBufferOffset, alignment);
    if (first > kUniformBufferBytes ||
        kBoneBlockBytes > kUniformBufferBytes - first)
        return false;

    const std::size_t stride = AlignUp(kBoneBlockBytes, alignment);
    if (stride < kBoneBlockBytes)
        return false;
    const std::size_t count = g_deferredCommands.size() + 1u;
    if (count == 0 || count == 1)
        return true;
    const std::size_t lastStartLimit =
        kUniformBufferBytes - kBoneBlockBytes;
    return count - 1u <= (lastStartLimit - first) / stride;
}

bool DeferredReplayReady()
{
    if (!studio::HooksReady() ||
        !g_useUniformBuffer ||
        !g_uniformBufferReady ||
        !g_program ||
        !g_useProgram ||
        !g_uniform4fv ||
        !g_uniformMatrix4fv ||
        !g_bindVertexArray ||
        !g_bindBuffer ||
        !g_bindBufferRange ||
        !g_bufferSubData ||
        !g_getIntegerv ||
        !g_activeTexture ||
        !g_texEnvi ||
        !g_getTexEnviv ||
        !g_shadeModel ||
        !g_goldBind ||
        !g_drawElements)
        return false;
    const UniformBufferFrame& frame =
        g_uniformBuffers[g_uniformBufferFrame];
    return frame.buffer != 0;
}

bool CaptureDeferredCullState(unsigned char& mirror,
                              int& rendererType)
{
    mirror = 0;
    rendererType = 1;
    // Phase 1 intentionally owns only Gold's ordinary renderer. Alternate
    // renderer types have observable cull transitions at DrawPoints time, keep
    // those immediate until their begin/end side effects are explicitly
    // recorded as commands too.
    if (!ReadRendererType(rendererType) || rendererType != 1 ||
        !g_mirrorPredicate || !g_hwBase)
        return false;
    __try
    {
        mirror = g_mirrorPredicate();
        *reinterpret_cast<unsigned char*>(
            g_hwBase + kMirrorNormalRva) = mirror;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

void BindGoldTextureSafe(unsigned glId)
{
    if (!g_goldBind)
        return;
    __try { g_goldBind(0, glId); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

enum class FlushSite
{
    InsideStudioDraw,
    OutsideStudioDraw
};

bool FlushDeferredSolidCommands(FlushSite site, FlushTrigger trigger);

// Creates the CHROME stream buffer name for the live context. Its storage is
// (re)specified by every flush that uses it. Names of an older context died
// with it and are dropped, not deleted.
bool EnsureChromeStreamBuffer()
{
    if (!g_genBuffers || !worldvbo::ContextGenerationReady())
        return false;
    const std::uint32_t generation = worldvbo::ContextGeneration();
    if (g_chromeStreamBuffer && g_chromeStreamGeneration == generation)
        return true;
    unsigned buffer = 0;
    __try
    {
        g_genBuffers(1, &buffer);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        buffer = 0;
    }
    if (!buffer)
        return false;
    g_chromeStreamBuffer = buffer;
    g_chromeStreamGeneration = generation;
    return true;
}

// MASKED and CHROME packets are recorded as well.
//
// MASKED meshes are alpha-tested (GREATER 0.5) and depth-written: every
// fragment is either discarded or written with its color and depth, the same
// order-independent class as the opaque packets. The flush applies Gold's
// BeginMaskedMaterial state around exactly those mesh draws and returns alpha
// test to its flush-entry state afterwards. At the original DrawPoints point
// only the state Gold leaves behind is reproduced (EndMaskedMaterial: alpha
// func NOTEQUAL 0, alpha test off, depth writes on).
//
// CHROME coordinates depend on the entity's bones and the view (chromeage /
// chrome basis). They are computed at the original call through the stock
// R_StudioChrome, in Gold's mesh order and with its chrome-table side effects
// (PrepareChromeCoordinates, the same prepass the immediate path uses), and
// the per-vertex result is copied into the packet. The flush stages every
// packet's coordinates into one stream buffer and points the VAO's raw-ST
// attribute at them for the CHROME meshes only, exactly the source the
// immediate path binds from the cache's chrome buffer.
//
// ADDITIVE meshes (ONE/ONE blend, no depth writes) and remap textures (the
// resolver may upload a per-entity texture) stay immediate/stock.
bool RecordDeferredDirectDraw(const DirectGlobals& state,
                              RetainedCache& cache,
                              const RetainedSubmodel& submodel,
                              bool& chromePrepared)
{
    chromePrepared = false;
    const unsigned rejectSide = state.player == 0 ? 1u : 0u;
    auto refuse = [rejectSide](RecordReject reason) -> bool
    {
        if (prof::Active())
            ++g_recordReject[rejectSide][static_cast<unsigned>(reason)];
        return false;
    };

    // The reserved indexed UBO point is module-owned and the flush rebinds the
    // exact range of every draw, so recording never reads it back.
    if (g_deferredFlushInProgress ||
        !g_deferredBarriersReady ||
        g_solidEntityPassDepth <= 0)
        return refuse(RecordReject::Scope);
    if (!DeferredReplayReady())
        return refuse(RecordReject::ReplayReady);
    if (!cache.vertexArray || !g_bindVertexArray)
        return refuse(RecordReject::VertexArray);
    if (!DeferredCapacityAvailable())
        return refuse(RecordReject::Capacity);
    if (g_preparedMeshes.empty() ||
        g_preparedMeshes.size() != submodel.meshes.size())
        return refuse(RecordReject::Prepared);

    bool hasChrome = false;
    bool hasMasked = false;
    for (const PreparedDirectMesh& prepared : g_preparedMeshes)
    {
        if (!prepared.mesh || !prepared.material.texture ||
            prepared.material.texture->glId == 0)
            return refuse(RecordReject::Texture);
        if (!prepared.material.directBind)
            return refuse(RecordReject::Remap);
        if ((prepared.material.flags & 0x20u) != 0u)
            return refuse(RecordReject::Additive);
        if ((prepared.material.flags & 0x02u) != 0u)
            hasChrome = true;
        if ((prepared.material.flags & 0x40u) != 0u)
            hasMasked = true;
    }
    // The flush reads and restores the alpha-test function around MASKED
    // draws, it must not discover a missing entry point after acceptance.
    if (hasMasked &&
        (!g_alphaFunc || !g_getFloatv || !g_isEnabled ||
         !g_enable || !g_disable || !g_depthMask))
        return refuse(RecordReject::ReplayReady);

    std::size_t chromeStreamBase = 0;
    std::size_t chromeStreamEnd = g_deferredChromeStreamBytes;
    if (hasChrome)
    {
        // Same capacity check as the stream placement below, before any
        // Gold-visible chrome work.
        const std::size_t vertexBytes = sizeof(ChromeUv);
        if (!g_bufferData || !g_bufferSubData ||
            !g_vertexAttribPointer ||
            !EnsureChromeStreamBuffer() ||
            submodel.vertexCount == 0 ||
            submodel.firstVertex > kChromeStreamMaxBytes / vertexBytes ||
            submodel.vertexCount > kChromeStreamMaxBytes / vertexBytes)
            return refuse(RecordReject::ChromeCapacity);
        const std::size_t firstBytes = submodel.firstVertex * vertexBytes;
        const std::size_t dataStart =
            (std::max)(AlignUp(g_deferredChromeStreamBytes, 16u), firstBytes);
        const std::size_t dataBytes = submodel.vertexCount * vertexBytes;
        if (dataStart > kChromeStreamMaxBytes ||
            dataBytes > kChromeStreamMaxBytes - dataStart)
            return refuse(RecordReject::ChromeCapacity);
        chromeStreamBase = dataStart - firstBytes;
        chromeStreamEnd = dataStart + dataBytes;
    }

    DirectMatrices candidateMatrices{};
    bool commitRunMatrices = false;
    if (g_deferredCommands.empty())
    {
        g_deferredRunMatricesValid = false;
        g_deferredLastEntity = nullptr;
        g_deferredRunMatricesVerify = false;
        // Once per barrier-delimited stretch of the pass, see g_passMatrices.
        if (!AcquireDirectMatrices(candidateMatrices))
            return refuse(RecordReject::Matrices);
        commitRunMatrices = true;
    }
    else
    {
        if (!g_deferredRunMatricesValid)
            return refuse(RecordReject::Matrices);
        // Production reuses the run matrices for every entity. While profiling,
        // one entity change in 256 is compared against live GL so a foreign
        // per-entity matrix writer shows up in the report. The first record
        // after a skipped (order-independent) barrier is always compared,
        // since stock brush/sprite/Studio code ran in between. A mismatch
        // splits the run exactly like the former per-entity capture did.
        const bool forcedVerify = g_deferredRunMatricesVerify;
        if (forcedVerify ||
            (state.entity != g_deferredLastEntity &&
             rendererlog::StatsEnabled() &&
             (++g_matrixVerifySequence & 0xFFu) == 0u))
        {
            if (!CaptureDirectMatrices(candidateMatrices))
                return refuse(RecordReject::Matrices);
            g_deferredRunMatricesVerify = false;
            if (!forcedVerify || prof::Active())
                ++g_flushProfile.matrixVerifies;
            if (std::memcmp(
                    &candidateMatrices,
                    &g_deferredRunMatrices,
                    sizeof(candidateMatrices)) != 0)
            {
                ++g_flushProfile.matrixMismatches;
                if (!g_matrixMismatchLogged)
                {
                    g_matrixMismatchLogged = true;
                    rendererlog::Line(
                        "studio renderer: GL matrices changed between Studio "
                        "entities without a barrier, deferred run split");
                }
                if (!FlushDeferredSolidCommands(
                        FlushSite::InsideStudioDraw,
                        FlushTrigger::MatrixSplit) ||
                    !g_deferredCommands.empty())
                    return refuse(RecordReject::MatrixSplit);
                if (PassSnapshotsAllowed())
                {
                    g_passMatrices = candidateMatrices;
                    g_passMatricesGeneration =
                        worldvbo::ContextGeneration();
                    g_passMatricesValid = true;
                }
                commitRunMatrices = true;
            }
        }
    }

    unsigned char mirror = 0;
    int rendererType = 1;
    if (!CaptureDeferredCullState(
            mirror, rendererType))
        return refuse(RecordReject::CullState);

    // Stock chrome prepass at the original call, see the notes above. A
    // refusal after this point leaves Gold's chrome table exactly as a stock
    // DrawPoints would (R_StudioChrome is idempotent for a given chromeage
    // stamp), and the immediate path reuses the prepared coordinates.
    if (hasChrome)
    {
        if (!PrepareChromeCoordinates(state, cache, submodel) ||
            g_chromeUvScratch.size() != submodel.vertexCount)
            return refuse(RecordReject::ChromePrepare);
        chromePrepared = true;
    }

    const std::size_t oldCommandCount =
        g_deferredCommands.size();
    const std::size_t oldMeshCount =
        g_deferredMeshes.size();
    const std::size_t oldBoneFloatCount =
        g_deferredBoneFloats.size();
    const std::size_t oldChromeUvCount =
        g_deferredChromeUvs.size();

    DeferredStudioCommand command{};
    command.cache = &cache;
    command.submodel = &submodel;
    command.meshFirst = oldMeshCount;
    command.boneFloatFirst = oldBoneFloatCount;
    command.colorBlend[0] = state.colorMix[0];
    command.colorBlend[1] = state.colorMix[1];
    command.colorBlend[2] = state.colorMix[2];
    command.colorBlend[3] = state.blend;
    command.ambient = state.ambient;
    command.shade = state.shade;
    command.mirror = mirror;
    command.rendererType = rendererType;
    command.segment = g_deferredShadows.size();
    command.hasChrome = hasChrome;
    command.hasMasked = hasMasked;

    try
    {
        if (hasChrome)
        {
            command.chromeUvFirst = oldChromeUvCount;
            command.chromeUvCount = g_chromeUvScratch.size();
            command.chromeFirstVertex = submodel.firstVertex;
            command.chromeStreamBase = chromeStreamBase;
            g_deferredChromeUvs.insert(
                g_deferredChromeUvs.end(),
                g_chromeUvScratch.begin(),
                g_chromeUvScratch.end());
        }
        const std::size_t requiredFloats =
            static_cast<std::size_t>(
                submodel.bonePaletteCount) * 16u;
        g_deferredBoneFloats.resize(
            oldBoneFloatCount + requiredFloats);
        std::size_t packedFloats = 0;
        if (!PackUniformBoneData(
                state, submodel,
                g_deferredBoneFloats.data() +
                    oldBoneFloatCount,
                requiredFloats,
                packedFloats) ||
            packedFloats != requiredFloats)
            throw 1;
        command.boneFloatCount = packedFloats;

        for (std::size_t preparedIndex = 0;
             preparedIndex < g_preparedMeshes.size();)
        {
            const PreparedDirectMesh& prepared =
                g_preparedMeshes[preparedIndex];
            const DirectMaterial& material =
                prepared.material;
            const RetainedSubmesh& mesh =
                *prepared.mesh;
            DeferredMeshDraw draw{};
            draw.firstIndex =
                SelectDrawFirstIndex(mesh, material);
            draw.indexCount = mesh.indexCount;
            draw.glId = material.texture->glId;
            draw.flags = material.flags;
            draw.sScale = material.sScale;
            draw.tScale = material.tScale;
            draw.flatLight = material.flatLight;
            draw.constantLight = material.constantLight;

            std::size_t lastPrepared = preparedIndex;
            while (lastPrepared + 1 <
                   g_preparedMeshes.size())
            {
                const PreparedDirectMesh& next =
                    g_preparedMeshes[lastPrepared + 1];
                if (!next.mesh ||
                    !next.material.directBind ||
                    !next.material.texture ||
                    next.material.texture->glId !=
                        draw.glId ||
                    next.material.flags != draw.flags ||
                    next.material.sScale != draw.sScale ||
                    next.material.tScale != draw.tScale ||
                    next.material.flatLight != draw.flatLight ||
                    next.material.constantLight != draw.constantLight ||
                    SelectDrawFirstIndex(
                        *next.mesh, next.material) !=
                        draw.firstIndex + draw.indexCount ||
                    draw.indexCount >
                        UINT32_MAX -
                            next.mesh->indexCount)
                    break;
                draw.indexCount +=
                    next.mesh->indexCount;
                ++lastPrepared;
            }
            g_deferredMeshes.push_back(draw);
            preparedIndex = lastPrepared + 1;
        }
        command.meshCount =
            g_deferredMeshes.size() -
            command.meshFirst;
        if (command.meshCount == 0)
            throw 1;
        g_deferredCommands.push_back(command);
    }
    catch (...)
    {
        g_deferredCommands.resize(oldCommandCount);
        g_deferredMeshes.resize(oldMeshCount);
        g_deferredBoneFloats.resize(
            oldBoneFloatCount);
        g_deferredChromeUvs.resize(oldChromeUvCount);
        return refuse(RecordReject::Store);
    }

    if (commitRunMatrices)
    {
        g_deferredRunMatrices = candidateMatrices;
        g_deferredRunMatricesValid = true;
    }
    g_deferredLastEntity = state.entity;
    if (hasChrome)
    {
        g_deferredChromeStreamBytes = chromeStreamEnd;
        ++g_deferredChromeRecords;
        if (prof::Active())
            ++g_flushProfile.chromeRecords;
    }
    if (hasMasked)
    {
        g_deferredRunHasMasked = true;
        ++g_deferredMaskedRecords;
        if (prof::Active())
            ++g_flushProfile.maskedRecords;
    }

    // Gold-visible accounting/current immediate state still happens at the
    // original DrawPoints point. Only the GPU submission is deferred.
    for (const PreparedDirectMesh& prepared :
         g_preparedMeshes)
    {
        if (!WriteMeshAccounting(
                prepared.material.flags,
                prepared.mesh->triangleCount))
            break;
    }
    const PreparedDirectMesh& last =
        g_preparedMeshes.back();
    BindGoldTextureSafe(last.material.texture->glId);
    g_deferredEntryTexture = last.material.texture->glId;
    g_deferredEntryTextureValid = true;
    (void)RestoreFinalImmediateState(
        state, cache, *last.mesh, last.material);
    // Stock leaves Gold's post-MASKED state behind (alpha func NOTEQUAL 0,
    // alpha test off, depth writes on). Entry already had alpha test off and
    // depth writes on (MaskedEntryStateReady), only the func can differ.
    if (hasMasked)
        (void)EndMaskedMaterial();

    ++g_deferredRecorded;
    return true;
}

// Orders the accepted packets and assigns their UBO offsets. Without
// instancing the order is the recording order and every packet owns one
// aligned 8 KB slot, exactly as before. With instancing, packets that share
// (retained cache, submodel, mirror/renderer state, per-mesh range, texture
// and material) are bucketed and each bucket is split into chunks that fit
// the instanced block. Chunk members are packed at their palette stride from
// one aligned start, which never needs more space than the per-packet slots
// reserved by DeferredCapacityAvailable.
bool BuildDeferredDrawPlan(bool instancingReady,
                           std::size_t& dataEnd)
{
    dataEnd = g_uniformBufferOffset;
    g_deferredOrder.clear();
    g_deferredBucketHeads.clear();
    g_deferredGroups.clear();
    const std::size_t alignment =
        static_cast<std::size_t>(g_uniformBufferAlignment);
    if (alignment == 0)
        return false;
    const std::size_t commandCount = g_deferredCommands.size();

    try
    {
        g_deferredOrder.reserve(commandCount);
        g_deferredGroups.reserve(commandCount);
        if (!instancingReady)
        {
            for (std::size_t i = 0; i < commandCount; ++i)
                g_deferredOrder.push_back(i);
        }
        else
        {
            // Counting sort by first-seen bucket. Buckets keep their first
            // recording position, members keep their recording order. Every
            // packet here is opaque, depth-written and blend-free, so only
            // equal-depth ties between different entities could observe the
            // changed submission order.
            for (std::size_t i = 0; i < commandCount; ++i)
            {
                DeferredStudioCommand& command = g_deferredCommands[i];
                std::size_t bucket = g_deferredBucketHeads.size();
                for (std::size_t b = 0;
                     b < g_deferredBucketHeads.size(); ++b)
                {
                    if (DeferredCommandsShareInstanceKey(
                            g_deferredCommands[g_deferredBucketHeads[b]],
                            command))
                    {
                        bucket = b;
                        break;
                    }
                }
                if (bucket == g_deferredBucketHeads.size())
                    g_deferredBucketHeads.push_back(i);
                command.bucket = bucket;
            }
            const std::size_t bucketCount = g_deferredBucketHeads.size();
            // Reuse the heads vector as per-bucket write cursors.
            std::vector<std::size_t>& cursor = g_deferredBucketHeads;
            for (std::size_t b = 0; b < bucketCount; ++b)
                cursor[b] = 0;
            for (const DeferredStudioCommand& command : g_deferredCommands)
                ++cursor[command.bucket];
            std::size_t running = 0;
            for (std::size_t b = 0; b < bucketCount; ++b)
            {
                const std::size_t count = cursor[b];
                cursor[b] = running;
                running += count;
            }
            g_deferredOrder.resize(commandCount);
            for (std::size_t i = 0; i < commandCount; ++i)
                g_deferredOrder[cursor[g_deferredCommands[i].bucket]++] = i;
        }
    }
    catch (...)
    {
        return false;
    }

    std::size_t next = g_uniformBufferOffset;
    auto placeSingle = [&](std::size_t orderIndex,
                           std::size_t records) -> bool
    {
        const std::size_t aligned = AlignUp(next, alignment);
        if (aligned > kUniformBufferBytes ||
            kBoneBlockBytes > kUniformBufferBytes - aligned)
            return false;
        g_deferredCommands[g_deferredOrder[orderIndex]].uniformOffset =
            aligned;
        DeferredDrawGroup group{};
        group.orderFirst = orderIndex;
        group.count = 1;
        group.uniformOffset = aligned;
        group.recordsPerInstance = records;
        g_deferredGroups.push_back(group);
        next = aligned + kBoneBlockBytes;
        return true;
    };

    std::size_t orderIndex = 0;
    while (orderIndex < commandCount)
    {
        const DeferredStudioCommand& head =
            g_deferredCommands[g_deferredOrder[orderIndex]];
        if (!head.cache || !head.cache->vertexArray ||
            head.meshCount == 0 ||
            head.meshFirst > g_deferredMeshes.size() ||
            head.meshCount > g_deferredMeshes.size() - head.meshFirst ||
            head.boneFloatFirst > g_deferredBoneFloats.size() ||
            head.boneFloatCount >
                g_deferredBoneFloats.size() - head.boneFloatFirst ||
            head.boneFloatCount == 0 ||
            head.boneFloatCount % kBoneRecordFloats != 0 ||
            head.boneFloatCount * sizeof(float) > kBoneBlockBytes)
            return false;
        const std::size_t records =
            head.boneFloatCount / kBoneRecordFloats;

        std::size_t runEnd = orderIndex + 1;
        if (instancingReady)
        {
            while (runEnd < commandCount &&
                   g_deferredCommands[g_deferredOrder[runEnd]].bucket ==
                       head.bucket)
                ++runEnd;
        }

        std::size_t maxInstances = 1;
        if (instancingReady && g_instancedBlockBytes > 0)
        {
            maxInstances =
                g_instancedBlockBytes / (records * kBoneRecordBytes);
            if (maxInstances > g_instancedCapacity)
                maxInstances = g_instancedCapacity;
        }

        while (orderIndex < runEnd)
        {
            std::size_t count = runEnd - orderIndex;
            if (count > maxInstances)
                count = maxInstances;
            if (count >= 2)
            {
                const std::size_t aligned = AlignUp(next, alignment);
                const std::size_t stride = records * kBoneRecordBytes;
                if (aligned <= kUniformBufferBytes &&
                    g_instancedBlockBytes <=
                        kUniformBufferBytes - aligned)
                {
                    for (std::size_t member = 0; member < count; ++member)
                    {
                        DeferredStudioCommand& command =
                            g_deferredCommands[
                                g_deferredOrder[orderIndex + member]];
                        if (command.boneFloatFirst >
                                g_deferredBoneFloats.size() ||
                            command.boneFloatCount >
                                g_deferredBoneFloats.size() -
                                    command.boneFloatFirst)
                            return false;
                        command.uniformOffset = aligned + member * stride;
                    }
                    DeferredDrawGroup group{};
                    group.orderFirst = orderIndex;
                    group.count = count;
                    group.uniformOffset = aligned;
                    group.recordsPerInstance = records;
                    g_deferredGroups.push_back(group);
                    next = aligned + count * stride;
                    orderIndex += count;
                    continue;
                }
                // The bound instanced range would run past the ring buffer,
                // replay this chunk through ordinary per-packet slots.
            }
            for (std::size_t member = 0; member < count; ++member)
            {
                const DeferredStudioCommand& command =
                    g_deferredCommands[g_deferredOrder[orderIndex]];
                if (command.boneFloatFirst > g_deferredBoneFloats.size() ||
                    command.boneFloatCount >
                        g_deferredBoneFloats.size() - command.boneFloatFirst ||
                    !placeSingle(orderIndex, records))
                    return false;
                ++orderIndex;
            }
        }
    }
    dataEnd = next;
    return true;
}

// Deferred client shadows (r_studio_shadowdefer 1).
//
// CS draws a blob shadow under every player through the engine's
// StudioRenderShadow (client+AFE42 -> hw+0x9D7A0): tri RenderMode
// kRenderTransAlpha (SRC_ALPHA/ONE_MINUS_SRC_ALPHA blend, depth writes off,
// MODULATE, smooth), color (0,0,0,1), the shadow sprite bound on TMU0, one
// quad lying on the floor, then RenderMode kRenderNormal (blend off, depth
// writes on, MODULATE, flat).
//
// Skipping the barrier would not be exact. The quad blends and writes no
// depth, so its pixels depend on what is already in the framebuffer.
// Wherever an opaque packet fragment lies behind the quad but in front of the
// stored depth (a dropped weapon or another player's feet under the slightly
// raised quad, a model below a ledge the quad overhangs), stock darkens that
// packet only if it was drawn before the shadow. Depth testing resolves the
// opposite case (packet in front of the quad) in either order, but not this
// one, so neither "shadow now, earlier packets later" nor "all shadows after
// the run" reproduces stock.
//
// The shadow draw itself is therefore deferred and replayed by the flush at
// its original position between the packets recorded before and after it.
// Packets are opaque and depth-written, shadows only blend, so both reach GL
// in stock order with stock depth contents and the image is unchanged.
// - At the original call the provider still runs, with all four corners at
//   the eye point: a zero-area quad that also lies in front of the near
//   plane of Gold's perspective projection emits no fragment, while every
//   Gold-visible side effect (GL state, Gold's TMU/binding caches, tri API
//   globals, the VGUI2 flush/texture reset of tri Begin) happens exactly
//   where stock has it. The VGUI2 text batch only fills during VGUI paint,
//   after the 3D view, so that flush has nothing to draw here.
// - At replay the flush first returns GL to its barrier-entry state, then
//   puts back what the shadow inherits from its caller as captured at the
//   original call (ShadowEnv, program 0, TMU0, the pass matrices verified
//   at that call) and calls the provider with the original sprite and
//   corners. Everything the shadow leaves behind returns to the
//   barrier-entry values afterwards, including blend func, current color,
//   current texture coordinate and the tri API globals.
// - While a shadow is pending, barriers in front of any other framebuffer
//   draw always flush (PendingRunAdmitsForeignDraw), and instancing never
//   groups packets across a shadow (DeferredStudioCommand::segment).
bool PendingRunAdmitsForeignDraw()
{
    return g_deferredShadows.empty();
}

bool ShadowReplayFunctionsReady()
{
    return g_isEnabled && g_enable && g_disable && g_getIntegerv &&
           g_getFloatv && g_cullFace && g_depthFunc && g_alphaFunc &&
           g_blendFunc && g_color4f && g_texCoord4fv && g_activeTexture &&
           g_useProgram && g_matrixMode && g_pushMatrix && g_popMatrix &&
           g_loadMatrixf;
}

bool CaptureShadowEnv(ShadowEnv& env)
{
    env = {};
    if (!g_isEnabled || !g_getIntegerv || !g_getFloatv)
        return false;
    __try
    {
        env.cull = g_isEnabled(GL_CULL_FACE) != 0 ? 1 : 0;
        env.depthTest = g_isEnabled(GL_DEPTH_TEST) != 0 ? 1 : 0;
        env.alphaTest = g_isEnabled(GL_ALPHA_TEST) != 0 ? 1 : 0;
        env.fog = g_isEnabled(GL_FOG) != 0 ? 1 : 0;
        env.stencil = g_isEnabled(GL_STENCIL_TEST) != 0 ? 1 : 0;
        env.polygonOffset =
            g_isEnabled(GL_POLYGON_OFFSET_FILL) != 0 ? 1 : 0;
        g_getIntegerv(GL_CULL_FACE_MODE, &env.cullMode);
        g_getIntegerv(GL_DEPTH_FUNC, &env.depthFunc);
        g_getIntegerv(GL_ALPHA_TEST_FUNC, &env.alphaFunc);
        g_getFloatv(GL_ALPHA_TEST_REF, &env.alphaRef);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

void SetShadowCap(unsigned cap, unsigned char enabled)
{
    if (enabled)
        g_enable(cap);
    else
        g_disable(cap);
}

// Moves live GL state from `from` to `to`, touching only what differs.
// Called inside the replay's SEH scope.
unsigned TransitionShadowEnv(const ShadowEnv& from, const ShadowEnv& to)
{
    unsigned changes = 0;
    if (from.cull != to.cull)
    {
        SetShadowCap(GL_CULL_FACE, to.cull);
        ++changes;
    }
    if (from.depthTest != to.depthTest)
    {
        SetShadowCap(GL_DEPTH_TEST, to.depthTest);
        ++changes;
    }
    if (from.alphaTest != to.alphaTest)
    {
        SetShadowCap(GL_ALPHA_TEST, to.alphaTest);
        ++changes;
    }
    if (from.fog != to.fog)
    {
        SetShadowCap(GL_FOG, to.fog);
        ++changes;
    }
    if (from.stencil != to.stencil)
    {
        SetShadowCap(GL_STENCIL_TEST, to.stencil);
        ++changes;
    }
    if (from.polygonOffset != to.polygonOffset)
    {
        SetShadowCap(GL_POLYGON_OFFSET_FILL, to.polygonOffset);
        ++changes;
    }
    if (from.cullMode != to.cullMode)
    {
        g_cullFace(static_cast<unsigned>(to.cullMode));
        ++changes;
    }
    if (from.depthFunc != to.depthFunc)
    {
        g_depthFunc(static_cast<unsigned>(to.depthFunc));
        ++changes;
    }
    if (from.alphaFunc != to.alphaFunc || from.alphaRef != to.alphaRef)
    {
        g_alphaFunc(static_cast<unsigned>(to.alphaFunc), to.alphaRef);
        ++changes;
    }
    return changes;
}

bool InvokeShadowProvider(RenderShadowFn provider,
                          int sprite,
                          const float points[4][3])
{
    if (!provider)
        return false;
    // The provider takes non-const pointers; hand it private copies.
    float corners[4][3];
    std::memcpy(corners, points, sizeof(corners));
    __try
    {
        provider(sprite, corners[0], corners[1], corners[2], corners[3]);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// Flush entry state, queried once per flush and handed back to Gold by
// RestoreFlushEntryState.
struct FlushEntryState
{
    int program = 0;
    int vao = 0;
    int activeTexture = static_cast<int>(GL_TEXTURE0);
    int texEnvMode = static_cast<int>(GL_REPLACE);
    int shadeModel = static_cast<int>(GL_FLAT);
    int texture0 = 0;
    bool blend = false;
    int depthWrite = 1;
};

void RestoreFlushEntryState(const FlushEntryState& entry)
{
    __try
    {
        g_bindBuffer(
            GL_UNIFORM_BUFFER,
            static_cast<unsigned>(
                g_genericUniformRestore));
        g_useProgram(
            static_cast<unsigned>(
                entry.program));
        g_bindVertexArray(
            static_cast<unsigned>(
                entry.vao));
        g_activeTexture(GL_TEXTURE0);
        if (entry.texture0 > 0)
            g_goldBind(0, entry.texture0);
        else if (g_deferredEntryTextureValid)
            g_goldBind(0, static_cast<int>(g_deferredEntryTexture));
        g_texEnvi(
            GL_TEXTURE_ENV,
            GL_TEXTURE_ENV_MODE,
            entry.texEnvMode);
        g_shadeModel(
            static_cast<unsigned>(entry.shadeModel));
        if (entry.blend)
            g_enable(GL_BLEND);
        else
            g_disable(GL_BLEND);
        g_depthMask(entry.depthWrite ? 1 : 0);
        if (entry.activeTexture !=
            static_cast<int>(GL_TEXTURE0))
            g_activeTexture(
                static_cast<unsigned>(
                    entry.activeTexture));
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// Replays g_deferredShadows[first, last) at the barrier-entry GL state that
// RestoreFlushEntryState just reinstated. On return the caller must still
// re-establish program, VAO, blend, depth mask, shade model, TMU0 env and
// binding and the active texture: the shadow sets those itself. Everything
// else it touches is put back here. matrixState is per flush: 0 unknown,
// 1 live GL matrices equal the run matrices, 2 they must be loaded.
void ReplayDeferredShadows(std::size_t first,
                           std::size_t last,
                           const FlushEntryState& entry,
                           int& matrixState)
{
    if (first >= last || last > g_deferredShadows.size())
        return;
    const bool collect = prof::Active();
    const long long replayStart = collect ? prof::Now() : 0;

    // Gold's TMU shadow is 0 after RestoreFlushEntryState's Gold bind. Make GL
    // agree before anything is queried, the shadow binds through Gold.
    bool saved = false;
    ShadowEnv live{};
    int blendSrc = static_cast<int>(GL_ONE);
    int blendDst = 0;
    float color[4]{1.0f, 1.0f, 1.0f, 1.0f};
    float texCoord[4]{0.0f, 0.0f, 0.0f, 1.0f};
    std::uint32_t triState[6]{};
    __try
    {
        g_activeTexture(GL_TEXTURE0);
        if (entry.program != 0)
            g_useProgram(0);
        g_getIntegerv(GL_BLEND_SRC, &blendSrc);
        g_getIntegerv(GL_BLEND_DST, &blendDst);
        g_getFloatv(GL_CURRENT_COLOR, color);
        g_getFloatv(GL_CURRENT_TEXTURE_COORDS, texCoord);
        triState[0] = *reinterpret_cast<const std::uint32_t*>(
            g_hwBase + kTriRenderModeRva);
        std::memcpy(&triState[1], g_hwBase + kTriColorRva,
                    4u * sizeof(std::uint32_t));
        triState[5] = *reinterpret_cast<const std::uint32_t*>(
            g_hwBase + kVguiCurrentTextureRva);
        saved = true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        saved = false;
    }
    const bool envKnown = saved && CaptureShadowEnv(live);

    if (matrixState == 0)
    {
        DirectMatrices liveMatrices{};
        matrixState =
            CaptureDirectMatrices(liveMatrices) &&
            std::memcmp(&liveMatrices, &g_deferredRunMatrices,
                        sizeof(liveMatrices)) == 0
                ? 1
                : 2;
    }

    bool matricesLoaded = false;
    int previousMatrixMode = static_cast<int>(GL_MODELVIEW);
    __try
    {
        if (matrixState == 2)
        {
            g_getIntegerv(GL_MATRIX_MODE, &previousMatrixMode);
            g_matrixMode(GL_PROJECTION);
            g_pushMatrix();
            g_loadMatrixf(g_deferredRunMatrices.projection);
            g_matrixMode(GL_TEXTURE);
            g_pushMatrix();
            g_loadMatrixf(g_deferredRunMatrices.texture);
            g_matrixMode(GL_MODELVIEW);
            g_pushMatrix();
            g_loadMatrixf(g_deferredRunMatrices.modelView);
            matricesLoaded = true;
            if (collect)
                ++g_flushProfile.shadowMatrixLoads;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        matricesLoaded = false;
    }

    ShadowEnv current = live;
    for (std::size_t i = first; i < last; ++i)
    {
        const DeferredShadow& shadow = g_deferredShadows[i];
        if (envKnown)
        {
            unsigned changes = 0;
            __try
            {
                changes = TransitionShadowEnv(current, shadow.env);
                current = shadow.env;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
            if (collect)
                g_flushProfile.shadowEnvFixes += changes;
        }
        const bool drawn = InvokeShadowProvider(
            shadow.provider, shadow.sprite, shadow.points);
        if (collect)
        {
            if (drawn)
                ++g_flushProfile.shadowsReplayed;
            else
                ++g_flushProfile.shadowReplayFailures;
        }
    }

    __try
    {
        if (envKnown)
            (void)TransitionShadowEnv(current, live);
        if (matricesLoaded)
        {
            g_matrixMode(GL_MODELVIEW);
            g_popMatrix();
            g_matrixMode(GL_TEXTURE);
            g_popMatrix();
            g_matrixMode(GL_PROJECTION);
            g_popMatrix();
            g_matrixMode(static_cast<unsigned>(previousMatrixMode));
        }
        if (saved)
        {
            // The shadow's GL_Bind left TMU0 active, matching Gold's shadow.
            g_blendFunc(static_cast<unsigned>(blendSrc),
                        static_cast<unsigned>(blendDst));
            g_color4f(color[0], color[1], color[2], color[3]);
            g_texCoord4fv(texCoord);
            *reinterpret_cast<std::uint32_t*>(
                g_hwBase + kTriRenderModeRva) = triState[0];
            std::memcpy(g_hwBase + kTriColorRva, &triState[1],
                        4u * sizeof(std::uint32_t));
            *reinterpret_cast<std::uint32_t*>(
                g_hwBase + kVguiCurrentTextureRva) = triState[5];
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    if (collect)
        g_flushProfile.shadowReplayTicks += prof::Now() - replayStart;
}

// Flush-side material transitions, called inside the flush's SEH scope with
// the packet's VAO bound. The raw-ST attribute is VAO state: CHROME meshes
// point it at the stream image, every other mesh and every later user of the
// VAO (immediate draws) needs the static source back.
void BindChromeStreamRawSt(std::size_t base)
{
    g_bindBuffer(GL_ARRAY_BUFFER, g_chromeStreamBuffer);
    g_vertexAttribPointer(
        kAttribRawST, 2, GL_FLOAT, GL_FALSE_VALUE,
        sizeof(ChromeUv),
        reinterpret_cast<const void*>(base));
}

void BindStaticRawSt(const RetainedCache& cache)
{
    g_bindBuffer(GL_ARRAY_BUFFER, cache.vertexBuffer);
    g_vertexAttribPointer(
        kAttribRawST, 2, GL_SHORT, GL_FALSE_VALUE,
        sizeof(RetainedVertex),
        reinterpret_cast<const void*>(
            offsetof(RetainedVertex, rawST)));
}

// Gold's MASKED state (BeginMaskedMaterial). Depth writes are already forced
// on by the flush.
void BeginFlushMasked()
{
    g_enable(GL_ALPHA_TEST);
    g_alphaFunc(GL_GREATER, 0.5f);
}

void EndFlushMasked(bool alphaTest, int alphaFunc, float alphaRef)
{
    g_alphaFunc(static_cast<unsigned>(alphaFunc), alphaRef);
    if (!alphaTest)
        g_disable(GL_ALPHA_TEST);
}

bool FlushDeferredSolidCommandsImpl()
{
    if (g_deferredFlushInProgress)
        return false;
    if (!g_useUniformBuffer ||
        !g_uniformBufferReady ||
        !g_program ||
        !g_useProgram ||
        !g_uniform4fv ||
        !g_uniformMatrix4fv ||
        !g_bindVertexArray ||
        !g_bindBuffer ||
        !g_bindBufferRange ||
        !g_bufferSubData ||
        !g_getIntegerv ||
        !g_activeTexture ||
        !g_texEnvi ||
        !g_getTexEnviv ||
        !g_shadeModel ||
        !g_goldBind ||
        !g_drawElements ||
        !g_isEnabled ||
        !g_enable ||
        !g_disable ||
        !g_depthMask)
        return false;
    if (!g_deferredRunMatricesValid)
        return false;

    UniformBufferFrame& frame =
        g_uniformBuffers[g_uniformBufferFrame];
    if (!frame.buffer)
        return false;

    // Preflight every UBO range before Gold has any chance to lose an
    // accepted deferred packet. RecordDeferredDirectDraw already reserves
    // capacity, this protects against state drift and keeps the staging loop
    // free of predictable early exits.
    std::size_t nextUniformOffset = g_uniformBufferOffset;
    if (!BuildDeferredDrawPlan(
            EnsureInstancedProgram(), nextUniformOffset) &&
        !BuildDeferredDrawPlan(false, nextUniformOffset))
        return false;
    if (g_deferredGroups.empty())
        return false;

    // Only state this flush changes and whose previous value is not already
    // known is read back here:
    // - ARRAY_BUFFER is only bound for CHROME packets (read below in that
    //   case), ELEMENT_ARRAY_BUFFER is VAO state and returns with the
    //   previous VAO.
    // - Every recorded packet uses Gold's ordinary renderer type, which has no
    //   cull transition, so cull face/mode are never touched.
    // - The reserved indexed UBO point and the generic UBO target are
    //   module-owned (see g_lastUniformBlock*).
    // - The active TMU comes from Gold's own shadow, and TMU0 ends bound to
    //   the texture the last recorded DrawPoints left bound.
    // Program, VAO, TMU0 env mode and shade model can be changed by Gold or
    // foreign code between packets and stay queried, once per flush.
    int previousProgram = 0;
    int previousVao = 0;
    int previousActiveTexture =
        static_cast<int>(GL_TEXTURE0);
    int previousTexEnvMode =
        static_cast<int>(GL_REPLACE);
    int previousShadeModel =
        static_cast<int>(GL_FLAT);
    // Recorded packets are opaque. A flush can run after Gold has already
    // switched to a blended pass (e.g. the glow-shell ONE/ONE pass when the
    // base-pass boundary flush could not submit), so the draws force opaque
    // blend/depth-write state and restore the caller's state afterwards.
    bool previousBlend = false;
    int previousDepthWrite = 1;
    // TMU0 binding at flush entry. Right after a record it is the texture
    // that packet left bound. Order-independent stock or immediate draws may
    // have run since (see the skipped barriers), so the live binding is the
    // value to hand back to Gold.
    int previousTexture0 = 0;
    // CHROME packets rebind GL_ARRAY_BUFFER to the chrome stream and back to
    // their cache VBO, MASKED packets change the alpha test. Both are read
    // only when the run carries such a packet and are restored on exit.
    const bool runHasChrome = g_deferredChromeStreamBytes != 0;
    const bool runHasMasked = g_deferredRunHasMasked;
    int previousArrayBuffer = 0;
    bool previousAlphaTest = false;
    int previousAlphaFunc = static_cast<int>(GL_NOTEQUAL);
    float previousAlphaRef = 0.0f;
    if (runHasChrome &&
        (!g_chromeStreamBuffer ||
         g_chromeStreamGeneration != worldvbo::ContextGeneration() ||
         !g_bufferData || !g_vertexAttribPointer ||
         g_deferredChromeStreamBytes > kChromeStreamMaxBytes))
        return false;
    if (runHasMasked && (!g_alphaFunc || !g_getFloatv))
        return false;
    if (!ReadActiveTexture(previousActiveTexture))
        return false;
    __try
    {
        if (runHasChrome)
            g_getIntegerv(
                GL_ARRAY_BUFFER_BINDING, &previousArrayBuffer);
        if (runHasMasked)
        {
            previousAlphaTest = g_isEnabled(GL_ALPHA_TEST) != 0;
            g_getIntegerv(GL_ALPHA_TEST_FUNC, &previousAlphaFunc);
            g_getFloatv(GL_ALPHA_TEST_REF, &previousAlphaRef);
        }
        g_getIntegerv(
            GL_CURRENT_PROGRAM, &previousProgram);
        g_getIntegerv(
            GL_VERTEX_ARRAY_BINDING, &previousVao);
        if (previousActiveTexture !=
            static_cast<int>(GL_TEXTURE0))
            g_activeTexture(GL_TEXTURE0);
        g_getTexEnviv(
            GL_TEXTURE_ENV,
            GL_TEXTURE_ENV_MODE,
            &previousTexEnvMode);
        g_getIntegerv(
            GL_SHADE_MODEL,
            &previousShadeModel);
        g_getIntegerv(
            GL_TEXTURE_BINDING_2D,
            &previousTexture0);
        previousBlend = g_isEnabled(GL_BLEND) != 0;
        g_getIntegerv(
            GL_DEPTH_WRITEMASK,
            &previousDepthWrite);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        __try
        {
            if (previousActiveTexture != static_cast<int>(GL_TEXTURE0))
                g_activeTexture(static_cast<unsigned>(previousActiveTexture));
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        return false;
    }
    FlushEntryState entry{};
    entry.program = previousProgram;
    entry.vao = previousVao;
    entry.activeTexture = previousActiveTexture;
    entry.texEnvMode = previousTexEnvMode;
    entry.shadeModel = previousShadeModel;
    entry.texture0 = previousTexture0;
    entry.blend = previousBlend;
    entry.depthWrite = previousDepthWrite;
    // Deferred client shadows replayed so far, see ReplayDeferredShadows.
    std::size_t nextShadow = 0;
    int shadowMatrixState = 0;

    bool submitted = false;
    bool rangeBound = false;
    std::size_t boundRangeOffset = 0;
    std::size_t boundRangeSize = 0;
    // VAO whose raw-ST attribute currently points at the chrome stream, and
    // whether Gold's MASKED alpha test is applied. Both are undone at the end
    // of every packet, before shadow replay and on a faulted submission.
    const RetainedCache* chromeCache = nullptr;
    bool maskedActive = false;
    g_deferredFlushInProgress = true;
    __try
    {
        // csgl3-style ordering: stage every constant block first. No draw is
        // interleaved with a buffer write. With the persistent ring the blocks
        // are copied straight into the coherent mapping (the planned ranges
        // lie past every offset already used this frame, and the region's
        // fence was retired at rotation). Otherwise prefer one mapped range
        // for the whole deferred run, this turns N driver uploads into one
        // map/unmap while keeping fixed UBO offsets for every planned draw.
        bool batchUploaded = false;
        bool persistentUploaded = false;
        if (g_uniformPersistentBase)
        {
            std::uint8_t* const region =
                g_uniformPersistentBase + frame.base;
            bool copyOk = true;
            for (const DeferredStudioCommand& command :
                 g_deferredCommands)
            {
                const std::size_t bytes =
                    command.boneFloatCount * sizeof(float);
                if (command.uniformOffset > kUniformBufferBytes ||
                    bytes > kUniformBufferBytes - command.uniformOffset)
                {
                    copyOk = false;
                    break;
                }
                std::memcpy(
                    region + command.uniformOffset,
                    g_deferredBoneFloats.data() +
                        command.boneFloatFirst,
                    bytes);
            }
            if (!copyOk)
                __leave;
            persistentUploaded = true;
            if (prof::Active())
                g_uniformPersistentWrites += g_deferredCommands.size();
        }
        else
        {
            g_bindBuffer(
                GL_UNIFORM_BUFFER, frame.buffer);
        }
        const std::size_t mapStart =
            g_deferredGroups.front().uniformOffset;
        const std::size_t mapEnd = nextUniformOffset;
        if (!persistentUploaded &&
            g_mapBufferRange && g_unmapBuffer &&
            mapEnd >= mapStart &&
            mapEnd <= kUniformBufferBytes)
        {
            const std::size_t mapBytes = mapEnd - mapStart;
            void* mapped = g_mapBufferRange(
                GL_UNIFORM_BUFFER,
                static_cast<std::ptrdiff_t>(mapStart),
                static_cast<std::ptrdiff_t>(mapBytes),
                // The buffer is orphaned once per frame and ranges are
                // never rewritten within a frame, so no GPU sync is needed.
                GL_MAP_WRITE_BIT |
                    GL_MAP_INVALIDATE_RANGE_BIT |
                    GL_MAP_UNSYNCHRONIZED_BIT);
            if (mapped)
            {
                bool copyOk = true;
                for (const DeferredStudioCommand& command :
                     g_deferredCommands)
                {
                    const std::size_t bytes =
                        command.boneFloatCount *
                        sizeof(float);
                    if (command.uniformOffset < mapStart)
                    {
                        copyOk = false;
                        break;
                    }
                    const std::size_t relative =
                        command.uniformOffset - mapStart;
                    if (relative > mapBytes ||
                        bytes > mapBytes - relative)
                    {
                        copyOk = false;
                        break;
                    }
                    std::memcpy(
                        static_cast<std::uint8_t*>(mapped) + relative,
                        g_deferredBoneFloats.data() +
                            command.boneFloatFirst,
                        bytes);
                }
                const bool unmapped =
                    g_unmapBuffer(GL_UNIFORM_BUFFER) != 0;
                batchUploaded = copyOk && unmapped;
            }
        }
        if (!persistentUploaded && !batchUploaded)
        {
            ++g_deferredSubDataUploads;
            for (DeferredStudioCommand& command :
                 g_deferredCommands)
            {
                const std::size_t bytes =
                    command.boneFloatCount *
                    sizeof(float);
                g_bufferSubData(
                    GL_UNIFORM_BUFFER,
                    static_cast<std::ptrdiff_t>(
                        command.uniformOffset),
                    static_cast<std::ptrdiff_t>(
                        bytes),
                    g_deferredBoneFloats.data() +
                        command.boneFloatFirst);
            }
        }
        else if (batchUploaded)
        {
            ++g_deferredMappedUploads;
        }
        g_uniformBufferOffset = nextUniformOffset;

        // CHROME coordinates, staged like the bone blocks before any draw:
        // orphan the stream storage, then copy each packet's range to its
        // planned offset (see DeferredStudioCommand::chromeStreamBase).
        if (runHasChrome)
        {
            g_bindBuffer(GL_ARRAY_BUFFER, g_chromeStreamBuffer);
            g_bufferData(
                GL_ARRAY_BUFFER,
                static_cast<std::ptrdiff_t>(g_deferredChromeStreamBytes),
                nullptr,
                GL_STREAM_DRAW);
            for (const DeferredStudioCommand& command : g_deferredCommands)
            {
                if (!command.hasChrome)
                    continue;
                const std::size_t offset =
                    command.chromeStreamBase +
                    command.chromeFirstVertex * sizeof(ChromeUv);
                const std::size_t bytes =
                    command.chromeUvCount * sizeof(ChromeUv);
                if (command.chromeUvFirst > g_deferredChromeUvs.size() ||
                    command.chromeUvCount >
                        g_deferredChromeUvs.size() - command.chromeUvFirst ||
                    offset > g_deferredChromeStreamBytes ||
                    bytes > g_deferredChromeStreamBytes - offset)
                    __leave;
                if (bytes == 0)
                    continue;
                g_bufferSubData(
                    GL_ARRAY_BUFFER,
                    static_cast<std::ptrdiff_t>(offset),
                    static_cast<std::ptrdiff_t>(bytes),
                    g_deferredChromeUvs.data() + command.chromeUvFirst);
            }
            if (prof::Active())
                g_flushProfile.chromeStreamBytes +=
                    g_deferredChromeStreamBytes;
        }

        g_useProgram(g_program);
        if (g_gammaFitProgramRevision !=
            g_gammaFitRevision)
        {
            g_uniform4fv(
                g_gammaFit0Location, 1,
                g_gammaFit0);
            g_uniform4fv(
                g_gammaFit1Location, 1,
                g_gammaFit1);
            g_gammaFitProgramRevision =
                g_gammaFitRevision;
        }
        if (!UploadDirectMatrices(g_deferredRunMatrices))
            __leave;
        g_shadeModel(GL_SMOOTH);
        if (previousBlend)
            g_disable(GL_BLEND);
        if (!previousDepthWrite)
            g_depthMask(1);
        g_texEnvi(
            GL_TEXTURE_ENV,
            GL_TEXTURE_ENV_MODE,
            static_cast<int>(GL_MODULATE));

        unsigned boundVao = UINT_MAX;
        unsigned boundTexture = UINT_MAX;
        unsigned activeProgram = g_program;
        float lastColorBlend[4]{};
        float lastParams[4]{};
        bool colorBlendValid = false;
        bool paramsValid = false;
        bool instancedMatricesReady = false;
        float instancedLayout[4]{-1.0f, 0.0f, 0.0f, 0.0f};
        float instanceParams[kInstanceCap][4]{};
        float instanceColors[kInstanceCap][4]{};
        for (const DeferredDrawGroup& group : g_deferredGroups)
        {
            const DeferredStudioCommand& command =
                g_deferredCommands[g_deferredOrder[group.orderFirst]];

            // Client shadows recorded before this group's packets reach GL
            // here, in stock order. Groups never span a shadow and their
            // segments never decrease (see BuildDeferredDrawPlan).
            if (nextShadow < command.segment &&
                nextShadow < g_deferredShadows.size())
            {
                const std::size_t shadowEnd =
                    (std::min)(command.segment,
                               g_deferredShadows.size());
                // GL_ARRAY_BUFFER is not VAO state. The shadow's triangle
                // API draw must see the binding it had at its original call.
                if (runHasChrome)
                    g_bindBuffer(
                        GL_ARRAY_BUFFER,
                        static_cast<unsigned>(previousArrayBuffer));
                RestoreFlushEntryState(entry);
                ReplayDeferredShadows(
                    nextShadow, shadowEnd, entry, shadowMatrixState);
                nextShadow = shadowEnd;
                g_activeTexture(GL_TEXTURE0);
                g_useProgram(g_program);
                g_shadeModel(GL_SMOOTH);
                g_disable(GL_BLEND);
                g_depthMask(1);
                g_texEnvi(
                    GL_TEXTURE_ENV,
                    GL_TEXTURE_ENV_MODE,
                    static_cast<int>(GL_MODULATE));
                activeProgram = g_program;
                boundVao = UINT_MAX;
                boundTexture = UINT_MAX;
            }

            if (group.count >= 2)
            {
                const int instanceCount =
                    static_cast<int>(group.count);
                if (activeProgram != g_instancedProgram)
                {
                    g_useProgram(g_instancedProgram);
                    activeProgram = g_instancedProgram;
                }
                if (g_instancedGammaFitRevision !=
                    g_gammaFitRevision)
                {
                    g_uniform4fv(
                        g_instancedGammaFit0Location, 1,
                        g_gammaFit0);
                    g_uniform4fv(
                        g_instancedGammaFit1Location, 1,
                        g_gammaFit1);
                    g_instancedGammaFitRevision =
                        g_gammaFitRevision;
                }
                if (!instancedMatricesReady)
                {
                    g_uniformMatrix4fv(
                        g_instancedModelViewLocation, 1,
                        GL_FALSE_VALUE,
                        g_deferredRunMatrices.modelView);
                    g_uniformMatrix4fv(
                        g_instancedProjectionLocation, 1,
                        GL_FALSE_VALUE,
                        g_deferredRunMatrices.projection);
                    g_uniformMatrix4fv(
                        g_instancedTextureMatrixLocation, 1,
                        GL_FALSE_VALUE,
                        g_deferredRunMatrices.texture);
                    instancedMatricesReady = true;
                }
                const float records =
                    static_cast<float>(group.recordsPerInstance);
                if (instancedLayout[0] != records)
                {
                    instancedLayout[0] = records;
                    g_uniform4fv(
                        g_instancedLayoutLocation, 1,
                        instancedLayout);
                }

                g_bindBufferRange(
                    GL_UNIFORM_BUFFER,
                    g_boneBlockBinding,
                    frame.buffer,
                    static_cast<std::ptrdiff_t>(
                        frame.base + group.uniformOffset),
                    static_cast<std::ptrdiff_t>(
                        g_instancedBlockBytes));
                rangeBound = true;
                boundRangeOffset = frame.base + group.uniformOffset;
                boundRangeSize = g_instancedBlockBytes;

                for (std::size_t member = 0;
                     member < group.count; ++member)
                {
                    std::memcpy(
                        instanceColors[member],
                        g_deferredCommands[
                            g_deferredOrder[
                                group.orderFirst + member]].colorBlend,
                        sizeof(instanceColors[member]));
                }
                g_uniform4fv(
                    g_instancedColorBlendLocation,
                    instanceCount,
                    &instanceColors[0][0]);

                if (boundVao != command.cache->vertexArray)
                {
                    g_bindVertexArray(
                        command.cache->vertexArray);
                    boundVao = command.cache->vertexArray;
                }

                for (std::size_t meshIndex = 0;
                     meshIndex < command.meshCount;
                     ++meshIndex)
                {
                    const DeferredMeshDraw& draw =
                        g_deferredMeshes[
                            command.meshFirst + meshIndex];
                    if (boundTexture != draw.glId)
                    {
                        g_goldBind(0, draw.glId);
                        boundTexture = draw.glId;
                    }
                    // Texture scale and FLATSHADE are bucket-wide. Lighting
                    // (ambient/shade or the sampled flat light) stays per
                    // entity, exactly as the single-packet path encodes it.
                    for (std::size_t member = 0;
                         member < group.count; ++member)
                    {
                        const DeferredStudioCommand& item =
                            g_deferredCommands[
                                g_deferredOrder[
                                    group.orderFirst + member]];
                        const DeferredMeshDraw& itemDraw =
                            g_deferredMeshes[
                                item.meshFirst + meshIndex];
                        const bool flat = itemDraw.constantLight;
                        instanceParams[member][0] =
                            flat ? -itemDraw.sScale : itemDraw.sScale;
                        instanceParams[member][1] = itemDraw.tScale;
                        instanceParams[member][2] =
                            flat ? itemDraw.flatLight : item.ambient;
                        instanceParams[member][3] =
                            flat ? 0.0f : item.shade;
                    }
                    g_uniform4fv(
                        g_instancedParamsLocation,
                        instanceCount,
                        &instanceParams[0][0]);
                    g_drawElementsInstanced(
                        GL_TRIANGLES,
                        static_cast<int>(draw.indexCount),
                        RetainedIndexType(*command.cache),
                        reinterpret_cast<const void*>(
                            static_cast<std::uintptr_t>(
                                draw.firstIndex) *
                            RetainedIndexBytes(*command.cache)),
                        instanceCount);
                    ++g_instancedDrawCalls;
                }
                g_instancedEntities += group.count;
                g_instancedSavedDraws +=
                    (group.count - 1u) * command.meshCount;
                continue;
            }

            if (activeProgram != g_program)
            {
                g_useProgram(g_program);
                activeProgram = g_program;
            }
            g_bindBufferRange(
                GL_UNIFORM_BUFFER,
                g_boneBlockBinding,
                frame.buffer,
                static_cast<std::ptrdiff_t>(
                    frame.base + command.uniformOffset),
                static_cast<std::ptrdiff_t>(
                    kBoneBlockBytes));
            rangeBound = true;
            boundRangeOffset = frame.base + command.uniformOffset;
            boundRangeSize = kBoneBlockBytes;
            if (!colorBlendValid ||
                std::memcmp(lastColorBlend, command.colorBlend,
                            sizeof(lastColorBlend)) != 0)
            {
                g_uniform4fv(
                    g_colorBlendLocation, 1,
                    command.colorBlend);
                std::memcpy(lastColorBlend, command.colorBlend,
                            sizeof(lastColorBlend));
                colorBlendValid = true;
            }
            else
            {
                ++g_deferredColorUniformSkips;
            }

            if (boundVao !=
                command.cache->vertexArray)
            {
                g_bindVertexArray(
                    command.cache->vertexArray);
                boundVao =
                    command.cache->vertexArray;
            }

            for (std::size_t meshIndex = 0;
                 meshIndex < command.meshCount;
                 ++meshIndex)
            {
                const DeferredMeshDraw& draw =
                    g_deferredMeshes[
                        command.meshFirst +
                        meshIndex];
                if (boundTexture != draw.glId)
                {
                    g_goldBind(0, draw.glId);
                    boundTexture = draw.glId;
                }
                const float params[4] = {
                    draw.constantLight
                        ? -draw.sScale
                        : draw.sScale,
                    draw.tScale,
                    draw.constantLight
                        ? draw.flatLight
                        : command.ambient,
                    draw.constantLight
                        ? 0.0f
                        : command.shade
                };
                if (!paramsValid ||
                    std::memcmp(lastParams, params,
                                sizeof(lastParams)) != 0)
                {
                    g_uniform4fv(
                        g_paramsLocation, 1,
                        params);
                    std::memcpy(lastParams, params,
                                sizeof(lastParams));
                    paramsValid = true;
                }
                else
                {
                    ++g_deferredParamsUniformSkips;
                }
                const bool chromeDraw = (draw.flags & 0x02u) != 0u;
                const bool maskedDraw = (draw.flags & 0x40u) != 0u;
                if (chromeDraw && !chromeCache)
                {
                    BindChromeStreamRawSt(command.chromeStreamBase);
                    chromeCache = command.cache;
                }
                else if (!chromeDraw && chromeCache)
                {
                    BindStaticRawSt(*chromeCache);
                    chromeCache = nullptr;
                }
                if (maskedDraw && !maskedActive)
                {
                    BeginFlushMasked();
                    maskedActive = true;
                }
                else if (!maskedDraw && maskedActive)
                {
                    EndFlushMasked(
                        previousAlphaTest, previousAlphaFunc,
                        previousAlphaRef);
                    maskedActive = false;
                }
                g_drawElements(
                    GL_TRIANGLES,
                    static_cast<int>(
                        draw.indexCount),
                    RetainedIndexType(*command.cache),
                    reinterpret_cast<const void*>(
                        static_cast<std::uintptr_t>(
                            draw.firstIndex) *
                        RetainedIndexBytes(*command.cache)));
            }
            // Packet end: the VAO's static raw-ST source and the flush-entry
            // alpha test come back before the next packet or shadow.
            if (chromeCache)
            {
                BindStaticRawSt(*chromeCache);
                chromeCache = nullptr;
            }
            if (maskedActive)
            {
                EndFlushMasked(
                    previousAlphaTest, previousAlphaFunc,
                    previousAlphaRef);
                maskedActive = false;
            }
        }
        submitted = true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        submitted = false;
    }

    // A faulted submission may have left a packet's material state applied.
    if (chromeCache)
    {
        __try
        {
            g_bindVertexArray(chromeCache->vertexArray);
            BindStaticRawSt(*chromeCache);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        chromeCache = nullptr;
    }
    if (maskedActive)
    {
        __try
        {
            EndFlushMasked(
                previousAlphaTest, previousAlphaFunc, previousAlphaRef);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        maskedActive = false;
    }
    if (runHasChrome)
    {
        __try
        {
            g_bindBuffer(
                GL_ARRAY_BUFFER,
                static_cast<unsigned>(previousArrayBuffer));
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    RestoreFlushEntryState(entry);
    // Shadows recorded after the last packet, or not yet replayed because
    // the submission faulted, are drawn at the restored barrier state and
    // the state they leave behind is restored once more.
    if (nextShadow < g_deferredShadows.size())
    {
        ReplayDeferredShadows(
            nextShadow, g_deferredShadows.size(), entry,
            shadowMatrixState);
        RestoreFlushEntryState(entry);
    }

    if (rangeBound)
        NoteOwnedUniformRange(
            frame.buffer, boundRangeOffset, boundRangeSize);
    g_deferredFlushInProgress = false;

    if (submitted)
        g_deferredFlushed +=
            g_deferredCommands.size();
    else
        rendererlog::Line(
            "studio renderer: deferred solid replay failed after Gold accepted %u packet(s)",
            static_cast<unsigned>(
                g_deferredCommands.size()));

    if (prof::Active())
    {
        g_flushProfile.groups += g_deferredGroups.size();
        for (const DeferredDrawGroup& group : g_deferredGroups)
        {
            if (group.count >= 2)
                ++g_flushProfile.instancedGroups;
        }
    }

    g_deferredCommands.clear();
    g_deferredShadows.clear();
    g_deferredMeshes.clear();
    g_deferredBoneFloats.clear();
    g_deferredChromeUvs.clear();
    g_deferredChromeStreamBytes = 0;
    g_deferredRunHasMasked = false;
    g_deferredOrder.clear();
    g_deferredGroups.clear();
    g_deferredRunMatricesValid = false;
    g_deferredLastEntity = nullptr;
    g_deferredEntryTextureValid = false;
    g_entityUniformKeyValid = false;
    g_colorBlendUniformValid = false;
    g_paramsUniformValid = false;
    return submitted;
}

bool CurrentEntityIsPlayer()
{
    if (!g_hwBase)
        return false;
    __try
    {
        const std::uint8_t* const entity =
            *reinterpret_cast<std::uint8_t* const*>(
                g_hwBase + kCurrentEntityRva);
        return entity &&
               *reinterpret_cast<const int*>(entity + 4) != 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// A barrier that was proven order-independent for the pending run.
void NoteBarrierSkip(FlushTrigger trigger)
{
    if (g_deferredCommands.empty())
        return;
    g_deferredRunMatricesVerify = true;
    if (prof::Active())
        ++g_flushProfile.siteSkips[static_cast<unsigned>(trigger)];
}

// Flushes from inside a Studio draw (retained kernel, client shadow provider,
// glow-shell base pass) of a player already run inside studio.cpp's timed
// StudioDrawPlayer. Every other flush is otherwise invisible to the Studio
// phase and is charged here while profiling.
bool FlushDeferredSolidCommands(FlushSite site, FlushTrigger trigger)
{
    if (g_deferredCommands.empty())
    {
        g_deferredRunMatricesValid = false;
        g_deferredLastEntity = nullptr;
        g_deferredEntryTextureValid = false;
        return true;
    }
    if (!prof::Active())
        return FlushDeferredSolidCommandsImpl();

    const std::uint64_t records = g_deferredCommands.size();
    const long long start = prof::Now();
    const bool flushed = FlushDeferredSolidCommandsImpl();
    const long long elapsed = prof::Now() - start;
    g_profiledFlushTicks += elapsed;
    ++g_profiledFlushCount;
    ++g_flushProfile.flushes;
    ++g_flushProfile.siteFlushes[static_cast<unsigned>(trigger)];
    g_flushProfile.records += records;
    if (records > g_flushProfile.maxRecords)
        g_flushProfile.maxRecords = records;
    g_flushProfile.ticks += elapsed;
    const bool alreadyTimed =
        site == FlushSite::InsideStudioDraw &&
        CurrentEntityIsPlayer();
    if (!alreadyTimed)
    {
        prof::AddStudio(elapsed);
        g_flushProfile.chargedTicks += elapsed;
    }
    return flushed;
}

bool TryDirectDraw(void* wrapperCaller)
{
    ++g_directAttempts;
    const bool directClientCaller =
        g_clientBase &&
        wrapperCaller ==
            g_clientBase + kClientNormalDrawReturnRva;
    const bool fastBatchBridge =
        studio_drawbatch::RetainedDirectScopeAllowed();
    if (!directClientCaller && !fastBatchBridge)
        return DirectFallback(DirectFallbackReason::Caller);

    DirectGlobals state{};
    if (!ReadDirectGlobals(state))
        return GlobalFallback(GlobalRejectReason::ReadGlobals);
    const bool nonPlayerPhase1 = state.player == 0;
    if (nonPlayerPhase1)
    {
        // Opaque StudioDrawModel retention is still experimental.  Keep the
        // production-proven player renderer independent until cross-DrawModel
        // batching is fast enough to justify widening default coverage.
        if (ReadNonPlayerMode() != 1)
            return GlobalFallback(GlobalRejectReason::NotPlayer);
        ++g_nonPlayerAttempts;
        // Phase 1 owns only ordinary opaque StudioDrawModel calls in the solid
        // entity pass. Viewmodels and translucent entities are drawn outside
        // it. FOLLOW/aiment entities and renderfx other than the glow shell
        // are admitted, see DrawModelEntryCompatible.
        NonPlayerReject reject = NonPlayerReject::Count;
        if (g_solidEntityPassDepth <= 0)
            reject = NonPlayerReject::OutsidePass;
        else if (g_nativeOpaqueDrawModelDepth <= 0)
            reject = NonPlayerReject::DrawModelScope;
        else if (state.renderFx == 19)
            reject = NonPlayerReject::GlowShell;
        else if (!g_clientStudioShadowBarrierReady ||
                 !ReadExactRDrawEntitiesNormal() ||
                 !state.entityModel)
            reject = NonPlayerReject::Other;
        if (reject != NonPlayerReject::Count)
        {
            if (prof::Active())
                ++g_nonPlayerReject[static_cast<unsigned>(reject)];
            return GlobalFallback(GlobalRejectReason::NotPlayer);
        }
    }
    if (state.renderMode != 0)
        return GlobalFallback(GlobalRejectReason::RenderMode);
    if (state.setupRenderMode != 0)
    {
        if (prof::Active())
        {
            if (state.setupRenderMode > 0 && state.setupRenderMode < 16)
                ++g_setupModeRejectValues[state.setupRenderMode];
            else
                ++g_setupModeRejectOther;
        }
        return GlobalFallback(GlobalRejectReason::SetupRenderMode);
    }
    if (!nonPlayerPhase1 && state.renderFx == 19)
        return GlobalFallback(GlobalRejectReason::GlowShell);
    if (state.forceFlags != 0)
        return GlobalFallback(GlobalRejectReason::ForceFlags);
    if (state.numStudioLights != 0)
        return GlobalFallback(GlobalRejectReason::StudioLights);
    if (state.debugStudioMode >= 2)
        return GlobalFallback(GlobalRejectReason::DebugMode);
    if (state.skin < 0)
        return GlobalFallback(GlobalRejectReason::Skin);
    if (state.boneCount <= 0 || state.boneCount > 128)
        return GlobalFallback(GlobalRejectReason::BoneCount);
    if (!studio_drawbatch::ShadowWillBeSkipped())
        return GlobalFallback(GlobalRejectReason::Shadow);

    const auto negativeIt = g_negativeCacheRetry.find(state.header);
    if (negativeIt != g_negativeCacheRetry.end())
    {
        if (g_directAttempts < negativeIt->second)
            return DirectFallback(DirectFallbackReason::NegativeCache);
        g_negativeCacheRetry.erase(negativeIt);
    }

    RetainedCache* cache = ResolveCurrentCache();
    if (!cache)
    {
        g_negativeCacheRetry[state.header] =
            g_directAttempts + kNegativeCacheRetryAttempts;
        return DirectFallback(DirectFallbackReason::CacheResolve);
    }
    g_negativeCacheRetry.erase(state.header);
    if (nonPlayerPhase1 &&
        cache->ownerModel != state.entityModel)
        return DirectFallback(DirectFallbackReason::CacheResolve);
    auto submodelIt =
        cache->submodelLookup.find(state.currentModel);
    if (submodelIt == cache->submodelLookup.end())
        return DirectFallback(DirectFallbackReason::Submodel);
    RetainedSubmodel& submodel =
        cache->submodels[submodelIt->second];

    if (!g_textureHeader)
        return DirectFallback(DirectFallbackReason::TextureHeader);
    StudioHeaderRaw* textureHeader = ResolveTextureHeaderSafe();
    if (!textureHeader ||
        textureHeader->id != 0x54534449 ||
        textureHeader->version != 10 ||
        textureHeader->length <
            static_cast<int>(sizeof(StudioHeaderRaw)))
        return DirectFallback(DirectFallbackReason::TextureHeader);

    if (submodel.meshes.empty())
    {
        // The stock blank-bodygroup fallback is evaluated by the fallback
        // barrier in DirectKernelDispatch like any other stock DrawPoints.
        if (nonPlayerPhase1)
            return DirectFallback(DirectFallbackReason::Submodel);
        const StudioModelRaw* blank = submodel.source;
        if (!blank || blank->nummesh != 0 || blank->numverts != 0 ||
            blank->numnorms != 0)
            return DirectFallback(DirectFallbackReason::Submodel);

        // Exact ordinary-renderer blank bodygroup: Gold's inner DrawPoints
        // skips vertex/light/material/emitter work, but still evaluates the
        // mirror predicate and stores mirrorNormal before the empty emitter.
        unsigned char mirror = 0;
        int rendererType = 1;
        if (!CaptureDeferredCullState(mirror, rendererType))
            return DirectFallback(DirectFallbackReason::Submodel);
        ++g_directDraws;
        return true;
    }

    int skinFamily = 0;
    if (state.skin != 0 &&
        state.skin < textureHeader->numskinfamilies)
        skinFamily = state.skin;
    if (skinFamily < 0)
        return DirectFallback(DirectFallbackReason::Skin);

    // Full preflight happens before the first direct submission. Prepare the
    // exact material decision once and reuse it in submission.
    g_preparedMeshes.clear();
    try
    {
        if (g_preparedMeshes.capacity() < submodel.meshes.size())
            g_preparedMeshes.reserve(submodel.meshes.size());
    }
    catch (...)
    {
        return MaterialFallback(MaterialReject::Reserve);
    }
    bool flatLightKnown[2]{};
    float flatLightValue[2] = {1.0f, 1.0f};
    bool hasChrome = false;
    bool hasMasked = false;
    bool hasAdditive = false;
    for (const RetainedSubmesh& mesh : submodel.meshes)
    {
        DirectMaterial material{};
        MaterialReject materialReject = MaterialReject::Count;
        if (!ResolveDirectMaterial(
                textureHeader, skinFamily, mesh, material, materialReject))
            return MaterialFallback(materialReject);
        if (mesh.indexCount == 0 ||
            mesh.indexCount > static_cast<std::uint32_t>(INT_MAX))
            return MaterialFallback(MaterialReject::IndexCount);
        if (mesh.lastVertex >= cache->vertices.size())
            return MaterialFallback(MaterialReject::LastVertex);
        // Gold's R_StudioLighting (hw+0x96B50) returns before the FLATSHADE
        // and normal paths when the mesh is FULLBRIGHT and the current entity
        // is not a player: it stores exactly 1.0f and skips the lightgamma
        // table lookup. DrawPoints then colors with colormix * 1.0, alpha
        // r_blend, the same as the shader's constant-light path. The value
        // is sampled from the stock function below, so it is that 1.0 bit
        // for bit, not an assumption. Players never take the shortcut and
        // keep their ordinary per-vertex lighting.
        material.constantLight =
            (material.flags & 1u) != 0u ||
            (nonPlayerPhase1 && (material.flags & 4u) != 0u);
        if (material.constantLight)
        {
            const unsigned flatVariant =
                (material.flags & 4u) != 0u ? 1u : 0u;
            if (!flatLightKnown[flatVariant])
            {
                const RetainedVertex& sample =
                    cache->vertices[mesh.lastVertex];
                if (!SampleStockLighting(
                        flatLightValue[flatVariant],
                        static_cast<int>(sample.normalBone),
                        static_cast<int>(material.flags),
                        sample.normal))
                    return MaterialFallback(MaterialReject::FlatLight);
                flatLightKnown[flatVariant] = true;
            }
            material.flatLight = flatLightValue[flatVariant];
        }
        if ((material.flags & 0x02u) != 0u)
            hasChrome = true;
        if ((material.flags & 0x40u) != 0u)
            hasMasked = true;
        if ((material.flags & 0x20u) != 0u)
            hasAdditive = true;
        try
        {
            g_preparedMeshes.push_back({&mesh, material});
        }
        catch (...)
        {
            g_preparedMeshes.clear();
            return MaterialFallback(MaterialReject::Store);
        }
    }

    if (hasMasked && !MaskedEntryStateReady())
        return DirectFallback(DirectFallbackReason::MaterialState);
    if (hasAdditive && !AdditiveEntryStateReady())
        return DirectFallback(DirectFallbackReason::MaterialState);
    if (!hasAdditive && !OpaqueEntryBlendOff())
        return DirectFallback(DirectFallbackReason::MaterialState);

    if (!g_gammaFitKnown)
        UpdateGammaFit();
    if (!g_gammaFitKnown ||
        !EnsureDirectProgram() ||
        !UploadGpu(*cache))
        return DirectFallback(DirectFallbackReason::ProgramGpu);

    if (!StampUsedPositionBones(submodel))
        return DirectFallback(DirectFallbackReason::BoneStamp);

    bool chromePrepared = false;
    if (RecordDeferredDirectDraw(
            state, *cache, submodel, chromePrepared))
    {
        if (nonPlayerPhase1)
            ++g_nonPlayerDeferredDraws;
        ++g_directDraws;
        return true;
    }

    // Phase-1 non-player coverage is intentionally deferred-only.  If the
    // recorder cannot represent this exact DrawModel case, fail open to Gold
    // rather than widening coverage through the immediate retained path. The
    // fallback barrier in DirectKernelDispatch orders that stock draw.
    if (nonPlayerPhase1)
    {
        if (prof::Active())
            ++g_executeNonPlayerRecord;
        return DirectFallback(DirectFallbackReason::Execute);
    }

    // The per-cache CHROME buffer is only used by the immediate path.
    if (hasChrome && !EnsureChromeUvBuffer(*cache))
        return DirectFallback(DirectFallbackReason::ProgramGpu);

    // A pending recorded run must become visible before an immediate retained
    // draw that is not order-independent of it. ADDITIVE meshes blend without
    // depth writes. CHROME and MASKED-only entities that the recorder still
    // declined (remap textures, capacity) draw opaque or alpha-tested with
    // depth writes, the same order-independent class as the pending packets.
    // If the immediate draw fails, the stock replay is ordered by the
    // fallback barrier. The immediate upload takes one block from the same
    // frame region the flush will plan from, so the pending run must still
    // fit after it.
    if (!g_deferredCommands.empty())
    {
        if (!hasAdditive && PendingRunAdmitsForeignDraw() &&
            DeferredCapacityAvailable() &&
            DepthOrderedOpaqueGlState())
            NoteBarrierSkip(FlushTrigger::Immediate);
        else
            (void)FlushDeferredSolidCommands(
                FlushSite::InsideStudioDraw, FlushTrigger::Immediate);
    }

    if (hasChrome)
    {
        // Preserve Gold's mesh-order R_StudioChrome calls and chromeage/basis
        // side effects, then upload only the selected submodel's contiguous UV
        // range. A recorder that declined after its own prepass already left
        // the exact coordinates in g_chromeCoords/g_chromeUvScratch, nothing
        // that runs in between (flush, barrier) touches them.
        if (!chromePrepared &&
            !PrepareChromeCoordinates(state, *cache, submodel))
            return MaterialFallback(MaterialReject::ChromePrepare);
        if (!UploadChromeCoordinates(*cache, submodel))
            return MaterialFallback(MaterialReject::ChromeUpload);
    }

    const bool useVertexArray =
        cache->vertexArray != 0 && g_bindVertexArray != nullptr;
    int previousVertexArray = 0;
    int previousArrayBuffer = 0;
    int previousElementBuffer = 0;
    int maxTextureUnits = 0;
    const bool collectTiming = prof::Active();
    const long long stateStart =
        collectTiming ? prof::Now() : 0;
    const bool stateReady = DirectGlStateReady(
            useVertexArray,
            previousVertexArray,
            previousArrayBuffer,
            previousElementBuffer,
            maxTextureUnits);
    if (collectTiming)
    {
        g_directTiming.stateTicks +=
            prof::Now() - stateStart;
        ++g_directTiming.stateCalls;
    }
    if (!stateReady)
        return DirectFallback(DirectFallbackReason::GlState);

    if (!ExecuteDirectDraw(
            state, *cache, submodel, textureHeader,
            previousVertexArray,
            previousArrayBuffer, previousElementBuffer,
            maxTextureUnits))
    {
        if (prof::Active())
            ++g_executeImmediate;
        return DirectFallback(DirectFallbackReason::Execute);
    }

    ++g_directDraws;
    return true;
}

bool __cdecl DirectKernelDispatch(void* wrapperCaller)
{
    if (ReadMode() != 1)
        return false;
    if (g_deferredFlushInProgress)
        return false;
    const bool collectTiming = prof::Active();
    const long long attemptStart =
        collectTiming ? prof::Now() : 0;
    const long long flushTicksStart = g_profiledFlushTicks;
    const std::uint64_t flushCountStart = g_profiledFlushCount;
    const bool direct = TryDirectDraw(wrapperCaller);
    if (g_directProgramOwned)
    {
        if (ReleaseOwnedDirectProgram())
            ++g_forcedProgramRestores;
    }
    // Fallback barrier: Gold's stock DrawPoints runs next. Opaque, depth-
    // written stock output is order-independent of the pending packets, but
    // not of a pending (blended) client shadow. Anything blended or additive
    // needs the run in the framebuffer first.
    if (!direct && !g_deferredCommands.empty())
    {
        if (PendingRunAdmitsForeignDraw() &&
            StockDrawPointsOrderIndependent())
            NoteBarrierSkip(FlushTrigger::Fallback);
        else
            (void)FlushDeferredSolidCommands(
                FlushSite::InsideStudioDraw, FlushTrigger::Fallback);
    }
    if (collectTiming)
    {
        // Flushes run from here submit the pending run of other entities and
        // replay its deferred shadows. Charging them to this draw made
        // directTotal grow with run length (6.0 -> 8.5 us once shadows stopped
        // ending runs), so they are reported separately.
        const long long flushTicks =
            g_profiledFlushTicks - flushTicksStart;
        g_directTiming.dispatchFlushTicks += flushTicks;
        g_directTiming.dispatchFlushes +=
            g_profiledFlushCount - flushCountStart;
        const long long elapsed =
            prof::Now() - attemptStart - flushTicks;
        if (direct)
        {
            g_directTiming.directAttemptTicks += elapsed;
            ++g_directTiming.directAttemptCalls;
        }
        else
        {
            g_directTiming.fallbackAttemptTicks += elapsed;
            ++g_directTiming.fallbackAttemptCalls;
        }
    }
    if (direct && g_directDraws == 1)
    {
        rendererlog::Line(
            "studio renderer: first direct retained DrawPoints completed");
    }
    if ((g_directAttempts & 0xFFFu) == 0 && rendererlog::StatsEnabled())
    {
        rendererlog::Line(
            "studio renderer: direct attempts=%llu draws=%llu fallback=%llu coverage=%.1f%% deferred=%llu flushed=%llu pending=%u uboMap=%llu uboSub=%llu colorSkip=%llu paramsSkip=%llu instDraw=%llu instEnt=%llu instSaved=%llu progFix=%llu chrome=%llu chromeNormals=%llu deferredChrome=%llu deferredMasked=%llu",
            static_cast<unsigned long long>(g_directAttempts),
            static_cast<unsigned long long>(g_directDraws),
            static_cast<unsigned long long>(g_directFallbacks),
            g_directAttempts
                ? (100.0 * static_cast<double>(g_directDraws) /
                   static_cast<double>(g_directAttempts))
                : 0.0,
            static_cast<unsigned long long>(g_deferredRecorded),
            static_cast<unsigned long long>(g_deferredFlushed),
            static_cast<unsigned>(g_deferredCommands.size()),
            static_cast<unsigned long long>(g_deferredMappedUploads),
            static_cast<unsigned long long>(g_deferredSubDataUploads),
            static_cast<unsigned long long>(g_deferredColorUniformSkips),
            static_cast<unsigned long long>(g_deferredParamsUniformSkips),
            static_cast<unsigned long long>(g_instancedDrawCalls),
            static_cast<unsigned long long>(g_instancedEntities),
            static_cast<unsigned long long>(g_instancedSavedDraws),
            static_cast<unsigned long long>(g_forcedProgramRestores),
            static_cast<unsigned long long>(g_chromePrepasses),
            static_cast<unsigned long long>(g_chromeNormalCalls),
            static_cast<unsigned long long>(g_deferredChromeRecords),
            static_cast<unsigned long long>(g_deferredMaskedRecords));
        if (prof::Active())
        {
            char materialRejects[320]{};
            std::size_t materialUsed = 0;
            for (unsigned i = 0;
                 i < static_cast<unsigned>(MaterialReject::Count) &&
                 materialUsed < sizeof(materialRejects);
                 ++i)
            {
                const int written = std::snprintf(
                    materialRejects + materialUsed,
                    sizeof(materialRejects) - materialUsed,
                    " %s=%llu", kMaterialRejectNames[i],
                    static_cast<unsigned long long>(g_materialReject[i]));
                if (written <= 0)
                    break;
                materialUsed += static_cast<std::size_t>(written);
            }
            rendererlog::Line(
                "studio renderer: material rejects%s; execute "
                "nonplayerRecord=%llu immediate=%llu",
                materialRejects,
                static_cast<unsigned long long>(g_executeNonPlayerRecord),
                static_cast<unsigned long long>(g_executeImmediate));
            for (unsigned side = 0; side < 2u; ++side)
            {
                char recordRejects[400]{};
                std::size_t recordUsed = 0;
                for (unsigned i = 0;
                     i < static_cast<unsigned>(RecordReject::Count) &&
                     recordUsed < sizeof(recordRejects);
                     ++i)
                {
                    const int written = std::snprintf(
                        recordRejects + recordUsed,
                        sizeof(recordRejects) - recordUsed,
                        " %s=%llu", kRecordRejectNames[i],
                        static_cast<unsigned long long>(
                            g_recordReject[side][i]));
                    if (written <= 0)
                        break;
                    recordUsed += static_cast<std::size_t>(written);
                }
                rendererlog::Line(
                    "studio renderer: record rejects %s:%s",
                    side == 0 ? "player" : "nonplayer",
                    recordRejects);
            }
            rendererlog::Line(
                "studio renderer: nonplayer phase1 attempts=%llu deferred=%llu",
                static_cast<unsigned long long>(g_nonPlayerAttempts),
                static_cast<unsigned long long>(g_nonPlayerDeferredDraws));
            rendererlog::Line(
                "studio renderer: nonplayer rejects outsidePass=%llu drawModelScope=%llu glow=%llu other=%llu; drawModel entry rejects flags=%llu mode=%llu caller=%llu drawentities=%llu entity=%llu rendermode=%llu glow=%llu force=%llu",
                static_cast<unsigned long long>(g_nonPlayerReject[static_cast<unsigned>(NonPlayerReject::OutsidePass)]),
                static_cast<unsigned long long>(g_nonPlayerReject[static_cast<unsigned>(NonPlayerReject::DrawModelScope)]),
                static_cast<unsigned long long>(g_nonPlayerReject[static_cast<unsigned>(NonPlayerReject::GlowShell)]),
                static_cast<unsigned long long>(g_nonPlayerReject[static_cast<unsigned>(NonPlayerReject::Other)]),
                static_cast<unsigned long long>(g_drawModelEntryReject[static_cast<unsigned>(DrawModelEntryReject::Flags)]),
                static_cast<unsigned long long>(g_drawModelEntryReject[static_cast<unsigned>(DrawModelEntryReject::Mode)]),
                static_cast<unsigned long long>(g_drawModelEntryReject[static_cast<unsigned>(DrawModelEntryReject::Caller)]),
                static_cast<unsigned long long>(g_drawModelEntryReject[static_cast<unsigned>(DrawModelEntryReject::DrawEntities)]),
                static_cast<unsigned long long>(g_drawModelEntryReject[static_cast<unsigned>(DrawModelEntryReject::Entity)]),
                static_cast<unsigned long long>(g_drawModelEntryReject[static_cast<unsigned>(DrawModelEntryReject::RenderMode)]),
                static_cast<unsigned long long>(g_drawModelEntryReject[static_cast<unsigned>(DrawModelEntryReject::GlowShell)]),
                static_cast<unsigned long long>(g_drawModelEntryReject[static_cast<unsigned>(DrawModelEntryReject::ForceFlags)]));
            rendererlog::Line(
                "studio renderer: fallback reasons caller=%llu global=%llu neg=%llu cache=%llu submodel=%llu texture=%llu skin=%llu material=%llu matstate=%llu program=%llu stamp=%llu glstate=%llu execute=%llu",
                static_cast<unsigned long long>(g_directFallbackReason[static_cast<unsigned>(DirectFallbackReason::Caller)]),
                static_cast<unsigned long long>(g_directFallbackReason[static_cast<unsigned>(DirectFallbackReason::GlobalState)]),
                static_cast<unsigned long long>(g_directFallbackReason[static_cast<unsigned>(DirectFallbackReason::NegativeCache)]),
                static_cast<unsigned long long>(g_directFallbackReason[static_cast<unsigned>(DirectFallbackReason::CacheResolve)]),
                static_cast<unsigned long long>(g_directFallbackReason[static_cast<unsigned>(DirectFallbackReason::Submodel)]),
                static_cast<unsigned long long>(g_directFallbackReason[static_cast<unsigned>(DirectFallbackReason::TextureHeader)]),
                static_cast<unsigned long long>(g_directFallbackReason[static_cast<unsigned>(DirectFallbackReason::Skin)]),
                static_cast<unsigned long long>(g_directFallbackReason[static_cast<unsigned>(DirectFallbackReason::Material)]),
                static_cast<unsigned long long>(g_directFallbackReason[static_cast<unsigned>(DirectFallbackReason::MaterialState)]),
                static_cast<unsigned long long>(g_directFallbackReason[static_cast<unsigned>(DirectFallbackReason::ProgramGpu)]),
                static_cast<unsigned long long>(g_directFallbackReason[static_cast<unsigned>(DirectFallbackReason::BoneStamp)]),
                static_cast<unsigned long long>(g_directFallbackReason[static_cast<unsigned>(DirectFallbackReason::GlState)]),
                static_cast<unsigned long long>(g_directFallbackReason[static_cast<unsigned>(DirectFallbackReason::Execute)]));
            rendererlog::Line(
                "studio renderer: global rejects read=%llu nonplayer=%llu rendermode=%llu setupmode=%llu glow=%llu force=%llu lights=%llu debug=%llu skin=%llu bones=%llu shadow=%llu",
                static_cast<unsigned long long>(g_globalRejectReason[static_cast<unsigned>(GlobalRejectReason::ReadGlobals)]),
                static_cast<unsigned long long>(g_globalRejectReason[static_cast<unsigned>(GlobalRejectReason::NotPlayer)]),
                static_cast<unsigned long long>(g_globalRejectReason[static_cast<unsigned>(GlobalRejectReason::RenderMode)]),
                static_cast<unsigned long long>(g_globalRejectReason[static_cast<unsigned>(GlobalRejectReason::SetupRenderMode)]),
                static_cast<unsigned long long>(g_globalRejectReason[static_cast<unsigned>(GlobalRejectReason::GlowShell)]),
                static_cast<unsigned long long>(g_globalRejectReason[static_cast<unsigned>(GlobalRejectReason::ForceFlags)]),
                static_cast<unsigned long long>(g_globalRejectReason[static_cast<unsigned>(GlobalRejectReason::StudioLights)]),
                static_cast<unsigned long long>(g_globalRejectReason[static_cast<unsigned>(GlobalRejectReason::DebugMode)]),
                static_cast<unsigned long long>(g_globalRejectReason[static_cast<unsigned>(GlobalRejectReason::Skin)]),
                static_cast<unsigned long long>(g_globalRejectReason[static_cast<unsigned>(GlobalRejectReason::BoneCount)]),
                static_cast<unsigned long long>(g_globalRejectReason[static_cast<unsigned>(GlobalRejectReason::Shadow)]));
            rendererlog::Line(
                "studio renderer: setupmode rejects 1=%llu 2=%llu 3=%llu 4=%llu 5=%llu 6=%llu 7=%llu 8=%llu 9=%llu 10=%llu 11=%llu 12=%llu 13=%llu 14=%llu 15=%llu other=%llu",
                static_cast<unsigned long long>(g_setupModeRejectValues[1]),
                static_cast<unsigned long long>(g_setupModeRejectValues[2]),
                static_cast<unsigned long long>(g_setupModeRejectValues[3]),
                static_cast<unsigned long long>(g_setupModeRejectValues[4]),
                static_cast<unsigned long long>(g_setupModeRejectValues[5]),
                static_cast<unsigned long long>(g_setupModeRejectValues[6]),
                static_cast<unsigned long long>(g_setupModeRejectValues[7]),
                static_cast<unsigned long long>(g_setupModeRejectValues[8]),
                static_cast<unsigned long long>(g_setupModeRejectValues[9]),
                static_cast<unsigned long long>(g_setupModeRejectValues[10]),
                static_cast<unsigned long long>(g_setupModeRejectValues[11]),
                static_cast<unsigned long long>(g_setupModeRejectValues[12]),
                static_cast<unsigned long long>(g_setupModeRejectValues[13]),
                static_cast<unsigned long long>(g_setupModeRejectValues[14]),
                static_cast<unsigned long long>(g_setupModeRejectValues[15]),
                static_cast<unsigned long long>(g_setupModeRejectOther));
            rendererlog::Line(
                "studio renderer: cache resolve failures read=%llu cachedSub=%llu identity=%llu build=%llu emplace=%llu finalSub=%llu",
                static_cast<unsigned long long>(g_cacheResolveFailure.readState),
                static_cast<unsigned long long>(g_cacheResolveFailure.cachedSubmodel),
                static_cast<unsigned long long>(g_cacheResolveFailure.identity),
                static_cast<unsigned long long>(g_cacheResolveFailure.build),
                static_cast<unsigned long long>(g_cacheResolveFailure.emplace),
                static_cast<unsigned long long>(g_cacheResolveFailure.finalSubmodel));
            rendererlog::Line(
                "studio renderer: direct timing us/call "
                "state=%.3f uniforms=%.3f uboUpload=%.3f uboBind=%.3f "
                "material=%.3f draw=%.3f restore=%.3f directTotal=%.3f "
                "fallbackPreflight=%.3f (flush time excluded) "
                "dispatchFlush=%.3f us x %llu",
                TimingMicrosPerCall(
                    g_directTiming.stateTicks,
                    g_directTiming.stateCalls),
                TimingMicrosPerCall(
                    g_directTiming.uniformTicks,
                    g_directTiming.uniformCalls),
                TimingMicrosPerCall(
                    g_directTiming.uboUploadTicks,
                    g_directTiming.uboUploads),
                TimingMicrosPerCall(
                    g_directTiming.uboBindTicks,
                    g_directTiming.uboBinds),
                TimingMicrosPerCall(
                    g_directTiming.materialTicks,
                    g_directTiming.materialCalls),
                TimingMicrosPerCall(
                    g_directTiming.drawTicks,
                    g_directTiming.drawCalls),
                TimingMicrosPerCall(
                    g_directTiming.restoreTicks,
                    g_directTiming.restoreCalls),
                TimingMicrosPerCall(
                    g_directTiming.directAttemptTicks,
                    g_directTiming.directAttemptCalls),
                TimingMicrosPerCall(
                    g_directTiming.fallbackAttemptTicks,
                    g_directTiming.fallbackAttemptCalls),
                TimingMicrosPerCall(
                    g_directTiming.dispatchFlushTicks,
                    g_directTiming.dispatchFlushes),
                static_cast<unsigned long long>(
                    g_directTiming.dispatchFlushes));
            rendererlog::Line(
                "studio renderer: ubo mode=%s persistentWrites=%llu "
                "fenceWaits=%llu",
                g_uniformPersistentBase ? "persistent" : "orphan",
                static_cast<unsigned long long>(g_uniformPersistentWrites),
                static_cast<unsigned long long>(g_uniformFenceWaits));
        }
    }
    return direct;
}

__declspec(naked) void DirectKernelHook()
{
    __asm
    {
        // The patched normal callsite lives inside hw+0x9B570. EBP therefore
        // still belongs to Gold's wrapper, and [EBP+4] is the original client
        // caller return. Tail fallback preserves the inner 0x9B0A0 ABI exactly.
        push dword ptr [ebp + 4]
        call DirectKernelDispatch
        add esp, 4
        test al, al
        jz stock
        ret
    stock:
        jmp dword ptr [g_innerDrawPoints]
    }
}

void* CurrentRelativeCallTarget(std::uint8_t* callsite);
bool PatchRelativeCallTarget(std::uint8_t* callsite,
                             void* expected,
                             void* replacement);

// Brush, sprite and client shadow draws are the only non-Studio GL work Gold
// interleaves with Studio draws inside the solid entity pass. They flush the
// deferred run (or skip it when order-independent, or defer the client shadow
// into it) and end the current matrix / client-array snapshots.
void __cdecl SolidBarrierFlush()
{
    // Brush barrier. An opaque solid-pass brush entity is order-independent
    // of the pending run (BrushDrawOrderIndependent). Snapshots still end
    // here, the brush rewrites client arrays and the matrix stack.
    if (!g_deferredCommands.empty() && PendingRunAdmitsForeignDraw() &&
        BrushDrawOrderIndependent())
        NoteBarrierSkip(FlushTrigger::Brush);
    else
        (void)FlushDeferredSolidCommands(
            FlushSite::OutsideStudioDraw, FlushTrigger::Brush);
    InvalidatePassSnapshots();
}

void ShadowBarrierFlush()
{
    // The shadow is blended over what is already in the framebuffer, so
    // without deferral the pending packets must land first.
    (void)FlushDeferredSolidCommands(
        FlushSite::InsideStudioDraw, FlushTrigger::Shadow);
    InvalidatePassSnapshots();
}

// Eye position of an orthonormal Gold view matrix, checked against the
// near distance of its glFrustum projection. All four shadow corners placed
// there make the original-time provider call rasterize nothing: the quad has
// zero area and lies in front of the near plane (z_eye > -near is clipped by
// any perspective frustum).
bool ComputeShadowEyePoint(const DirectMatrices& matrices, float eye[3])
{
    const float* mv = matrices.modelView;
    const float* p = matrices.projection;
    if (p[3] != 0.0f || p[7] != 0.0f ||
        p[11] != -1.0f || p[15] != 0.0f ||
        mv[3] != 0.0f || mv[7] != 0.0f ||
        mv[11] != 0.0f || mv[15] != 1.0f)
        return false;
    const float denominator = p[10] - 1.0f;
    if (denominator == 0.0f)
        return false;
    const float nearPlane = p[14] / denominator;
    if (!(nearPlane > 0.0f))
        return false;
    // Column-major: R[i][j] = mv[j * 4 + i], t[i] = mv[12 + i], eye = -R^T t.
    for (int j = 0; j < 3; ++j)
    {
        eye[j] = -(mv[j * 4 + 0] * mv[12] +
                   mv[j * 4 + 1] * mv[13] +
                   mv[j * 4 + 2] * mv[14]);
    }
    // Verify instead of trusting orthonormality.
    const float limit = nearPlane * 0.25f;
    for (int i = 0; i < 3; ++i)
    {
        const float component =
            mv[0 * 4 + i] * eye[0] +
            mv[1 * 4 + i] * eye[1] +
            mv[2 * 4 + i] * eye[2] +
            mv[12 + i];
        if (!(std::fabs(component) <= limit))
            return false;
    }
    return true;
}

bool ReadClientShadowCall(const std::uintptr_t* args,
                          DeferredShadow& shadow)
{
    if (!args || !g_clientStudioRenderShadowSlot || !g_hwBase ||
        !g_getIntegerv)
        return false;
    __try
    {
        shadow.provider = reinterpret_cast<RenderShadowFn>(
            *g_clientStudioRenderShadowSlot);
        shadow.sprite = static_cast<int>(args[0]);
        for (int corner = 0; corner < 4; ++corner)
        {
            const float* point =
                reinterpret_cast<const float*>(args[1 + corner]);
            if (!point)
                return false;
            std::memcpy(shadow.points[corner], point,
                        sizeof(shadow.points[corner]));
        }
        // The shadow binds through Gold on TMU0. Gold's TMU shadow must be
        // TMU0 here so replay reproduces the same unit selection.
        if (*reinterpret_cast<const int*>(
                g_hwBase + kGoldCurrentTmuRva) != 0)
            return false;
        int program = -1;
        g_getIntegerv(GL_CURRENT_PROGRAM, &program);
        return shadow.provider != nullptr && program == 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// See the deferred client shadow notes above FlushDeferredSolidCommandsImpl.
// Only used while a run is pending: with nothing recorded the stock call is
// already in stock order.
bool TryDeferClientShadow(const std::uintptr_t* args)
{
    if (ReadShadowDeferMode() != 1 ||
        g_deferredCommands.empty() ||
        g_deferredFlushInProgress ||
        g_solidEntityPassDepth <= 0 ||
        !g_deferredRunMatricesValid ||
        !ShadowReplayFunctionsReady())
        return false;

    DeferredShadow shadow{};
    if (!ReadClientShadowCall(args, shadow) ||
        !CaptureShadowEnv(shadow.env))
        return false;
    // Replay draws under the run matrices, so they must be the live ones.
    DirectMatrices live{};
    float eye[3]{};
    if (!CaptureDirectMatrices(live) ||
        std::memcmp(&live, &g_deferredRunMatrices, sizeof(live)) != 0 ||
        !ComputeShadowEyePoint(live, eye))
        return false;

    try
    {
        g_deferredShadows.push_back(shadow);
    }
    catch (...)
    {
        return false;
    }
    const float degenerate[4][3] = {
        {eye[0], eye[1], eye[2]},
        {eye[0], eye[1], eye[2]},
        {eye[0], eye[1], eye[2]},
        {eye[0], eye[1], eye[2]}
    };
    if (!InvokeShadowProvider(
            shadow.provider, shadow.sprite, degenerate))
    {
        g_deferredShadows.pop_back();
        return false;
    }
    if (prof::Active())
    {
        ++g_flushProfile.shadowsDeferred;
        ++g_flushProfile.siteSkips[
            static_cast<unsigned>(FlushTrigger::Shadow)];
    }
    InvalidatePassSnapshots();
    return true;
}

// Called from the client+AFE42 hook with a pointer to the provider's five
// stack arguments. Returns true when the call has been fully handled.
bool __cdecl ShadowBarrierDispatch(const std::uintptr_t* args)
{
    if (TryDeferClientShadow(args))
        return true;
    ShadowBarrierFlush();
    return false;
}

unsigned char g_shadowCallHandled = 0;

__declspec(naked) void DrawBrushModelBarrierCall()
{
    __asm
    {
        pushfd
        pushad
        call SolidBarrierFlush
        call spritevbo::Flush
        call worldvbo::BeginBrushScope
        popad
        popfd
        call dword ptr [g_originalDrawBrushModel]
        pushfd
        pushad
        call worldvbo::EndBrushScope
        popad
        popfd
        ret
    }
}

bool __cdecl TryBeginSolidSpriteCapture()
{
    InvalidatePassSnapshots();
    // Sprite barrier, skipped for alpha-tested opaque solid sprites
    // (SpriteDrawOrderIndependent).
    if (!g_deferredCommands.empty() && PendingRunAdmitsForeignDraw() &&
        SpriteDrawOrderIndependent())
        NoteBarrierSkip(FlushTrigger::Sprite);
    else if (!FlushDeferredSolidCommands(
                 FlushSite::OutsideStudioDraw, FlushTrigger::Sprite))
    {
        spritevbo::Flush();
        return false;
    }
    if (spritevbo::BeginCurrentSolidSprite())
        return true;
    spritevbo::Flush();
    return false;
}

__declspec(naked) void DrawSpriteModelBarrierCall()
{
    __asm
    {
        pushfd
        pushad
        call TryBeginSolidSpriteCapture
        test al, al
        jz stock
        popad
        popfd
        call dword ptr [g_originalDrawSpriteModel]
        pushfd
        pushad
        call spritevbo::EndCurrentSolidSprite
        popad
        popfd
        ret
    stock:
        popad
        popfd
        jmp dword ptr [g_originalDrawSpriteModel]
    }
}

__declspec(naked) void ClientStudioShadowBarrierCall()
{
    __asm
    {
        // client+AFE42 is the exact FF15 StudioRenderShadow provider call.
        // Either the shadow is deferred into the pending run (the provider
        // already ran, return to the client) or the run is flushed and the
        // call tail-dispatches through the live provider slot. The cdecl
        // caller pops the five arguments itself and reads neither flags nor
        // a result after the call.
        sub esp, 80h
        movups [esp + 00h], xmm0
        movups [esp + 10h], xmm1
        movups [esp + 20h], xmm2
        movups [esp + 30h], xmm3
        movups [esp + 40h], xmm4
        movups [esp + 50h], xmm5
        movups [esp + 60h], xmm6
        movups [esp + 70h], xmm7
        pushfd
        pushad
        // 80h xmm save + 4 flags + 20h registers + 4 return address.
        lea eax, [esp + 0A8h]
        push eax
        call ShadowBarrierDispatch
        add esp, 4
        mov byte ptr [g_shadowCallHandled], al
        popad
        popfd
        movups xmm0, [esp + 00h]
        movups xmm1, [esp + 10h]
        movups xmm2, [esp + 20h]
        movups xmm3, [esp + 30h]
        movups xmm4, [esp + 40h]
        movups xmm5, [esp + 50h]
        movups xmm6, [esp + 60h]
        movups xmm7, [esp + 70h]
        add esp, 80h
        cmp byte ptr [g_shadowCallHandled], 0
        jne handled
        push eax
        mov eax, dword ptr [g_clientStudioRenderShadowSlot]
        mov eax, dword ptr [eax]
        xchg eax, dword ptr [esp]
        ret
    handled:
        ret
    }
}

bool IndirectAbsoluteCallMatches(std::uint8_t* callsite, void** slot)
{
    if (!callsite || !slot)
        return false;
    __try
    {
        if (callsite[0] != 0xFF || callsite[1] != 0x15)
            return false;
        const std::uint32_t encoded =
            *reinterpret_cast<const std::uint32_t*>(callsite + 2);
        return encoded ==
            static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(slot));
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool PatchIndirectAbsoluteCallBarrier(std::uint8_t* callsite,
                                      void** expectedSlot,
                                      void* replacement)
{
    if (!callsite || !expectedSlot || !replacement ||
        !IndirectAbsoluteCallMatches(callsite, expectedSlot))
        return false;

    std::uint8_t original[6]{};
    std::memcpy(original, callsite, sizeof(original));
    DWORD oldProtect = 0;
    if (!VirtualProtect(
            callsite, sizeof(original),
            PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

    callsite[0] = 0xE8;
    *reinterpret_cast<std::int32_t*>(callsite + 1) =
        static_cast<std::int32_t>(
            reinterpret_cast<std::uint8_t*>(replacement) -
            (callsite + 5));
    callsite[5] = 0x90;
    DWORD ignored = 0;
    VirtualProtect(
        callsite, sizeof(original),
        oldProtect, &ignored);
    FlushInstructionCache(
        GetCurrentProcess(), callsite, sizeof(original));

    if (CurrentRelativeCallTarget(callsite) == replacement &&
        callsite[5] == 0x90)
        return true;

    if (VirtualProtect(
            callsite, sizeof(original),
            PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        std::memcpy(callsite, original, sizeof(original));
        VirtualProtect(
            callsite, sizeof(original),
            oldProtect, &ignored);
        FlushInstructionCache(
            GetCurrentProcess(), callsite, sizeof(original));
    }
    return false;
}

bool InstallDeferredSolidBarriers()
{
    if (!g_hwBase || !g_clientBase)
        return false;
    g_originalDrawBrushModel = reinterpret_cast<SolidBarrierFn>(
        g_hwBase + kDrawBrushModelRva);
    g_originalDrawSpriteModel = reinterpret_cast<SolidBarrierFn>(
        g_hwBase + kDrawSpriteModelRva);

    std::uint8_t* brushCall =
        g_hwBase + kDrawBrushModelCallRva;
    std::uint8_t* spriteCall =
        g_hwBase + kDrawSpriteModelCallRva;
    void* const brushStock =
        reinterpret_cast<void*>(g_originalDrawBrushModel);
    void* const spriteStock =
        reinterpret_cast<void*>(g_originalDrawSpriteModel);
    void* const brushHook =
        reinterpret_cast<void*>(&DrawBrushModelBarrierCall);
    void* const spriteHook =
        reinterpret_cast<void*>(&DrawSpriteModelBarrierCall);
    g_clientStudioRenderShadowSlot =
        reinterpret_cast<void**>(
            g_clientBase + kClientStudioRenderShadowSlotRva);
    std::uint8_t* const clientShadowCall =
        g_clientBase + kClientStudioRenderShadowCallRva;
    void* const clientShadowHook =
        reinterpret_cast<void*>(&ClientStudioShadowBarrierCall);

    void* brushCurrent = CurrentRelativeCallTarget(brushCall);
    bool brushOk = brushCurrent == brushHook;
    if (!brushOk && brushCurrent == brushStock)
        brushOk = PatchRelativeCallTarget(
            brushCall, brushStock, brushHook);

    void* spriteCurrent = CurrentRelativeCallTarget(spriteCall);
    bool spriteOk = spriteCurrent == spriteHook;
    if (!spriteOk && spriteCurrent == spriteStock)
        spriteOk = PatchRelativeCallTarget(
            spriteCall, spriteStock, spriteHook);

    bool clientShadowOk =
        CurrentRelativeCallTarget(clientShadowCall) ==
            clientShadowHook &&
        clientShadowCall[5] == 0x90;
    if (!clientShadowOk)
        clientShadowOk = PatchIndirectAbsoluteCallBarrier(
            clientShadowCall,
            g_clientStudioRenderShadowSlot,
            clientShadowHook);
    g_clientStudioShadowBarrierReady = clientShadowOk;

    g_deferredBarriersReady = brushOk && spriteOk;
    rendererlog::Line(
        "studio renderer: deferred solid barriers brush=%s sprite=%s clientShadow=%s recorder=%s",
        brushOk ? "ok" : "fail",
        spriteOk ? "ok" : "fail",
        clientShadowOk ? "ok" : "fail",
        g_deferredBarriersReady ? "ready" : "disabled");
    return g_deferredBarriersReady;
}

void* CurrentRelativeCallTarget(std::uint8_t* callsite)
{
    if (!callsite)
        return nullptr;
    __try
    {
        if (callsite[0] != 0xE8)
            return nullptr;
        const std::int32_t rel =
            *reinterpret_cast<const std::int32_t*>(callsite + 1);
        return callsite + 5 + rel;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
}

bool PatchRelativeCallTarget(std::uint8_t* callsite,
                             void* expected,
                             void* replacement)
{
    if (!callsite || !expected || !replacement ||
        CurrentRelativeCallTarget(callsite) != expected)
        return false;
    DWORD oldProtect = 0;
    if (!VirtualProtect(
            callsite, 5, PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;
    callsite[0] = 0xE8;
    *reinterpret_cast<std::int32_t*>(callsite + 1) =
        static_cast<std::int32_t>(
            reinterpret_cast<std::uint8_t*>(replacement) -
            (callsite + 5));
    DWORD ignored = 0;
    VirtualProtect(callsite, 5, oldProtect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), callsite, 5);
    return true;
}

bool SetDirectKernelHook(bool enabled)
{
    if (!g_hwBase || !g_innerDrawPoints)
        return false;
    std::uint8_t* callsite =
        g_hwBase + kInnerDrawPointsNormalCallRva;
    void* current = CurrentRelativeCallTarget(callsite);
    void* hook = reinterpret_cast<void*>(&DirectKernelHook);
    void* stock = reinterpret_cast<void*>(g_innerDrawPoints);

    if (enabled)
    {
        if (current == hook)
        {
            g_innerCallHooked = true;
            return true;
        }
        if (current != stock)
        {
            g_innerCallHooked = false;
            return false;
        }
        g_innerCallHooked =
            PatchRelativeCallTarget(callsite, stock, hook);
        return g_innerCallHooked;
    }

    if (current == hook)
    {
        if (!PatchRelativeCallTarget(callsite, hook, stock))
            return false;
    }
    g_innerCallHooked = false;
    return CurrentRelativeCallTarget(callsite) == stock;
}
void LogFlushProfile()
{
    FlushProfileStats& stats = g_flushProfile;
    if (stats.frames == 0)
        return;
    LARGE_INTEGER frequency{};
    const double ticksToMs =
        QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0
            ? 1000.0 / static_cast<double>(frequency.QuadPart)
            : 0.0;
    const double frames = static_cast<double>(stats.frames);
    // Per trigger: flushes/frame and skipped (order-independent) barriers/frame.
    char sites[640]{};
    std::size_t used = 0;
    for (unsigned i = 0; i < kFlushTriggerCount && used < sizeof(sites); ++i)
    {
        const int written = std::snprintf(
            sites + used, sizeof(sites) - used, " %s=%.2f/%.2f",
            kFlushTriggerNames[i],
            static_cast<double>(stats.siteFlushes[i]) / frames,
            static_cast<double>(stats.siteSkips[i]) / frames);
        if (written <= 0)
            break;
        used += static_cast<std::size_t>(written);
    }
    rendererlog::Line(
        "studio renderer: deferred shadows/frame=%.2f replayed=%llu "
        "replayFail=%llu envFix=%llu matrixLoad=%llu replayMs/frame=%.4f",
        static_cast<double>(stats.shadowsDeferred) / frames,
        static_cast<unsigned long long>(stats.shadowsReplayed),
        static_cast<unsigned long long>(stats.shadowReplayFailures),
        static_cast<unsigned long long>(stats.shadowEnvFixes),
        static_cast<unsigned long long>(stats.shadowMatrixLoads),
        static_cast<double>(stats.shadowReplayTicks) * ticksToMs / frames);
    rendererlog::Line(
        "studio renderer: deferred chrome/frame=%.2f masked/frame=%.2f "
        "chromeStreamKB/frame=%.1f",
        static_cast<double>(stats.chromeRecords) / frames,
        static_cast<double>(stats.maskedRecords) / frames,
        static_cast<double>(stats.chromeStreamBytes) / 1024.0 / frames);
    rendererlog::Line(
        "studio renderer: deferred flushes/frame=%.2f records/flush=%.2f "
        "maxRecords=%llu groups/flush=%.2f instGroups/frame=%.2f "
        "flushMs/frame=%.4f chargedMs/frame=%.4f matrixVerify=%llu "
        "matrixMismatch=%llu frames=%llu sites(flush/skip):%s",
        static_cast<double>(stats.flushes) / frames,
        stats.flushes
            ? static_cast<double>(stats.records) /
                  static_cast<double>(stats.flushes)
            : 0.0,
        static_cast<unsigned long long>(stats.maxRecords),
        stats.flushes
            ? static_cast<double>(stats.groups) /
                  static_cast<double>(stats.flushes)
            : 0.0,
        static_cast<double>(stats.instancedGroups) / frames,
        static_cast<double>(stats.ticks) * ticksToMs / frames,
        static_cast<double>(stats.chargedTicks) * ticksToMs / frames,
        static_cast<unsigned long long>(stats.matrixVerifies),
        static_cast<unsigned long long>(stats.matrixMismatches),
        static_cast<unsigned long long>(stats.frames),
        sites);
    stats = {};
}

void EndPassSnapshotScope()
{
    InvalidatePassSnapshots();
    studio_drawbatch::SetClientArrayProofScope(false);
}
} // namespace

bool BeginDrawModelBatchScope(int flags, void* caller)
{
    if (!DrawModelEntryCompatible(flags, caller))
        return false;
    ++g_nativeOpaqueDrawModelDepth;
    return true;
}

void EndDrawModelBatchScope(bool entered)
{
    if (entered && g_nativeOpaqueDrawModelDepth > 0)
        --g_nativeOpaqueDrawModelDepth;
}

bool Install(HMODULE client, HMODULE hw, cl_enginefunc_t* engine)
{
    if (!client || !hwbuild::MatchesTarget(hw) ||
        !engine || !engine->pfnRegisterVariable)
    {
        rendererlog::Line(
            "studio renderer: exact hw/engine unavailable, disabled");
        return false;
    }

    g_clientBase = reinterpret_cast<std::uint8_t*>(client);
    g_hwBase = reinterpret_cast<std::uint8_t*>(hw);
    g_entryEngine = engine;
    g_innerDrawPoints =
        reinterpret_cast<InnerDrawPointsFn>(
            g_hwBase + kInnerDrawPointsRva);
    g_textureResolver =
        reinterpret_cast<TextureResolverFn>(
            g_hwBase + kTextureResolverRva);
    g_goldBind =
        reinterpret_cast<GoldBindFn>(
            g_hwBase + 0x00064D40u);
    g_textureHeader =
        reinterpret_cast<TextureHeaderFn>(
            g_hwBase + 0x000984D0u);
    g_mirrorPredicate =
        reinterpret_cast<MirrorPredicateFn>(
            g_hwBase + kMirrorPredicateRva);
    g_studioLighting =
        g_hwBase + kStudioLightingRva;
    g_studioChrome =
        g_hwBase + kStudioChromeRva;
    (void)InstallDeferredSolidBarriers();
    __try
    {
        g_mode = engine->pfnRegisterVariable("r_studio_renderer", "1", 0);
        g_nonPlayerMode =
            engine->pfnRegisterVariable("r_studio_nonplayer", "1", 0);
        g_meshoptMode =
            engine->pfnRegisterVariable("r_meshoptimizer", "1", 0);
        g_instancingMode =
            engine->pfnRegisterVariable("r_studio_instancing", "0", 0);
        g_shadowDeferMode =
            engine->pfnRegisterVariable("r_studio_shadowdefer", "1", 0);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        g_mode = nullptr;
        g_nonPlayerMode = nullptr;
        g_meshoptMode = nullptr;
        g_instancingMode = nullptr;
        g_shadowDeferMode = nullptr;
    }
    if (!g_mode || !g_nonPlayerMode ||
        !g_meshoptMode || !g_instancingMode || !g_shadowDeferMode)
        return false;

    if (CurrentRelativeCallTarget(
            g_hwBase + kInnerDrawPointsNormalCallRva) !=
        reinterpret_cast<void*>(g_innerDrawPoints))
    {
        rendererlog::Line(
            "studio renderer: normal inner DrawPoints call signature mismatch, "
            "direct path disabled");
        return false;
    }

    rendererlog::Line(
        "studio renderer: whole-model retained cache ready "
        "(r_studio_renderer default 1, r_studio_nonplayer default 1, "
        "r_meshoptimizer default 1, r_studio_instancing default 0, "
        "r_studio_shadowdefer default 1)");
    return true;
}

void UpdateFrame()
{
    if (!g_mode || !g_hwBase)
        return;
    if (g_solidEntityPassDepth <= 0)
        EndPassSnapshotScope();
    else
        InvalidatePassSnapshots();
    if (prof::Active())
    {
        ++g_flushProfile.frames;
        if (g_flushProfile.frames >= 600u)
        {
            if (rendererlog::StatsEnabled())
                LogFlushProfile();
            g_flushProfile = {};
        }
    }
    else if (g_flushProfile.frames != 0)
    {
        g_flushProfile = {};
    }
    const int mode = ReadMode();
    if (mode == 1)
    {
        RemoveSetupLightingHook();
        UpdateGammaFit();
        if (EnsureDirectProgram() && g_useUniformBuffer)
            (void)RotateUniformBufferFrame();
        const bool hooked = SetDirectKernelHook(true);
        // Keep the established fast Studio scope active. The retained kernel
        // now runs inside that scope, unsupported cases therefore fall through
        // to Gold with FastSkin/FastLighting/predecode still enabled.
        studio_drawbatch::SetRetainedRendererActive(false);
        if (!hooked)
            return;
        return;
    }

    ReleaseUniformBufferBinding();
    studio_drawbatch::SetRetainedRendererActive(false);
    (void)SetDirectKernelHook(false);
    if (mode != 2)
    {
        RemoveSetupLightingHook();
        return;
    }
    if (!RefreshSetupLightingHook())
        return;
    UpdateGammaFit();

    // Validator mode only warms/captures. Gold remains authoritative.
    (void)ResolveCurrentCache();
}

void BeginSolidEntityPass()
{
    spritevbo::BeginSolidPass();
    if (ReadMode() != 1)
        return;
    // A (nested) pass may run under different view matrices, so a pending
    // run is made visible and every snapshot restarts at each pass boundary.
    if (!g_deferredCommands.empty())
        (void)FlushDeferredSolidCommands(
            FlushSite::OutsideStudioDraw, FlushTrigger::PassBegin);
    InvalidatePassSnapshots();
    ++g_solidEntityPassDepth;
    studio_drawbatch::SetClientArrayProofScope(PassSnapshotsAllowed());
}

void EndSolidEntityPass()
{
    if (g_solidEntityPassDepth > 0)
    {
        (void)FlushDeferredSolidCommands(
            FlushSite::OutsideStudioDraw, FlushTrigger::PassEnd);
        --g_solidEntityPassDepth;
        if (g_solidEntityPassDepth == 0)
            EndPassSnapshotScope();
        else
            InvalidatePassSnapshots();
    }
    spritevbo::EndSolidPass();
}

void FlushDeferredStudioCommands()
{
    // Glow-shell base pass boundary, reached from inside a Studio draw. Kept
    // unconditional: the next pass is ONE/ONE additive without depth writes.
    (void)FlushDeferredSolidCommands(
        FlushSite::InsideStudioDraw, FlushTrigger::Glow);
}

void FlushBeforeStockDrawModel(int flags, void* caller)
{
    // A StudioDrawModel the recorder does not own. Opaque solid-pass studio
    // entities only reach the framebuffer through the hooked DrawPoints and
    // the client shadow, both of which keep their own barriers. Without
    // STUDIO_RENDER (e.g. Gold's setup call for a FOLLOW entity's parent)
    // the call only sets up bones, attachments and events, nothing pending
    // is affected and nothing reaches the framebuffer.
    if (!g_deferredCommands.empty() &&
        ((flags & 1) == 0 ||
         StockDrawModelOrderIndependent(flags, caller)))
        NoteBarrierSkip(FlushTrigger::DrawModel);
    else
        (void)FlushDeferredSolidCommands(
            FlushSite::OutsideStudioDraw, FlushTrigger::DrawModel);
    spritevbo::Flush();
}

void FlushBeforeDrawPlayer(int flags)
{
    // Without STUDIO_RENDER the call only sets up bones, attachments and
    // events. Recorded packets already own copies of their bone palettes, so
    // nothing pending is affected and nothing reaches the framebuffer.
    if (!g_deferredCommands.empty() && (flags & 1) == 0)
        NoteBarrierSkip(FlushTrigger::DrawPlayer);
    else
        (void)FlushDeferredSolidCommands(
            FlushSite::OutsideStudioDraw, FlushTrigger::DrawPlayer);
    spritevbo::Flush();
}
} // namespace studio_renderer
