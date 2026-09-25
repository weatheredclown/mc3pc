////////////////////////////////////////
// model.h
////////////////////////////////////////

#ifndef GFX_MODEL_H
#define GFX_MODEL_H

#include "core/output.h"
#include "core/types.h"
#include "vector/Matrix34.h"
#include "vector/matrix44.h"
#include "gfx/rv1mem.h"
#include "gfx/shader.h"
#include "atl/bitset.h"

class gfxModel;
gfxModel *gfxGetModel(const char *name,int lod = 0);
gfxModel *gfxGetModel(const char *name,int flags,int fvf);
// <subfolder>/<name>; the bare name first for callers that pushed the folder (model.cpp).
gfxModel *gfxGetModelPrefix(const char *subfolder, const char *name, int flags = 0, int fvf = 0);

class gfxGeometry {
public:
    // Vertex quantisation the PS2 exporter/loader used (bits per position,
    // texture coordinate and normal component).  The PC loader keeps every
    // attribute as float, so these only round-trip what the game sets (car
    // loads force 16-bit positions; the turbo-blur test forces 32-bit UVs).
    static int GetPositionBits() { return sm_PositionBits; }
    static void SetPositionBits(int bits) { sm_PositionBits = bits; }
    static int GetTexCoordBits() { return sm_TexCoordBits; }
    static void SetTexCoordBits(int bits) { sm_TexCoordBits = bits; }
    static int GetNormalBits() { return sm_NormalBits; }
    static void SetNormalBits(int bits) { sm_NormalBits = bits; }
    virtual ~gfxGeometry() {}
    virtual void Draw() const { Quitf("gfxGeometry::Draw - not implemented"); }
    virtual void DrawSkinned(class Matrix34 *matrices) const { Quitf("gfxGeometry::DrawSkinned - not implemented"); }
    virtual class gfxModel *GetModel() const { return 0; }
private:
    static int sm_PositionBits;
    static int sm_TexCoordBits;
    static int sm_NormalBits;
};

// Model name cache maintenance.  gfxGetModel hands out cache-owned models
// and counts each hand-out as a reference; gfxFreeModel / Release drop one.
// Prune deletes the cached models nobody references any more (the game
// calls it between level layers), Kill deletes every cached model, Print
// lists the cache (name, refs, vertex count).
void gfxModelPruneHashtable();
void gfxModelKillHashtable();
void gfxModelPrintHashtable();

class rmcModelGeometry : public gfxGeometry {
public:
    class gfxModel *model;
    rmcModelGeometry(class gfxModel *m) : model(m) {}
    virtual ~rmcModelGeometry();
    virtual void Draw() const override;
    virtual void DrawSkinned(class Matrix34 *matrices) const override;
    virtual class gfxModel *GetModel() const override { return model; }
};

class gfxModelBase {
public:
    // Materials whose name ends with this suffix (mc3: "_dmg", the damage
    // overlays) are drawn after every other packet of the model, so their
    // alpha-blended surfaces land on top of the body they cover.  NULL/""
    // disables it.  Read by the loader as each model is parsed.
    static void SetAlphaMaterialSuffix(const char *suffix);
    static const char *GetAlphaMaterialSuffix();
    enum {
        OK_IF_MISSING = 1,
        NO_RESKIN = 2
    };

    int m_ShaderCount;
    class gfxGeometry *m_Geometries[64];
    class gfxShader *m_Shaders[64];

    gfxModelBase() : m_ShaderCount(0) {
        for (int i = 0; i < 64; i++) {
            m_Geometries[i] = NULL;
            m_Shaders[i] = NULL;
        }
    }

