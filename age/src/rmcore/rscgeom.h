#ifndef RMCORE_RSCGEOM_H
#define RMCORE_RSCGEOM_H

////////////////////////////////////////
// rmcore/rscgeom.h
//
// Geometry out of a console resource pack: the PS2 rmcModel objects and
// their VIF1 DMA streams, decoded into plain vertex batches.
//
// rmcModel (PS2, vtable 0x007a1e18 in the SLUS-21355 packs), from the city
// pack: +0 vtable, +4 0, +8 u16 pair, +c ptr, +10 ptr, +14 ptr, +18 ptr GS
// block, +1c ptr GS block, +20 int, +30 five {ptr geometry list, u16 count}
// pairs (the game's partNUMPARTS: main, ground, reflect, HDR, alpha), +58/+5c
// ptrs, +60 int.  A geometry list is a chain of 16-byte headers
// {ptr VIF data, u16, u16 numVerts, ptr next, u16, u16}.
//
// The VIF data follows the Sony VIFcode format: 4-byte codes {u16 imm, u8
// num, u8 cmd}; cmd 0x60..0x7f = UNPACK with vn = ((cmd>>2)&3)+1 elements of
// vl = cmd&3 (32/16/8/5-bit) each, num entries, written to VU address imm.
// The batches this port understands (validated over every geometry of the
// city pack):
//   V1-32 @0x9e/0x1c5  : {float scale, count} for the batch that follows
//   V3-16 @0x100/0x227 : positions, s16 * scale
//   V2-16 @0xd0/0x1f7  : texture coordinates, s16 / 4096 (assumed)
//   V4-16 @0xd0/0x1f7  : texture coordinates + 2 extra s16
//   V4-8 / V3-8 @0xa0/0x1c7 : per-vertex normals + ADC (see rscGeomBatch::normals)
//   MSCAL 6            : draw the batch (VU program 6) as a triangle strip
// STMASK/STROW/STCOL/ITOP/NOP are skipped; DIRECT/DIRECTHL (GS register
// packets) end the geometry.
////////////////////////////////////////

#include "core/types.h"
#include "atl/array.h"
#include "vector/vector3.h"
#include "vector/vector2.h"

class datResourceImage;

struct rscTriangle {
	int i0, i1, i2;
};

struct rscGeomBatch {
	atArray<Vector3> verts;
	atArray<Vector2> uvs;
	// Second UV set: z/w of a 4-component uv unpack (VU Tex1, e.g. city_window's window
	// layer "texsrc 1"); 0 entries when the batch has none.
	atArray<Vector2> uvs2;
	atArray<u32> colors;        // RGBA8 (0 entries when the batch has none)
	// VU address 0xa0/0x1c7 "NrmAdc": per-vertex normal (signed bytes / 127) with the
	// strip kick (ADC) bit in the low bit of x; 0 entries when the batch has none.
	// adc also comes from the low bit of U when there is no NrmAdc stream (unlit geometry).
	atArray<Vector3> normals;
	atArray<u8> adc;
	// VU matrix slot per vertex, from the 4th component of a 4-wide position
	// unpack (skinned city ped meshes); 0 entries when the batch has none.
	atArray<u16> bones;
	float scale;

	// Generates clean indexed triangles from this batch.
	// primMode: 0 = auto/strip with quad detection, 1 = list, 2 = fan, 3 = explicit quad
	void BuildTriangles(atArray<rscTriangle> &outTris, int primMode = 0) const;
};

struct rscGeometry {
	int part;                   // index of the model's geometry list (one shader per list, model +0x0c u16 per list) - NOT the city draw pass, which the model type assigns to the whole model
	int index;                  // position in the part's list
	u32 addr;                   // geometry header address
	int declaredVerts;          // header's vertex count
	int shaderIdx;              // index into the model's shader table (car packs: the node's +0xc u16 block)
	atArray<rscGeomBatch> batches;
};

struct rscModel {
	u32 addr;
	atArray<rscGeometry> geometries;
	Vector3 boxMin, boxMax;
	int numVerts;
	bool isTaillight = false;
	bool isHeadlight = false;
	int boneIndex = 0;          // vehicle nodes: u16 at node +6, the skeleton bone the mesh is local to
	atArray<atArray<u32>> cpvSets; // per-instance CPV color tables (RGBA8 packed)
};

