#ifndef GFX_LOADIMG_H
#define GFX_LOADIMG_H

#include "gfx/image.h"
#include "vector/randmath.h" // required for other includes, DO NOT REMOVE

// Loads "<name>.tga" through ASSET (24/32-bit, raw or RLE). texture.cpp.
gfxImage *gfxLoadTargaImage(const char *name, bool mipmaps);
// Any supported image by base name (.tex/.tga/.png through the texture loader).
gfxImage *gfxLoadTexImage(const char *name, bool mipmaps);

// Loads "<name>.ipu" through ASSET: the PS2 "ipum" stills (loading screens,
// legal notices, title card) decoded in software.  gfx/ipu.cpp.
gfxImage *gfxLoadIpuImage(const char *name);
gfxImage *gfxLoadIpuImageMem(const char *name, const void *data, int numBytes);

// Writes "<name>.tga" (uncompressed 32-bit, type 2) as a loose file relative
// to the working directory (map-tile export).  Returns false on failure.
bool gfxSaveTargaImage(const char *name, const gfxImage *img);

#endif // GFX_LOADIMG_H

// Asset-side switches read at heap begin (mc3 mcHeap::Begin): the texture
// folder searched after the bare name, and the model file extension.
extern const char *gfxLoadImageFolder;        // gfx/texture.cpp
extern const char *gfxModelFilenameExtension; // gfx/model.cpp