    // Delegates to the first geometry's model; a hardcoded 0 here made every
    // multi-bone old-format skinned entity (lab doors/hatches) fail the
    // palette assert in rndLODGroup::PreDrawSkeletalSetup.
    int GetMatrixCount() const;
    // Delegates to the first geometry's model verts; a zero box here collapses
    // rndLODGroup's cull sphere to a point and culls old-format entities that
    // are plainly on screen.
    void GetBoundingBox(class Vector3 &min, class Vector3 &max) const;
    virtual ~gfxModelBase() {
        for (int i = 0; i < m_ShaderCount; i++) {
            delete m_Geometries[i];
        }
    }
    bool LoadMod(const char *type, const char *filename, int flags, int fvf, class gfxShaderFactory *factory);
    int GetShaderCount() const { return m_ShaderCount; }
    class gfxShader **GetShaders() { return m_Shaders; }
    class gfxShader **GetShaders() const { return (class gfxShader **)m_Shaders; }
};

#include "atl/array.h"
#include <string>
#include <vector>
#include "vector/vector2.h"
#include "gfx/misc.h"

struct gfxModelAdjunct {
    u32 vertex_idx;
    u32 normal_idx;
    u32 color_idx;
    int tex1_idx;
    int tex2_idx;    // second UV set (lightmap/detail); -1 when absent
    u32 bone_idx;
};

struct gfxModelStrip {
    u32 type; // 1=str, 2=stp
    atArray<u32> indices;
};

// "reskin <adjunct> <matrix> <w0> <w1> <w2> <w3>": the blend weights for a
// vertex shared between matrices.  The PC renderer draws rigid per-matrix and
// never reads these, but they are part of the file - kept so a model that is
// loaded and written back out is not a skinning-weight-shaped hole.
struct gfxModelReskin {
    int a, b;
    float w[4];
};

// The car light a lens material answers to (gfxModelMaterial::car_light).
enum eCarLight {
    CAR_LIGHT_NONE = 0,
    CAR_LIGHT_HEAD,        // headlights / fog lights
    CAR_LIGHT_TAIL,        // running lights
    CAR_LIGHT_BRAKE,       // running lights + brakes
    CAR_LIGHT_THIRDBRAKE,  // the high-mounted brake light: brakes only
    CAR_LIGHT_REVERSE
};

// The car's live light levels, 0..1 each (rmcCarModel::Update computes them and
// hands them down through rscSetCarLights).
struct gfxCarLights {
    float head = 0.0f;
    float tail = 0.0f;
    float brake = 0.0f;
    float thirdBrake = 0.0f;
    float reverse = 0.0f;
};

// PC PORT: a car paint job's colour ramp - the stops mcCarPaintJob::GetGradientData
// produces (gloss: white, black, base colour; metallic: white, normalised colour x
// scale, base colour; ...).  The consoles wrote them into the 256-entry palette of the
// metal-paint map (mcCarMetallicPaint::MakeGradient), whose pixels hold their distance
// from the map centre, and drew carpaint through that map with reflection texgen and
// modulate2x: panels facing the viewer read the start of the ramp, grazing ones its
// end.  gfxModel::Draw evaluates the same ramp per vertex for car_paint materials.
struct gfxCarPaintRamp {
    enum { kMaxStops = 8 };
    int count = 0;                  // < 2: no ramp, car_paint shades with car_color
    int pos[kMaxStops] = {};        // 0..255 along the ramp
    float rgb[kMaxStops][3] = {};
};

struct gfxModelPacket {
    atArray<gfxModelAdjunct> adjuncts;
    atArray<gfxModelStrip> strips;
    u32 material_index;
    atArray<u32> bone_map;
    atArray<gfxModelReskin> reskins;   // passthrough; see gfxModelReskin
    bool draw_last = false;   // material matched gfxModelBase::SetAlphaMaterialSuffix at load
};