// One bone of the vehicle skeleton (crSkeletonData at drawable +0x68; 0x44-byte
// crBoneData records, see crskeleton/skeldata.h).  `global` is the composed rest
// transform (local = FromEulersXYZ(rotation) at offset, times the parent's global)
// that takes a part's bone-local vertices into model space.
struct rscBone {
	char name[48];
	Vector3 restPosition;       // +0x00 absolute rest position (exporter cache)
	Vector3 offset;             // +0x20 rest translation relative to the parent
	Vector3 rotation;           // +0x2c rest rotation, XYZ eulers (radians)
	u16 dofs;                   // +0x10
	int parent;                 // from +0x1c, -1 = root
	Matrix34 global;
};
int rscDecodeSkeleton(const datResourceImage &image, u32 drawableAddr, atArray<rscBone> &out);

// The vehicle's kit manifest: rmcCarModelType keeps, per customisable family
// (front/rear bumper, side skirt, spoiler, hood, blower, lights, brush guard,
// grille, one-shot kit, wheelie bar, body type), a table of bone indices, one per
// kit, index 0 being the stock piece; plus the drop-shadow / neon bone.  A part
// node belongs to the stock car unless its bone (or an ancestor) is a kit bone
// with index >= 1, or the drop-shadow bone.  Offsets follow the port's
// rmcCarModelType(datResource&) reader (24 kits per family on the Remix disc).
struct rscCarKits {
	atArray<u8> variantBone;    // per bone: 1 = a kit bone with index >= 1
	atArray<u8> stockBone;      // per bone: 1 = index 0 of some family
	int dropShadowBone;
	int numFamiliesFound;
};
bool rscDecodeCarKits(const datResourceImage &image, u32 modelTypeAddr, const atArray<rscBone> &bones, rscCarKits &out);
// True when `boneIndex` or any ancestor is a kit variant bone or the drop-shadow bone.
bool rscIsKitVariantBone(const rscCarKits &kits, const atArray<rscBone> &bones, int boneIndex);
// Bakes the bone's rest transform into a decoded part (vertices + normals + box).
void rscPlaceModelOnBone(rscModel &model, const rscBone &bone);

enum { rscModelVTable = 0x007a1e18, rscCarModelVTable = 0x007a0f98, rscPageModelVTable = 0x007a22a0 };

