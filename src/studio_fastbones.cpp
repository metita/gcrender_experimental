#include "studio_fastbones.h"
#include "log.h"
#include "studio_fast_math.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace studio_fastbones
{
namespace
{
constexpr std::uint32_t kKernelRva = 0xAC3D0;
constexpr std::uint32_t kRendererVtableRva = 0x161970;
constexpr std::uint32_t kCalcRotationsSlotOffset = 0x48;
constexpr std::uint32_t kClientTimestamp = 0x6A49A30C;
constexpr std::uint32_t kClientImageSize = 0x26F000;
constexpr int kMagic = 0x54534449;
constexpr int kVersion = 10;
constexpr int kMaxBones = 128;
constexpr int kPoseCacheEntries = 512;
constexpr std::size_t kRendererFrameCount = 0x20;

constexpr int STUDIO_X  = 0x0001;
constexpr int STUDIO_Y  = 0x0002;
constexpr int STUDIO_Z  = 0x0004;
constexpr int STUDIO_LX = 0x0040;
constexpr int STUDIO_LY = 0x0080;
constexpr int STUDIO_LZ = 0x0100;

struct studiohdr_t
{
    int id, version; char name[64]; int length;
    float eye[3], min[3], max[3], bbmin[3], bbmax[3]; int flags;
    int numbones, boneindex, numbonecontrollers, bonecontrollerindex;
    int numhitboxes, hitboxindex, numseq, seqindex, numseqgroups, seqgroupindex;
    int numtextures, textureindex, texturedataindex, numskinref, numskinfamilies, skinindex;
    int numbodyparts, bodypartindex, numattachments, attachmentindex;
    int soundtable, soundindex, soundgroups, soundgroupindex, numtransitions, transitionindex;
};
struct mstudiobone_t
{
    char name[32]; int parent, flags, bonecontroller[6]; float value[6], scale[6];
};
struct mstudioseqdesc_t
{
    char label[32]; float fps; int flags, activity, actweight, numevents, eventindex, numframes;
    int numpivots, pivotindex, motiontype, motionbone; float linearmovement[3];
    int automoveposindex, automoveangleindex; float bbmin[3], bbmax[3];
    int numblends, animindex, blendtype[2]; float blendstart[2], blendend[2];
    int blendparent, seqgroup, entrynode, exitnode, nodeflags, nextseq;
};
struct mstudioanim_t { unsigned short offset[6]; };
union mstudioanimvalue_t { struct { unsigned char valid, total; } num; short value; };

static_assert(sizeof(studiohdr_t) == 244, "studiohdr layout");
static_assert(sizeof(mstudiobone_t) == 112, "bone layout");
static_assert(sizeof(mstudioseqdesc_t) == 176, "seq layout");
static_assert(sizeof(mstudioanim_t) == 12, "anim layout");
static_assert(offsetof(cl_entity_t, curstate) + offsetof(entity_state_t, controller) == 0x318, "entity controller ABI");
static_assert(offsetof(cl_entity_t, latched) + offsetof(latchedvars_t, prevcontroller) == 0xB3C, "entity latch ABI");
static_assert(offsetof(cl_entity_t, mouth) == 0xB08, "entity mouth ABI");

struct FpState { alignas(16) unsigned char bytes[512]; };
static_assert(sizeof(FpState) == 512, "fxsave size");

void* g_original = nullptr;
cvar_t* g_mode = nullptr;
cvar_t* g_poseCacheMode = nullptr;
cl_enginefunc_t* g_engine = nullptr;
std::uint64_t g_calls = 0, g_fast = 0, g_fallback = 0, g_reject = 0, g_validate = 0, g_mismatch = 0;
std::uint64_t g_stockTicks = 0, g_fastTicks = 0, g_timed = 0;
float g_maxPos = 0.0f, g_maxQ = 0.0f;

struct PoseKey
{
    std::uintptr_t header{};
    std::uintptr_t sequence{};
    std::uintptr_t animation{};
    std::uint32_t frameBits{};
    std::uint32_t framerateBits{};
    std::uint32_t adjBits[4]{};
    int boneCount{};
};

struct PoseCacheEntry
{
    bool valid{};
    std::uint32_t frameToken{};
    PoseKey key{};
    std::uint8_t preEnv[32]{};
    std::uint8_t postEnv[32]{};
    float pos[kMaxBones][3]{};
    float q[kMaxBones][4]{};
};

PoseCacheEntry g_poseCache[kPoseCacheEntries]{};
std::uint64_t g_poseLookups = 0;
std::uint64_t g_poseCandidates = 0;
std::uint64_t g_poseFpEligible = 0;
std::uint64_t g_poseHits = 0;
std::uint64_t g_poseOutputMismatch = 0;
std::uint64_t g_posePreEnvMismatch = 0;
std::uint64_t g_posePostEnvMismatch = 0;
std::uint64_t g_poseStores = 0;

bool ExactClient(HMODULE module)
{
    if (!module) return false;
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(module);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(reinterpret_cast<std::uint8_t*>(module) + dos->e_lfanew);
    return nt->Signature == IMAGE_NT_SIGNATURE && nt->FileHeader.TimeDateStamp == kClientTimestamp &&
        nt->OptionalHeader.SizeOfImage == kClientImageSize;
}

bool Span(std::uint32_t length, int offset, std::size_t bytes)
{
    if (offset < 0) return false;
    const auto o = static_cast<std::uint32_t>(offset);
    return o <= length && bytes <= static_cast<std::size_t>(length - o);
}

struct Stream { const std::uint8_t* base; std::uint32_t length; };
bool InRange(const Stream& s, const void* p, std::size_t bytes)
{
    const auto a = reinterpret_cast<std::uintptr_t>(p), b = reinterpret_cast<std::uintptr_t>(s.base);
    return a >= b && a - b <= s.length && bytes <= s.length - (a - b);
}

struct Sample { short first{}, second{}; bool interpolatePosition{}, secondAvailable{}; };
bool Decode(const Stream& s, const mstudioanimvalue_t* chain, int frame, Sample& out)
{
    if (frame < 0 || !InRange(s, chain, sizeof(*chain))) return false;
    const mstudioanimvalue_t* run = chain;
    int k = frame;
    if (run->num.total < run->num.valid) k = 0;
    unsigned steps = 0;
    while (run->num.total <= k)
    {
        if (!run->num.total) return false;
        k -= run->num.total;
        run += run->num.valid + 1;
        if (++steps > 65536 || !InRange(s, run, sizeof(*run))) return false;
        if (run->num.total < run->num.valid) k = 0;
    }
    if (k < 0 || !run->num.valid) return false;
    const int first = run->num.valid > k ? k + 1 : run->num.valid;
    const int second = run->num.valid > k + 1 ? k + 2 : (run->num.total > k + 1 ? first : run->num.valid + 2);
    if (!InRange(s, run, (static_cast<std::size_t>(first) + 1) * sizeof(*run))) return false;
    const bool secondOk = InRange(s, run, (static_cast<std::size_t>(second) + 1) * sizeof(*run));
    out.first = run[first].value;
    out.second = secondOk ? run[second].value : out.first;
    out.interpolatePosition = run->num.valid > k ? run->num.valid > k + 1 : run->num.total <= k + 1;
    out.secondAvailable = secondOk;
    return true;
}

void AngleQuaternion4(const float angles[4][3], float out[4][4])
{
    __m128 sr, cr, sp, cp, sy, cy;
    auto comp = [&](int axis) { return _mm_mul_ps(_mm_setr_ps(angles[0][axis], angles[1][axis], angles[2][axis], angles[3][axis]), _mm_set1_ps(0.5f)); };
    studio_fastmath::SinCos4(comp(0), sr, cr); studio_fastmath::SinCos4(comp(1), sp, cp); studio_fastmath::SinCos4(comp(2), sy, cy);
    __m128 x = _mm_sub_ps(_mm_mul_ps(_mm_mul_ps(sr, cp), cy), _mm_mul_ps(_mm_mul_ps(cr, sp), sy));
    __m128 y = _mm_add_ps(_mm_mul_ps(_mm_mul_ps(cr, sp), cy), _mm_mul_ps(_mm_mul_ps(sr, cp), sy));
    __m128 z = _mm_sub_ps(_mm_mul_ps(_mm_mul_ps(cr, cp), sy), _mm_mul_ps(_mm_mul_ps(sr, sp), cy));
    __m128 w = _mm_add_ps(_mm_mul_ps(_mm_mul_ps(cr, cp), cy), _mm_mul_ps(_mm_mul_ps(sr, sp), sy));
    _MM_TRANSPOSE4_PS(x,y,z,w); _mm_storeu_ps(out[0],x); _mm_storeu_ps(out[1],y); _mm_storeu_ps(out[2],z); _mm_storeu_ps(out[3],w);
}

void Slerp(const float p[4], const float srcQ[4], float t, float out[4])
{
    float q[4]; std::memcpy(q, srcQ, sizeof(q));
    float a=0,b=0; for(int i=0;i<4;++i){ a+=(p[i]-q[i])*(p[i]-q[i]); b+=(p[i]+q[i])*(p[i]+q[i]); }
    if (a>b) for(float& v:q) v=-v;
    const float cosom=p[0]*q[0]+p[1]*q[1]+p[2]*q[2]+p[3]*q[3];
    if (1.0f+cosom>0.000001f)
    {
        float sp,sq;
        if (1.0f-cosom>0.000001f)
        {
            const float omega=studio_fastmath::Acos(cosom); __m128 ss,cc;
            studio_fastmath::SinCos4(_mm_setr_ps(omega,(1.0f-t)*omega,t*omega,0),ss,cc); float v[4]; _mm_storeu_ps(v,ss);
            sp=v[1]/v[0]; sq=v[2]/v[0];
        } else { sp=1.0f-t; sq=t; }
        for(int i=0;i<4;++i) out[i]=sp*p[i]+sq*q[i];
    }
    else
    {
        float tmp[4]={-q[1],q[0],-q[3],q[2]}; __m128 ss,cc;
        studio_fastmath::SinCos4(_mm_setr_ps((1.0f-t)*1.5707963267948966f,t*1.5707963267948966f,0,0),ss,cc); float v[4]; _mm_storeu_ps(v,ss);
        for(int i=0;i<3;++i) out[i]=v[0]*p[i]+v[1]*tmp[i]; out[3]=tmp[3];
    }
}

bool FastKernel(studiohdr_t* h, mstudioseqdesc_t* seq, mstudioanim_t* anim, float frame, const float adj[4], float framerate,
                float (*pos)[3], float (*q)[4])
{
    const Stream s{reinterpret_cast<const std::uint8_t*>(h), static_cast<std::uint32_t>(h->length)};
    auto* bones = reinterpret_cast<mstudiobone_t*>(reinterpret_cast<std::uint8_t*>(h)+h->boneindex);
    const int f = static_cast<int>(frame); const float t=frame-static_cast<float>(f);
    float a1[kMaxBones][3], a2[kMaxBones][3];
    for(int i=0;i<h->numbones;++i)
    {
        for(int j=0;j<3;++j)
        {
            if(!anim[i].offset[j+3]) a1[i][j]=a2[i][j]=bones[i].value[j+3];
            else { auto* chain=reinterpret_cast<mstudioanimvalue_t*>(reinterpret_cast<std::uint8_t*>(&anim[i])+anim[i].offset[j+3]); Sample sm{}; if(!Decode(s,chain,f,sm)||(!sm.secondAvailable&&t!=0)) return false; a1[i][j]=bones[i].value[j+3]+sm.first*bones[i].scale[j+3]; a2[i][j]=bones[i].value[j+3]+sm.second*bones[i].scale[j+3]; }
            const int c=bones[i].bonecontroller[j+3]; if(c>=0){a1[i][j]+=adj[c];a2[i][j]+=adj[c];}
            pos[i][j]=bones[i].value[j];
            if(anim[i].offset[j]) { auto* chain=reinterpret_cast<mstudioanimvalue_t*>(reinterpret_cast<std::uint8_t*>(&anim[i])+anim[i].offset[j]); Sample sm{}; if(!Decode(s,chain,f,sm)||(sm.interpolatePosition&&!sm.secondAvailable&&t!=0)) return false; pos[i][j]+= (sm.interpolatePosition ? (sm.first*(1-t)+t*sm.second) : sm.first)*bones[i].scale[j]; }
            const int pc=bones[i].bonecontroller[j]; if(pc>=0) pos[i][j]+=adj[pc];
        }
    }
    for(int i=0;i<h->numbones;)
    {
        const int count=(h->numbones-i>=4)?4:1; float qa[4][4]{}, qb[4][4]{};
        if(count==4){ AngleQuaternion4(a1+i,qa); AngleQuaternion4(a2+i,qb); }
        else { float aa[4][3]{}; std::memcpy(aa[0],a1[i],12); AngleQuaternion4(aa,qa); std::memcpy(aa[0],a2[i],12); AngleQuaternion4(aa,qb); }
        for(int n=0;n<count;++n){ const int bi=i+n; if(a1[bi][0]!=a2[bi][0]||a1[bi][1]!=a2[bi][1]||a1[bi][2]!=a2[bi][2]) Slerp(qa[n],qb[n],t,q[bi]); else std::memcpy(q[bi],qa[n],16); }
        i+=count;
    }
    const int mb=seq->motionbone;
    if(seq->motiontype&STUDIO_X)pos[mb][0]=0; if(seq->motiontype&STUDIO_Y)pos[mb][1]=0; if(seq->motiontype&STUDIO_Z)pos[mb][2]=0;
    const float motion=0.0f*((1.0f-(frame-static_cast<float>(f)))/static_cast<float>(seq->numframes))*framerate;
    if(seq->motiontype&STUDIO_LX)pos[mb][0]+=motion*seq->linearmovement[0]; if(seq->motiontype&STUDIO_LY)pos[mb][1]+=motion*seq->linearmovement[1]; if(seq->motiontype&STUDIO_LZ)pos[mb][2]+=motion*seq->linearmovement[2];
    return true;
}

__declspec(noinline) void InvokeOriginal(void* renderer, void* pos, void* q, void* seq, void* anim, std::uint32_t frame, const FpState* before, FpState* after)
{
    __asm {
        mov eax, before
        fxrstor [eax]
        push frame
        push anim
        push seq
        push q
        push pos
        mov ecx, renderer
        call dword ptr [g_original]
        mov eax, after
        fxsave [eax]
    }
}

bool Validate(void* renderer, mstudioseqdesc_t* seq, mstudioanim_t* anim, std::uint32_t frameBits, studiohdr_t*& h, cl_entity_t*& ent)
{
    if(!renderer||!seq||!anim) return false;
    h=*reinterpret_cast<studiohdr_t**>(reinterpret_cast<std::uint8_t*>(renderer)+0x4C);
    ent=*reinterpret_cast<cl_entity_t**>(reinterpret_cast<std::uint8_t*>(renderer)+0x30);
    if(!h||!ent||h->id!=kMagic||h->version!=kVersion||h->length<static_cast<int>(sizeof(studiohdr_t))||h->length>16*1024*1024||h->numbones<1||h->numbones>kMaxBones||h->numbonecontrollers<0||h->numbonecontrollers>4||h->numseq<1||h->numseq>4096) return false;
    const auto length=static_cast<std::uint32_t>(h->length);
    if(!Span(length,h->boneindex,sizeof(mstudiobone_t)*h->numbones)||!Span(length,h->seqindex,sizeof(mstudioseqdesc_t)*h->numseq)) return false;
    const auto base=reinterpret_cast<std::uintptr_t>(h); const auto sb=base+h->seqindex, sa=reinterpret_cast<std::uintptr_t>(seq);
    if(sa<sb||sa-sb>=sizeof(mstudioseqdesc_t)*h->numseq||(sa-sb)%sizeof(mstudioseqdesc_t)) return false;
    if(seq->seqgroup!=0||seq->numframes<1||seq->numframes>8192||(seq->numblends!=1&&seq->numblends!=2&&seq->numblends!=4&&seq->numblends!=9)||seq->motionbone<0||seq->motionbone>=h->numbones||(seq->motiontype&~0x1FF)!=0||seq->animindex<0) return false;
    const std::size_t stride=sizeof(mstudioanim_t)*h->numbones; if(!Span(length,seq->animindex,stride*seq->numblends)) return false;
    const auto ab=base+seq->animindex, aa=reinterpret_cast<std::uintptr_t>(anim); if(aa<ab||aa-ab>=stride*seq->numblends||(aa-ab)%stride) return false;
    float f; std::memcpy(&f,&frameBits,4); if(!std::isfinite(f)) return false;
    auto* bones=reinterpret_cast<mstudiobone_t*>(base+h->boneindex); for(int i=0;i<h->numbones;++i)for(int c=0;c<6;++c)if(bones[i].bonecontroller[c]<-1||bones[i].bonecontroller[c]>=4)return false;
    return true;
}

float Diff(float a,float b){ return (!std::isfinite(a)||!std::isfinite(b))?std::numeric_limits<float>::infinity():std::fabs(a-b); }

std::uint32_t FloatBits(float value)
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

bool PoseKeyEqual(const PoseKey& a, const PoseKey& b)
{
    return std::memcmp(&a, &b, sizeof(a)) == 0;
}

std::uint32_t PoseHash(const PoseKey& key)
{
    const auto* words = reinterpret_cast<const std::uint32_t*>(&key);
    constexpr std::size_t count = sizeof(PoseKey) / sizeof(std::uint32_t);
    std::uint32_t h = 2166136261u;
    for (std::size_t i = 0; i < count; ++i)
    {
        h ^= words[i];
        h *= 16777619u;
    }
    return h;
}

bool ReadFrameToken(void* renderer, std::uint32_t& token)
{
    __try
    {
        token = *reinterpret_cast<const std::uint32_t*>(
            static_cast<const std::uint8_t*>(renderer) + kRendererFrameCount);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

void SaveFpState(FpState& state)
{
    __asm
    {
        mov eax, state
        fxsave [eax]
    }
}

int ReadPoseCacheMode()
{
    int mode = 0;
    __try
    {
        if (g_poseCacheMode)
            mode = static_cast<int>(g_poseCacheMode->value);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        mode = 0;
    }
    return (mode == 1 || mode == 2) ? mode : 0;
}

PoseKey MakePoseKey(studiohdr_t* h, mstudioseqdesc_t* seq, mstudioanim_t* anim,
                    float frame, const float adj[4], float framerate)
{
    PoseKey key{};
    key.header = reinterpret_cast<std::uintptr_t>(h);
    key.sequence = reinterpret_cast<std::uintptr_t>(seq);
    key.animation = reinterpret_cast<std::uintptr_t>(anim);
    key.frameBits = FloatBits(frame);
    key.framerateBits = FloatBits(framerate);
    for (int i = 0; i < 4; ++i)
        key.adjBits[i] = FloatBits(adj[i]);
    key.boneCount = h ? h->numbones : 0;
    return key;
}

void LogPoseCacheStats()
{
    if ((g_calls & 0xFFFu) != 0)
        return;
    rendererlog::Line("posecache: lookups=%llu candidates=%llu fpEligible=%llu hits=%llu outMis=%llu preEnvMis=%llu postEnvMis=%llu stores=%llu",
               static_cast<unsigned long long>(g_poseLookups),
               static_cast<unsigned long long>(g_poseCandidates),
               static_cast<unsigned long long>(g_poseFpEligible),
               static_cast<unsigned long long>(g_poseHits),
               static_cast<unsigned long long>(g_poseOutputMismatch),
               static_cast<unsigned long long>(g_posePreEnvMismatch),
               static_cast<unsigned long long>(g_posePostEnvMismatch),
               static_cast<unsigned long long>(g_poseStores));
}

int __cdecl Dispatch(void* renderer, void* pos, void* q, void* seqp, void* animp, std::uint32_t frameBits, FpState* caller)
{
    ++g_calls;
    const int mode = g_mode ? static_cast<int>(g_mode->value) : 0;
    FpState stockAfter{};
    if (mode <= 0)
    {
        InvokeOriginal(renderer, pos, q, seqp, animp, frameBits, caller, &stockAfter);
        std::memcpy(caller, &stockAfter, sizeof(*caller));
        return 0;
    }

    auto* seq = static_cast<mstudioseqdesc_t*>(seqp);
    auto* anim = static_cast<mstudioanim_t*>(animp);
    studiohdr_t* h = nullptr;
    cl_entity_t* ent = nullptr;
    if (!Validate(renderer, seq, anim, frameBits, h, ent))
    {
        ++g_reject;
        ++g_fallback;
        InvokeOriginal(renderer, pos, q, seqp, animp, frameBits, caller, &stockAfter);
        std::memcpy(caller, &stockAfter, sizeof(*caller));
        return 0;
    }

    float frame = 0.0f;
    std::memcpy(&frame, &frameBits, sizeof(frame));
    const float mx = static_cast<float>(seq->numframes - 1);
    if (frame > mx) frame = 0.0f;
    else if (frame < -0.01f) frame = -0.01f;

    using InterpolantFn = float(__fastcall*)(void*, void*);
    using AdjFn = void(__fastcall*)(void*, void*, float, float*,
                                    const unsigned char*, const unsigned char*, int);
    auto** vt = *reinterpret_cast<void***>(renderer);
    auto interpolant = reinterpret_cast<InterpolantFn>(vt[0x2C / 4]);
    auto adjFn = reinterpret_cast<AdjFn>(vt[0x3C / 4]);
    float adj[4]{};
    float scratchPos[kMaxBones][3], scratchQ[kMaxBones][4];
    LARGE_INTEGER t0{}, t1{}, t2{};
    if (mode == 2)
    {
        QueryPerformanceCounter(&t0);
        InvokeOriginal(renderer, pos, q, seqp, animp, frameBits, caller, &stockAfter);
        QueryPerformanceCounter(&t1);
    }

    __asm
    {
        mov eax, caller
        fxrstor [eax]
    }
    const float dadt = interpolant(renderer, nullptr);
    adjFn(renderer, nullptr, dadt, adj, ent->curstate.controller,
          ent->latched.prevcontroller, ent->mouth.mouthopen & 0xFF);

    float (*outPos)[3] = mode == 2 ? scratchPos : static_cast<float(*)[3]>(pos);
    float (*outQ)[4] = mode == 2 ? scratchQ : static_cast<float(*)[4]>(q);

    const int cacheMode = ReadPoseCacheMode();
    const bool poseStats = cacheMode == 2;
    std::uint32_t frameToken = 0;
    const bool haveFrameToken = cacheMode != 0 && ReadFrameToken(renderer, frameToken);
    PoseKey poseKey{};
    if (cacheMode != 0)
        poseKey = MakePoseKey(h, seq, anim, frame, adj, ent->curstate.framerate);
    PoseCacheEntry* cacheEntry = nullptr;
    bool poseCandidate = false;
    bool poseFpEligible = false;
    FpState kernelBefore{};
    FpState kernelAfter{};
    if (haveFrameToken)
    {
        if (poseStats)
            ++g_poseLookups;
        SaveFpState(kernelBefore);
        cacheEntry = &g_poseCache[PoseHash(poseKey) & (kPoseCacheEntries - 1)];
        poseCandidate = cacheEntry->valid && cacheEntry->frameToken == frameToken &&
                        PoseKeyEqual(cacheEntry->key, poseKey);
        if (poseCandidate)
        {
            if (poseStats)
                ++g_poseCandidates;
            poseFpEligible = std::memcmp(cacheEntry->preEnv, kernelBefore.bytes, 32) == 0;
            if (poseStats && poseFpEligible)
                ++g_poseFpEligible;
            else if (poseStats && !poseFpEligible)
                ++g_posePreEnvMismatch;
        }
    }

    // Production cache hit: exact key, same renderer frame, and identical
    // architectural FP environment before the local pose kernel.
    if (mode == 1 && cacheMode == 1 && poseCandidate && poseFpEligible)
    {
        const std::size_t posBytes = static_cast<std::size_t>(h->numbones) * sizeof(outPos[0]);
        const std::size_t qBytes = static_cast<std::size_t>(h->numbones) * sizeof(outQ[0]);
        std::memcpy(outPos, cacheEntry->pos, posBytes);
        std::memcpy(outQ, cacheEntry->q, qBytes);

        FpState replay = kernelBefore;
        std::memcpy(replay.bytes, cacheEntry->postEnv, 32);
        std::memcpy(caller, &replay, sizeof(replay));
        ++g_fast;
        return 1;
    }

    const bool ok = FastKernel(h, seq, anim, frame, adj, ent->curstate.framerate, outPos, outQ);
    if (haveFrameToken)
        SaveFpState(kernelAfter);
    if (mode == 2)
        QueryPerformanceCounter(&t2);
    if (!ok)
    {
        ++g_fallback;
        InvokeOriginal(renderer, pos, q, seqp, animp, frameBits, caller, &stockAfter);
        std::memcpy(caller, &stockAfter, sizeof(*caller));
        return 0;
    }

    if (haveFrameToken)
    {
        if (poseCandidate && cacheMode == 2)
        {
            const std::size_t posBytes = static_cast<std::size_t>(h->numbones) * sizeof(outPos[0]);
            const std::size_t qBytes = static_cast<std::size_t>(h->numbones) * sizeof(outQ[0]);
            if (std::memcmp(cacheEntry->pos, outPos, posBytes) != 0 ||
                std::memcmp(cacheEntry->q, outQ, qBytes) != 0)
                ++g_poseOutputMismatch;
            if (std::memcmp(cacheEntry->postEnv, kernelAfter.bytes, 32) != 0)
                ++g_posePostEnvMismatch;
        }

        cacheEntry = &g_poseCache[PoseHash(poseKey) & (kPoseCacheEntries - 1)];
        cacheEntry->valid = false;
        cacheEntry->frameToken = frameToken;
        cacheEntry->key = poseKey;
        std::memcpy(cacheEntry->preEnv, kernelBefore.bytes, 32);
        std::memcpy(cacheEntry->postEnv, kernelAfter.bytes, 32);
        const std::size_t posBytes = static_cast<std::size_t>(h->numbones) * sizeof(outPos[0]);
        const std::size_t qBytes = static_cast<std::size_t>(h->numbones) * sizeof(outQ[0]);
        std::memcpy(cacheEntry->pos, outPos, posBytes);
        std::memcpy(cacheEntry->q, outQ, qBytes);
        cacheEntry->valid = true;
        if (poseStats)
            ++g_poseStores;
    }

    if (mode == 2)
    {
        ++g_validate;
        if (t1.QuadPart >= t0.QuadPart && t2.QuadPart >= t1.QuadPart)
        {
            g_stockTicks += static_cast<std::uint64_t>(t1.QuadPart - t0.QuadPart);
            g_fastTicks += static_cast<std::uint64_t>(t2.QuadPart - t1.QuadPart);
            ++g_timed;
        }
        float mp = 0.0f, mq = 0.0f;
        auto* sp = static_cast<float(*)[3]>(pos);
        auto* sq = static_cast<float(*)[4]>(q);
        for (int i = 0; i < h->numbones; ++i)
        {
            for (int j = 0; j < 3; ++j)
                mp = (std::max)(mp, Diff(sp[i][j], scratchPos[i][j]));
            for (int j = 0; j < 4; ++j)
                mq = (std::max)(mq, Diff(sq[i][j], scratchQ[i][j]));
        }
        g_maxPos = (std::max)(g_maxPos, mp);
        g_maxQ = (std::max)(g_maxQ, mq);
        if (mp > 0.002f || mq > 0.0005f)
            ++g_mismatch;
        std::memcpy(caller, &stockAfter, sizeof(*caller));
    }
    else
    {
        ++g_fast;
        if (haveFrameToken)
            std::memcpy(caller, &kernelAfter, sizeof(*caller));
        else
        {
            __asm
            {
                mov eax, caller
                fxsave [eax]
            }
        }
    }

    if ((g_calls & 1023) == 0)
    {
        LARGE_INTEGER fq{};
        QueryPerformanceFrequency(&fq);
        const double stockUs = (g_timed && fq.QuadPart)
            ? (1.0e6 * static_cast<double>(g_stockTicks) /
               static_cast<double>(fq.QuadPart) / static_cast<double>(g_timed)) : 0.0;
        const double fastUs = (g_timed && fq.QuadPart)
            ? (1.0e6 * static_cast<double>(g_fastTicks) /
               static_cast<double>(fq.QuadPart) / static_cast<double>(g_timed)) : 0.0;
        rendererlog::Line("fastbones: calls=%llu fast=%llu fallback=%llu reject=%llu validate=%llu mismatch=%llu maxPos=%.6g maxQ=%.6g stock_us=%.3f fast_us=%.3f",
                   g_calls, g_fast, g_fallback, g_reject, g_validate, g_mismatch,
                   g_maxPos, g_maxQ, stockUs, fastUs);
    }
    if (poseStats)
        LogPoseCacheStats();
    return 1;
}

__declspec(naked) void KernelHook()
{
    __asm {
        // Preserve the exact stock path unless the user explicitly requests
        // mode 1 or 2. This tail-jump touches neither the stack nor FP state.
        mov eax, dword ptr [g_mode]
        test eax, eax
        jz stock
        cmp dword ptr [eax + 0Ch], 03F800000h
        je fast
        cmp dword ptr [eax + 0Ch], 040000000h
        je fast
stock:
        jmp dword ptr [g_original]
fast:
        push ebp
        mov ebp, esp
        and esp, -16
        sub esp, 512
        fxsave [esp]
        mov eax, esp
        push eax
        push dword ptr [ebp + 24]
        push dword ptr [ebp + 20]
        push dword ptr [ebp + 16]
        push dword ptr [ebp + 12]
        push dword ptr [ebp + 8]
        push ecx
        call Dispatch
        add esp, 28
        fxrstor [esp]
        mov esp, ebp
        pop ebp
        ret 20
    }
}
}

void Install(HMODULE client, cl_enginefunc_t* engine)
{
    if(!client||!engine||!engine->pfnRegisterVariable){ rendererlog::Line("fastbones: invalid install args"); return; }
    if(!ExactClient(client)){ rendererlog::Line("fastbones: client build mismatch, disabled"); return; }
    g_engine=engine;
    g_mode=engine->pfnRegisterVariable("r_studio_bones","1",0);
    g_poseCacheMode=engine->pfnRegisterVariable("r_studio_posecache","1",0);
    auto* base = reinterpret_cast<std::uint8_t*>(client);
    auto** slot = reinterpret_cast<void**>(base + kRendererVtableRva + kCalcRotationsSlotOffset);
    void* expected = base + kKernelRva;
    if (*slot != expected)
    {
        rendererlog::Line("fastbones: vtable slot mismatch (%p expected %p), disabled", *slot, expected);
        return;
    }
    g_original = expected;
    DWORD oldProtect = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProtect))
    {
        rendererlog::Line("fastbones: unable to make renderer vtable writable");
        g_original = nullptr;
        return;
    }
    *slot = reinterpret_cast<void*>(&KernelHook);
    VirtualProtect(slot, sizeof(void*), oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));
    rendererlog::Line("fastbones: patched renderer vtable CalcRotations slot (mode 0 direct-stock, 1 fast, 2 validate)");
}
}