struct gfxModelMaterial {
    std::string name;
    float diffuse[3];
    // Passthrough: the renderer lights from diffuse alone, but ambient,
    // specular and the illumination keyword belong to the file and would
    // otherwise be flattened to zero by a save.
    float ambient[3] = { 0.0f, 0.0f, 0.0f };
    float specular[3] = { 0.0f, 0.0f, 0.0f };
    std::string illum = "diffuse";
    std::string texture_name;    // texture slot 0
    std::string texture_name2;   // texture slot 1 (sampled via the tex2 UV set)
    // The names as the .mod itself spelled them, before the shader fallback
    // filled any in and before the game retextured the material at runtime (the
    // rider skin, car paint).  Save writes these, so exporting a model that has
    // been retextured in memory does not bake the runtime choice into the file.
    std::string source_texture_name;
    std::string source_texture_name2;
    bool has_source_textures = false;
    int tex2_mode = 0;           // stage-2 combine: 0 modulate (lightmap), 1 decal (blend by alpha), 2 add weighted by the base texture's alpha (city windows), 3 decal of the vertex-coloured layer (city_road detail)
    // Non-zero: stage-2 UVs are the first UV set times this (city_road "scales/scalet") instead of the second set.
    float tex2_uvscale[2] = { 0.0f, 0.0f };
    bool tex2_reflect = false;   // stage-2 coordinates generated from view-space reflection vector (city_window_daytime)
    // PC PORT: from the material's .shader (sMaterialFromShader): "lighting
    // none" and "depthwrite off" — FX materials like BCSteam are unlit,
    // non-depth-writing translucents; ignoring these drew them as lit opaque
    // sheets (milky skirts around the M03 trash piles).
    bool unlit = false;
    bool no_zwrite = false;
    // "drawbucket <n>": render-bucket this material draws in (-1 = default
    // opaque bucket 0).  "slides"/"slidet waveform <amp> sawtooth <rate>":
    // UV scroll rates (u/v per second) — conveyor belts, drifting steam.
    int drawbucket = -1;
    float scroll_u = 0.0f;
    float scroll_v = 0.0f;
    float scroll_u2 = 0.0f;
    float scroll_v2 = 0.0f;
    bool additive = false;       // "blendset add" (pass 0): src*alpha + dest
    // PC PORT: the geometry is a camera-facing billboard, not a fixed quad.
    // MC3's city flare shaders (mcShaderFlareTexscroll, mc3 mclevel/shaders.cpp)
    // rebuild the world matrix on every draw so the flare turns to face the
    // camera about its own up axis.  Drawn with the authored matrix instead,
    // each flare shows as the slanted rectangle it is modelled as - the white
    // slabs under the city's wall lamps.
    bool billboard = false;
    // PC PORT: city materials (mclevel/level.cpp): the PS2 draw state from the model's
    // city pass and the shader class/template (rmcore rscCityBlendState) - -1 not a city
    // material, 0 opaque, 1 rmcbsNormal + alpha test > 45, 2 city_window_cutout's
    // inverted blend.  Texture alpha does not decide it.
    int city_blend = -1;
    // PC PORT: vehicle materials (rmcore/rscgeom.cpp classifies the pack's shader
    // templates).  car_shade materials ignore diffuse/lighting: gfxModel::Draw
    // lights them per vertex (hemisphere ambient + key light + Blinn-Phong +
    // fresnel clearcoat / environment tint), the same model rscview renders with,
    // and blends the glass ones.  car_paint materials take the car's paint colour
    // (gfxModel::SetCarPaint).  car_chrome reflects the bound texture through the
    // reflection-vector texgen when no colour texture is set.  car_cutout
    // materials are images only (decals, the drop shadow): nothing draws until
    // their texture binds.
    bool car_shade = false;
    bool car_paint = false;
    bool car_blend = false;
    bool car_metallic = false;
    bool car_emissive = false;
    bool car_glass = false;
    // Glass opacity is car_color's alpha face-on, rising by this much at grazing
    // angles (fresnel).  Windows use the original drwShaderCarWindows range
    // (WinFresnelMin 0.2 .. WinFresnelMax 0.9); light lenses keep 0.45.
    float car_glass_fres = 0.45f;
    bool car_chrome = false;
    bool car_cutout = false;
    // Which of the car's lights drives this lens.  MC3's light shaders
    // (drwShaderTaillight and friends in mcgfx/mcShader.c) never switch the
    // lens geometry on and off - they raise the light group's AMBIENT toward
    // white by an "emissive value" taken from the car's current light state.
    // An unlit lens is therefore just a normally lit textured surface, and only
    // a lit one washes out to its base colour.  car_light names the value to
    // use; gfxModel::SetCarLights supplies them.
    u8 car_light = 0;             // eCarLight
    u32 car_color = 0xffffffffu;  // (a<<24)|(r<<16)|(g<<8)|b
    float car_spec = 0.0f;
    float car_gloss = 1.0f;
    float car_reflect = 0.0f;
    u32 primitive_count = 0;
    u32 packet_count = 0;
};