// Every rmcModel in the image (found by its vtable value; `vtable` overrides).
int rscFindModels(const datResourceImage &image, atArray<u32> &addrs, u32 vtable = rscModelVTable);
// Every vehicle model node in the image.
int rscFindCarModels(const datResourceImage &image, atArray<u32> &addrs);
// Decodes one rmcModel's five part lists.
bool rscDecodeModel(const datResourceImage &image, u32 modelAddr, rscModel &out);
// strictHeader false accepts a model whose header does not pass the usual checks -
// a car part node reached through the kit tables, where the caller already knows
// what it is holding.
bool rscDecodeModel(const datResourceImage &image, u32 modelAddr, rscModel &out, bool strictHeader);
// Decodes one vehicle model node.
bool rscDecodeCarModel(const datResourceImage &image, u32 nodeAddr, rscModel &out);
// Decodes the VIF stream at `dataAddr` (up to `endAddr`) into batches.
// maxVerts > 0: stop after the batch that completes that many vertices (a geometry's
// budget); the end address is only a bound.
bool rscDecodeVifStream(const datResourceImage &image, u32 dataAddr, u32 endAddr, atArray<rscGeomBatch> &batches, int maxVerts);
bool rscDecodeVifStream(const datResourceImage &image, u32 dataAddr, u32 endAddr, atArray<rscGeomBatch> &batches);
// Converts one decoded rscModel into gfxModel packets attached to boneIdx.
class gfxModel;
struct gfxModelMaterial;
class rmcDrawable;
class rmcDrawable;
// One entry of a vehicle model's shader table (drawable +0x08 -> group -> table): the
// .shadert template it instantiates and the textures it names, in object order (the
// colour map first for textured templates; __envmap__ / __specular__ / __metalflake__ /
// __decal__ are the shared effect maps).  Layout from vp_lancer_04_g.pck: shader object
// {vtable(class), ..., ptr template name}, followed by inline or pointed texture objects
// (vtable 007a1260: {vtable, flags, ptr name, hash}).
// Page-file drawables (rim / brake pages) name their textures with 007a27a8 objects
// instead: {vtable, VRAM address, ?, ?, TEX0 lo/hi (+0x10/+0x14, PSM / log2 size as in
// the city blocks), zeros, +0x4c block offset, +0x50 {u16 page entry, u16 flags}}; the
// block lives in the same page file as the drawable and has the city block format.
struct rscPageTexRef {
	u32 entry, offset;
	u16 width, height;
	u8 psm;
};
struct rscShaderInfo {
	char templateName[64];
	int numTextures;
	char textures[6][48];
	int numPageTextures;
	rscPageTexRef pageTextures[4];
	int drawBucket = 0;
};
int rscDecodeCarShaders(const datResourceImage &image, u32 drawableAddr, atArray<rscShaderInfo> &out);
// Fills a gfxModelMaterial's car_* shading terms and colour from the shader's
// template name (carpaint, chrome, car_window, colored_glass, emissive lights,
// black_matte, rubber, carbon_fiber, decal...) and its first colour texture name.
// `paint` is the car's paint colour (mkrgba); templates that take the paint get
// car_paint set so gfxModel::SetCarPaint can retint them later.
void rscClassifyCarMaterial(struct gfxModelMaterial &mat, const char *templateName, const char *texName, u32 paint);
// Retints every car_paint material of the drawable's LODs (custom paint job).
void rscSetCarPaint(class rmcDrawable *drawable, u32 paint);
// Hands the paint job's colour ramp to every LOD (gfxCarPaintRamp; the console metal-paint map).
void rscSetCarPaintRamp(class rmcDrawable *drawable, const struct gfxCarPaintRamp &ramp);
// Hands the car's live light levels to the drawable's LODs, so the head/tail/
// brake/reverse lens materials shade as lit or unlit (gfxModel::SetCarLights).
void rscSetCarLights(class rmcDrawable *drawable, const struct gfxCarLights &lights);
// Builds LOD 0 of `target` from every vehicle model node found in `image` (pages
// with no drawable header, e.g. the tyre pages of tire.ppf: a tread texture plus
// the tyre mesh).  The single material is classified from `templateName` (see
// rscClassifyCarMaterial) and textured with `textureName` when given.  False
// when the image holds no model.
bool rscLoadModelsFromImage(const datResourceImage &image, class rmcDrawable *target, const char *templateName, const char *textureName);
// Builds (and registers under `name`) a page texture named by a shader; NULL when the
// format is not handled (PSMT4).
class gfxTexture *rscLoadPageTexture(int ppfSlot, const rscPageTexRef &ref, const char *name);
// Describes the single texture block a texture page holds (owner-less page: the
// block header's size gives the format: 0x1500 = 64x64 PSMT8 + CLUT, 0x4500 =
// 128x128 PSMT8 + CLUT).  False for pages that are not a lone PSMT8 block.
bool rscProbePageTexture(int ppfSlot, int entry, rscPageTexRef &ref);
// A texture resident in a vehicle pack: the rmcTextureProxyPS2 object (vtable
// 0x007a14a0) named `name` carries TEX0 at +0x10 (PSM, log2 width/height) and
// at +0x48 the address of its block {32-byte header, 0x70 pad, pixels, CLUT}.
// The per-car trim atlas ("<car>_trim", 256x256 PSMT8) lives this way.  Builds
// and registers the texture under `name`; NULL when the pack has no such object.
class gfxTexture *rscLoadResidentTexture(const datResourceImage &image, const char *name);
// A texture stored as a block in an owner-less page of a mounted page file, found
// by name: every such page carries one 0x007a27a8 page-texture object per block
// with the texture's name inline at +0xa0 (TEX0 at +0x10, block offset +0x4c,
// page entry +0x50).  The licence plates ("ca_plate_1"...) live this way in
// decal.ppf / decal_g.ppf, four 128x64 PSMT8 plates per page.  Scans the page
// headers of `slot` once and the candidate pages on demand; NULL when absent.
class gfxTexture *rscLoadPageTextureByName(int slot, const char *name);
// The same texture decoded one stage earlier: the 8-bit indices and the 256-entry
// RGBA CLUT, rather than a finished texture.  The licence-plate composite has to
// work in index space, because it writes the *font* texture's indices into the
// plate and colours them through the *plate's* CLUT - that is how a state's plate
// gives its letters their colour.  idx is width*height bytes, clut 256*4.
bool rscLoadPageTextureIndexed(int slot, const char *name, atArray<u8> &idx, u8 clut[256 * 4], int &width, int &height);
// A CSM1 CLUT is stored with the middle 8-entry group of each 32 swapped: put an
// index through this before reading the CLUT that rscLoadPageTextureIndexed gave.
inline int rscClutIndex(int i) { return (((i & 0x18) == 0x08) || ((i & 0x18) == 0x10)) ? (i ^ 0x18) : i; }
// The same index, enumerated.  A caller that has to work out which textures a
// page file actually holds - the rider skins, whose names are not derivable
// from the rider list that selects them - needs to read the names rather than
// guess them.  Builds the index on first use, like the lookup above.
int rscGetNumPageTextures(int slot);
const char *rscGetPageTextureName(int slot, int index);

