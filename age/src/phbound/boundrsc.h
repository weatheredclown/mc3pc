#ifndef PHBOUND_BOUNDRSC_H
#define PHBOUND_BOUNDRSC_H

////////////////////////////////////////
// phbound/boundrsc.h
//
// phBound from a console resource pack image (see data/rscimage.h).
//
// PS2 layout of a polygonal bound object, reconstructed from the shipped
// SLUS-21355 bound packs (resources/city/<city>_bnd.pck) and validated
// against them (index ranges, unit normals, plane tests):
//   +0x00  vtable (PS2 code address)
//   +0x04  u8 type (phBound::eBoundType: 9 OCTREE, 10 OCTREEGRID), u8, u8, u8
//   +0x08  float[3] box min           +0x14  float[3] box max
//   +0x20  float[3] centroid          +0x2c  float[3] centre of gravity
//   +0x38  float radius (around centroid)   +0x3c  float radius (around origin)
//   +0x48  u16                        +0x4c  int numVertices
//   +0x50  int numPolygons            +0x54  u16
//   +0x58  int -1, +0x5c 0, +0x60 -1, +0x64 0
//   +0x68  ptr vertices: numVertices x float[3] (12 bytes, not quadwords)
//   +0x6c  ptr polygons: numPolygons x 32 bytes:
//            float[4] unit normal + area, u16 vertex[4] (vertex[3] == 0 marks a
//            triangle), u16 neighbour[4] (-1 = none)
//   +0x70  ptr int[numMaterials] material ids     +0x74  int numMaterials
//   +0x78.. spatial tree data (octree / grid nodes), rebuilt on PC
// The per-polygon material index is not in the polygon record; polygons
// take the bound's first material until that table is found.
////////////////////////////////////////

#include "core/types.h"

class phBound;
class datResourceImage;

// Builds a polygonal phBound (octree types included) from the object at
// `addr`; NULL when the type is not a polygonal bound this reader knows.
phBound *phBoundLoadFromResource(const datResourceImage &image, u32 addr);

#endif // PHBOUND_BOUNDRSC_H