enum eFVF {
    FVF_VCT1 = 1,
    FVF_VNT1 = 2
};

class gfxModel
{
public:
    atArray<Vector3> vertices;
    atArray<Vector3> normals;
    atArray<gfxPackedColor> colors;
    atArray<Vector2> tex_coords;
    atArray<Vector2> tex_coords2;   // second UV set (lightmap/detail)
    atArray<atArray<gfxPackedColor>> cpv_sets; // per-instance CPV color tables
    atArray<gfxModelMaterial> materials;
    atArray<gfxModelPacket> packets;
    class gfxShader m_Shader;
    class gfxShader* m_ShaderStub[1];

    // Sets car_color on every car_paint material (vehicles: the custom paint job).
    void SetCarPaint(u32 rgba);
    // The car's current light levels, read by the car shading for any material
    // with a car_light.  Cheap enough to push every frame.
    void SetCarLights(const gfxCarLights &lights) { m_CarLights = lights; }
    const gfxCarLights &GetCarLights() const { return m_CarLights; }
    // The car's paint ramp (see gfxCarPaintRamp); pushed with the paint colour.
    void SetCarPaintRamp(const gfxCarPaintRamp &ramp) { m_CarPaintRamp = ramp; }
    // How lit a material's lens is right now, 0..1, following the emissive
    // values the original light shaders compute in their Bind.
    float GetCarLightLevel(const gfxModelMaterial &m) const;
    // Highest skeleton bone index any packet's bone map names (0 = every packet
    // is on the root, so a single world matrix places the whole model).
    int GetMaxBoneIndex() const;

    unsigned m_LastFrameDrawn;
    // PC PORT: render-bucket filter for the current Draw call (-1 = draw all
    // packets).  With a pass >= 0, packets draw only in their material's
    // drawbucket (unflagged materials = bucket 0) — steam/glow order after
    // the opaques the way the PS2 bucket loop did.
    int m_DrawPassFilter = -1;
    // Packet draw order: identity unless some packets are flagged draw_last
    // (alpha-suffix materials), which then trail the rest.  Built at load.
    atArray<int> m_DrawOrder;
    int PacketDrawIndex(int i) const { return m_DrawOrder.GetCount() == packets.GetCount() ? m_DrawOrder[i] : i; }
    void BuildDrawOrder();
    // Environment-map override for the current Draw (DrawEnvMapped): every
    // packet samples this texture through reflection-vector texgen, unlit,
    // at this alpha.
    struct EnvMapDraw { class gfxTexture *tex = nullptr; float alpha = 1.0f; bool active = false; };
    EnvMapDraw m_EnvMap;
    // Set per frame by the car that owns this model; all zero for everything else.
    gfxCarLights m_CarLights;
    gfxCarPaintRamp m_CarPaintRamp;
    // Outstanding references (gfxGetModel hand-outs + AddRef).
    int RefCount;

    bool m_IsModelRelative;

    gfxModel() : MatrixCount(0), m_LastFrameDrawn(0xffffffffu), RefCount(0), m_IsModelRelative(false) {
        m_ShaderStub[0] = &m_Shader;
    }

    enum { OK_IF_MISSING = 1 };