// ---------------------------------------------------------------------------
// City textures.  A city pack's shader group (root +0x0c -> groups[g], 16 bytes:
// vtable, table, u16 count) lists rmcShader instances {vtable, 0, ptr data block};
// the 160-byte data block holds TEX0 for up to three mip levels (+0x10/+0x14 is
// mip 0: PSM at bits 20-25 (0x13 PSMT8, 0x14 PSMT4), log2 width/height at 26-29 /
// 30-33) and, per mip, the page file entry (low 16 bits of +0x50 / +0x5c / +0x68)
// and the byte offset of the texture's block inside that 68 KB page (+0x4c / +0x58
// / +0x64).  +0x48 names a resident copy instead: {0, hash, ptr data, size}.
// A texture block = 32-byte header {owner, 0, 0, size incl. header, CD, ffff|kind,
// 0, CD}, 112 bytes of CD padding, the swizzled indices, and - in the smallest
// mip's block only - the 256-entry RGBA CLUT (alpha 0x80 = opaque), i.e. pixels at
// block +0x90.  Validated on sd_midnight_clear (graffiti, brick, signage decode).
// A city geometry picks its shader through the u16 per geometry at model +0x0c.
// ---------------------------------------------------------------------------
struct rscTexInfo {
	u8 psm;
	// The pixels are stored unswizzled and are read straight through.  The city
	// and the rider skins are written through a 32-bit frame buffer and need
	// rscUnswizzlePsmt8; the vehicle decal page file is not (the PS2 clears
	// rmcEnableSwizzle around the plate, decal and logo textures), and running
	// the unswizzle over it scrambles every plate.  Zero-initialised = swizzled.
	bool linear;
	u16 width, height;
	int numMips;
	u32 mipEntry[3];
	u32 mipOffset[3];
	u32 residentAddr;     // image address of the resident mip-0 block header (0 = paged)
	u32 residentSize;
	// Resident textures (the 25 shared ones in a city, e.g. the road surface every
	// road shader references through a 007a20e0 wrapper): data block +0x48, +0x54,
	// +0x60, +0x6c point at one 32-byte block header per mip {0, hash, ptr object,
	// size, CD, ffff|kind, 0, CD}; the pixels follow the header at +0x90 and the
	// CLUT follows the smallest mip's pixels, as in the paged blocks.
	int numResidentMips;
	u32 residentMip[4];
	// The shader instance's +4 word: bits 0-6 = class (0 rmcShaderBasic, 1 rmcShaderComplex,
	// 2 rmcShaderInstance, 16 mcShaderCityWindow, 17 road instance, 19 texscroll, 20
	// mcShaderTexAnimation; 0xff = no instance), bits 15-21 = template slot for the
	// instance classes (Remix: 0 city_facade, 1 city_window, 2 city_window_daytime,
	// 3 city_window_cutout, 4 city_road, 5 hdr_object, 6 doublesided,
	// 7 double_sided_hdr_object, 8 doublesided_prop).
	u8 shaderType;
	u8 templateSlot;
	// mcShaderCityWindow (class 16): param 1 at instance +0xc = the window layer's texture
	// object (parse with rscParseTexInfo), params 2-4 at +0x10.. = WindowTint as floats (GS
	// scale, 128 = 1.0).  The template draws it as a second pass: texsrc 1, modulate by the
	// tint, alphablend on, colorwrite rgb.  0 / 255 when the shader has no layer.
	// The road instance (class 17, city_road) uses the same slots differently: param 1 = the
	// detail texture (texture %1), params 2-3 = its scales / scalet (UV multipliers, 4.0).
	u32 layerTex;
	u8 tint[3];
	float layerScale[2];
	// water.shadert: the surface scrolls its UVs and blends translucently.
	bool isWater;
	float scrollU, scrollV;       // UV units per second
};
int rscDecodeCityShaders(const datResourceImage &image, int groupIndex, atArray<rscTexInfo> &out);
// How the PS2 draws a city geometry, from the draw part its MODEL is filed under in the
// city model type (type +0x3c + (lod * 5 + part) * 4: 0 main, 1 ground, 2 reflect, 3 HDR,
// 4 alpha; citymodel.cpp pass states - not rscGeometry::part) and its shader's class /
// template (template states override the pass).  Texture alpha never decides it.
// -1 = unknown class.
enum rscCityBlend {
	rscCityOpaque = 0,      // blend Cs, alpha test always
	rscCityBlend45 = 1,     // rmcbsNormal (SrcAlpha, InvSrcAlpha), alpha > 45 (PS2 scale, 90 of 255 decoded)
	rscCityInvBlend = 2,    // city_window_cutout: ps2blend Cd Cs As Cs = As*Cd + (1-As)*Cs
	rscCityWater = 3,       // water.shadert: translucent alpha blend, two-sided, depth-write off
};
int rscCityBlendState(int part, int shaderType, int templateSlot, bool isWater = false);
// A shader's second LAYERED_TEXTURE map, for the templates that carry one.  The garage
// interior is the one place in the game that ships baked lightmaps: the shaders in
// <city>_<tod>_<weather>_garage_props.pck that instantiate garage_lightmap_subtract(_*)
// draw "BaseTexture, texsrc 0" then "LightMap, texsrc 1, blendset lightmap" - the
// lightmap multiplies the base through the geometry's second UV set.  These shaders
// (vtable 0x007a1f58) keep their template name at +0x28 and the second map's image
// object at +0x24.  False unless both a ".shadert" name and a parsable image are there.
bool rscDecodeShaderLayerMap(const datResourceImage &image, u32 shaderAddr, rscTexInfo &out,
                             char *templateOut, int templateLen);
