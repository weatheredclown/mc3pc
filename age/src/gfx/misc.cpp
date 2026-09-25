#include "gfx/misc.h"
#include "data/memory.h"

gfxMaterial gfxMaterial::FlatWhite;

// Default to the dedicated geometry/texture partitions; mc3 hood loading
// swaps in the unique/instanced variants per component.
int gfxMemory::sm_GeometryBucket = MEMBUCKET_GEOMETRY;
int gfxMemory::sm_TextureBucket = MEMBUCKET_TEXTURES;