    class gfxShader** GetShaders() { return m_ShaderStub; }
    int GetShaderCount() const { return 1; }
    static void SetAlphaMaterialSuffix(const char *suffix) { gfxModelBase::SetAlphaMaterialSuffix(suffix); }

    static gfxModel* Create(const char* name) {
        return gfxGetModel(name, 0);
    }

    static gfxModel* Create(const char* type, const char* name, int flags, int fvf) {
        return gfxGetModel(name, flags, fvf);
    }

    static void Delete(gfxModel *model) { if (model) model->Release(); }
    // Drop one model from the name cache (and free it) so the next
    // gfxGetModel of that name reloads from disk.
    static void DeleteModelHash(const char *name);
    void Delete() { Release(); }

	int GetMatrixCount() const	{return MatrixCount;}
	bool IsModelRelative() const { return m_IsModelRelative; }
	void SetModelRelative(bool mr) { m_IsModelRelative = mr; }

	void Draw(Matrix44 *matrices, int pass, const class atBitSet *enables, int cpvIndex = -1);
	void Draw(Matrix44 *matrices, int pass, int cpvIndex);
	void Draw(Matrix44 *matrices, int pass);
	void Draw(Matrix44 *matrices = NULL) { Draw(matrices, -1); }
	void Draw(const Matrix34 &mtx, int pass, int cpvIndex);
	void Draw(const Matrix34 &mtx, int pass);
	void Draw(const Matrix34 &mtx) { Draw(mtx, -1); }
	void Draw(Matrix34 *matrices, int pass = -1, int cpvIndex = -1) {
		if (matrices) Draw(*matrices, pass, cpvIndex);
		else Draw((Matrix44*)NULL, pass, cpvIndex);
	}
	void DrawTexturesOnly(Matrix34 *matrices = NULL) { Draw(matrices); }
	void DrawTexturesOnly(const Matrix34 &mtx) { Draw(mtx); }
	// rmcore-style entry points (shader group + per-draw data).  A plain
	// gfxModel carries its materials in its packets, so the group is not
	// consulted; `bucket` selects the render bucket (material drawbucket,
	// unflagged = 0) and the model's single colour set serves every cpvIndex.
	void Draw(const class rmcShaderGroup &shaderGroup, const union rmcShaderData *data, int bucket, int lod, class atBitSet *enables = nullptr, int variant = 0) const;
	void DrawCpv(const class rmcShaderGroup &shaderGroup, const union rmcShaderData *data, int bucket, int lod, int cpvIndex = 0, int variant = 0) const;
	void DrawSkinned(Matrix34 *matrices);
	void DrawSkinned(const class rmcShaderGroup &shaderGroup, const union rmcShaderData *data, const class Matrix34 *mtxs, int mtxCount, int bucket = 0, int lod = 0) const;
	void DrawSkinnedTexturesOnly(Matrix34 *matrices) const { const_cast<gfxModel*>(this)->DrawSkinned(matrices); }
	void DrawSkinnedTexturesOnly(Matrix44 *matrices) const;
    void GetBoundingBox(class Vector3 &min, class Vector3 &max) const;
    void AddRef() { ++RefCount; }
    // Drop a reference.  A model outside the name cache frees itself at
    // zero; a cache-owned one stays until gfxModelPruneHashtable.
    void Release();
    int GetRefCount() const { return RefCount; }
    int GetAdjunctCount() const { return vertices.GetCount(); }
    void GetPosition(Vector3 &out, int idx) const { if (idx >= 0 && idx < vertices.GetCount()) out = vertices[idx]; else out = Vector3(0.0f, 0.0f, 0.0f); }
    // Draw the model reflecting `tex` (sphere-mapped through the view-space
    // reflection vector), unlit, at `alpha` under the caller's blend set --
    // the fixed-function environment-map pass (car paint reflections, the
    // turbo-blur distortion).  NULL tex reflects the current RSTATE texture.
    void DrawEnvMapped(int lod = 0, class gfxTexture *tex = nullptr, float alpha = 1.0f);

