////////////////////////////////////////
// misc.h
////////////////////////////////////////

#ifndef GFX_MISC_H
#define GFX_MISC_H

#include "core/types.h"
#include "vector/vector2.h"
#include "vector/vector3.h"
#include "gfx/statetypes.h"
#include "gfx/texture.h"

typedef u32 gfxPackedColor;

#ifndef FIXCOLOR
#define FIXCOLOR(c) (c)
#endif

#define MKRGBA(r,g,b,a) (((a)<<24)|((r)<<16)|((g)<<8)|(b))
#define MKRGB(r,g,b) (((255)<<24)|((r)<<16)|((g)<<8)|(b))

class gfxColor {
public:
    float r, g, b, a;
    gfxColor() : r(1.0f), g(1.0f), b(1.0f), a(1.0f) {}
    gfxColor(float _r, float _g, float _b, float _a = 1.0f) : r(_r), g(_g), b(_b), a(_a) {}
};

#include "vector/Vector4.h"

class gfxMaterial {
public:
    static gfxMaterial FlatWhite;
    gfxColor Color;
    Vector4 diffuse;
    Vector4 emissive;
};

// Memory buckets the model and texture loaders allocate under (eMemoryBucket,
// data/memory.h).  The game saves/restores them around level component loads
// so the tagged allocator (data/memory.cpp) attributes geometry and texture
// memory to the right partition.  gfxGetModel / gfxGetTexture scope their
// loads with datUseMemoryBucket(GetGeometryBucket()/GetTextureBucket()).
class gfxMemory {
public:
    static int GetGeometryBucket() { return sm_GeometryBucket; }
    static int GetTextureBucket() { return sm_TextureBucket; }
    static void SetBuckets(int geom, int tex) { sm_GeometryBucket = geom; sm_TextureBucket = tex; }
    static void SetGeometryBucket(int b) { sm_GeometryBucket = b; }
    static void SetTextureBucket(int b) { sm_TextureBucket = b; }
private:
    static int sm_GeometryBucket;
    static int sm_TextureBucket;
};

#endif // GFX_MISC_H