// A shader's template name on its own (at +0x28), for the classes that keep one but
// have no second map - the prop packs' city_flarebg, city_flarelightmap, water and
// road_reflector.  The name is the only thing that says a billboard is a glow rather
// than ordinary textured geometry.  False when there is no valid ".shadert" name.
bool rscDecodeShaderTemplateName(const datResourceImage &image, u32 shaderAddr,
                                 char *templateOut, int templateLen);
// Where a shader's own gfxImage lives.  The plain classes keep a pointer to it at
// +0x08, but a shader that carries a template name (city_flarebg, city_flarelightmap,
// water, road_reflector, city_specular, city_texscroll_doublesided) stores the name
// inline at +0x30 and EMBEDS its image right after it, 16-byte aligned; the +0x08 word
// is something else, and following it landed rscParseTexInfo on whatever object
// happened to come next.  That is how the city's wall lamps drew a lit-window texture
// - a bright white slab - in place of their glow.  0 when nothing usable is there.
u32 rscShaderImageAddr(const datResourceImage &image, u32 shaderAddr);
// Extracts texture dimensions, format, and resident/page offsets from a gfxImage block (vtable 0x007a2320 or wrapper).
bool rscParseTexInfo(const datResourceImage &image, u32 blk, rscTexInfo &info);
// One rmcShaderGroup at `groupAddr` ({vtable, table @+4, u16 count @+8}), slot by slot; a
// city root's sky group is its +0x10 word.
int rscDecodeShaderGroup(const datResourceImage &image, u32 groupAddr, atArray<rscTexInfo> &out);
// The mcSkyHatClass layer models (layer 0 = the dome), found through its "skyhat_<city>_<tod>"
// name like the game's city builder; their shader indices address the sky shader group.
int rscFindSkyhatModels(const datResourceImage &image, atArray<u32> &addrs);
// PSMT8 stored through a 32-bit frame buffer ("swizzled"), width a multiple of 16.
void rscUnswizzlePsmt8(const u8 *src, u8 *dst, int width, int height);
// The same with the row-parity column swap chosen explicitly.
void rscUnswizzlePsmt8(const u8 *src, u8 *dst, int width, int height, bool swap);
// Mean RGB step between neighbouring texels after unswizzling PSMT8 `src` with/without the
// swap through the CSM1 `clut` (256 x RGBA8); a wrong unswizzle scrambles blocks and scores higher.
float rscPsmt8Roughness(const u8 *src, const u8 *clut, int width, int height, bool swap);
// PSMT4 stored through a 32-bit frame buffer ("swizzled").
void rscUnswizzlePsmt4(const u8 *src, u8 *dst, int width, int height);
// Builds (and registers under `name`, see gfxRegisterRgbaTexture) the texture
// described by `info`, reading its blocks from page file slot `ppfSlot` or from the
// image when resident.  NULL when the format is not handled yet (PSMT4).
class gfxTexture *rscLoadCityTexture(const datResourceImage &image, int ppfSlot, const rscTexInfo &info, const char *name);
// The same with a GS-style modulation baked in before it is registered: RGB *= rgbScale/128
// (NULL = none) and alpha *= alphaScale/128, both clamped - e.g. a window layer's tint.
class gfxTexture *rscLoadCityTexture(const datResourceImage &image, int ppfSlot, const rscTexInfo &info, const char *name,
                                     const u8 *rgbScale, int alphaScale);