    bool Save(class Stream *s) const;
    bool Save(const char *filename) const;

    // Header/trailer fields the renderer has no use for, kept so Save is the
    // inverse of the load rather than a lossy re-export.  -1 = the source file
    // did not carry it.
    int SourcePrimitiveCount = -1;   // header "primitives"
    int SourceAdjunctCount = -1;     // header "adjuncts"
    int SourceReskinCount = -1;      // header "reskins"
    // "mtxv" / "mtxn": one vertex and normal count PER MATRIX, in matrix order,
    // summing to the model's vertex and normal totals - so they also say which
    // matrix owns each vertex.  Read as a single number they look like a count
    // and the rest of the table is lost.
    atArray<int> MtxVertCounts;
    atArray<int> MtxNormCounts;

	int MatrixCount;	// +00  number of skinning matrices
};

// ".em" edge list for stencil shadow volumes (gfx/edgemodel.cpp).
class gfxEdgeModel {
public:
    bool IsOptimized;
    gfxEdgeModel() : IsOptimized(false) {}

    // Stencil shadow pass (mc3 mcShadow "nice shadows"): Init clears the
    // stencil and switches RSTATE into volume counting; every
    // GenerateShadowVolume between rasterises its volume into the stencil;
    // Finalize darkens the pixels the volumes covered by `color` (dest -
    // color) and restores the state.  Calls are static: one pass serves
    // any number of edge models.
    static void InitShadowVolumePass();
    static void FinalizeShadowVolumePass(u32 color = 0);
    static bool IsShadowVolumePassActive();

    enum ExtrudeStyle {
        asPoint = 0,
        asDir = 1
    };

    struct SilSource {
        Vector3 Pos;    // asPoint: light position; asDir: direction the light travels
        int Style;
    };

    // Volume from the silhouette (skinned with m[bone], extruded by f).
    // Inside an Init/Finalize pass it only marks the stencil; outside one
    // (mc3 without "nice shadows") it runs a self-contained pass so the
    // covered pixels are darkened once by c1 regardless of face overlap.
    // `closed` requests end caps (only the extruded far cap matters for the
    // depth-pass count); `zpass` selects that count mode (the only one the
    // backend implements).
    void GenerateShadowVolume(const SilSource &src, float f, int bones, const Matrix34 *m, u32 c1, u32 c2, u32 c3, u32 c4, bool closed = true, bool zpass = true) const;

    struct Vert { Vector3 Pos; int Bone; };
    struct Tri  { int V[3]; };
    struct Edge { int V[2]; int T[2]; };
    std::vector<Vert> Verts;
    std::vector<Tri>  Tris;
    std::vector<Edge> Edges;

    static gfxEdgeModel* Create(const char* name);
    static gfxEdgeModel* Create(class datTokenizer& tok);

    // Skins with m[bone], extrudes the light-facing silhouette by `f` and draws
    // the volume sides with vgl (caller: RSTATE.SetStencilMode(1)).
    void GenerateSilhouette(const SilSource &src, float f, int bones, const Matrix34 *m, u32 c1, u32 c2, u32 c3, u32 c4) const;

    struct Packet {
        static bool IsCW(const Vector3 &v1, const Vector3 &v2, const Vector3 &v3);
    };
};

// Build a renderable model from an editor mesh (".mesh" files: BlastFire, coronas).
gfxModel *gfxModelFromMesh(const class mshMesh &mesh);

// Loaders
gfxModel *gfxGetModel(const char *name,int lod);
gfxModel *gfxGetModel(const char *name,int flags,int fvf);
// Drop the reference gfxGetModel handed out (see gfxModel::Release).
inline void gfxFreeModel(gfxModel *model) { if (model) model->Release(); }

// Overridable model filename extension (".mod", or "xmod" for the PC export).
extern const char *gfxModelFilenameExtension;

// RSTATE comes along with the model API (game sources rely on it).
#include "gfx/rstate.h"

#endif // GFX_MODEL_H
