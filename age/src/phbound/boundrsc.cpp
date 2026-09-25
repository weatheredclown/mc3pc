#include "phbound/boundrsc.h"

#include "phcore/surface.h"

#include "core/output.h"
#include "data/rscimage.h"
#include "phbound/bound.h"
#include "phbound/boundgeom.h"
#include "phbound/boundoctree.h"
#include "phbound/boundpolyhedron.h"
#include "phcore/material.h"
#include "phcore/materialmgr.h"
#include "vector/vector3.h"

#include <math.h>

phBound *phBoundLoadFromResource(const datResourceImage &image, u32 addr)
{
	if (!image.IsValidAddress(addr, 0x78)) {
		Quitf("phBoundLoadFromResource: '%s' bound at %08x is outside the image", image.GetName(), addr);
		return 0;
	}
	datResourceTokenizer tok(image, addr);
	tok.GetVTable();
	u8 type = tok.GetU8();
	u8 b1 = tok.GetU8(), b2 = tok.GetU8(), b3 = tok.GetU8();
	(void)b1; (void)b2; (void)b3;
	Vector3 boxMin, boxMax, centroid, cg;
	boxMin.x = tok.GetFloat(); boxMin.y = tok.GetFloat(); boxMin.z = tok.GetFloat();
	boxMax.x = tok.GetFloat(); boxMax.y = tok.GetFloat(); boxMax.z = tok.GetFloat();
	centroid.x = tok.GetFloat(); centroid.y = tok.GetFloat(); centroid.z = tok.GetFloat();
	cg.x = tok.GetFloat(); cg.y = tok.GetFloat(); cg.z = tok.GetFloat();
	float radius = tok.GetFloat();
	float radiusOrigin = tok.GetFloat();
	(void)radiusOrigin;

	if (type == 3)
		type = phBound::GEOMETRY;   // the vehicle packs store GEOMETRY as 3 (console enum); this port numbers it 6

	if (type != phBound::OCTREE && type != phBound::OCTREEGRID && type != phBound::POLYHEDRON && type != phBound::GEOMETRY && type != phBound::QUADTREE) {
		Quitf("phBoundLoadFromResource: '%s' bound at %08x has unhandled type %d", image.GetName(), addr, type);
		return 0;
	}

	tok.Seek(addr + 0x4c);
	int numVerts = tok.GetInt();
	int numPolys = tok.GetInt();

	tok.Seek(addr + 0x68);
	u32 vertsAddr = tok.GetPtr();
	u32 polysAddr = tok.GetPtr();
	u32 matsAddr = tok.GetPtr();
	int numMaterials = (type == phBound::GEOMETRY) ? 0 : tok.GetInt();

	if (numVerts <= 0 || numPolys <= 0 || !vertsAddr || !polysAddr ||
		!image.IsValidAddress(vertsAddr, (u32)numVerts * 12) || !image.IsValidAddress(polysAddr, (u32)numPolys * 32)) {
		Quitf("phBoundLoadFromResource: '%s' bound at %08x: %d verts / %d polys do not fit the image", image.GetName(), addr, numVerts, numPolys);
		return 0;
	}

	phBound *b = 0;
	if (type == phBound::GEOMETRY) {
		b = new phBoundGeometry();
	} else if (type == phBound::OCTREE || type == phBound::OCTREEGRID) {
		b = new phBoundOctree();
	} else {
		b = new phBoundPolyhedron();
	}

	// materials: ids into the material manager's table
	if (matsAddr && numMaterials > 0 && numMaterials < 4096 && image.IsValidAddress(matsAddr, (u32)numMaterials * 4)) {
		datResourceTokenizer mt(image, matsAddr);
		for (int i = 0; i < numMaterials; i++) {
			int id = mt.GetInt();
			phMaterial *mat = phMaterialMgr::sm_Instance ? phMaterialMgr::sm_Instance->GetMaterial(id) : 0;
			// Never store a null: the game walks a bound's material list and
			// dereferences every entry (mcLevelSimple::SaveMaterials and the
			// rain tuning beside it), so an id the manager does not know has
			// to resolve to the default surface instead.
			if (!mat) mat = phSurfaceMgr::GetDefaultSurface();
			b->Materials.Append(mat);
		}
	}
	if (b->Materials.GetCount() == 0)
		b->Materials.Append(phSurfaceMgr::GetDefaultSurface());

	// vertices
	b->Vertices.Reserve(numVerts);
	datResourceTokenizer vt(image, vertsAddr);
	for (int i = 0; i < numVerts; i++) {
		Vector3 v;
		v.x = vt.GetFloat(); v.y = vt.GetFloat(); v.z = vt.GetFloat();
		b->Vertices.Append(v);
	}

	// polygons -> triangle list
	int badIndex = 0, tris = 0, quads = 0;
	datResourceTokenizer pt(image, polysAddr);
	for (int i = 0; i < numPolys; i++) {
		pt.Seek(polysAddr + (u32)i * 32);
		float nx = pt.GetFloat(), ny = pt.GetFloat(), nz = pt.GetFloat(), area = pt.GetFloat();
		(void)nx; (void)ny; (void)nz; (void)area;
		int v0 = pt.GetU16(), v1 = pt.GetU16(), v2 = pt.GetU16(), v3 = pt.GetU16();
		s16 n0 = (s16)pt.GetU16(), n1 = (s16)pt.GetU16(), n2 = (s16)pt.GetU16(), n3 = (s16)pt.GetU16();
		(void)n0; (void)n1; (void)n2; (void)n3;
		// phPolygon::InitTriangle stores 0 as the fourth vertex (alpha disassembly);
		// the neighbour slots are -1 for any edge without a neighbour, quad or not.
		// Reading "n3 == -1" as "triangle" dropped one triangle from ~3500 of the SD
		// city's 20k polygons (the circuit start line among them: the car fell through).
		const bool isQuad = (v3 != 0);
		if (v0 >= numVerts || v1 >= numVerts || v2 >= numVerts || (isQuad && v3 >= numVerts)) {
			badIndex++;
			continue;
		}
		const int mtl = 0;      // per-polygon material index not located yet
		b->PolyMaterials.Append(mtl);
		b->TriVerts.Append(v0); b->TriVerts.Append(v1); b->TriVerts.Append(v2);
		if (isQuad) {
			quads++;
			b->PolyMaterials.Append(mtl);
			b->TriVerts.Append(v0); b->TriVerts.Append(v2); b->TriVerts.Append(v3);
		} else {
			tris++;
		}
	}
	if (badIndex) {
		Quitf("phBoundLoadFromResource: '%s' bound at %08x: %d polygons with out-of-range vertices", image.GetName(), addr, badIndex);
		return 0;
	}

	b->Centroid = centroid;
	b->CGOffset = cg;
	b->ActualRadius = radius > 0.0f ? radius : (boxMax - boxMin).Mag() * 0.5f;
	Displayf("phBoundLoadFromResource: '%s' %08x type %d: %d verts, %d polys (%d tris, %d quads), %d materials, box (%.0f %.0f %.0f)-(%.0f %.0f %.0f)",
		image.GetName(), addr, type, numVerts, numPolys, tris, quads, numMaterials, boxMin.x, boxMin.y, boxMin.z, boxMax.x, boxMax.y, boxMax.z);
	return b;
}