// shaderToMaterial (numShaders entries) maps a geometry's shaderIdx to a material index
// already appended to outModel; without it every packet uses material 0.
bool gfxModelFromRscModel(const rscModel &partModel, int boneIdx, gfxModel &outModel, const int *shaderToMaterial = 0, int numShaders = 0);
// Decodes a console city image (vtable 0x007a2320) and registers it into gfxTexture.
bool rscDecodeCityImage(const datResourceImage &image, u32 imgAddr, char *outTexName = 0, size_t maxLen = 0);
// Decodes a complete rmcDrawable / drwShaderModel from its resource image address using its LOD table.
// ppfSlot: the page file the drawable's page textures live in (page-file drawables);
// -1 = none.
rmcDrawable *rscLoadDrawableFromResource(const datResourceImage &image, u32 drawableAddr, rmcDrawable *target = nullptr, int ppfSlot = -1);

// ---------------------------------------------------------------------------
// Vehicle page files (resources/vehicle/rim|brake|exhaust.ppf).  A page is a
// 32-byte header {owner, 0, 0, size incl. header, CD, kind, 0, CD} followed by
// the payload.  For an object page `owner` is the address of the proxy that
// names it in decal_g.pck and is also the address the payload was built at: the
// drwShaderModel sits at payload +0 (vtable 007aa728, shader group at +8, LOD
// tables at +0x10..+0x1c, 16 bytes each: u16, u16 count, class word, ptr nodes,
// 0xff - no name / bone arrays - with the node pointers following inline), the
// geometry nodes are vtable 007a22a0 and the VIF data comes last.  Texture pages
// (tire.ppf, the 173 texture-only rims, the page before each mesh rim) have owner
// 0 and hold swizzled PSMT8 indices at +0x70 of the payload then the CLUT.
// Loads an object page as a resource image based at its owner address; false
// for owner-less pages.  Validated on rim.ppf 30/33, brake.ppf 1, exhaust.ppf 0.
// ---------------------------------------------------------------------------
bool rscLoadPageImage(int ppfSlot, int entry, datResourceImage &image, const char *name);
// Builds a complete gfxModel containing all vehicle part packets tagged by bone index.
bool rscBuildCarGfxModel(const datResourceImage &image, u32 tblAddr, gfxModel &outModel);

#endif // RMCORE_RSCGEOM_H

