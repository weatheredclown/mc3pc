#include "rmcore/rscgeom.h"
#include "rmcore/cpvpalette.h"

#include "core/output.h"
#include "data/args.h"
#include "data/rscimage.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>

extern "C" void ageHeapCheck(const char *tag);

static bool sDropLog = false;   // -viflog: BuildTriangles reports the triangles it rejects as degenerate

static const float kUvScale = 1.0f / 4096.0f;

int rscFindModels(const datResourceImage &image, atArray<u32> &addrs, u32 vtable)
{
	addrs.Reset();
	const u8 *d = image.GetData();
	u32 n = image.GetSize();
	for (u32 o = 0; o + 0x64 <= n; o += 4) {
		u32 v = (u32)d[o] | ((u32)d[o + 1] << 8) | ((u32)d[o + 2] << 16) | ((u32)d[o + 3] << 24);
		if (v == vtable) {
			u32 zero = (u32)d[o + 4] | ((u32)d[o + 5] << 8) | ((u32)d[o + 6] << 16) | ((u32)d[o + 7] << 24);
			u32 numParts = (u32)d[o + 0x20] | ((u32)d[o + 0x21] << 8) | ((u32)d[o + 0x22] << 16) | ((u32)d[o + 0x23] << 24);
			if (zero == 0 && numParts >= 1 && numParts <= 8)
				addrs.Append(image.ToAddress(o));
		}
	}
	return addrs.GetCount();
}

bool rscDecodeVifStream(const datResourceImage &image, u32 dataAddr, u32 endAddr, atArray<rscGeomBatch> &batches)
{
	return rscDecodeVifStream(image, dataAddr, endAddr, batches, 0);
}

bool rscDecodeVifStream(const datResourceImage &image, u32 dataAddr, u32 endAddr, atArray<rscGeomBatch> &batches, int maxVerts)
{
	int totalVerts = 0;
	if (!image.IsValidAddress(dataAddr, 4)) return false;
	if (!image.IsValidAddress(endAddr) || endAddr <= dataAddr) endAddr = image.GetBase() + image.GetSize();

	rscGeomBatch cur;
	cur.scale = 1.0f;
	bool pending = false;
	int codes = 0;

	static int sVifLog = -1;
	if (sVifLog < 0) sVifLog = ARGS.Get("viflog") ? 1 : 0;
	int cl = 1, wl = 1;             // STCYCL: with cl < wl only cl of every wl vectors come from the data
	int row[4] = {0, 0, 0, 0};      // STROW: the base added in STMOD offset/difference mode
	int mode = 0;                   // STMOD: 0 normal, 1 offset (data + row), 2 difference (row accumulates)
	u32 posNext = 0, uvNext = 0;    // VU address where a split position / uv unpack continues
	// a component of an integer unpack, with the STROW/STMOD decompression applied
	auto readComp = [&](u32 p, int c, int bits, bool usn) -> int {
		int v;
		if (bits == 8) v = usn ? (int)image.ReadU8(p) : (int)(s8)image.ReadU8(p);
		else if (bits == 16) v = usn ? (int)image.ReadU16(p) : (int)(s16)image.ReadU16(p);
		else v = (int)image.ReadU32(p);
		if (mode == 1) v += row[c];
		else if (mode == 2) { v += row[c]; row[c] = v; }
		return v;
	};
	u32 o = dataAddr;
	while (o + 4 <= endAddr && codes < 100000) {
		codes++;
		u16 imm = image.ReadU16(o);
		u8 num = image.ReadU8(o + 2);
		u8 cmd = image.ReadU8(o + 3) & 0x7f;
		o += 4;
		if (sVifLog && cmd >= 0x60) {
			int vn = ((cmd >> 2) & 3) + 1, vl = cmd & 3;
			Displayf("  vif %08x: UNPACK V%d-%s addr %#x num %d flg %d usn %d  first %08x %08x", o - 4, vn,
			         vl == 0 ? "32" : vl == 1 ? "16" : vl == 2 ? "8" : "5", imm & 0x3ff, num ? num : 256, (imm >> 15) & 1, (imm >> 14) & 1,
			         image.IsValidAddress(o, 8) ? image.ReadU32(o) : 0, image.IsValidAddress(o, 8) ? image.ReadU32(o + 4) : 0);
			if (vl == 2 && vn == 4 && (num ? num : 256) <= 96) {
				char line[96 * 12 + 8]; line[0] = 0;
				for (int i = 0; i < (num ? num : 256) && image.IsValidAddress(o + (u32)i * 4, 4); i++) {
					char q[16];
					formatf(q, sizeof(q), "%02x%02x%02x%02x ", image.ReadU8(o + (u32)i * 4), image.ReadU8(o + (u32)i * 4 + 1), image.ReadU8(o + (u32)i * 4 + 2), image.ReadU8(o + (u32)i * 4 + 3));
					strcat(line, q);
				}
				Displayf("      bytes: %s", line);
			}
		} else if (sVifLog) {
			Displayf("  vif %08x: cmd %#x imm %#x num %d", o - 4, cmd, imm, num);
		}
		if (cmd >= 0x60) {
			int vn = ((cmd >> 2) & 3) + 1;
			int vl = cmd & 3;
			int bits = vl == 0 ? 32 : vl == 1 ? 16 : vl == 2 ? 8 : 5;
			int count = num ? num : 256;
			int dataVecs = count;
			if (cl < wl) dataVecs = cl * (count / wl) + (count % wl < cl ? count % wl : cl);
			u32 bytes = vl == 3 ? (u32)dataVecs * 2 : (u32)(dataVecs * vn * bits) / 8;
			if (vl == 3 && dataVecs * 16 % 32) bytes = (u32)(dataVecs * 16 + 31) / 32 * 4;   // V4-5: 16 bits per vector
			u32 pad = (4 - bytes % 4) % 4;
			u32 addr = imm & 0x3ff;
			if (!image.IsValidAddress(o, bytes)) break;
			if (dataVecs != count) {
				// row-fill unpack (a per-batch constant), not per-vertex data
				o += bytes + pad;
				continue;
			}

			if ((vn == 1 || vn == 4) && bits == 32 && (addr == 0x98 || addr == 0x9e || addr == 0x1c5)) {
				// batch header: scale + count
				if (pending && cur.verts.GetCount()) { totalVerts += cur.verts.GetCount(); batches.Append(cur); }
				cur.verts.Reset(); cur.uvs.Reset(); cur.uvs2.Reset(); cur.colors.Reset(); cur.normals.Reset(); cur.adc.Reset(); cur.bones.Reset();
				cur.scale = image.ReadFloat(o);
				pending = true;
			} else if ((bits == 16 || bits == 8) &&
			           ((vn == 3 && (addr == 0xee || addr == 0x100 || addr == 0x21b || addr == 0x227 || (posNext && addr == posNext && cur.verts.GetCount()))) ||
			            (vn == 4 && bits == 16 && (addr == 0xee || addr == 0x100 || addr == 0x21b || addr == 0x227)))) {
				// Positions: 16-bit, or 8-bit deltas in STMOD difference mode (city packs), which
				// arrive as several unpacks at consecutive VU addresses, each after its own STROW.
				//
				// A skinned mesh (the city ped packs) unpacks four components at
				// the same VU address instead of three: xyz, then the VU matrix
				// slot the vertex is skinned to.  The VU holds a bone as four
				// quadwords, so that slot is the bone index times four.
				bool continuation = posNext && addr == posNext && cur.verts.GetCount();
				if (!continuation) {
					if (pending && cur.verts.GetCount()) { totalVerts += cur.verts.GetCount(); batches.Append(cur); cur.verts.Reset(); cur.uvs.Reset(); cur.uvs2.Reset(); cur.colors.Reset(); cur.normals.Reset(); cur.adc.Reset(); cur.bones.Reset(); }
					if (cur.scale <= 0.0f || cur.scale == 1.0f) cur.scale = 1.0f / 32768.0f;
					cur.verts.Reset();
					cur.bones.Reset();
				}
				int comp = bits / 8;
				int stride = vn * comp;
				bool usn = ((imm >> 14) & 1) != 0;
				for (int i = 0; i < count; i++) {
					u32 p = o + (u32)i * stride;
					Vector3 v((float)readComp(p, 0, bits, usn) * cur.scale,
					          (float)readComp(p + comp, 1, bits, usn) * cur.scale,
					          (float)readComp(p + 2 * comp, 2, bits, usn) * cur.scale);
					cur.verts.Append(v);
					if (vn == 4) {
						int slot = readComp(p + 3 * comp, 3, bits, usn);
						cur.bones.Append((u16)(slot > 0 ? slot / 4 : 0));
					}
				}
				posNext = addr + (u32)count;
				pending = true;
			} else if ((bits == 16 || bits == 8) && (vn == 2 || vn == 4) &&
			           (addr == 0xc4 || addr == 0xd0 || addr == 0x1f1 || addr == 0x1f7 || (uvNext && addr == uvNext && cur.uvs.GetCount()))) {
				bool continuation = uvNext && addr == uvNext && cur.uvs.GetCount();
				if (!continuation) { cur.uvs.Reset(); cur.uvs2.Reset(); }
				int stride = vn * bits / 8;
				bool usn = ((imm >> 14) & 1) != 0;
				// In the CPV-indexed city layout (uvs at 0xd0 / 0x1f7) the strip kick (ADC) bit is
				// also the low bit of U.  Unlit geometry (no NrmAdc unpack, e.g. most city walls)
				// carries it only here; a NrmAdc stream decoded later in the batch replaces it
				// (they agree in all 1310 sd_midnight_clear batches with both).  The 0xc4 / 0x1f1
				// layout (vehicles) keeps plain U.
				bool cpvLayout = addr == 0xd0 || addr == 0x1f7 || (continuation && cur.adc.GetCount());
				bool adcFromUv = cpvLayout && cur.adc.GetCount() == cur.uvs.GetCount();
				for (int i = 0; i < count; i++) {
					u32 p = o + (u32)i * stride;
					int u = readComp(p, 0, bits, usn);
					Vector2 t((float)u * kUvScale, (float)readComp(p + stride / vn, 1, bits, usn) * kUvScale);
					cur.uvs.Append(t);
					if (vn == 4)            // z/w = the second UV set (Tex1)
						cur.uvs2.Append(Vector2((float)readComp(p + 2 * stride / vn, 2, bits, usn) * kUvScale,
						                        (float)readComp(p + 3 * stride / vn, 3, bits, usn) * kUvScale));
					if (adcFromUv) cur.adc.Append((u8)(u & 1));
				}
				uvNext = addr + (u32)count;
			} else if (bits == 8 && (vn == 4 || vn == 3) && (addr == 0x9a || addr == 0xa0 || addr == 0x1c7)) {
				// NrmAdc / ADC stream: bit 0 of first byte carries strip kick (ADC) flag, the
				// rest is the vertex normal (signed bytes).  Page models (rims, brakes) have no
				// separate normal stream, so take the normal from here unless one was decoded.
				atArray<u8> uvAdc;
				if (sVifLog && cur.adc.GetCount() == count) uvAdc = cur.adc;
				cur.adc.Reset();
				bool fillNormals = cur.normals.GetCount() != count;
				if (fillNormals) cur.normals.Reset();
				if (vn == 4 && rmcCpvPalette::HasCurrent()) {
					cur.colors.Reset();
					const rmcCpvPalette &pal = rmcCpvPalette::GetCurrent();
					for (int i = 0; i < count; i++) {
						u32 p = o + (u32)i * vn;
						u8 cpvIdx = image.ReadU8(p + 3);
						cur.colors.Append(pal.GetPackedColor(cpvIdx));
					}
				} else if (vn == 3 && rmcCpvPalette::HasCurrent()) {
					// In batches where all vertices share a single constant CPV index (e.g. HDR models),
					// the PS2 VU exporter unpacks V3-8 and supplies the 4th component (W) from the
					// ROW register via STROW / STMASK (0x40404040).
					cur.colors.Reset();
					const rmcCpvPalette &pal = rmcCpvPalette::GetCurrent();
					u8 cpvIdx = (u8)row[3];
					gfxPackedColor col = pal.GetPackedColor(cpvIdx);
					for (int i = 0; i < count; i++) {
						cur.colors.Append(col);
					}
				}
				for (int i = 0; i < count; i++) {
					u32 p = o + (u32)i * vn;
					s8 bx = (s8)image.ReadU8(p);
					cur.adc.Append((u8)(bx & 1));
					if (fillNormals) {
						s8 by = (s8)image.ReadU8(p + 1), bz = (s8)image.ReadU8(p + 2);
						float nx = (float)(s8)(bx & ~1) / 127.0f, ny = (float)by / 127.0f, nz = (float)bz / 127.0f;
						float len = sqrtf(nx * nx + ny * ny + nz * nz);
						if (len > 1e-4f) { nx /= len; ny /= len; nz /= len; }
						cur.normals.Append(Vector3(nx, ny, nz));
					}
				}
				if (uvAdc.GetCount() == count) {
					int differ = 0;
					for (int i = 0; i < count; i++) if (uvAdc[i] != cur.adc[i]) differ++;
					Displayf("      uv-lsb kick vs NrmAdc: %d of %d differ", differ, count);
				}
			} else if (bits == 8 && (vn == 4 || vn == 3) && (addr == 0x118 || addr == 0x245)) {
				// Pure vertex normal stream: decode into cur.normals, do not touch cur.adc.
				// An UNSIGNED unpack here is not a normal at all: the vehicle packs put their
				// vertex colour at 0x118 / 0x245 (rgb 0x80 = 1.0, then the CPV index) and the
				// signed normals in the NrmAdc stream at 0x9a / 0x1c7 that follows it.  Read as
				// signed bytes, 0x7f / 0x80 / 0x81 became (+-1, +-1, +-1) / sqrt(3), whose signs
				// flipped with the baked colour - and, filling cur.normals first, they stopped
				// the real NrmAdc normals being read.  Every vehicle normal was one of eight
				// diagonals, which is what made the bodywork look faceted and blotchy.
				const bool colourStream = ((imm >> 14) & 1) != 0;
				if (!colourStream) cur.normals.Reset();
				if (vn == 4 && rmcCpvPalette::HasCurrent()) {
					cur.colors.Reset();
					const rmcCpvPalette &pal = rmcCpvPalette::GetCurrent();
					for (int i = 0; i < count; i++) {
						u32 p = o + (u32)i * vn;
						u8 cpvIdx = image.ReadU8(p + 3);
						cur.colors.Append(pal.GetPackedColor(cpvIdx));
					}
				}
				for (int i = 0; i < count && !colourStream; i++) {
					u32 p = o + (u32)i * vn;
					s8 bx = (s8)image.ReadU8(p), by = (s8)image.ReadU8(p + 1), bz = (s8)image.ReadU8(p + 2);
					float nx = (float)bx / 127.0f, ny = (float)by / 127.0f, nz = (float)bz / 127.0f;
					float len = sqrtf(nx * nx + ny * ny + nz * nz);
					if (len > 1e-4f) { nx /= len; ny /= len; nz /= len; }
					cur.normals.Append(Vector3(nx, ny, nz));
				}
			}
			o += bytes + pad;
		} else if (cmd == 0x14 || cmd == 0x15 || cmd == 0x17) {
			// MSCAL / MSCALF / MSCNT: the VU draws what was unpacked
			if (cur.verts.GetCount()) { totalVerts += cur.verts.GetCount(); batches.Append(cur); }
			cur.verts.Reset(); cur.uvs.Reset(); cur.uvs2.Reset(); cur.colors.Reset(); cur.normals.Reset(); cur.adc.Reset(); cur.bones.Reset();
			pending = false;
			if (maxVerts > 0 && totalVerts >= maxVerts) break;    // the geometry's vertex budget is spent
		} else if (cmd == 0x01) {
			cl = imm & 0xff; wl = (imm >> 8) & 0xff;   // STCYCL
			if (cl == 0) cl = 256;
			if (wl == 0) wl = 256;
		} else if (cmd == 0x05) {
			mode = imm & 3;                 // STMOD
		} else if (cmd == 0x20) {
			o += 4;                         // STMASK
		} else if (cmd == 0x30) {
			for (int c = 0; c < 4; c++) row[c] = (int)image.ReadU32(o + (u32)c * 4);   // STROW
			o += 16;
		} else if (cmd == 0x31) {
			o += 16;                        // STCOL
		} else if (cmd == 0x4a) {
			o += (num ? num : 256) * 8;     // MPG (microprogram upload)
		} else if (cmd == 0x50 || cmd == 0x51) {
			break;                          // DIRECT: GS packets, not geometry
		}
		// NOP, STCYCL, OFFSET, BASE, ITOP, STMOD, MSKPATH3, MARK, FLUSH*: no payload
	}
	if (cur.verts.GetCount()) batches.Append(cur);
	if (sVifLog) {
		for (int b = 0; b < batches.GetCount(); b++) {
			atArray<rscTriangle> t; sDropLog = true; batches[b].BuildTriangles(t, 0); sDropLog = false;
			// slivers: triangles with an edge longer than half the batch's diagonal
			Vector3 bmin(1e30f, 1e30f, 1e30f), bmax(-1e30f, -1e30f, -1e30f);
			for (int v = 0; v < batches[b].verts.GetCount(); v++) {
				const Vector3 &q = batches[b].verts[v];
				if (q.x < bmin.x) bmin.x = q.x; if (q.x > bmax.x) bmax.x = q.x;
				if (q.y < bmin.y) bmin.y = q.y; if (q.y > bmax.y) bmax.y = q.y;
				if (q.z < bmin.z) bmin.z = q.z; if (q.z > bmax.z) bmax.z = q.z;
			}
			float diag = (bmax - bmin).Mag();
			for (int k = 0; k < t.GetCount(); k++) {
				const Vector3 &v0 = batches[b].verts[t[k].i0], &v1 = batches[b].verts[t[k].i1], &v2 = batches[b].verts[t[k].i2];
				float e = (v1 - v0).Mag(); float e2 = (v2 - v1).Mag(); float e3 = (v0 - v2).Mag();
				if (e2 > e) e = e2; if (e3 > e) e = e3;
				if (e > 0.5f * diag)
					Displayf("      sliver tri %d (%d,%d,%d) edge %.3f of diag %.3f", k, t[k].i0, t[k].i1, t[k].i2, e, diag);
			}
			char pat[300]; int pn = 0;
			for (int i = 0; i < batches[b].adc.GetCount() && pn < 290; i++) pat[pn++] = batches[b].adc[i] ? '1' : '0';
			pat[pn] = 0;
			Displayf("  batch %d: %d verts, %d uvs, %d normals, %d adc, scale %g -> %d tris%s  adc=%s", b, batches[b].verts.GetCount(), batches[b].uvs.GetCount(),
			         batches[b].normals.GetCount(), batches[b].adc.GetCount(), batches[b].scale, t.GetCount(),
			         batches[b].adc.GetCount() == batches[b].verts.GetCount() ? "" : " [no ADC: heuristic strip]", pat);
		}
	}
	return batches.GetCount() > 0;
}

int rscFindCarModels(const datResourceImage &image, atArray<u32> &addrs)
{
	addrs.Reset();
	const u8 *d = image.GetData();
	u32 n = image.GetSize();
	for (u32 o = 0; o + 0x18 <= n; o += 4) {
		u32 v = (u32)d[o] | ((u32)d[o + 1] << 8) | ((u32)d[o + 2] << 16) | ((u32)d[o + 3] << 24);
		if (v == rscCarModelVTable || v == rscPageModelVTable) {
			u32 count = (u32)d[o + 8] | ((u32)d[o + 9] << 8) | ((u32)d[o + 10] << 16) | ((u32)d[o + 11] << 24);
			u32 list = (u32)d[o + 0x10] | ((u32)d[o + 0x11] << 8) | ((u32)d[o + 0x12] << 16) | ((u32)d[o + 0x13] << 24);
			if (count >= 1 && count <= 64 && image.IsValidAddress(list, count * 8))
				addrs.Append(image.ToAddress(o));
		}
	}
	return addrs.GetCount();
}

bool rscDecodeCarModel(const datResourceImage &image, u32 nodeAddr, rscModel &out)
{
	out.addr = nodeAddr;
	out.geometries.Reset();
	out.numVerts = 0;
	out.boxMin = Vector3(1e30f, 1e30f, 1e30f);
	out.boxMax = Vector3(-1e30f, -1e30f, -1e30f);
	if (!image.IsValidAddress(nodeAddr, 0x18)) return false;
	out.boneIndex = (int)image.ReadU16(nodeAddr + 6);

	int count = (int)image.ReadU32(nodeAddr + 8);
	u32 listAddr = image.ReadU32(nodeAddr + 0x10);
	if (!listAddr || count <= 0 || count > 64) return false;
	if (!image.IsValidAddress(listAddr, (u32)count * 8)) return false;
	// +0x0c: u16 per geometry, the index into the drawable's shader table
	u32 shaderIdxBlock = image.ReadU32(nodeAddr + 0xc);
	if (shaderIdxBlock && !image.IsValidAddress(shaderIdxBlock, (u32)count * 2)) shaderIdxBlock = 0;

	// Car and rider models do not use city CPV palettes. Suppress sm_Current
	// during decode so leftover VU register values are not treated as palette indices.
	const rmcCpvPalette *savedPal = rmcCpvPalette::GetCurrentPtr();
	rmcCpvPalette::SetCurrentPtr(nullptr);

	for (int k = 0; k < count; k++) {
		u32 entryAddr = listAddr + (u32)k * 8;
		u32 firstWord = image.ReadU32(entryAddr);
		if (!firstWord) continue;

		u32 vifData = 0;
		u16 stripLen = 0, numVerts = 0;
		if (image.IsValidAddress(firstWord, 8) && image.IsValidAddress(image.ReadU32(firstWord), 8)) {
			// Indirect geometry structure (car body, rim): {vif ptr, u16 strip length
			// (vertices incl. the ADC restart pairs), u16 unique vertex count}.  Neither
			// is a quadword count: the VIF stream runs on until the next geometry's, and
			// it is the vertex count that tells the walker where this one ends.
			u32 geomAddr = firstWord;
			vifData = image.ReadU32(geomAddr);
			stripLen = image.ReadU16(geomAddr + 4);
			numVerts = image.ReadU16(geomAddr + 6);
		} else {
			// Direct geometry structure (tire, standalone models)
			vifData = firstWord;
			stripLen = image.ReadU16(entryAddr + 4);
			numVerts = image.ReadU16(entryAddr + 6);
		}
		if (!vifData || !image.IsValidAddress(vifData, 4)) continue;
		// end bound: the closest later geometry start in this model, else the image end
		u32 end = image.GetBase() + image.GetSize();
		for (int j = 0; j < count; j++) {
			u32 e2 = listAddr + (u32)j * 8;
			u32 w2 = image.ReadU32(e2);
			if (!w2) continue;
			u32 v2 = (image.IsValidAddress(w2, 8) && image.IsValidAddress(image.ReadU32(w2), 8)) ? image.ReadU32(w2) : w2;
			if (v2 > vifData && v2 < end) end = v2;
		}

		rscGeometry geom;
		geom.part = 0;
		geom.index = k;
		geom.addr = entryAddr;
		geom.declaredVerts = numVerts;
		geom.shaderIdx = shaderIdxBlock ? (int)image.ReadU16(shaderIdxBlock + (u32)k * 2) : 0;
		if (geom.shaderIdx < 0 || geom.shaderIdx > 4095) geom.shaderIdx = 0;
		(void)stripLen;

		rscDecodeVifStream(image, vifData, end, geom.batches, numVerts);
		for (int b = 0; b < geom.batches.GetCount(); b++) {
			const rscGeomBatch &bt = geom.batches[b];
			for (int v = 0; v < bt.verts.GetCount(); v++) {
				const Vector3 &p = bt.verts[v];
				if (p.x < out.boxMin.x) out.boxMin.x = p.x; if (p.x > out.boxMax.x) out.boxMax.x = p.x;
				if (p.y < out.boxMin.y) out.boxMin.y = p.y; if (p.y > out.boxMax.y) out.boxMax.y = p.y;
				if (p.z < out.boxMin.z) out.boxMin.z = p.z; if (p.z > out.boxMax.z) out.boxMax.z = p.z;
			}
			out.numVerts += bt.verts.GetCount();
		}
		out.geometries.Append(geom);
	}
	rmcCpvPalette::SetCurrentPtr(savedPal);
	return out.numVerts > 0;
}


int rscDecodeSkeleton(const datResourceImage &image, u32 drawableAddr, atArray<rscBone> &out)
{
	out.Reset();
	if (!drawableAddr || !image.IsValidAddress(drawableAddr + 0x6c, 4)) return 0;
	u32 skel = image.ReadU32(drawableAddr + 0x68);
	if (!skel || !image.IsValidAddress(skel, 12)) return 0;
	int numBones = (int)image.ReadU16(skel);
	u32 bones = image.ReadU32(skel + 8);
	const u32 kBoneSize = 0x44;
	if (numBones <= 0 || numBones > 1024 || !bones || !image.IsValidAddress(bones, (u32)numBones * kBoneSize)) return 0;
	out.Resize(numBones);
	for (int i = 0; i < numBones; i++) {
		u32 b = bones + (u32)i * kBoneSize;
		rscBone &bn = out[i];
		u32 nameAddr = image.ReadU32(b + 0x0c);
		const char *nm = nameAddr ? image.ReadString(nameAddr) : "";
		strncpy(bn.name, nm ? nm : "", sizeof(bn.name) - 1);
		bn.name[sizeof(bn.name) - 1] = 0;
		bn.restPosition = Vector3(image.ReadFloat(b + 0x00), image.ReadFloat(b + 0x04), image.ReadFloat(b + 0x08));
		bn.offset = Vector3(image.ReadFloat(b + 0x20), image.ReadFloat(b + 0x24), image.ReadFloat(b + 0x28));
		bn.rotation = Vector3(image.ReadFloat(b + 0x2c), image.ReadFloat(b + 0x30), image.ReadFloat(b + 0x34));
		bn.dofs = image.ReadU16(b + 0x10);
		u32 parent = image.ReadU32(b + 0x1c);
		bn.parent = (parent >= bones && parent < bones + (u32)numBones * kBoneSize) ? (int)((parent - bones) / kBoneSize) : -1;
		bn.global.Identity();
	}
	// compose rest globals; bones are stored parents-first, but walk defensively
	for (int pass = 0; pass < 2; pass++) {
		for (int i = 0; i < numBones; i++) {
			rscBone &bn = out[i];
			Matrix34 local;
			local.Identity();
			local.FromEulersXYZ(bn.rotation);
			local.d = bn.offset;
			if (bn.parent >= 0 && bn.parent < numBones && bn.parent != i) bn.global.Dot(local, out[bn.parent].global);
			else bn.global = local;
		}
	}
	return numBones;
}

bool rscDecodeCarKits(const datResourceImage &image, u32 modelTypeAddr, const atArray<rscBone> &bones, rscCarKits &out)
{
	out.variantBone.Reset();
	out.stockBone.Reset();
	out.dropShadowBone = -1;
	out.numFamiliesFound = 0;
	int nb = bones.GetCount();
	if (!modelTypeAddr || nb <= 0 || !image.IsValidAddress(modelTypeAddr, 0x3190)) return false;
	out.variantBone.Resize(nb);
	out.stockBone.Resize(nb);
	for (int i = 0; i < nb; i++) { out.variantBone[i] = 0; out.stockBone[i] = 0; }
	const int kKits = 24;
	static const struct { const char *name; u32 offset; int count; } kFamilies[] = {
		{"front bumper", 0x2afc, kKits}, {"rear bumper", 0x2b5c, kKits}, {"side skirt", 0x2bbc, kKits}, {"spoiler", 0x2c1c, kKits},
		{"hood", 0x2c7c, kKits}, {"blower", 0x2cdc, kKits}, {"blower valve", 0x2d3c, kKits}, {"headlight", 0x2dd4, kKits},
		{"taillight", 0x2e34, kKits}, {"custom exhaust", 0x2f54, kKits}, {"body type", 0x2fb4, 8}, {"brush guard", 0x2fd4, kKits},
		{"front grille", 0x3034, kKits}, {"one-shot kit", 0x3094, kKits}, {"wheelie bar", 0x30f4, kKits},
	};
	for (size_t f = 0; f < sizeof(kFamilies) / sizeof(kFamilies[0]); f++) {
		bool any = false;
		for (int i = 0; i < kFamilies[f].count; i++) {
			int b = (int)image.ReadU32(modelTypeAddr + kFamilies[f].offset + (u32)i * 4);
			if (b < 0 || b >= nb) continue;
			(i == 0 ? out.stockBone : out.variantBone)[b] = 1;
			any = true;
		}
		if (any) out.numFamiliesFound++;
	}
	int shadow = (int)image.ReadU32(modelTypeAddr + 0x2da0);
	if (shadow >= 0 && shadow < nb) out.dropShadowBone = shadow;
	return true;
}

bool rscIsKitVariantBone(const rscCarKits &kits, const atArray<rscBone> &bones, int boneIndex)
{
	int nb = bones.GetCount();
	for (int guard = 0; guard < 64 && boneIndex >= 0 && boneIndex < nb && boneIndex < kits.variantBone.GetCount(); guard++) {
		if (kits.variantBone[boneIndex] || boneIndex == kits.dropShadowBone) return true;
		boneIndex = bones[boneIndex].parent;
	}
	return false;
}

void rscPlaceModelOnBone(rscModel &model, const rscBone &bone)
{
	const Matrix34 &m = bone.global;
	model.boxMin = Vector3(1e30f, 1e30f, 1e30f);
	model.boxMax = Vector3(-1e30f, -1e30f, -1e30f);
	for (int g = 0; g < model.geometries.GetCount(); g++) {
		rscGeometry &geom = model.geometries[g];
		for (int b = 0; b < geom.batches.GetCount(); b++) {
			rscGeomBatch &bt = geom.batches[b];
			for (int v = 0; v < bt.verts.GetCount(); v++) {
				Vector3 p;
				m.Transform(bt.verts[v], p);
				bt.verts[v] = p;
				if (p.x < model.boxMin.x) model.boxMin.x = p.x; if (p.x > model.boxMax.x) model.boxMax.x = p.x;
				if (p.y < model.boxMin.y) model.boxMin.y = p.y; if (p.y > model.boxMax.y) model.boxMax.y = p.y;
				if (p.z < model.boxMin.z) model.boxMin.z = p.z; if (p.z > model.boxMax.z) model.boxMax.z = p.z;
			}
			for (int v = 0; v < bt.normals.GetCount(); v++) {
				const Vector3 &n = bt.normals[v];
				bt.normals[v] = m.a * n.x + m.b * n.y + m.c * n.z;
			}
		}
	}
}

bool rscDecodeModel(const datResourceImage &image, u32 modelAddr, rscModel &out)
{
	return rscDecodeModel(image, modelAddr, out, true);
}

bool rscDecodeModel(const datResourceImage &image, u32 modelAddr, rscModel &out, bool strictHeader)
{
	out.addr = modelAddr;
	out.geometries.Reset();
	out.numVerts = 0;
	out.boxMin = Vector3(1e30f, 1e30f, 1e30f);
	out.boxMax = Vector3(-1e30f, -1e30f, -1e30f);
	if (!image.IsValidAddress(modelAddr, 0x28)) return false;

	u32 zero = image.ReadU32(modelAddr + 4);
	if (strictHeader && zero != 0) return false;

	int numParts = (int)image.ReadU32(modelAddr + 0x20);
	u32 partArrayAddr = modelAddr + 0x30;

	if (numParts < 1 || numParts > 32) {
		numParts = (int)(image.ReadU16(modelAddr + 8) & 0xff);
		partArrayAddr = image.ReadU32(modelAddr + 0x10);
	}
	u32 ptrAt10 = image.ReadU32(modelAddr + 0x10);
	if (image.IsValidAddress(ptrAt10, (u32)numParts * 8)) {
		partArrayAddr = ptrAt10;
	}
	if (numParts < 1 || numParts > 32 || !image.IsValidAddress(partArrayAddr, (u32)numParts * 8)) return false;

	u32 shaderIdxBlock = image.ReadU32(modelAddr + 0x0c);
	u16 numShaders = image.ReadU16(modelAddr + 0x08);

	u32 modelCpvAddr = image.ReadU32(modelAddr + 0x14);
	u32 geomCpvArray = 0;
	int numGeomCpv = 0;
	if (modelCpvAddr && image.IsValidAddress(modelCpvAddr, 0x14)) {
		geomCpvArray = image.ReadU32(modelCpvAddr);
		numGeomCpv = (int)image.ReadU32(modelCpvAddr + 0x10);
		if (!image.IsValidAddress(geomCpvArray, (u32)numGeomCpv * 8)) {
			geomCpvArray = 0;
			numGeomCpv = 0;
		}
	}

	for (int part = 0; part < numParts; part++) {
		int shIdx = 0;
		if (shaderIdxBlock && part < (int)numShaders && image.IsValidAddress(shaderIdxBlock + (u32)part * 2, 2)) {
			shIdx = (int)image.ReadU16(shaderIdxBlock + (u32)part * 2);
		}

		u32 partOffset = partArrayAddr + (u32)part * 8;
		u32 listAddr = image.ReadU32(partOffset);
		int count = (int)image.ReadU16(partOffset + 4);
		if (!listAddr || count <= 0 || count > 256) continue;
		if (!image.IsValidAddress(listAddr, (u32)count * 8)) continue;

		u32 countsPtr = 0, colorsPtr = 0;
		if (geomCpvArray && part < numGeomCpv) {
			countsPtr = image.ReadU32(geomCpvArray + (u32)part * 8);
			colorsPtr = image.ReadU32(geomCpvArray + (u32)part * 8 + 4);
		}

		for (int k = 0; k < count; k++) {
			u32 entryAddr = listAddr + (u32)k * 8;
			u32 data = image.ReadU32(entryAddr);
			u16 qwc = image.ReadU16(entryAddr + 4);
			u16 declared = image.ReadU16(entryAddr + 6);

			rscGeometry geom;
			geom.part = part;
			geom.index = k;
			geom.addr = entryAddr;
			geom.declaredVerts = declared;
			geom.shaderIdx = shIdx;

			if (data && qwc > 0) {
				u32 end = data + (u32)qwc * 16;
				if (image.IsValidAddress(data, (u32)qwc * 16)) {
					rscDecodeVifStream(image, data, end, geom.batches);
				}
			}

			if (countsPtr && colorsPtr && rmcCpvPalette::HasCurrent()) {
				u32 nBatches = image.ReadU32(countsPtr);
				if ((u32)k < nBatches && image.IsValidAddress(countsPtr, 4 + nBatches * 4) && image.IsValidAddress(colorsPtr, nBatches * 4)) {
					u32 vertCount = image.ReadU32(countsPtr + 4 + (u32)k * 4);
					u32 streamPtr = image.ReadU32(colorsPtr + (u32)k * 4);
					if (streamPtr && vertCount > 0 && image.IsValidAddress(streamPtr, vertCount)) {
						const rmcCpvPalette &pal = rmcCpvPalette::GetCurrent();
						u32 streamOffset = 0;
						for (int b = 0; b < geom.batches.GetCount(); b++) {
							rscGeomBatch &bt = geom.batches[b];
							if (bt.colors.GetCount() == 0 && streamOffset < vertCount) {
								int toFill = bt.verts.GetCount();
								if (streamOffset + (u32)toFill > vertCount)
									toFill = (int)(vertCount - streamOffset);
								bt.colors.Reset();
								for (int v = 0; v < toFill; v++) {
									u8 idx = image.ReadU8(streamPtr + streamOffset + (u32)v);
									bt.colors.Append(pal.GetPackedColor(idx));
								}
								streamOffset += (u32)toFill;
							}
						}
					}
				}
			}
			for (int b = 0; b < geom.batches.GetCount(); b++) {
				const rscGeomBatch &bt = geom.batches[b];
				for (int v = 0; v < bt.verts.GetCount(); v++) {
					const Vector3 &p = bt.verts[v];
					if (p.x < out.boxMin.x) out.boxMin.x = p.x; if (p.x > out.boxMax.x) out.boxMax.x = p.x;
					if (p.y < out.boxMin.y) out.boxMin.y = p.y; if (p.y > out.boxMax.y) out.boxMax.y = p.y;
					if (p.z < out.boxMin.z) out.boxMin.z = p.z; if (p.z > out.boxMax.z) out.boxMax.z = p.z;
				}
				out.numVerts += bt.verts.GetCount();
			}
			out.geometries.Append(geom);
		}
	}

	// PC port: Decode all per-instance CPV sets for instanced models (numCpvSets at modelAddr + 0x0a).
	// modelCpvAddr (+0x14) points to an array of numCpvSets pointers; each entry is an rmcModelCpv
	// with per-part rmcGeometryCpv streams.
	u16 numCpvSets = image.ReadU16(modelAddr + 0x0a);
	if (numCpvSets > 0 && modelCpvAddr && rmcCpvPalette::HasCurrent() && image.IsValidAddress(modelCpvAddr, (u32)numCpvSets * 4)) {
		const rmcCpvPalette &pal = rmcCpvPalette::GetCurrent();
		out.cpvSets.Resize(numCpvSets);
		for (int s = 0; s < (int)numCpvSets; s++) {
			u32 setArray = image.ReadU32(modelCpvAddr + (u32)s * 4);
			atArray<u32> &curSet = out.cpvSets[s];
			curSet.Reserve(out.numVerts);
			for (int g = 0; g < out.geometries.GetCount(); g++) {
				const rscGeometry &geom = out.geometries[g];
				int part = geom.part;
				int k = geom.index;
				u32 countsPtr = 0, colorsPtr = 0;
				if (setArray && image.IsValidAddress(setArray + (u32)part * 8, 8)) {
					countsPtr = image.ReadU32(setArray + (u32)part * 8);
					colorsPtr = image.ReadU32(setArray + (u32)part * 8 + 4);
				}
				u32 streamPtr = 0, vertCount = 0;
				if (countsPtr && colorsPtr && image.IsValidAddress(countsPtr, 4)) {
					u32 nBatches = image.ReadU32(countsPtr);
					if ((u32)k < nBatches && image.IsValidAddress(countsPtr, 4 + nBatches * 4) && image.IsValidAddress(colorsPtr, nBatches * 4)) {
						vertCount = image.ReadU32(countsPtr + 4 + (u32)k * 4);
						streamPtr = image.ReadU32(colorsPtr + (u32)k * 4);
					}
				}
				u32 streamOffset = 0;
				for (int b = 0; b < geom.batches.GetCount(); b++) {
					const rscGeomBatch &bt = geom.batches[b];
					int bVerts = bt.verts.GetCount();
					if (streamPtr && vertCount > 0 && image.IsValidAddress(streamPtr, vertCount) && streamOffset < vertCount) {
						int toFill = bVerts;
						if (streamOffset + (u32)toFill > vertCount) toFill = (int)(vertCount - streamOffset);
						for (int v = 0; v < toFill; v++) {
							u8 idx = image.ReadU8(streamPtr + streamOffset + (u32)v);
							curSet.Append(pal.GetPackedColor(idx));
						}
						for (int v = toFill; v < bVerts; v++) {
							curSet.Append(bt.colors.GetCount() == bVerts ? bt.colors[v] : 0x80808080u);
						}
						streamOffset += (u32)toFill;
					} else {
						for (int v = 0; v < bVerts; v++) {
							curSet.Append(bt.colors.GetCount() == bVerts ? bt.colors[v] : 0x80808080u);
						}
					}
				}
			}
		}
	}
	return out.numVerts > 0;
}

void rscGeomBatch::BuildTriangles(atArray<rscTriangle> &tris, int primMode) const
{
	tris.Reset();
	int n = verts.GetCount();
	if (n < 3) return;

	if (primMode == 1) {
		// Triangle list
		for (int i = 0; i + 2 < n; i += 3) {
			const Vector3 &v0 = verts[i];
			const Vector3 &v1 = verts[i+1];
			const Vector3 &v2 = verts[i+2];
			if (!v0.IsEqual(v1, 1e-4f) && !v1.IsEqual(v2, 1e-4f) && !v0.IsEqual(v2, 1e-4f)) {
				rscTriangle t = { i, i + 1, i + 2 };
				tris.Append(t);
			}
		}
		return;
	}

	if (primMode == 2) {
		// Triangle fan
		for (int i = 1; i + 1 < n; i++) {
			const Vector3 &v0 = verts[0];
			const Vector3 &v1 = verts[i];
			const Vector3 &v2 = verts[i+1];
			if (!v0.IsEqual(v1, 1e-4f) && !v1.IsEqual(v2, 1e-4f) && !v0.IsEqual(v2, 1e-4f)) {
				rscTriangle t = { 0, i, i + 1 };
				tris.Append(t);
			}
		}
		return;
	}

	// PS2 VU ADC strip semantics: every vertex enters the strip window; a vertex whose
	// ADC bit is set is added WITHOUT a drawing kick.  So the triangle ending at vertex i
	// is drawn iff adc[i] == 0 and i >= 2, over the window (i-2, i-1, i), with the GS
	// alternating the winding by the vertex position.  A strip restart is two ADC
	// vertices in a row; a lone ADC vertex just skips one triangle and the strip goes on
	// (treating it as a restart dropped the following triangle as well -> holes).
	bool hasValidAdc = (primMode == 0 && adc.GetCount() == n && n >= 3);
	if (hasValidAdc) {
		for (int i = 2; i < n; i++) {
			if (adc[i]) continue;
			bool odd = (i & 1) != 0;
			int i0 = odd ? i - 1 : i - 2;
			int i1 = odd ? i - 2 : i - 1;
			const Vector3 &v0 = verts[i0];
			const Vector3 &v1 = verts[i1];
			const Vector3 &v2 = verts[i];
			if (!v0.IsEqual(v1, 1e-4f) && !v1.IsEqual(v2, 1e-4f) && !v0.IsEqual(v2, 1e-4f)) {
				rscTriangle t = { i0, i1, i };
				tris.Append(t);
			} else if (sDropLog) {
				Displayf("      dropped degenerate tri (%d,%d,%d): (%.4f %.4f %.4f) (%.4f %.4f %.4f) (%.4f %.4f %.4f)", i0, i1, i,
				         v0.x, v0.y, v0.z, v1.x, v1.y, v1.z, v2.x, v2.y, v2.z);
			}
		}
		return;
	}

	// 4-vertex quads: explicit quad mode (primMode == 3) OR single 4-vertex quad (primMode == 0 && n == 4)
	if (primMode == 3 || (primMode == 0 && n == 4)) {
		for (int q = 0; q + 3 < n; q += 4) {
			const Vector3 &v0 = verts[q];
			const Vector3 &v1 = verts[q+1];
			const Vector3 &v2 = verts[q+2];
			const Vector3 &v3 = verts[q+3];
			if (!v0.IsEqual(v1, 1e-4f) && !v1.IsEqual(v2, 1e-4f) && !v0.IsEqual(v2, 1e-4f)) {
				rscTriangle t = { q, q + 1, q + 2 };
				tris.Append(t);
			}
			if (q + 4 < n && v3.IsEqual(verts[q + 4], 1e-4f))
				continue;
			if (!v2.IsEqual(v1, 1e-4f) && !v1.IsEqual(v3, 1e-4f) && !v2.IsEqual(v3, 1e-4f)) {
				rscTriangle t = { q + 2, q + 1, q + 3 };
				tris.Append(t);
			}
		}
		return;
	}

	// A skinned batch (a ped) restarts its strips through the ADC bit and
	// nothing else: every part of it is within a few centimetres of the next,
	// so the bridge-jump distance test below - which is what separates one
	// piece of city geometry from another - never fires, and the bridging
	// triangles come out as spikes reaching across the body.  The bit means
	// "this vertex does not kick a triangle"; two in a row are the two vertices
	// a new strip needs before it can draw, so the run is where it begins.
	if (adc.GetCount() == n && bones.GetCount() == n) {
		int stripStart = 0;
		for (int i = 0; i + 2 < n; i++) {
			if (adc[i + 2]) {
				if (!adc[i + 1])
					stripStart = i + 2;          // first of the run: the new strip's first vertex
				continue;
			}
			int even = ((i - stripStart) % 2) == 0;
			int i0 = even ? i : i + 1;
			int i1 = even ? i + 1 : i;
			int i2 = i + 2;
			const Vector3 &v0 = verts[i0];
			const Vector3 &v1 = verts[i1];
			const Vector3 &v2 = verts[i2];
			if (!v0.IsEqual(v1, 1e-4f) && !v1.IsEqual(v2, 1e-4f) && !v0.IsEqual(v2, 1e-4f)) {
				rscTriangle t = { i0, i1, i2 };
				tris.Append(t);
			}
		}
		return;
	}

	// Triangle strip with degenerate culling, duplicate-vertex restart, and bridge jump detection
	int stripStart = 0;
	int i = 0;
	while (i < n - 2) {
		// If vertex i+2 is identical to i+3, vertex i+2 begins a new strip;
		// triangle i (ending at i+2) is the bridge triangle connecting old strip to new strip!
		// Suppress it and jump to the new strip start.
		if (i + 3 < n && verts[i + 2].IsEqual(verts[i + 3], 1e-4f)) {
			stripStart = i + 2;
			i = i + 2;
			continue;
		}
		// Bridge jump restart: if distance between consecutive strip vertices is unnaturally large
		// compared to previous edge (e.g. jumping across opposite sides of a cylinder/part), start new strip
		float d_bridge = (verts[i + 2] - verts[i + 1]).Mag();
		float d_prev = (verts[i + 1] - verts[i]).Mag();
		if (d_prev > 1e-4f && d_bridge > 3.5f * d_prev && d_bridge > 0.40f) {
			stripStart = i + 2;
			i = i + 2;
			continue;
		}
		int i0 = ((i - stripStart) % 2 == 0) ? i : i + 1;
		int i1 = ((i - stripStart) % 2 == 0) ? i + 1 : i;
		int i2 = i + 2;
		const Vector3 &v0 = verts[i0];
		const Vector3 &v1 = verts[i1];
		const Vector3 &v2 = verts[i2];
		if (!v0.IsEqual(v1, 1e-4f) && !v1.IsEqual(v2, 1e-4f) && !v0.IsEqual(v2, 1e-4f)) {
			rscTriangle t = { i0, i1, i2 };
			tris.Append(t);
		}
		i++;
	}
}

bool rscDecodePsmt8ToRgba(const u8 *pixelIndices, const u8 *paletteData, u32 *outRgba, int width, int height)
{
	if (!pixelIndices || !outRgba || width <= 0 || height <= 0) return false;
	int totalPixels = width * height;

	if (paletteData) {
		const u32 *pal32 = (const u32*)paletteData;
		for (int i = 0; i < totalPixels; i++) {
			int pIdx = pixelIndices[i];
			// PS2 GS CLUT unswizzle for PSMT8 in PSMCT32 mode
			if ((pIdx & 0x18) == 0x08 || (pIdx & 0x18) == 0x10)
				pIdx ^= 0x18;
			outRgba[i] = pal32[pIdx];
		}
	} else {
		for (int i = 0; i < totalPixels; i++) {
			u8 idx = pixelIndices[i];
			outRgba[i] = (u32)idx | ((u32)idx << 8) | ((u32)idx << 16) | 0xff000000;
		}
	}
	return true;
}

#include "gfx/model.h"
#include "gfx/texture.h"
#include "data/pager.h"
#include "rmcore/drawable.h"
#include "rmcore/lodgroup.h"
#include <vector>
#include <map>

#ifndef CITY_PAGE_FILE_SLOT
#define CITY_PAGE_FILE_SLOT 123
#endif

#include "data/pager.h"
#include "gfx/texture.h"

// rmcShader(uchar) stores the class in +4 bits 0-6; the instance classes pack the template
// slot into bits 15-21 (Bind indexes sm_Templates with (w >> 13) & 0x1fc).
static void sReadShaderClass(const datResourceImage &image, u32 inst, rscTexInfo &info)
{
	info.shaderType = 0xff;
	info.templateSlot = 0;
	info.layerTex = 0;
	info.tint[0] = info.tint[1] = info.tint[2] = 255;
	info.layerScale[0] = info.layerScale[1] = 1.0f;
	info.isWater = false;
	info.scrollU = 0.0f;
	info.scrollV = 0.0f;
	if (!image.IsValidAddress(inst, 8)) return;
	u32 vtab = image.ReadU32(inst);
	u32 w = image.ReadU32(inst + 4);
	info.shaderType = (u8)(w & 0x7f);
	info.templateSlot = (u8)((w >> 15) & 0x7f);
	if (vtab == 0x007a1f58 && image.IsValidAddress(inst, 0x2c)) {
		// rmcShaderComplex: check for water.shadert
		u32 namePtr = image.ReadU32(inst + 0x28);
		if (image.IsValidAddress(namePtr, 1)) {
			const char *shName = image.ReadString(namePtr);
			if (shName && strstr(shName, "water")) {
				info.isWater = true;
				// Base wave parameters (matching water.shadert)
				info.scrollU = 0.0f;
				info.scrollV = 0.04f;
				info.layerScale[0] = 0.25f;
				info.layerScale[1] = 0.25f;

				// Pass 1 TexMtx rate if present
				u32 texMtx = image.ReadU32(inst + 0x1c);
				if (image.IsValidAddress(texMtx, 0x28)) {
					float r = image.ReadFloat(texMtx + 0x24);
					if (r > 0.001f && r < 10.0f) info.scrollV = r;
				}
			}
		}
	} else if (info.shaderType == 16 && image.IsValidAddress(inst, 0x20)) {
		// mcShaderCityWindow params (4 bytes each from +8): BaseTexture, WindowTexture, r, g, b, a
		info.layerTex = image.ReadU32(inst + 0x0c);
		for (int c = 0; c < 3; c++) {
			float f = image.ReadFloat(inst + 0x10 + (u32)c * 4);
			info.tint[c] = (u8)(f < 0.0f ? 0 : f > 255.0f ? 255 : (int)(f + 0.5f));
		}
	} else if (info.shaderType == 17 && image.IsValidAddress(inst, 0x18)) {
		// road instance params: color texture, texture1 (detail), scales, scalet
		info.layerTex = image.ReadU32(inst + 0x0c);
		info.layerScale[0] = image.ReadFloat(inst + 0x10);
		info.layerScale[1] = image.ReadFloat(inst + 0x14);
	}
}

int rscCityBlendState(int part, int shaderType, int templateSlot, bool isWater)
{
	if (isWater) return rscCityWater;
	switch (shaderType) {
	case 2:                                         // rmcShaderInstance: the template's states win
		if (templateSlot == 3) return rscCityInvBlend;                          // city_window_cutout
		if (templateSlot >= 5 && templateSlot <= 8) return rscCityBlend45;     // hdr_object, doublesided(_hdr/_prop)
		if (templateSlot == 4) return rscCityOpaque;                            // city_road sets nothing: opaque base
		break;                                      // city_facade: its states are unused, the pass decides
	case 16: return rscCityOpaque;                  // mcShaderCityWindow base pass (alphablend off)
	case 17: return rscCityOpaque;                  // road instance
	case 0: case 1: case 19: case 20: break;        // Basic / Complex / texscroll / TexAnimation bind no blend state
	default: return -1;
	}
	return (part == 2 || part == 4) ? rscCityBlend45 : rscCityOpaque;   // REFLECT / ALPHA passes blend, the rest are opaque
}

// The second LAYERED_TEXTURE map of a shader that carries one (see rscgeom.h).  Only the
// 0x007a1f58 class keeps a template name, at +0x28, with its second image at +0x24; the
// name is validated as a printable ".shadert" before either is trusted, so a shader of
// another class cannot be mistaken for one of these.
bool rscDecodeShaderLayerMap(const datResourceImage &image, u32 shaderAddr, rscTexInfo &out,
                             char *templateOut, int templateLen)
{
	if (templateOut && templateLen > 0) templateOut[0] = 0;
	if (!shaderAddr || !image.IsValidAddress(shaderAddr, 0x2c)) return false;
	if (image.ReadU32(shaderAddr) != 0x007a1f58) return false;

	const u32 namePtr = image.ReadU32(shaderAddr + 0x28);
	if (!image.IsValidAddress(namePtr, 1)) return false;
	const char *name = image.ReadString(namePtr);
	if (!name || !name[0]) return false;
	for (const char *c = name; *c; c++)
		if (*c < 32 || *c > 126) return false;
	const size_t len = strlen(name);
	if (len < 9 || _stricmp(name + len - 8, ".shadert") != 0) return false;

	if (!rscParseTexInfo(image, image.ReadU32(shaderAddr + 0x24), out)) return false;

	if (templateOut && templateLen > 0) {
		strncpy(templateOut, name, (size_t)templateLen - 1);
		templateOut[templateLen - 1] = 0;
	}
	return true;
}

// A shader's template name alone, for any shader class that keeps one at +0x28.
// The layer-map decoder above insists on one class and on a second image; the
// prop packs' city_flarebg / city_flarelightmap / water / road_reflector carry a
// name without either.  Validated the same way - a printable string ending in
// ".shadert" - so a pointer that merely happens to sit at +0x28 of some other
// class cannot be mistaken for a template name.
bool rscDecodeShaderTemplateName(const datResourceImage &image, u32 shaderAddr,
                                 char *templateOut, int templateLen)
{
	if (templateOut && templateLen > 0) templateOut[0] = 0;
	if (!shaderAddr || !image.IsValidAddress(shaderAddr, 0x2c)) return false;

	const u32 namePtr = image.ReadU32(shaderAddr + 0x28);
	if (!image.IsValidAddress(namePtr, 1)) return false;
	const char *name = image.ReadString(namePtr);
	if (!name || !name[0]) return false;
	for (const char *c = name; *c; c++)
		if (*c < 32 || *c > 126) return false;
	const size_t len = strlen(name);
	if (len < 9 || _stricmp(name + len - 8, ".shadert") != 0) return false;

	if (templateOut && templateLen > 0) {
		strncpy(templateOut, name, (size_t)templateLen - 1);
		templateOut[templateLen - 1] = 0;
	}
	return true;
}


u32 rscShaderImageAddr(const datResourceImage &image, u32 shaderAddr)
{
	if (!shaderAddr || !image.IsValidAddress(shaderAddr, 0x2c)) return 0;
	// A shader that carries a template name holds it inline at +0x30 and embeds its
	// gfxImage immediately after, padded to 16.  Verified against every prop and city
	// pack: sd_dusk_clear_props' eight named shaders all land on a 0x007a2320 texture
	// or a 0x007a20e0 wrapper this way, and all eight read a texture from another
	// shader entirely through +0x08 (shader 53, a city_flarebg, got a lit window).
	char templ[80];
	const u32 namePtr = image.ReadU32(shaderAddr + 0x28);
	if (namePtr > shaderAddr && namePtr < shaderAddr + 0x40 &&
	    rscDecodeShaderTemplateName(image, shaderAddr, templ, sizeof(templ))) {
		const u32 embedded = (namePtr + (u32)strlen(templ) + 1 + 15) & ~15u;
		if (image.IsValidAddress(embedded, 4)) {
			const u32 vtab = image.ReadU32(embedded);
			if (vtab == 0x007a2320 || vtab == 0x007a20e0) return embedded;
		}
	}
	return image.IsValidAddress(shaderAddr, 12) ? image.ReadU32(shaderAddr + 8) : 0;
}

int rscDecodeCityShaders(const datResourceImage &image, int groupIndex, atArray<rscTexInfo> &out)
{
	out.Reset();
	u32 root = image.GetBase();
	int numGroups = (int)image.ReadU32(root + 8);
	u32 groups = image.ReadU32(root + 0xc);
	if (numGroups <= 0 || numGroups > 64 || !image.IsValidAddress(groups, (u32)numGroups * 16)) return 0;
	// Every group table spans the same shader index space but only fills its own
	// slots (group 0 of sd_midnight_clear: 311 of 1516); groupIndex < 0 merges them,
	// first non-null slot wins.
	int firstGroup = groupIndex < 0 ? 0 : groupIndex, lastGroup = groupIndex < 0 ? numGroups - 1 : groupIndex;
	if (lastGroup >= numGroups) return 0;
	int count = (int)image.ReadU16(groups + (u32)firstGroup * 16 + 8);
	if (count <= 0 || count > 65535) return 0;
	for (int i = 0; i < count; i++) {
		rscTexInfo info;
		memset(&info, 0, sizeof(info));
		u32 inst = 0;
		for (int gi = firstGroup; gi <= lastGroup && !inst; gi++) {
			u32 g = groups + (u32)gi * 16;
			u32 table = image.ReadU32(g + 4);
			int gcount = (int)image.ReadU16(g + 8);
			if (!table || i >= gcount || !image.IsValidAddress(table + (u32)i * 4, 4)) continue;
			u32 v = image.ReadU32(table + (u32)i * 4);
			if (image.IsValidAddress(v, 12)) inst = v;
		}
		u32 vtab = image.IsValidAddress(inst, 4) ? image.ReadU32(inst) : 0;
		// A named shader (water, road_reflector, city_specular, city_texscroll_doublesided)
		// embeds its gfxImage after the name; the rest keep a pointer at +8.
		(void)vtab;
		u32 blk = rscShaderImageAddr(image, inst);
		rscParseTexInfo(image, blk, info);
		sReadShaderClass(image, inst, info);
		out.Append(info);
	}
	return out.GetCount();
}

int rscDecodeShaderGroup(const datResourceImage &image, u32 groupAddr, atArray<rscTexInfo> &out)
{
	out.Reset();
	if (!image.IsValidAddress(groupAddr, 16)) return 0;
	u32 table = image.ReadU32(groupAddr + 4);
	int count = (int)(image.ReadU32(groupAddr + 8) & 0xffff);
	if (!table || count <= 0 || !image.IsValidAddress(table, (u32)count * 4)) return 0;
	for (int i = 0; i < count; i++) {
		rscTexInfo info;
		memset(&info, 0, sizeof(info));
		u32 inst = image.ReadU32(table + (u32)i * 4);
		u32 vtab = image.IsValidAddress(inst, 4) ? image.ReadU32(inst) : 0;
		// A named shader (water, road_reflector, city_specular, city_texscroll_doublesided)
		// embeds its gfxImage after the name; the rest keep a pointer at +8.
		(void)vtab;
		u32 blk = rscShaderImageAddr(image, inst);
		rscParseTexInfo(image, blk, info);
		sReadShaderClass(image, inst, info);
		out.Append(info);
	}
	return out.GetCount();
}

int rscFindSkyhatModels(const datResourceImage &image, atArray<u32> &addrs)
{
	addrs.Reset();
	const u8 *d = image.GetData();
	u32 n = image.GetSize();
	u32 nameAddr = 0;
	for (u32 o = 0; o + 8 <= n && !nameAddr; o += 4)
		if (!memcmp(d + o, "skyhat_", 7)) nameAddr = image.ToAddress(o);
	if (!nameAddr) return 0;
	for (u32 o = 0; o + 4 <= n; o += 4) {
		u32 v = (u32)d[o] | ((u32)d[o + 1] << 8) | ((u32)d[o + 2] << 16) | ((u32)d[o + 3] << 24);
		if (v != nameAddr) continue;
		// mcSkyHatClass: name pointer at +8, layer count at +0x10, rmcModel pointers from +0x14
		u32 obj = image.ToAddress(o) - 8;
		if (!image.IsValidAddress(obj, 0x14 + 8 * 4)) continue;
		int layers = (int)image.ReadU32(obj + 0x10);
		if (layers < 1 || layers > 8) continue;
		for (int l = 0; l < layers; l++) {
			u32 m = image.ReadU32(obj + 0x14 + (u32)l * 4);
			if (image.IsValidAddress(m, 4) && image.ReadU32(m) == rscModelVTable) addrs.Append(m);
		}
		break;
	}
	return addrs.GetCount();
}

bool rscParseTexInfo(const datResourceImage &image, u32 blk, rscTexInfo &info)
{
	memset(&info, 0, sizeof(info));
	if (!blk || !image.IsValidAddress(blk, 4)) return false;

	// Many instances point at a texture wrapper (vtable 007a20e0: flags, name, ...) that
	// carries the 160-byte data block inline a little further on; find its vtable.
	if (image.ReadU32(blk) != 0x007a2320) {
		u32 found = 0;
		for (u32 o = 4; o <= 0x300 && image.IsValidAddress(blk + o, 4); o += 4) {
			u32 val = image.ReadU32(blk + o);
			if (val == 0x007a2320) { found = blk + o; break; }
			if (image.IsValidAddress(val, 4) && image.ReadU32(val) == 0x007a2320) { found = val; break; }
		}
		blk = found;
	}
	if (!blk || !image.IsValidAddress(blk, 0xa0)) return false;

	u32 lo = image.ReadU32(blk + 0x10), hi = image.ReadU32(blk + 0x14);
	info.psm = (u8)((lo >> 20) & 0x3f);
	info.width = (u16)(1u << ((lo >> 26) & 0xf));
	info.height = (u16)(1u << (((lo >> 30) & 3) | ((hi & 3) << 2)));
	static const u32 kOff[3] = { 0x4c, 0x58, 0x64 };
	for (int m = 0; m < 3; m++) {
		u32 ref = image.ReadU32(blk + kOff[m] + 4);
		if (!ref || ref == 0xcdcdcdcd) break;
		info.mipEntry[m] = ref & 0xffff;
		info.mipOffset[m] = image.ReadU32(blk + kOff[m]);
		info.numMips = m + 1;
	}
	// Resident mips: one block header per mip at +0x48 / +0x54 / +0x60 / +0x6c.
	// (The header's +8 points back at the texture object, not at the pixels; the
	// pixels sit at header + 0x90 like every other block.)
	info.numResidentMips = 0;
	for (int m = 0; m < 4; m++) {
		u32 res = image.ReadU32(blk + 0x48 + (u32)m * 12);
		if (!image.IsValidAddress(res, 32) || image.ReadU32(res) != 0) break;
		u32 sz = image.ReadU32(res + 12);
		if (sz < 0x90 || sz > 0x20000 || !image.IsValidAddress(res, sz)) break;
		info.residentMip[m] = res;
		info.numResidentMips = m + 1;
	}
	if (info.numResidentMips) {
		info.residentAddr = info.residentMip[0];
		info.residentSize = image.ReadU32(info.residentMip[0] + 12);
	}
	return (info.width > 0 && info.height > 0 && (info.residentAddr || info.numMips > 0));
}

void rscUnswizzlePsmt8(const u8 *src, u8 *dst, int width, int height)
{
	// The 8-bit texture was written through a 32-bit frame buffer: 16-row strips of
	// width*16 bytes, eight 2*width-byte rows per strip, 32-byte chunks per 16 texels,
	// WITH the (y+2)>>2 row-parity column swap, at every size.  Measured with
	// -swizzletest (mean neighbour RGB step, the wrong order scrambles 4x4 blocks):
	// all 1406 sd city + sky textures (16x16..256x256, page and resident) score lower
	// with the swap (1135 by >10%, none lower without), as do the car trim atlas and
	// all 11 rider skins; visually the 128x64 zoo billboard (sh 855) and 64x64 bricks
	// are clean only with it.  An earlier "no swap up to 64 rows" rule rested on
	// ca_plate_1, but the licence plates decode illegibly either way (a separate,
	// open problem), so they are no evidence for it.
	//
	// -pagetexnoswap forces it off, -pagetexswap on, for checking a texture.
	int sSwapOn = 1;
	static int sForce = -2;
	if (sForce == -2) sForce = ARGS.Get("pagetexswap") ? 1 : (ARGS.Get("pagetexnoswap") ? 0 : -1);
	if (sForce >= 0) sSwapOn = sForce;
	rscUnswizzlePsmt8(src, dst, width, height, sSwapOn != 0);
}

void rscUnswizzlePsmt8(const u8 *src, u8 *dst, int width, int height, bool swap)
{
	const int sSwap = swap ? 1 : 0;
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			int block = (y & ~0xf) * width + (x & ~0xf) * 2;
			int posY = (((y & ~3) >> 1) + (y & 1)) & 7;
			int swapSel = sSwap ? ((((y + 2) >> 2) & 1) * 4) : 0;
			int column = posY * width * 2 + ((x + swapSel) & 7) * 4;
			int byteNum = ((y >> 1) & 1) + ((x >> 2) & 2);
			dst[y * width + x] = src[block + column + byteNum];
		}
	}
}

float rscPsmt8Roughness(const u8 *src, const u8 *clut, int width, int height, bool swap)
{
	if (width < 2 || height < 2) return 0.0f;
	atArray<u8> idx;
	idx.Resize(width * height);
	rscUnswizzlePsmt8(src, &idx[0], width, height, swap);
	auto rgb = [&](int i) -> const u8 * {
		u32 k = idx[i];
		if ((k & 0x18) == 0x08 || (k & 0x18) == 0x10) k ^= 0x18;   // CSM1 CLUT layout
		return clut + k * 4;
	};
	double sum = 0.0;
	u32 n = 0;
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			const u8 *c = rgb(y * width + x);
			if (x + 1 < width) {
				const u8 *r = rgb(y * width + x + 1);
				sum += abs((int)c[0] - r[0]) + abs((int)c[1] - r[1]) + abs((int)c[2] - r[2]);
				n++;
			}
			if (y + 1 < height) {
				const u8 *d = rgb((y + 1) * width + x);
				sum += abs((int)c[0] - d[0]) + abs((int)c[1] - d[1]) + abs((int)c[2] - d[2]);
				n++;
			}
		}
	}
	return n ? (float)(sum / n) : 0.0f;
}

static const u8 s_BlockTable32[4][8] = {
	{  0,  1,  4,  5, 16, 17, 20, 21 },
	{  2,  3,  6,  7, 18, 19, 22, 23 },
	{  8,  9, 12, 13, 24, 25, 28, 29 },
	{ 10, 11, 14, 15, 26, 27, 30, 31 },
};

static const u8 s_ColumnTable32[8][8] = {
	{  0,  1,  4,  5,  8,  9, 12, 13 },
	{  2,  3,  6,  7, 10, 11, 14, 15 },
	{ 16, 17, 20, 21, 24, 25, 28, 29 },
	{ 18, 19, 22, 23, 26, 27, 30, 31 },
	{ 32, 33, 36, 37, 40, 41, 44, 45 },
	{ 34, 35, 38, 39, 42, 43, 46, 47 },
	{ 48, 49, 52, 53, 56, 57, 60, 61 },
	{ 50, 51, 54, 55, 58, 59, 62, 63 },
};

static const u8 s_BlockTable4[8][4] = {
	{  0,  2,  8, 10 },
	{  1,  3,  9, 11 },
	{  4,  6, 12, 14 },
	{  5,  7, 13, 15 },
	{ 16, 18, 24, 26 },
	{ 17, 19, 25, 27 },
	{ 20, 22, 28, 30 },
	{ 21, 23, 29, 31 },
};

static const u16 s_ColumnTable4[16][32] = {
	{   0,   8,  32,  40,  64,  72,  96, 104,   2,  10,  34,  42,  66,  74,  98, 106,   4,  12,  36,  44,  68,  76, 100, 108,   6,  14,  38,  46,  70,  78, 102, 110 },
	{  16,  24,  48,  56,  80,  88, 112, 120,  18,  26,  50,  58,  82,  90, 114, 122,  20,  28,  52,  60,  84,  92, 116, 124,  22,  30,  54,  62,  86,  94, 118, 126 },
	{  65,  73,  97, 105,   1,   9,  33,  41,  67,  75,  99, 107,   3,  11,  35,  43,  69,  77, 101, 109,   5,  13,  37,  45,  71,  79, 103, 111,   7,  15,  39,  47 },
	{  81,  89, 113, 121,  17,  25,  49,  57,  83,  91, 115, 123,  19,  27,  51,  59,  85,  93, 117, 125,  21,  29,  53,  61,  87,  95, 119, 127,  23,  31,  55,  63 },
	{ 192, 200, 224, 232, 128, 136, 160, 168, 194, 202, 226, 234, 130, 138, 162, 170, 196, 204, 228, 236, 132, 140, 164, 172, 198, 206, 230, 238, 134, 142, 166, 174 },
	{ 208, 216, 240, 248, 144, 152, 176, 184, 210, 218, 242, 250, 146, 154, 178, 186, 212, 220, 244, 252, 148, 156, 180, 188, 214, 222, 246, 254, 150, 158, 182, 190 },
	{ 129, 137, 161, 169, 193, 201, 225, 233, 131, 139, 163, 171, 195, 203, 227, 235, 133, 141, 165, 173, 197, 205, 229, 237, 135, 143, 167, 175, 199, 207, 231, 239 },
	{ 145, 153, 177, 185, 209, 217, 241, 249, 147, 155, 179, 187, 211, 219, 243, 251, 149, 157, 181, 189, 213, 221, 245, 253, 151, 159, 183, 191, 215, 223, 247, 255 },
	{ 256, 264, 288, 296, 320, 328, 352, 360, 258, 266, 290, 298, 322, 330, 354, 362, 260, 268, 292, 300, 324, 332, 356, 364, 262, 270, 294, 302, 326, 334, 358, 366 },
	{ 272, 280, 304, 312, 336, 344, 368, 376, 274, 282, 306, 314, 338, 346, 370, 378, 276, 284, 308, 316, 340, 348, 372, 380, 278, 286, 310, 318, 342, 350, 374, 382 },
	{ 321, 329, 353, 361, 257, 265, 289, 297, 323, 331, 355, 363, 259, 267, 291, 299, 325, 333, 357, 365, 261, 269, 293, 301, 327, 335, 359, 367, 263, 271, 295, 303 },
	{ 337, 345, 369, 377, 273, 281, 305, 313, 339, 347, 371, 379, 275, 283, 307, 315, 341, 349, 373, 381, 277, 285, 309, 317, 343, 351, 375, 383, 279, 287, 311, 319 },
	{ 448, 456, 480, 488, 384, 392, 416, 424, 450, 458, 482, 490, 386, 394, 418, 426, 452, 460, 484, 492, 388, 396, 420, 428, 454, 462, 486, 494, 390, 398, 422, 430 },
	{ 464, 472, 496, 504, 400, 408, 432, 440, 466, 474, 498, 506, 402, 410, 434, 442, 468, 476, 500, 508, 404, 412, 436, 444, 470, 478, 502, 510, 406, 414, 438, 446 },
	{ 385, 393, 417, 425, 449, 457, 481, 489, 387, 395, 419, 427, 451, 459, 483, 491, 389, 397, 421, 429, 453, 461, 485, 493, 391, 399, 423, 431, 455, 463, 487, 495 },
	{ 401, 409, 433, 441, 465, 473, 497, 505, 403, 411, 435, 443, 467, 475, 499, 507, 405, 413, 437, 445, 469, 477, 501, 509, 407, 415, 439, 447, 471, 479, 503, 511 },
};

// PSMT4 "4-bit through a 32-bit frame buffer": the common unswizzle4 formula
// WITH its (y+2)>>2 column swap (unlike PSMT8, which this data stores without
// it).  Chosen by the mip-consistency test on sd's shared resident road texture
// (mip 1 vs downscaled mip 0: 3.1 with the swap, 8.0 without, 17-19 for the
// byte-wise / linear readings); the GS block-table reconstruction that preceded
// it was never validated because the resident pixels were read from the wrong
// address.
void rscUnswizzlePsmt4(const u8 *src, u8 *dst, int width, int height)
{
	const int pagesHorz = (width + 127) / 128;
	const int pagesVert = (height + 127) / 128;
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			int pageX = x & ~127, pageY = y & ~127;
			int pageNumber = (pageY / 128) * pagesHorz + (pageX / 128);
			int page32Y = (pageNumber / pagesVert) * 32;
			int page32X = (pageNumber % pagesVert) * 64;
			int pageLocation = page32Y * width * 2 + page32X * 4;
			int locX = x & 127, locY = y & 127;
			int blockLocation = ((locX & ~31) >> 1) * height + (locY & ~15) * 2;
			int swapSelector = (((y + 2) >> 2) & 1) * 4;
			int posY = (((y & ~3) >> 1) + (y & 1)) & 7;
			int columnLocation = posY * height * 2 + ((x + swapSelector) & 7) * 4;
			int byteNum = (x >> 3) & 3;
			int nibble = (y >> 1) & 1;
			u8 b = src[pageLocation + blockLocation + columnLocation + byteNum];
			dst[y * width + x] = (u8)((b >> (nibble * 4)) & 0xf);
		}
	}
}

static bool sReadPageBlock(int slot, u32 entry, u32 offset, atArray<u8> &block)
{
	block.Reset();
	if (!datPageFile::IsMounted(slot)) return false;
	if ((int)entry >= datPageFile::GetNumEntries(slot)) return false;
	u64 file = (u64)datPageFile::GetEntrySector(slot, (int)entry) * 2048ull + offset;
	u8 hdr[32];
	if (!datPageFile::ReadPageByOffset(slot, file, hdr, 32)) return false;
	u32 size = (u32)hdr[12] | ((u32)hdr[13] << 8) | ((u32)hdr[14] << 16) | ((u32)hdr[15] << 24);
	if (size <= 32 || size > 0x11000) return false;
	block.Resize((int)size);
	return datPageFile::ReadPageByOffset(slot, file, &block[0], size);
}

// -texlog: report every page texture built (rscview / game diagnostics).
// Evaluated on first use: ARGS is not populated during static initialisation.
static bool sTexLogEnabled()
{
	static int state = -1;
	if (state < 0) state = ARGS.Get("texlog") ? 1 : 0;
	return state == 1;
}

gfxTexture *rscLoadCityTexture(const datResourceImage &image, int ppfSlot, const rscTexInfo &info, const char *name)
{
	return rscLoadCityTexture(image, ppfSlot, info, name, NULL, 128);
}

gfxTexture *rscLoadCityTexture(const datResourceImage &image, int ppfSlot, const rscTexInfo &info, const char *name,
                               const u8 *rgbScale, int alphaScale)
{
	const bool sTexLog = sTexLogEnabled();
	const u32 kPixels = 0x90;           // 32-byte header + 112 bytes of padding
	if (info.psm != 0x13 && info.psm != 0x14) return NULL;
	if (info.width < 8 || info.height < 8 || info.width > 1024 || info.height > 1024) return NULL;
	u32 npix = (u32)info.width * info.height;
	u32 nbytes = (info.psm == 0x14) ? (npix / 2) : npix;
	u32 clutBytes = (info.psm == 0x14) ? 64 : 1024;

	atArray<u8> pixBlock, clutBlock;
	const u8 *pix = 0, *clut = 0;
	if (info.residentAddr) {
		// resident: [hdr][pad][pixels] per mip block in the image, the CLUT after the
		// smallest mip's pixels (validated on sd's shared road texture, 4 mips)
		if (!image.IsValidAddress(info.residentAddr, kPixels + nbytes)) return NULL;
		pix = image.At(info.residentAddr + kPixels);
		int last = info.numResidentMips > 0 ? info.numResidentMips - 1 : 0;
		u32 lastAddr = info.numResidentMips > 0 ? info.residentMip[last] : info.residentAddr;
		u32 lastNpix = npix >> (2 * last);
		u32 lastNbytes = (info.psm == 0x14) ? (lastNpix / 2) : lastNpix;
		if (!image.IsValidAddress(lastAddr, kPixels + lastNbytes + clutBytes)) return NULL;
		clut = image.At(lastAddr + kPixels + lastNbytes);
	} else {
		if (info.numMips <= 0) return NULL;
		if (!sReadPageBlock(ppfSlot, info.mipEntry[0], info.mipOffset[0], pixBlock)) return NULL;
		if ((u32)pixBlock.GetCount() < kPixels + nbytes) return NULL;
		int last = info.numMips - 1;
		u32 lastNpix = npix >> (2 * last);
		u32 lastNbytes = (info.psm == 0x14) ? (lastNpix / 2) : lastNpix;
		if (last == 0) {
			if ((u32)pixBlock.GetCount() < kPixels + nbytes + clutBytes) return NULL;
			clut = &pixBlock[0] + kPixels + nbytes;
		} else {
			if (!sReadPageBlock(ppfSlot, info.mipEntry[last], info.mipOffset[last], clutBlock)) return NULL;
			if ((u32)clutBlock.GetCount() < kPixels + lastNbytes + clutBytes) return NULL;
			clut = &clutBlock[0] + kPixels + lastNbytes;
		}
		pix = &pixBlock[0] + kPixels;
	}

	atArray<u8> linear;
	linear.Resize((int)npix);
	atArray<u8> rgba;
	rgba.Resize((int)npix * 4);

	if (info.psm == 0x13) {
		if (info.linear)
			memcpy(&linear[0], pix, npix);
		else
			rscUnswizzlePsmt8(pix, &linear[0], info.width, info.height);
		if (!info.linear && ARGS.Get("swizzletest")) {
			float r0 = rscPsmt8Roughness(pix, clut, info.width, info.height, false);
			float r1 = rscPsmt8Roughness(pix, clut, info.width, info.height, true);
			Displayf("[swizzle] city '%s' %ux%u %s mips %d: noswap %.2f swap %.2f -> %s", name ? name : "?", info.width, info.height,
			         info.residentAddr ? "resident" : "page", info.residentAddr ? info.numResidentMips : info.numMips, r0, r1,
			         r1 < r0 * 0.9f ? "SWAP" : r0 < r1 * 0.9f ? "NOSWAP" : "unclear");
		}
		for (u32 i = 0; i < npix; i++) {
			u32 idx = linear[(int)i];
			if ((idx & 0x18) == 0x08 || (idx & 0x18) == 0x10) idx ^= 0x18;   // CSM1 CLUT layout
			const u8 *c = clut + idx * 4;
			u32 a = (u32)c[3] * 2;
			rgba[(int)i * 4 + 0] = c[0];
			rgba[(int)i * 4 + 1] = c[1];
			rgba[(int)i * 4 + 2] = c[2];
			rgba[(int)i * 4 + 3] = (u8)(a > 255 ? 255 : a);
		}
	} else {
		rscUnswizzlePsmt4(pix, &linear[0], info.width, info.height);
		for (u32 i = 0; i < npix; i++) {
			u32 idx = linear[(int)i] & 0x0f;
			const u8 *c = clut + idx * 4;
			u32 a = (u32)c[3] * 2;
			rgba[(int)i * 4 + 0] = c[0];
			rgba[(int)i * 4 + 1] = c[1];
			rgba[(int)i * 4 + 2] = c[2];
			rgba[(int)i * 4 + 3] = (u8)(a > 255 ? 255 : a);
		}
	}

	// Check if this is a resident prop shader that is a flare / light cone
	bool isPropFlare = false;
	if (name && strncmp(name, "prop_sh_", 8) == 0 && image.IsLoaded()) {
		u32 idx = 0;
		if (sscanf(name, "prop_sh_%u", &idx) == 1) {
			u32 rootAddr = image.GetBase();
			if (image.IsValidAddress(rootAddr, 16)) {
				u32 pShaderGroup = image.ReadU32(rootAddr + 12);
				if (pShaderGroup && image.IsValidAddress(pShaderGroup, 16)) {
					u32 tablePtr = image.ReadU32(pShaderGroup + 4);
					u32 count = image.ReadU32(pShaderGroup + 8) & 0xffff;
					if (tablePtr && idx < count && image.IsValidAddress(tablePtr + idx * 4, 4)) {
						u32 shPtr = image.ReadU32(tablePtr + idx * 4);
						if (shPtr && image.IsValidAddress(shPtr, 0x60)) {
							for (u32 off = 0x20; off <= 0x50; off += 4) {
								const char *s = image.ReadString(shPtr + off);
								if (s && s[0] && (strstr(s, "flare") || strstr(s, "Flare") ||
								                  strstr(s, "cone") || strstr(s, "beam"))) {
									isPropFlare = true;
									break;
								}
							}
						}
					}
				}
			}
		}
	}

	if (isPropFlare) {
		bool anyTrans = false;
		for (u32 i = 0; i < npix; i++) {
			if (rgba[(int)i * 4 + 3] < 254) {
				anyTrans = true;
				break;
			}
		}
		// If the flare texture's CLUT had no alpha channel, synthesize alpha from luminance
		// so that the flare fades smoothly from bright center to dark edges.
		if (!anyTrans) {
			for (u32 i = 0; i < npix; i++) {
				u32 r = rgba[(int)i * 4 + 0];
				u32 g = rgba[(int)i * 4 + 1];
				u32 b = rgba[(int)i * 4 + 2];
				u32 lum = (r * 77 + g * 150 + b * 29) >> 8;
				rgba[(int)i * 4 + 3] = (u8)lum;
			}
		}
	}

	if (name && ARGS.Get("dumptex")) {
		// -dumptex: write every decoded city texture as <name>.tga (32-bit, top-down) in the cwd
		char fname[128];
		formatf(fname, sizeof(fname), "%s.tga", name);
		FILE *fp = fopen(fname, "wb");
		if (fp) {
			u16 w = info.width, h = info.height;
			u8 hdr[18] = { 0, 0, 2, 0,0,0,0,0, 0,0,0,0, (u8)(w & 0xFF), (u8)(w >> 8), (u8)(h & 0xFF), (u8)(h >> 8), 32, 0x28 };
			fwrite(hdr, 1, 18, fp);
			std::vector<u8> bgra(npix * 4);
			for (u32 i = 0; i < npix; i++) {
				bgra[i * 4 + 0] = rgba[i * 4 + 2]; // B
				bgra[i * 4 + 1] = rgba[i * 4 + 1]; // G
				bgra[i * 4 + 2] = rgba[i * 4 + 0]; // R
				bgra[i * 4 + 3] = rgba[i * 4 + 3]; // A
			}
			fwrite(bgra.data(), 1, bgra.size(), fp);
			fclose(fp);
		}
	}
	if (sTexLog) {
		u32 sum = 0, opaque = 0;
		u8 minA = 255, maxA = 0;
		for (u32 i = 0; i < npix; i++) {
			u8 a = rgba[(int)i * 4 + 3];
			sum += rgba[(int)i * 4];
			if (a >= 254) opaque++;
			if (a < minA) minA = a;
			if (a > maxA) maxA = a;
		}
		int lastMip = info.residentAddr ? (info.numResidentMips > 0 ? info.numResidentMips - 1 : 0) : (info.numMips > 0 ? info.numMips - 1 : 0);
		Displayf("rscLoadCityTexture: '%s' %ux%u psm %#x entry %u +%#x: mean red %u, %u of %u opaque, alpha %u..%u, %s mips %d (clut in mip %d%s)", name, info.width, info.height, info.psm,
		         info.mipEntry[0], info.mipOffset[0], sum / npix, opaque, npix, minA, maxA, info.residentAddr ? "resident" : "page",
		         info.residentAddr ? info.numResidentMips : info.numMips, lastMip, isPropFlare ? ", flare" : "");
	}
	if (rgbScale || alphaScale != 128) {
		// GS modulate: Cf = Ct * Cv >> 7 per channel (a tint of 128 is 1.0)
		for (u32 i = 0; i < npix; i++) {
			for (int c = 0; c < 3 && rgbScale; c++) {
				int v = (int)rgba[(int)i * 4 + c] * rgbScale[c] / 128;
				rgba[(int)i * 4 + c] = (u8)(v > 255 ? 255 : v);
			}
			int a = (int)rgba[(int)i * 4 + 3] * alphaScale / 128;
			rgba[(int)i * 4 + 3] = (u8)(a > 255 ? 255 : a);
		}
	}
	gfxTexture *tex = gfxRegisterRgbaTexture(name, info.width, info.height, &rgba[0]);
	if (isPropFlare && tex) {
		tex->SetAllTranslucent(true);
		tex->SetHasAlpha(true);
	}
	return tex;
}

// Which mounted page files hold their pixels unswizzled.  Measured by decoding
// every named texture in a file both ways and scoring mean neighbour RGB step
// (the wrong order scrambles 16x16 blocks): in decal_g.ppf 80 of the 143 score
// clearly lower read linearly and not one scores lower unswizzled - the other
// 63 are the font sheets, too flat for the score to separate, and those decode
// legibly linear too.  In rider.ppf all 70 score lower unswizzled.  That is the
// PS2's own split: it clears rmcEnableSwizzle only around the decal file's
// plate, decal and logo textures.  -pagetexlinear / -pagetexswizzled force it.
static bool sPageFileIsLinear(int slot)
{
	static int sForce = -2;
	if (sForce == -2) sForce = ARGS.Get("pagetexlinear") ? 1 : (ARGS.Get("pagetexswizzled") ? 0 : -1);
	if (sForce >= 0) return sForce != 0;
	const char *file = datPageFile::GetName(slot);
	if (!file) return false;
	char lower[160];
	strncpy(lower, file, sizeof(lower) - 1);
	lower[sizeof(lower) - 1] = 0;
	for (char *c = lower; *c; c++) *c = (char)tolower((unsigned char)*c);
	return strstr(lower, "decal") != NULL;
}

gfxTexture *rscLoadPageTexture(int ppfSlot, const rscPageTexRef &ref, const char *name)
{
	if (ppfSlot < 0 || ref.psm != 0x13 || ref.width < 16 || ref.height < 1) return NULL;
	rscTexInfo info;
	info.psm = ref.psm;
	info.width = ref.width;
	info.height = ref.height;

	// -pagetexwidth <n>: force the unswizzle width, to test whether a texture
	// that decodes with block-level scrambling is simply being read at the
	// wrong stride.
	{
		int forced = 0;
		if (ARGS.Get("pagetexwidth", 0, forced) && forced > 0) {
			info.height = (u16)((int)info.width * (int)info.height / forced);
			info.width = (u16)forced;
		}
	}
	info.numMips = 1;
	info.mipEntry[0] = ref.entry;
	info.mipOffset[0] = ref.offset;
	info.residentAddr = 0;
	info.residentSize = 0;
	info.numResidentMips = 0;
	info.linear = sPageFileIsLinear(ppfSlot);
	datResourceImage none;
	return rscLoadCityTexture(none, ppfSlot, info, name);
}

gfxTexture *rscLoadResidentTexture(const datResourceImage &image, const char *name)
{
	// one decode per (image, name): the same atlas is named by many shaders
	struct Entry { const datResourceImage *image; char name[64]; gfxTexture *tex; };
	static atArray<Entry> sCache;
	for (int i = 0; i < sCache.GetCount(); i++)
		if (sCache[i].image == &image && !_stricmp(sCache[i].name, name)) return sCache[i].tex;
	Entry e;
	e.image = &image;
	strncpy(e.name, name, sizeof(e.name) - 1);
	e.name[sizeof(e.name) - 1] = 0;
	e.tex = NULL;
	const u32 kProxyVTable = 0x007a14a0;
	// The ped packs build the same object from a different class, so it carries
	// a different vtable; the fields this reads - TEX0 at +0x10, the resident
	// block at +0x48, the name at +0x78 - are in the same places.
	const u32 kPedProxyVTable = 0x00793638;
	const u8 *d = image.GetData();
	u32 n = image.GetSize();
	for (u32 o = 0; o + 0x50 <= n; o += 4) {
		u32 v = (u32)d[o] | ((u32)d[o + 1] << 8) | ((u32)d[o + 2] << 16) | ((u32)d[o + 3] << 24);
		if (v != kProxyVTable && v != kPedProxyVTable) continue;
		u32 obj = image.ToAddress(o);
		u32 nameAddr = image.ReadU32(obj + 0x78);
		const char *nm = nameAddr ? image.ReadString(nameAddr) : 0;
		if (!nm || _stricmp(nm, name) != 0) continue;
		u32 tex0lo = image.ReadU32(obj + 0x10), tex0hi = image.ReadU32(obj + 0x14);
		rscTexInfo info;
		memset(&info, 0, sizeof(info));
		info.psm = (u8)((tex0lo >> 20) & 0x3f);
		info.width = (u16)(1u << ((tex0lo >> 26) & 0xf));
		info.height = (u16)(1u << (((tex0lo >> 30) & 3) | ((tex0hi & 3) << 2)));
		info.residentAddr = image.ReadU32(obj + 0x48);
		info.numMips = 1;
		if (!info.residentAddr || !image.IsValidAddress(info.residentAddr, 0x90)) break;
		e.tex = rscLoadCityTexture(image, -1, info, name);
		Displayf("rscLoadResidentTexture: '%s' proxy %08x psm %#x %ux%u block %08x -> %s", name, obj, info.psm, info.width, info.height,
		         info.residentAddr, e.tex ? "ok" : "not decoded");
		break;
	}
	sCache.Append(e);
	return e.tex;
}

// Index of every named texture block in a page file, built on the first lookup
// (106 texture-block pages in decal_g.ppf; scanning them per name was the cost).
struct rscPageTexIndexEntry { char name[40]; rscPageTexRef ref; };
struct rscPageTexIndex { int slot; char file[128]; atArray<rscPageTexIndexEntry> entries; };
static atArray<rscPageTexIndex *> s_PageTexIndexes;

static rscPageTexIndex *sBuildPageTexIndex(int slot)
{
	const char *file = datPageFile::GetName(slot);
	for (int i = 0; i < s_PageTexIndexes.GetCount(); i++)
		if (s_PageTexIndexes[i]->slot == slot && !_stricmp(s_PageTexIndexes[i]->file, file ? file : "")) return s_PageTexIndexes[i];
	rscPageTexIndex *ix = new rscPageTexIndex;
	ix->slot = slot;
	strncpy(ix->file, file ? file : "", sizeof(ix->file) - 1);
	ix->file[sizeof(ix->file) - 1] = 0;
	const u32 kPageTextureVTable = 0x007a27a8;
	int n = datPageFile::GetNumEntries(slot);
	int pages = 0;
	static atArray<u8> page;
	for (int entry = 0; entry < n; entry++) {
		u64 fileOff = (u64)datPageFile::GetEntrySector(slot, entry) * 2048ull;
		u8 hdr[32];
		if (!datPageFile::ReadPageByOffset(slot, fileOff, hdr, 32)) continue;
		u32 owner = (u32)hdr[0] | ((u32)hdr[1] << 8) | ((u32)hdr[2] << 16) | ((u32)hdr[3] << 24);
		u32 blockSize = (u32)hdr[12] | ((u32)hdr[13] << 8) | ((u32)hdr[14] << 16) | ((u32)hdr[15] << 24);
		u32 hdrPad1 = (u32)hdr[4] | ((u32)hdr[5] << 8) | ((u32)hdr[6] << 16) | ((u32)hdr[7] << 24);
		u32 hdrPad2 = (u32)hdr[8] | ((u32)hdr[9] << 8) | ((u32)hdr[10] << 16) | ((u32)hdr[11] << 24);
		u32 hdrMark = (u32)hdr[16] | ((u32)hdr[17] << 8) | ((u32)hdr[18] << 16) | ((u32)hdr[19] << 24);
		// A texture-block page is recognised by the shape of its first block
		// header - owner-less, two zero words, then the CD fill - and not by how
		// big that block is.  Three rider pages lead with a 0x180-byte block (an
		// 8x8 "%1" texture) ahead of the 128x128 skin, and a size floor threw the
		// whole page away, losing coprider_3, _4 and _5.
		if (owner != 0 || hdrPad1 != 0 || hdrPad2 != 0) continue;                     // not a texture-block page
		if (hdrMark != 0xcdcdcdcdu || blockSize < 0x40 || blockSize > 0x20000) continue;
		u32 size = 0;
		page.Resize(1 << 20);
		if (!datPageFile::ReadEntry(slot, entry, &page[0], (u32)page.GetCount(), &size) || size < 0x100) continue;
		pages++;
		for (u32 o = 0; o + 0xa0 + 40 <= size; o += 16) {
			u32 v = (u32)page[o] | ((u32)page[o + 1] << 8) | ((u32)page[o + 2] << 16) | ((u32)page[o + 3] << 24);
			if (v != kPageTextureVTable) continue;
			const char *nm = (const char *)&page[o + 0xa0];
			int len = 0;
			while (len < 39 && nm[len] >= 32 && nm[len] <= 126) len++;
			if (len == 0 || nm[len] != 0) continue;
			u32 lo = (u32)page[o + 0x10] | ((u32)page[o + 0x11] << 8) | ((u32)page[o + 0x12] << 16) | ((u32)page[o + 0x13] << 24);
			u32 hi = (u32)page[o + 0x14] | ((u32)page[o + 0x15] << 8) | ((u32)page[o + 0x16] << 16) | ((u32)page[o + 0x17] << 24);
			rscPageTexIndexEntry e;
			memcpy(e.name, nm, (size_t)len + 1);
			e.ref.entry = (u32)entry;
			e.ref.offset = (u32)page[o + 0x4c] | ((u32)page[o + 0x4d] << 8) | ((u32)page[o + 0x4e] << 16) | ((u32)page[o + 0x4f] << 24);
			if (e.ref.offset >= size) e.ref.offset = 0;
			e.ref.psm = (u8)((lo >> 20) & 0x3f);
			e.ref.width = (u16)(1u << ((lo >> 26) & 0xf));
			e.ref.height = (u16)(1u << (((lo >> 30) & 3) | ((hi & 3) << 2)));
			ix->entries.Append(e);
		}
	}
	Displayf("rscLoadPageTextureByName: indexed '%s.ppf': %d named textures in %d texture-block pages", ix->file, ix->entries.GetCount(), pages);
	s_PageTexIndexes.Append(ix);
	return ix;
}

gfxTexture *rscLoadPageTextureByName(int slot, const char *name)
{
	if (!name || !name[0]) return NULL;
	if (slot < 0 || !datPageFile::IsMounted(slot)) {
		Displayf("rscLoadPageTextureByName: '%s' requested but page-file slot %d is not mounted", name, slot);
		return NULL;
	}
	rscPageTexIndex *ix = sBuildPageTexIndex(slot);
	for (int i = 0; i < ix->entries.GetCount(); i++) {
		if (_stricmp(ix->entries[i].name, name) != 0) continue;
		const rscPageTexRef &ref = ix->entries[i].ref;
		gfxTexture *tex = rscLoadPageTexture(slot, ref, name);
		Displayf("rscLoadPageTextureByName: '%s' in '%s.ppf' entry %u +%#x psm %#x %ux%u -> %s", name, ix->file, ref.entry, ref.offset,
		         ref.psm, ref.width, ref.height, tex ? "ok" : "not decoded");

		// -dumppagetex <dir>: write every page texture that decodes, named after
		// the texture.  Lets a suspect decode be compared against a known-good
		// one out of the same path (a licence plate is legible or it is not).
		if (tex) {
			const char *dumpDir = NULL;
			if (ARGS.Get("dumppagetex", 0, &dumpDir) && dumpDir && *dumpDir) {
				char path[512];
				snprintf(path, sizeof(path), "%s/%s_%ux%u.png", dumpDir, name, ref.width, ref.height);
				gfxSaveTexture(tex, path);
				Displayf("rscLoadPageTextureByName: wrote '%s'", path);
			}
		}
		return tex;
	}
	return NULL;
}

bool rscLoadPageTextureIndexed(int slot, const char *name, atArray<u8> &idx, u8 clut[256 * 4], int &width, int &height)
{
	width = height = 0;
	if (!name || !name[0] || slot < 0 || !datPageFile::IsMounted(slot)) return false;
	rscPageTexIndex *ix = sBuildPageTexIndex(slot);
	for (int i = 0; i < ix->entries.GetCount(); i++) {
		if (_stricmp(ix->entries[i].name, name) != 0) continue;
		const rscPageTexRef &ref = ix->entries[i].ref;
		if (ref.psm != 0x13 || ref.width < 8 || ref.height < 8) return false;
		const u32 kPixels = 0x90;               // 32-byte block header + 112 bytes of padding
		const u32 npix = (u32)ref.width * ref.height;
		atArray<u8> block;
		if (!sReadPageBlock(slot, ref.entry, ref.offset, block)) return false;
		if ((u32)block.GetCount() < kPixels + npix + 1024) return false;
		const u8 *pix = &block[0] + kPixels;    // one mip, so the CLUT follows the pixels
		idx.Resize((int)npix);
		if (sPageFileIsLinear(slot)) memcpy(&idx[0], pix, npix);
		else rscUnswizzlePsmt8(pix, &idx[0], ref.width, ref.height);
		memcpy(clut, pix + npix, 1024);
		width = ref.width;
		height = ref.height;
		return true;
	}
	Displayf("rscLoadPageTextureIndexed: '%s' is not in '%s.ppf'", name, ix->file);
	return false;
}

int rscGetNumPageTextures(int slot)
{
	if (slot < 0 || !datPageFile::IsMounted(slot)) return 0;
	return sBuildPageTexIndex(slot)->entries.GetCount();
}

const char *rscGetPageTextureName(int slot, int index)
{
	if (slot < 0 || !datPageFile::IsMounted(slot)) return NULL;
	rscPageTexIndex *ix = sBuildPageTexIndex(slot);
	if (index < 0 || index >= ix->entries.GetCount()) return NULL;
	return ix->entries[index].name;
}

bool rscProbePageTexture(int ppfSlot, int entry, rscPageTexRef &ref)
{
	if (!datPageFile::IsMounted(ppfSlot)) return false;
	if (entry < 0 || entry >= datPageFile::GetNumEntries(ppfSlot)) return false;
	u64 file = (u64)datPageFile::GetEntrySector(ppfSlot, entry) * 2048ull;
	u8 hdr[32];
	if (!datPageFile::ReadPageByOffset(ppfSlot, file, hdr, 32)) return false;
	u32 owner = (u32)hdr[0] | ((u32)hdr[1] << 8) | ((u32)hdr[2] << 16) | ((u32)hdr[3] << 24);
	u32 size = (u32)hdr[12] | ((u32)hdr[13] << 8) | ((u32)hdr[14] << 16) | ((u32)hdr[15] << 24);
	if (owner) return false;
	ref.entry = (u32)entry;
	ref.offset = 0;
	ref.psm = 0x13;
	if (size == 0x1500) { ref.width = ref.height = 64; return true; }
	if (size == 0x4500) { ref.width = ref.height = 128; return true; }
	return false;
}

bool rscLoadPageImage(int ppfSlot, int entry, datResourceImage &image, const char *name)
{
	if (!datPageFile::IsMounted(ppfSlot)) { Warningf("rscLoadPageImage: slot %d is not mounted", ppfSlot); return false; }
	if (entry < 0 || entry >= datPageFile::GetNumEntries(ppfSlot)) { Warningf("rscLoadPageImage: entry %d out of range (slot %d has %d)", entry, ppfSlot, datPageFile::GetNumEntries(ppfSlot)); return false; }
	u64 file = (u64)datPageFile::GetEntrySector(ppfSlot, entry) * 2048ull;
	u8 hdr[32];
	if (!datPageFile::ReadPageByOffset(ppfSlot, file, hdr, 32)) { Warningf("rscLoadPageImage: cannot read the header of entry %d", entry); return false; }
	u32 owner = (u32)hdr[0] | ((u32)hdr[1] << 8) | ((u32)hdr[2] << 16) | ((u32)hdr[3] << 24);
	u32 size = (u32)hdr[12] | ((u32)hdr[13] << 8) | ((u32)hdr[14] << 16) | ((u32)hdr[15] << 24);
	// owner 0 = a raw texture block page, or fixed-base streamed model (e.g. tire.ppf)
	if (!owner) {
		const char *slotName = datPageFile::GetName(ppfSlot);
		if (slotName && strstr(slotName, "tire")) {
			owner = 0x06826a40 + (u32)entry * 0x10;
			u32 fullSize = 0;
			u8 tempBuf[32768];
			if (datPageFile::ReadEntry(ppfSlot, entry, tempBuf, sizeof(tempBuf), &fullSize) && fullSize > 0) {
				return image.LoadFromMemory(name, owner, tempBuf, fullSize);
			}
		}
		return false;
	}
	if (size <= 32 || size > 0x100000) { Warningf("rscLoadPageImage: entry %d has an implausible size %u", entry, size); return false; }
	atArray<u8> payload;
	payload.Resize((int)(size - 32));
	if (!datPageFile::ReadPageByOffset(ppfSlot, file + 32, &payload[0], size - 32)) { Warningf("rscLoadPageImage: cannot read %u bytes of entry %d", size - 32, entry); return false; }
	return image.LoadFromMemory(name, owner, &payload[0], size - 32);
}

static bool sEndsWith(const char *s, const char *suffix)
{
	size_t n = strlen(s), m = strlen(suffix);
	return n >= m && _stricmp(s + n - m, suffix) == 0;
}

int rscDecodeCarShaders(const datResourceImage &image, u32 drawableAddr, atArray<rscShaderInfo> &out)
{
	out.Reset();
	if (!image.IsValidAddress(drawableAddr, 0x0c)) return 0;
	u32 group = image.ReadU32(drawableAddr + 8);
	if (!image.IsValidAddress(group, 0x0c)) return 0;
	u32 table = image.ReadU32(group + 4);
	int count = (int)image.ReadU16(group + 8);
	if (!table || count <= 0 || count > 1024 || !image.IsValidAddress(table, (u32)count * 4)) return 0;
	const u32 kTextureVTable = 0x007a1260;
	const u32 kPageTextureVTable = 0x007a27a8;
	// A ped shader points straight at its colour map, two words in and with no
	// template name anywhere in the object, so the "a name that ends .shadert
	// first, textures only from word 10 on" shape the car shaders have does not
	// find it.  Match the class instead: this vtable with its name at +0x78.
	const u32 kPedTextureVTable = 0x00793638;
	for (int i = 0; i < count; i++) {
		rscShaderInfo info;
		info.templateName[0] = 0;
		info.numTextures = 0;
		info.numPageTextures = 0;
		u32 sh = image.ReadU32(table + (u32)i * 4);
		if (image.IsValidAddress(sh, 0x40)) {
			info.drawBucket = (int)image.ReadU32(sh + 4);
			for (int k = 1; k < 48 && image.IsValidAddress(sh + (u32)k * 4, 4); k++) {
				u32 v = image.ReadU32(sh + (u32)k * 4);
				const char *texName = 0;
				if (image.IsValidAddress(v, 0x7c) && image.ReadU32(v) == kPedTextureVTable) {
					texName = image.ReadString(image.ReadU32(v + 0x78));
				} else if (v == kTextureVTable && k >= 10) {
					texName = image.ReadString(image.ReadU32(sh + (u32)k * 4 + 8));
				} else if (image.IsValidAddress(v, 0x54) && image.ReadU32(v) == kPageTextureVTable) {
					u32 lo = image.ReadU32(v + 0x10), hi = image.ReadU32(v + 0x14);
					u32 ref = image.ReadU32(v + 0x50);
					bool dup = false;
					for (int t = 0; t < info.numPageTextures; t++)
						if (info.pageTextures[t].entry == (ref & 0xffff) && info.pageTextures[t].offset == image.ReadU32(v + 0x4c)) dup = true;
					if (!dup && info.numPageTextures < 4 && (ref & 0xffff) != 0xffff) {
						rscPageTexRef &pt = info.pageTextures[info.numPageTextures++];
						pt.entry = ref & 0xffff;
						pt.offset = image.ReadU32(v + 0x4c);
						pt.psm = (u8)((lo >> 20) & 0x3f);
						pt.width = (u16)(1u << ((lo >> 26) & 0xf));
						pt.height = (u16)(1u << (((lo >> 30) & 3) | ((hi & 3) << 2)));
					}
				} else if (image.IsValidAddress(v, 12)) {
					if (!info.templateName[0]) {
						// the template name: a printable string at exactly this address (a pointer
						// landing a few bytes before a name in the pool is something else)
						const char *str = image.ReadString(v);
						bool printable = str[0] != 0;
						for (const char *c = str; *c; c++) if (*c < 32 || *c > 126) { printable = false; break; }
						if (printable && sEndsWith(str, ".shadert")) {
							strncpy(info.templateName, str, sizeof(info.templateName) - 1);
							info.templateName[sizeof(info.templateName) - 1] = 0;
							continue;
						}
					}
					if (k >= 10 && image.ReadU32(v) == kTextureVTable)
						texName = image.ReadString(image.ReadU32(v + 8));
				}
				if (texName && texName[0] && info.numTextures < 6) {
					bool dup = false;
					for (int t = 0; t < info.numTextures; t++) if (!_stricmp(info.textures[t], texName)) dup = true;
					if (!dup) {
						strncpy(info.textures[info.numTextures], texName, sizeof(info.textures[0]) - 1);
						info.textures[info.numTextures][sizeof(info.textures[0]) - 1] = 0;
						info.numTextures++;
					}
				}
			}
		}
		if (!info.templateName[0] && image.IsValidAddress(sh, 4)) {
			// Shader classes the game creates in code (no .shadert template).  Which
			// class a vtable is was read off the parts that use it across the fleet:
			// hoods (241 geometries), tail-light lenses, decals, plain bumper pieces.
			u32 vt = image.ReadU32(sh);
			const char *pseudo = 0;
			switch (vt) {
			case 0x007aab90: case 0x007abe98: case 0x007aad90: pseudo = "carpaint_hood.shadert"; break;
			case 0x007aab30: case 0x007abe38: pseudo = "emissive_taillight.shadert"; break;
			case 0x007aa588: case 0x007ab890: case 0x007aa788: pseudo = "car_decal.shadert"; break;
			case 0x007a1078: case 0x007a2380: pseudo = "lit_textured.shadert"; break;
			}
			if (pseudo) { strncpy(info.templateName, pseudo, sizeof(info.templateName) - 1); info.templateName[sizeof(info.templateName) - 1] = 0; }
		}
		out.Append(info);
	}
	return out.GetCount();
}

// The shared effect maps a shader names besides its colour map.
static bool sIsEffectMap(const char *n)
{
	return !n[0] || !_stricmp(n, "none") || !_stricmp(n, "__envmap__") || !_stricmp(n, "__specular__") ||
		!_stricmp(n, "__metalflake__") || !_stricmp(n, "__decal__") || !_stricmp(n, "__logo__") ||
		!_stricmp(n, "__licenseplate__");
}


// Appends one gfxModelMaterial per shader (material index == shader index).
static inline u32 sRgba(u8 r, u8 g, u8 b, u8 a) { return ((u32)a << 24) | ((u32)r << 16) | ((u32)g << 8) | b; }

static void sCarShading(gfxModelMaterial &m, float spec, float gloss, float reflect, bool metallic = false, bool emissive = false, bool glass = false, bool chrome = false)
{
	m.car_spec = spec; m.car_gloss = gloss; m.car_reflect = reflect;
	m.car_metallic = metallic; m.car_emissive = emissive; m.car_glass = glass; m.car_chrome = chrome;
}

void rscClassifyCarMaterial(gfxModelMaterial &mat, const char *t, const char *tname, u32 paint)
{
	if (!t) t = "";
	if (!tname) tname = "";
	mat.car_shade = true;
	mat.car_paint = false;
	mat.car_blend = false;
	mat.car_cutout = false;
	mat.car_light = CAR_LIGHT_NONE;
	mat.car_color = sRgba(90, 90, 95, 255);
	sCarShading(mat, 0.25f, 24.0f, 0.05f);
	if (strstr(t, "carpaint")) {
		mat.car_color = paint;
		mat.car_paint = true;
		sCarShading(mat, 0.9f, 64.0f, 0.35f);
	} else if (strstr(t, "car_window")) {
		// Opacity 0.2 face-on up to 0.9 at grazing angles, drwShaderCarWindows'
		// WinFresnelMin / WinFresnelMax: the cabin shows through, as on the console.
		// A flat 0.59 floor hid it outright - the interior is barely darker than the tint.
		mat.car_color = sRgba(28, 36, 46, 51);
		mat.car_glass_fres = 0.7f;
		mat.car_blend = true;
		sCarShading(mat, 1.2f, 96.0f, 0.6f, false, false, true);
	} else if (strstr(t, "colored_glass") || strstr(t, "glass")) {
		// light lens covers: taillight lenses ruby, everything else crystal
		if (strstr(tname, "tl") || strstr(tname, "tail") || strstr(tname, "brake") ||
		    strstr(t, "taillight") || strstr(t, "brakelight"))
			mat.car_color = sRgba(200, 20, 20, 110);
		else
			mat.car_color = sRgba(235, 242, 255, 60);
		mat.car_blend = true;
		sCarShading(mat, 1.2f, 96.0f, 0.5f, false, false, true);
	} else if (strstr(t, "caliper")) {
		mat.car_color = sRgba(200, 30, 25, 255);
		sCarShading(mat, 0.6f, 32.0f, 0.1f);
	} else if (strstr(t, "rotor") || strstr(t, "disc")) {
		mat.car_color = sRgba(110, 112, 118, 255);
		sCarShading(mat, 0.5f, 24.0f, 0.15f, true);
	} else if (strstr(t, "chrome")) {
		mat.car_color = sRgba(240, 242, 245, 255);
		sCarShading(mat, 1.4f, 110.0f, 0.95f, true, false, false, true);
	} else if (strstr(t, "default_shiny")) {
		mat.car_color = sRgba(60, 62, 66, 255);
		sCarShading(mat, 0.6f, 40.0f, 0.15f);
	} else if (strstr(t, "black_matte") || strstr(t, "rubber") || strstr(t, "tire") || strstr(t, "tyre")) {
		mat.car_color = sRgba(30, 30, 33, 255);
		sCarShading(mat, 0.08f, 8.0f, 0.02f);
	} else if (strstr(t, "carbon_fiber")) {
		mat.car_color = sRgba(215, 215, 220, 255);     // modulates the weave texture
		sCarShading(mat, 0.5f, 32.0f, 0.12f);
	} else if (strstr(t, "headlight") || strstr(t, "fog_light") || strstr(t, "foglight")) {
		// Light lenses are NOT permanently emissive.  The original shaders
		// (mcgfx/mcShader.c) leave the geometry alone and lerp the ambient
		// toward white by the car's live value for that light, so an unlit lens
		// is an ordinary lit textured surface.  Marking them emissive here left
		// every headlight, taillight and reverse lens glowing flat-out around
		// the clock - the pale slabs over the tail clusters.
		mat.car_color = sRgba(240, 245, 255, 255);
		mat.car_light = CAR_LIGHT_HEAD;
		// The headlight element blends over the chrome reflector behind it, at
		// an opacity that follows the light (see sCarShade), so it needs the
		// blend bucket - the reflector has to be on screen before it lands.
		mat.car_blend = true;
		sCarShading(mat, 0.8f, 48.0f, 0.2f);
	} else if (strstr(t, "thirdbrake") || strstr(t, "third_brake")) {
		mat.car_color = sRgba(215, 25, 25, 255);
		mat.car_light = CAR_LIGHT_THIRDBRAKE;
		sCarShading(mat, 0.8f, 48.0f, 0.2f);
	} else if (strstr(t, "brakelight")) {
		mat.car_color = sRgba(215, 25, 25, 255);
		mat.car_light = CAR_LIGHT_BRAKE;
		sCarShading(mat, 0.8f, 48.0f, 0.2f);
	} else if (strstr(t, "taillight")) {
		mat.car_color = sRgba(215, 25, 25, 255);
		mat.car_light = CAR_LIGHT_TAIL;
		sCarShading(mat, 0.8f, 48.0f, 0.2f);
	} else if (strstr(t, "reverselight")) {
		mat.car_color = sRgba(240, 236, 220, 255);
		mat.car_light = CAR_LIGHT_REVERSE;
		sCarShading(mat, 0.8f, 48.0f, 0.2f);
	} else if (strstr(t, "licenseplate")) {
		mat.car_color = sRgba(255, 255, 255, 255);
		sCarShading(mat, 0.4f, 32.0f, 0.1f);
	} else if (strstr(t, "sprocket")) {
		mat.car_color = sRgba(255, 255, 255, 255);     // alpha-cutout image from the trim atlas
		mat.car_blend = true;
		mat.car_cutout = true;
	} else if (strstr(t, "trim") || strstr(tname, "trim")) {
		mat.car_color = sRgba(38, 38, 42, 255);
		sCarShading(mat, 0.35f, 24.0f, 0.08f);
	} else if (strstr(t, "decal") || strstr(tname, "decal")) {
		mat.car_color = sRgba(255, 255, 255, 255);
		mat.car_blend = true;
		mat.car_cutout = true;
	} else if (strstr(t, "drop_shadow")) {
		// the shadow image under the car, placed by its bone on the ground
		mat.car_color = sRgba(0, 0, 0, 170);
		mat.car_blend = true;
		mat.car_cutout = true;
		sCarShading(mat, 0.0f, 1.0f, 0.0f, false, true);
	} else if (strstr(t, "lit_textured")) {
		mat.car_color = sRgba(255, 255, 255, 255);    // white modulate; the paint when no texture binds
		sCarShading(mat, 0.35f, 24.0f, 0.08f);
	}
	if (strstr(t, "player_") && !mat.car_light && !mat.car_blend) {
		// lens covers on the tail cluster: translucent so the reflector shows
		// through.  Not for the player_ LIGHT shaders: drwShaderTaillight and
		// friends treat "player_" and "emissive_" identically, and drawing them
		// as translucent covers put a permanent milky sheet over the cluster.
		mat.car_color = (mat.car_color & 0x00ffffffu) | (170u << 24);
		mat.car_blend = true;
		sCarShading(mat, 1.0f, 64.0f, 0.3f, false, false, true);
	}
	mat.unlit = true;
	mat.drawbucket = mat.car_blend ? 1 : 0;
	mat.diffuse[0] = mat.diffuse[1] = mat.diffuse[2] = 1.0f;
}

void rscSetCarPaint(rmcDrawable *drawable, u32 paint)
{
	if (!drawable) return;
	for (int lod = 0; lod < 4; lod++) {
		rmcModel *m = drawable->GetLodGroup().GetModel(lod);
		gfxModel *g = m ? m->GetModel(0) : 0;
		if (g) g->SetCarPaint(paint);
	}
}

void rscSetCarPaintRamp(rmcDrawable *drawable, const gfxCarPaintRamp &ramp)
{
	if (!drawable) return;
	for (int lod = 0; lod < 4; lod++) {
		rmcModel *m = drawable->GetLodGroup().GetModel(lod);
		gfxModel *g = m ? m->GetModel(0) : 0;
		if (g) g->SetCarPaintRamp(ramp);
	}
}

void rscSetCarLights(rmcDrawable *drawable, const gfxCarLights &lights)
{
	if (!drawable) return;
	for (int lod = 0; lod < 4; lod++) {
		rmcModel *m = drawable->GetLodGroup().GetModel(lod);
		gfxModel *g = m ? m->GetModel(0) : 0;
		if (g) g->SetCarLights(lights);
	}
}

static const u32 kDefaultCarPaint = 0xff1e5fd7u;   // midnight blue until the custom paint is applied

static void sAppendShaderMaterials(const atArray<rscShaderInfo> &shaders, gfxModel &outModel)
{
	for (int i = 0; i < shaders.GetCount(); i++) {
		const rscShaderInfo &sh = shaders[i];
		gfxModelMaterial mat;
		const char *tmpl = sh.templateName;
		mat.name = tmpl[0] ? tmpl : "car_material";
		bool paintTemplate = strstr(tmpl, "carpaint") != 0;
		bool glassTemplate = strstr(tmpl, "car_window") != 0 || strstr(tmpl, "glass") != 0;
		// The colour map: the first real texture in the shader's list.  Paint is a
		// solid colour plus effect maps (a taillight / decal map listed by a
		// carpaint shader is not its colour); glass damage overlays are for the
		// damage system, not the pristine car.
		const char *colour = 0;
		for (int t = 0; t < sh.numTextures && !colour && !paintTemplate; t++) {
			const char *n = sh.textures[t];
			if (sIsEffectMap(n)) continue;
			if (glassTemplate && strstr(n, "_dmg")) continue;
			colour = n;
		}
		rscClassifyCarMaterial(mat, tmpl, colour ? colour : "", kDefaultCarPaint);
		if (colour) mat.texture_name = colour;
		if (strstr(tmpl, "licenseplate")) {
			// The live plate, not a fixed background: the game composites one per car
			// (mcCarCustom::CopyInLicenseTex) and points this name at the car that is
			// about to draw.  Materials resolve texture_name on every draw, so each
			// car gets its own.  rscview registers the same name for its own plate.
			mat.texture_name = "__licenseplate__";
		} else if (mat.car_chrome && !colour) {
			mat.texture_name = "fx_car_viewer_envmap";     // reflected through the texgen
		} else if (mat.car_chrome && colour) {
			// chrome under an overlay image (wheel faces): metallic shading, normal UVs
			mat.car_chrome = false;
			mat.car_reflect = 0.9f;
		} else if (!colour && strstr(tmpl, "lit_textured")) {
			// a per-car trim map that did not decode: painted bodywork
			mat.car_color = kDefaultCarPaint;
			mat.car_paint = true;
			sCarShading(mat, 0.7f, 48.0f, 0.25f);
		}
		mat.packet_count = 0;
		mat.primitive_count = 0;
		if (ARGS.Get("texlog"))
		{
			static const char *kLightNames[] = { "", " light:head", " light:tail", " light:brake", " light:thirdbrake", " light:reverse" };
			Displayf("  material %2d: %-32s tex '%s' colour %08x%s%s%s%s%s%s%s spec %.2f refl %.2f bucket %d", i, tmpl, mat.texture_name.c_str(), mat.car_color,
			         mat.car_paint ? " paint" : "", mat.car_blend ? " blend" : "", mat.car_cutout ? " cutout" : "", mat.car_chrome ? " chrome" : "",
			         mat.car_metallic ? " metallic" : "", mat.car_emissive ? " emissive" : "",
			         mat.car_light < (u8)(sizeof(kLightNames)/sizeof(kLightNames[0])) ? kLightNames[mat.car_light] : "",
			         mat.car_spec, mat.car_reflect, mat.drawbucket);
		}
		outModel.materials.Append(mat);
	}
}

static const u8 *s_GetCityPage(int p_idx, size_t &outLen)
{
	static std::map<int, std::vector<u8> > s_pageCache;
	auto it = s_pageCache.find(p_idx);
	if (it != s_pageCache.end()) {
		outLen = it->second.size();
		return it->second.data();
	}
	std::vector<u8> pageBuf(131072);
	u32 pageSize = 0;
	if (!datPageFile::ReadEntry(CITY_PAGE_FILE_SLOT, p_idx, pageBuf.data(), (u32)pageBuf.size(), &pageSize)) {
		outLen = 0;
		return nullptr;
	}
	if (pageSize > 0 && pageSize < pageBuf.size()) {
		pageBuf.resize(pageSize);
	}
	auto res = s_pageCache.insert(std::make_pair(p_idx, std::move(pageBuf)));
	outLen = res.first->second.size();
	return res.first->second.data();
}

bool rscDecodeCityImage(const datResourceImage &image, u32 imgAddr, char *outTexName, size_t maxLen)
{
	if (!imgAddr || !image.IsValidAddress(imgAddr, 0x68)) return false;
	u32 vt = image.ReadU32(imgAddr);
	if (vt != 0x007a2320) return false;

	char nameBuf[64];
	snprintf(nameBuf, sizeof(nameBuf), "city_img_%08x", imgAddr);
	if (outTexName && maxLen > 0) {
		strncpy(outTexName, nameBuf, maxLen - 1);
		outTexName[maxLen - 1] = '\0';
	}

	if (gfxTextureResident(nameBuf)) {
		return true;
	}

	if (!datPageFile::IsMounted(CITY_PAGE_FILE_SLOT)) {
		const char *imgName = image.GetName();
		if (imgName && imgName[0]) {
			datPageFile::Mount(imgName, CITY_PAGE_FILE_SLOT);
			if (!datPageFile::IsMounted(CITY_PAGE_FILE_SLOT) || datPageFile::GetNumEntries(CITY_PAGE_FILE_SLOT) == 0) {
				char fallbackPpf[256];
				strncpy(fallbackPpf, imgName, sizeof(fallbackPpf));
				fallbackPpf[sizeof(fallbackPpf) - 1] = 0;
				char *d = strstr(fallbackPpf, "_dawn_");
				if (!d) d = strstr(fallbackPpf, "_dusk_");
				if (!d) d = strstr(fallbackPpf, "_day_");
				if (d) {
					char rest[64];
					strncpy(rest, d + 5, sizeof(rest));
					strcpy(d, "_midnight");
					strcat(fallbackPpf, rest);
					datPageFile::Mount(fallbackPpf, CITY_PAGE_FILE_SLOT);
				}
			}
		}
	}
	if (!datPageFile::IsMounted(CITY_PAGE_FILE_SLOT)) {
		return false;
	}

	u16 rawW = image.ReadU16(imgAddr + 12);
	u16 rawH = image.ReadU16(imgAddr + 14);
	int w = (int)rawW;
	int h = (rawH > 0) ? (int)rawH : w;
	if (w < 1 || w > 2048 || h < 1 || h > 2048) return false;

	u16 p_px = image.ReadU16(imgAddr + 6);
	u32 offA = image.ReadU32(imgAddr + 0x58);
	if (offA != 0xcdcdcdcd) {
		u16 pA = image.ReadU16(imgAddr + 0x5c);
		if (pA != 0xcdcd && pA > 0) p_px = pA;
	} else {
		offA = image.ReadU32(imgAddr + 0x4c);
		if (offA != 0xcdcdcdcd) {
			u16 pA = image.ReadU16(imgAddr + 0x50);
			if (pA != 0xcdcd && pA > 0) p_px = pA;
		}
	}
	if (offA == 0xcdcdcdcd) offA = 0;

	u16 p_pal = p_px;
	u32 offB = image.ReadU32(imgAddr + 0x64);
	if (offB != 0xcdcdcdcd) {
		u16 pB = image.ReadU16(imgAddr + 0x68);
		if (pB != 0xcdcd && pB > 0) p_pal = pB;
	} else {
		offB = offA;
	}

	size_t pxPageLen = 0;
	const u8 *pxPageData = s_GetCityPage(p_px, pxPageLen);
	if (!pxPageData || pxPageLen == 0) return false;

	size_t palPageLen = 0;
	const u8 *palPageData = (p_pal == p_px) ? pxPageData : s_GetCityPage(p_pal, palPageLen);
	if (p_pal == p_px) palPageLen = pxPageLen;
	if (!palPageData || palPageLen == 0) return false;

	u32 pxStart = offA + 0x90;
	u32 palStart = offB + 0x490;
	size_t numPx = (size_t)w * (size_t)h;

	if (pxStart + numPx > pxPageLen || palStart + 1024 > palPageLen) {
		u32 altOff = image.ReadU32(imgAddr + 0x4c);
		if (altOff != 0xcdcdcdcd && altOff != offA) {
			u16 altP = image.ReadU16(imgAddr + 0x50);
			if (altP != 0xcdcd && altP > 0) {
				p_px = altP;
				pxPageData = s_GetCityPage(p_px, pxPageLen);
				if (!pxPageData) return false;
			}
			offA = altOff;
			offB = altOff;
			pxStart = offA + 0x90;
			palStart = offB + 0x490;
			if (p_pal == p_px) {
				palPageData = pxPageData;
				palPageLen = pxPageLen;
			}
		}
	}

	if (pxStart + numPx > pxPageLen || palStart + 1024 > palPageLen) {
		u16 mip1W = image.ReadU16(imgAddr + 0x1c);
		u16 mip1H = image.ReadU16(imgAddr + 0x1e);
		if (mip1W >= 1 && mip1H >= 1 && pxStart + (size_t)mip1W * (size_t)mip1H <= pxPageLen && palStart + 1024 <= palPageLen) {
			w = mip1W;
			h = mip1H;
			numPx = (size_t)w * (size_t)h;
		} else {
			u16 mip2W = image.ReadU16(imgAddr + 0x2c);
			u16 mip2H = image.ReadU16(imgAddr + 0x2e);
			if (mip2W >= 1 && mip2H >= 1 && pxStart + (size_t)mip2W * (size_t)mip2H <= pxPageLen && palStart + 1024 <= palPageLen) {
				w = mip2W;
				h = mip2H;
				numPx = (size_t)w * (size_t)h;
			} else if (pxStart < pxPageLen && palStart + 1024 <= palPageLen && (pxPageLen - pxStart) / w >= 1) {
				h = (int)((pxPageLen - pxStart) / w);
				numPx = (size_t)w * (size_t)h;
			} else {
				return false;
			}
		}
	}

	struct PalColor { u8 r, g, b, a; };
	PalColor pal[256];
	for (int i = 0; i < 256; i++) {
		pal[i].r = palPageData[palStart + (size_t)i * 4 + 0];
		pal[i].g = palPageData[palStart + (size_t)i * 4 + 1];
		pal[i].b = palPageData[palStart + (size_t)i * 4 + 2];
		u8 a = palPageData[palStart + (size_t)i * 4 + 3];
		if (a == 0x80 || a == 0x00) {
			pal[i].a = 255;
		} else if (a <= 128) {
			pal[i].a = (a * 2 > 255) ? 255 : (u8)(a * 2);
		} else {
			pal[i].a = 255;
		}
	}

	std::vector<u8> rgba(numPx * 4);
	for (size_t i = 0; i < numPx; i++) {
		u8 idx = pxPageData[pxStart + i];
		if ((idx & 0x18) == 0x08 || (idx & 0x18) == 0x10) {
			idx ^= 0x18;
		}
		const PalColor &c = pal[idx];
		rgba[i * 4 + 0] = c.r;
		rgba[i * 4 + 1] = c.g;
		rgba[i * 4 + 2] = c.b;
		rgba[i * 4 + 3] = c.a;
	}

	gfxTexture *tex = gfxRegisterRgbaTexture(nameBuf, w, h, rgba.data());
	return (tex != nullptr);
}

// PC PORT: which light cluster a part belongs to, read off its node name.  This
// is the rule rscview uses, and the game needs it for the same reason: a car
// pack's shader table is SHARED by the head and tail clusters, so classifying a
// shader on its own (rscClassifyCarMaterial) cannot tell a ruby tail lens from a
// crystal headlight cover.  On the lancer both clusters draw through shader 21,
// "colored_glass.shadert" with no texture at all.
static void rscTagCarLightPart(rscModel &m, const char *nodeName)
{
	if (!nodeName || !nodeName[0]) return;
	char lower[160];
	size_t n = strlen(nodeName);
	if (n >= sizeof(lower)) n = sizeof(lower) - 1;
	for (size_t i = 0; i < n; i++) {
		char c = nodeName[i];
		lower[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
	}
	lower[n] = 0;
	m.isTaillight = strstr(lower, "tl_") != 0 || strstr(lower, "tail") != 0;
	m.isHeadlight = strstr(lower, "hl_") != 0 || strstr(lower, "headlight") != 0 || strstr(lower, "headlamp") != 0;
}

// The lens material a light cluster needs, cloned off the shader's generic one
// the first time that cluster asks for it.  Without this every lens on the car
// came out the one pale crystal grey the untextured colored_glass classifies to,
// tail lamps included.  The three rules are rscview's (ResolveMaterial): a ruby
// tail lens, a crystal headlight cover, and an amber marker for a turn signal
// that sits inside the headlight cluster but draws through the tail shader.
static u32 sCarLightPartMaterial(gfxModel &outModel, u32 baseIdx, bool isTaillight, bool isHeadlight)
{
	if (!isTaillight && !isHeadlight) return baseIdx;
	if (baseIdx >= (u32)outModel.materials.GetCount()) return baseIdx;
	const gfxModelMaterial &base = outModel.materials[baseIdx];
	if (!base.car_shade) return baseIdx;

	// Match on the shader template, exactly as rscview does - the material's
	// name IS the template name (sAppendShaderMaterials).
	const char *tmpl = base.name.c_str();
	const bool tmplGlass = strstr(tmpl, "colored_glass") != 0 || strstr(tmpl, "glass") != 0;
	const bool tmplMarker = strstr(tmpl, "player_taillight") != 0 || strstr(tmpl, "player_brakelight") != 0;

	u32 color; float spec, gloss, reflect; const char *tag;
	if (isTaillight && tmplGlass) {
		color = sRgba(215, 20, 20, 150); spec = 1.2f; gloss = 96.0f; reflect = 0.5f; tag = "@tl";
	} else if (isHeadlight && tmplGlass) {
		color = sRgba(235, 242, 255, 60); spec = 1.2f; gloss = 96.0f; reflect = 0.5f; tag = "@hl";
	} else if (isHeadlight && tmplMarker) {
		color = sRgba(250, 160, 40, 170); spec = 1.0f; gloss = 64.0f; reflect = 0.3f; tag = "@hlmark";
	} else {
		return baseIdx;
	}

	std::string name = base.name + tag;
	for (int i = 0; i < outModel.materials.GetCount(); i++)
		if (outModel.materials[i].name == name) return (u32)i;

	gfxModelMaterial mat = base;      // keeps the texture, paint and light bindings
	mat.name = name;
	mat.car_color = color;
	mat.car_blend = true;
	mat.car_glass = true;
	mat.car_spec = spec;
	mat.car_gloss = gloss;
	mat.car_reflect = reflect;
	mat.drawbucket = 1;               // with the rest of the translucents
	mat.packet_count = 0;
	mat.primitive_count = 0;
	outModel.materials.Append(mat);
	if (ARGS.Get("texlog"))
		Displayf("  light lens: %-34s -> material %2d '%s' colour %08x", base.name.c_str(),
		         outModel.materials.GetCount() - 1, name.c_str(), color);
	return (u32)(outModel.materials.GetCount() - 1);
}

bool gfxModelFromRscModel(const rscModel &partModel, int boneIdx, gfxModel &outModel, const int *shaderToMaterial, int numShaders)
{
	bool added = false;
	for (int g = 0; g < partModel.geometries.GetCount(); g++) {
		const rscGeometry &geom = partModel.geometries[g];
		for (int b = 0; b < geom.batches.GetCount(); b++) {
			const rscGeomBatch &batch = geom.batches[b];
			int nVerts = batch.verts.GetCount();
			if (nVerts < 3) continue;

			atArray<rscTriangle> tris;
			batch.BuildTriangles(tris, 0);
			if (tris.GetCount() == 0) continue;

			gfxModelPacket pk;
			if (shaderToMaterial && geom.shaderIdx >= 0 && geom.shaderIdx < numShaders && shaderToMaterial[geom.shaderIdx] >= 0 && shaderToMaterial[geom.shaderIdx] < outModel.materials.GetCount()) {
				pk.material_index = sCarLightPartMaterial(outModel, (u32)shaderToMaterial[geom.shaderIdx],
				                                          partModel.isTaillight, partModel.isHeadlight);
			} else {
				char cityTexName[64] = {0};
				if (geom.shaderIdx >= 0) {
					char probe[64];
					snprintf(probe, sizeof(probe), "city_sh_%d", geom.shaderIdx);
					if (gfxTextureResident(probe)) {
						strncpy(cityTexName, probe, sizeof(cityTexName) - 1);
					} else {
						snprintf(probe, sizeof(probe), "city_sh_0_%d", geom.shaderIdx);
						if (gfxTextureResident(probe)) {
							strncpy(cityTexName, probe, sizeof(cityTexName) - 1);
						} else {
							for (int g = 0; g < 15; g++) {
								snprintf(probe, sizeof(probe), "city_sh_%d_%d", g, geom.shaderIdx);
								if (gfxTextureResident(probe)) {
									strncpy(cityTexName, probe, sizeof(cityTexName) - 1);
									break;
								}
							}
						}
					}
				}

				if (cityTexName[0] != '\0' && gfxTextureResident(cityTexName)) {
					int matIdx = -1;
					for (int m = 0; m < outModel.materials.GetCount(); m++) {
						if (outModel.materials[m].texture_name == cityTexName) {
							matIdx = m;
							break;
						}
					}
					if (matIdx == -1) {
						gfxModelMaterial mat;
						mat.name = cityTexName;
						mat.texture_name = cityTexName;
						mat.diffuse[0] = 1.0f;
						mat.diffuse[1] = 1.0f;
						mat.diffuse[2] = 1.0f;
						mat.packet_count = 0;
						mat.primitive_count = 0;
						matIdx = outModel.materials.GetCount();
						outModel.materials.Append(mat);
					}
					pk.material_index = (u32)matIdx;
				} else {
					char phName[64];
					snprintf(phName, sizeof(phName), "placeholder_%d", geom.shaderIdx);
					int matIdx = -1;
					for (int m = 0; m < outModel.materials.GetCount(); m++) {
						if (outModel.materials[m].name == phName) {
							matIdx = m;
							break;
						}
					}
					if (matIdx == -1) {
						gfxModelMaterial mat;
						mat.name = phName;
						mat.texture_name = "";
						// Neutral asphalt/concrete tone for untextured city surfaces
						mat.diffuse[0] = 0.3f;
						mat.diffuse[1] = 0.3f;
						mat.diffuse[2] = 0.32f;
						mat.packet_count = 0;
						mat.primitive_count = 0;
						matIdx = outModel.materials.GetCount();
						outModel.materials.Append(mat);
					}
					pk.material_index = (u32)matIdx;
				}
			}
			// A rigid part rides one bone as a whole; a skinned batch names a
			// bone per vertex, and the packet's bone map is the set it uses.
			bool hasBones = (batch.bones.GetCount() == nVerts);
			if (!hasBones)
				pk.bone_map.Append((u32)boneIdx);

			u32 vertBase = outModel.vertices.GetCount();
			bool hasNormals = (batch.normals.GetCount() == nVerts);
			bool hasUvs = (batch.uvs.GetCount() == nVerts);
			bool hasUvs2 = (batch.uvs2.GetCount() == nVerts);   // Tex1 (city_window's window layer)
			bool hasColors = (batch.colors.GetCount() == nVerts);

			for (int v = 0; v < nVerts; v++) {
				outModel.vertices.Append(batch.verts[v]);
				if (hasNormals) outModel.normals.Append(batch.normals[v]);
				else outModel.normals.Append(Vector3(0.0f, 1.0f, 0.0f));

				if (hasUvs) outModel.tex_coords.Append(batch.uvs[v]);
				else outModel.tex_coords.Append(Vector2(0.0f, 0.0f));
				int tex2Idx = -1;
				if (hasUvs2) {
					tex2Idx = outModel.tex_coords2.GetCount();
					outModel.tex_coords2.Append(batch.uvs2[v]);
				}

				// No CPV source for this batch.  The neutral vertex colour on the
				// GS is 0x80, not white: a textured draw multiplies the vertex
				// colour by 255/128, so 0x80 is 1.0 and 0xff is nearly 2.0.
				// Filling white here made every geometry the decoder found no
				// colour stream for render ~2.8x brighter than its CPV-lit
				// neighbours.  rscview already uses this neutral for the same
				// case (rscview.cpp, "colors ? b.colors[i] : 0x80808080u").
				if (hasColors) {
					gfxPackedColor c = (gfxPackedColor)batch.colors[v];
					if ((c & 0xff000000u) == 0) c |= 0xff000000u;
					outModel.colors.Append(c);
				} else {
					outModel.colors.Append(0xff808080u);
				}

				// -cpvlog: what the decoder actually bakes.  If the mean here is
				// near 128 the palette is being applied; near 255 it is not, and
				// the modulate rule then renders the city about twice as bright
				// as it should be.
				{
					static int s_cpvLog = -1;
					if (s_cpvLog < 0) s_cpvLog = ARGS.Get("cpvlog") ? 1 : 0;
					if (s_cpvLog) {
						static double s_sum = 0.0;
						static int s_withColor = 0, s_without = 0, s_next = 20000;
						u32 c = outModel.colors[outModel.colors.GetCount() - 1];
						s_sum += (double)(((c >> 16) & 0xff) + ((c >> 8) & 0xff) + (c & 0xff)) / 3.0;
						if (hasColors) s_withColor++; else s_without++;
						int total = s_withColor + s_without;
						if (total >= s_next) {
							s_next += 20000;
							Displayf("[CPV] %d verts baked: %d from a colour stream, %d neutral fallback, mean channel %.1f (128 = palette lit, 255 = white)",
							         total, s_withColor, s_without, s_sum / (double)total);
						}
					}
				}

				gfxModelAdjunct adj;
				adj.vertex_idx = vertBase + (u32)v;
				adj.normal_idx = vertBase + (u32)v;
				adj.tex1_idx = vertBase + (u32)v;
				adj.tex2_idx = tex2Idx;
				adj.color_idx = vertBase + (u32)v;
				adj.bone_idx = 0;
				if (hasBones) {
					u32 bone = batch.bones[v];
					int slot = -1;
					for (int m = 0; m < pk.bone_map.GetCount(); m++)
						if (pk.bone_map[m] == bone) { slot = m; break; }
					if (slot < 0) { slot = pk.bone_map.GetCount(); pk.bone_map.Append(bone); }
					adj.bone_idx = (u32)slot;
				}
				pk.adjuncts.Append(adj);
			}

			// -windinglog: does the decoded strip winding agree with the resource's
			// own vertex normals?  Front faces are CCW in world space (rgl
			// GetRasterState), so a triangle is the right way out when
			// (v1-v0)x(v2-v0) points the same way as its vertex normals.  A batch
			// that mostly disagrees renders inside-out: solid bodywork still reads
			// as a shape, but a thin single-sided panel (a spoiler wing) disappears
			// from the side it should be visible from and shows from the other.
			{
				static int s_windLog = -1;
				if (s_windLog < 0) s_windLog = ARGS.Get("windinglog") ? 1 : 0;
				if (s_windLog && hasNormals) {
					// |cos| says whether the stored normals are usable at all (near 1 =
					// they really are surface normals); the sign of cos then says which
					// way the decoded winding faces.  nrmLen flags a normal stream that
					// did not decode into unit vectors.
					int agree = 0, disagree = 0;
					float sumAbsCos = 0.0f, sumNrmLen = 0.0f;
					int scoredCos = 0;
					for (int t = 0; t < tris.GetCount(); t++) {
						const rscTriangle &tri = tris[t];
						if (tri.i0 >= nVerts || tri.i1 >= nVerts || tri.i2 >= nVerts) continue;
						Vector3 fn;
						fn.Cross(batch.verts[tri.i1] - batch.verts[tri.i0],
						         batch.verts[tri.i2] - batch.verts[tri.i0]);
						Vector3 vn = batch.normals[tri.i0] + batch.normals[tri.i1] + batch.normals[tri.i2];
						float d = fn.Dot(vn);
						if (d > 0.0f) agree++;
						else if (d < 0.0f) disagree++;
						float fl = fn.Mag(), vl = vn.Mag();
						if (fl > 1e-9f && vl > 1e-9f) {
							float c = d / (fl * vl);
							sumAbsCos += c < 0.0f ? -c : c;
							sumNrmLen += batch.normals[tri.i0].Mag();
							scoredCos++;
						}
					}
					int scored = agree + disagree;
					if (scored)
						Displayf("[WIND] bone %3d shader %3d mat %2d batch %d: %d tris, %d out, %d INSIDE-OUT (%.0f%%) |cos| %.2f nrmLen %.2f%s",
						         (int)boneIdx, geom.shaderIdx, (int)pk.material_index, b, tris.GetCount(), agree, disagree,
						         100.0f * (float)disagree / (float)scored,
						         scoredCos ? sumAbsCos / (float)scoredCos : 0.0f,
						         scoredCos ? sumNrmLen / (float)scoredCos : 0.0f,
						         disagree > agree ? "  <-- flipped" : "");
				}
			}

			for (int t = 0; t < tris.GetCount(); t++) {
				const rscTriangle &tri = tris[t];
				if (tri.i0 >= nVerts || tri.i1 >= nVerts || tri.i2 >= nVerts) continue;
				gfxModelStrip st;
				st.type = 1;
				st.indices.Append((u32)tri.i0);
				st.indices.Append((u32)tri.i1);
				st.indices.Append((u32)tri.i2);
				pk.strips.Append(st);
				outModel.materials[pk.material_index].primitive_count++;
			}

			outModel.materials[pk.material_index].packet_count++;
			outModel.packets.Append(pk);
			added = true;
		}
	}
	if (partModel.cpvSets.GetCount() > 0) {
		outModel.cpv_sets.Resize(partModel.cpvSets.GetCount());
		for (int s = 0; s < partModel.cpvSets.GetCount(); s++) {
			outModel.cpv_sets[s].Resize(partModel.cpvSets[s].GetCount());
			for (int v = 0; v < partModel.cpvSets[s].GetCount(); v++) {
				outModel.cpv_sets[s][v] = (gfxPackedColor)partModel.cpvSets[s][v];
			}
		}
	}
	if (outModel.materials.GetCount() == 0) {
		gfxModelMaterial mat;
		mat.name = "default";
		mat.diffuse[0] = mat.diffuse[1] = mat.diffuse[2] = 1.0f;
		outModel.materials.Append(mat);
	}
	return added;
}

bool rscBuildCarGfxModel(const datResourceImage &image, u32 tblAddr, gfxModel &outModel)
{
	if (!image.IsValidAddress(tblAddr, 0x14)) return false;
	u16 count = image.ReadU16(tblAddr + 2);
	u32 nodesPtr = image.ReadU32(tblAddr + 8);
	u32 namesPtr = image.ReadU32(tblAddr + 12);
	u32 bonesPtr = image.ReadU32(tblAddr + 16);
	if (count == 0 || count > 512) return false;
	if (!image.IsValidAddress(nodesPtr, (u32)count * 4)) return false;

	for (int i = 0; i < (int)count; i++) {
		u32 nodeAddr = image.ReadU32(nodesPtr + (u32)i * 4);
		u32 boneIdx = bonesPtr ? image.ReadU32(bonesPtr + (u32)i * 4) : 0;
		if (!nodeAddr) continue;

		rscModel partModel;
		if (!rscDecodeCarModel(image, nodeAddr, partModel)) continue;
		if (namesPtr && image.IsValidAddress(namesPtr, (u32)count * 4)) {
			u32 np = image.ReadU32(namesPtr + (u32)i * 4);
			if (np && image.IsValidAddress(np, 1)) rscTagCarLightPart(partModel, image.ReadString(np));
		}
		gfxModelFromRscModel(partModel, boneIdx, outModel);
	}

	outModel.BuildDrawOrder();
	return outModel.packets.GetCount() > 0;
}

bool rscLoadModelsFromImage(const datResourceImage &image, rmcDrawable *target, const char *templateName, const char *textureName)
{
	if (!target) return false;
	atArray<u32> addrs;
	rscFindCarModels(image, addrs);
	if (!addrs.GetCount()) return false;
	gfxModel *lodGfx = new gfxModel();
	gfxModelMaterial mat;
	mat.name = templateName ? templateName : "page_model";
	rscClassifyCarMaterial(mat, templateName ? templateName : "", textureName ? textureName : "", kDefaultCarPaint);
	if (textureName && textureName[0])
		mat.texture_name = textureName;     // a real colour map; the classifier's colour modulates it
	mat.packet_count = 0;
	mat.primitive_count = 0;
	lodGfx->materials.Append(mat);
	int shaderToMaterial[1] = { 0 };
	bool any = false;
	for (int i = 0; i < addrs.GetCount(); i++) {
		rscModel part;
		if (!rscDecodeCarModel(image, addrs[i], part)) continue;
		for (int g = 0; g < part.geometries.GetCount(); g++) part.geometries[g].shaderIdx = 0;
		if (gfxModelFromRscModel(part, 0, *lodGfx, shaderToMaterial, 1)) any = true;
	}
	if (!any || lodGfx->packets.GetCount() == 0) { delete lodGfx; return false; }
	if (ARGS.Get("texlog"))
		Displayf("rscLoadModelsFromImage: '%s' -> %d models, %d packets, material '%s' colour %08x tex '%s'",
		         image.GetName(), addrs.GetCount(), lodGfx->packets.GetCount(), mat.name.c_str(), mat.car_color, mat.texture_name.c_str());
	lodGfx->BuildDrawOrder();
	rmcModel *rmcMdl = new rmcModel();
	rmcMdl->SetModel(0, lodGfx);
	Vector3 bmin, bmax;
	lodGfx->GetBoundingBox(bmin, bmax);
	rmcMdl->SetBoundingBox(bmin, bmax);
	target->GetLodGroup().SetModel(0, rmcMdl);
	target->SetBoundingBox(bmin, bmax);
	Vector3 ext = bmax - bmin;
	target->GetLodGroup().SetCullRadius(0.5f * ext.Mag());
	return true;
}

// PC PORT: a car LOD table names every mod part ("vroot_Body_chptp_stk_..._LOD_hlod.mesh",
// bumpers, hoods, kit bodies) but leaves the node empty when the part lives in its own
// resource pack, "<name>.pck" next to the car pack (inside the mounted <car>.dat).  The
// console streamed those in by name; without them opponent cars built from <car>_o.pck
// were a chassis with wheels.  Decodes every car-model node of the part pack onto the
// slot's bone, with its shader indices into the owning drawable's shader table.
static int sAppendNamedCarPart(const datResourceImage &image, const char *partName, u32 boneIdx,
                               gfxModel &lodGfx, const int *shaderToMaterial, int numShaders)
{
	size_t len = partName ? strlen(partName) : 0;
	if (len < 6 || _stricmp(partName + len - 5, ".mesh") != 0) return 0;
	char path[384];
	const char *imageName = image.GetName();
	const char *slash = imageName ? strrchr(imageName, '/') : nullptr;
	const char *bslash = imageName ? strrchr(imageName, '\\') : nullptr;
	if (bslash > slash) slash = bslash;
	if (slash) formatf(path, sizeof(path), "%.*s/%s", (int)(slash - imageName), imageName, partName);
	else formatf(path, sizeof(path), "%s", partName);

	datResourceImage partImage;
	if (!partImage.Load(path)) return 0;
	atArray<u32> roots;
	rscFindCarModels(partImage, roots);
	int added = 0;
	for (int r = 0; r < roots.GetCount(); r++) {
		rscModel partModel;
		if (!rscDecodeCarModel(partImage, roots[r], partModel)) continue;
		rscTagCarLightPart(partModel, partName);   // the slot's name names the cluster
		gfxModelFromRscModel(partModel, boneIdx, lodGfx, shaderToMaterial, numShaders);
		added++;
	}
	return added;
}

rmcDrawable *rscLoadDrawableFromResource(const datResourceImage &image, u32 drawableAddr, rmcDrawable *target, int ppfSlot)
{
	if (!image.IsValidAddress(drawableAddr, 0x24)) return nullptr;

	rmcDrawable *drawable = target ? target : new rmcDrawable();
	bool hasAnyLods = false;
	atArray<rscShaderInfo> shaders;
	rscDecodeCarShaders(image, drawableAddr, shaders);
	// Page-file drawables: build each shader's first page texture and name it as
	// the shader's colour map (registered as "<image>_s<shader>").
	if (ppfSlot >= 0) {
		for (int i = 0; i < shaders.GetCount(); i++) {
			rscShaderInfo &sh = shaders[i];
			for (int t = 0; t < sh.numPageTextures; t++) {
				char name[96];
				formatf(name, sizeof(name), "%s_s%d", image.GetName(), i);
				if (!rscLoadPageTexture(ppfSlot, sh.pageTextures[t], name)) continue;
				if (sh.numTextures < 6) sh.numTextures++;
				for (int m = sh.numTextures - 1; m > 0; m--) strcpy(sh.textures[m], sh.textures[m - 1]);
				strncpy(sh.textures[0], name, sizeof(sh.textures[0]) - 1);
				sh.textures[0][sizeof(sh.textures[0]) - 1] = 0;
				break;
			}
		}
	}
	// Per-car textures (e.g. "<car>_trim" atlas) are resident inside the pack image.
	// Pre-load and register all resident textures referenced by this drawable's shaders.
	for (int i = 0; i < shaders.GetCount(); i++) {
		const rscShaderInfo &sh = shaders[i];
		for (int t = 0; t < sh.numTextures; t++) {
			if (sh.textures[t][0]) {
				rscLoadResidentTexture(image, sh.textures[t]);
			}
		}
	}
	atArray<int> shaderToMaterial;
	for (int i = 0; i < shaders.GetCount(); i++) shaderToMaterial.Append(i);
	if (shaders.GetCount()) {
		Displayf("rscLoadDrawableFromResource: %d shaders (e.g. [0] %s -> '%s')", shaders.GetCount(), shaders[0].templateName, shaders[0].numTextures ? shaders[0].textures[0] : "");
		if (ARGS.Get("texlog"))
			for (int i = 0; i < shaders.GetCount(); i++)
				Displayf("  shader %2d: %-36s tex '%s'%s", i, shaders[i].templateName, shaders[i].numTextures ? shaders[i].textures[0] : "", shaders[i].numPageTextures ? " (+page)" : "");
	}
	Vector3 overallMin(1e30f, 1e30f, 1e30f);
	Vector3 overallMax(-1e30f, -1e30f, -1e30f);

	for (int lod = 0; lod < 5; lod++) {
		u32 tblAddr = image.ReadU32(drawableAddr + 0x10 + (u32)lod * 4);
		if (!tblAddr || !image.IsValidAddress(tblAddr, 0x14)) continue;

		u16 count = image.ReadU16(tblAddr + 2);
		u32 nodesPtr = image.ReadU32(tblAddr + 8);
		u32 namesPtr = image.ReadU32(tblAddr + 12);
		u32 bonesPtr = image.ReadU32(tblAddr + 16);

		if (count == 0 || count > 512) continue;
		if (!nodesPtr || !image.IsValidAddress(nodesPtr, (u32)count * 4)) continue;
		// Page-file drawables (rim / brake / exhaust pages) use the 16-byte table:
		// +0xc is 0xff and the node pointers start at +0x10, so there are no name
		// or bone arrays to read (see rscLoadPageImage).
		if (!namesPtr || !image.IsValidAddress(namesPtr, 4)) { namesPtr = 0; bonesPtr = 0; }
		if (bonesPtr && (!image.IsValidAddress(bonesPtr, (u32)count * 4) ||
		                 (bonesPtr >= nodesPtr && bonesPtr < nodesPtr + (u32)count * 4)))
			bonesPtr = 0;

		gfxModel *lodGfx = new gfxModel();
		bool lodIsSkinned = false;
		sAppendShaderMaterials(shaders, *lodGfx);
		for (int i = 0; i < (int)count; i++) {
			u32 nodeAddr = image.ReadU32(nodesPtr + (u32)i * 4);
			u32 boneIdx = bonesPtr ? image.ReadU32(bonesPtr + (u32)i * 4) : 0;
			if (boneIdx > 255) boneIdx = 0;
			if (!nodeAddr) {
				if (namesPtr) {
					u32 np = image.ReadU32(namesPtr + (u32)i * 4);
					if (np && image.IsValidAddress(np, 1))
						sAppendNamedCarPart(image, image.ReadString(np), boneIdx, *lodGfx, shaders.GetCount() ? &shaderToMaterial[0] : 0, shaders.GetCount());
				}
				continue;
			}

			rscModel partModel;
			// A vehicle node first, then the plain rmcModel the other resourced
			// drawables hold.  A ped's lod points straight at an rmcModel with
			// its part lists at +0x30, which the node decoder cannot read: its
			// +8 is a packet count, not a geometry count, and its +0x10 is the
			// part array rather than a geometry list.
			if (!rscDecodeCarModel(image, nodeAddr, partModel) &&
			    !rscDecodeModel(image, nodeAddr, partModel, false)) {
				if (ARGS.Get("texlog"))
					Displayf("  node %3d at %08x: neither decoder read it (+4 %08x +8 %08x +0x10 %08x +0x20 %08x)",
					         i, nodeAddr, image.ReadU32(nodeAddr + 4), image.ReadU32(nodeAddr + 8),
					         image.ReadU32(nodeAddr + 0x10), image.ReadU32(nodeAddr + 0x20));
				continue;
			}
			char hck[64]; formatf(hck, sizeof(hck), "after-decode-node-%d", i);
			ageHeapCheck(hck);
			// The node's name says which light cluster this part is, which the
			// shader table alone cannot (see rscTagCarLightPart).
			const char *nodeName = "";
			if (namesPtr) { u32 np = image.ReadU32(namesPtr + (u32)i * 4); if (np && image.IsValidAddress(np, 1)) nodeName = image.ReadString(np); }
			rscTagCarLightPart(partModel, nodeName);
			if (ARGS.Get("texlog") && lod == 0) {
				const char *nm = nodeName;
				char shs[96] = ""; int nsh = 0;
				for (int g = 0; g < partModel.geometries.GetCount() && nsh < 8; g++, nsh++) { char one[12]; formatf(one, sizeof(one), "%s%d", g ? "," : "", partModel.geometries[g].shaderIdx); strcat(shs, one); }
				Displayf("  node %3d bone %3d %-48s %d geoms %5d verts shaders %s box (%.2f %.2f %.2f)-(%.2f %.2f %.2f)", i, (int)boneIdx, nm, partModel.geometries.GetCount(), partModel.numVerts, shs,
				         partModel.boxMin.x, partModel.boxMin.y, partModel.boxMin.z, partModel.boxMax.x, partModel.boxMax.y, partModel.boxMax.z);
			}
			// A batch that names a bone per vertex is skinned, and its vertices
			// are in model space - the bind pose - rather than local to one
			// bone.  Those two want different matrices at draw time, so the
			// distinction has to travel with the model: rmcDrawable::DrawSkinned
			// reads it back off IsModelRelative and composes the bone's global
			// with the inverse of its rest global instead of using the global
			// alone.  Skinned with the wrong one, a ped's limbs stretch away
			// from the body as spikes.
			for (int g = 0; g < partModel.geometries.GetCount() && !lodIsSkinned; g++)
				for (int b = 0; b < partModel.geometries[g].batches.GetCount(); b++)
					if (partModel.geometries[g].batches[b].bones.GetCount()) { lodIsSkinned = true; break; }
			gfxModelFromRscModel(partModel, boneIdx, *lodGfx, shaders.GetCount() ? &shaderToMaterial[0] : 0, shaders.GetCount());
			formatf(hck, sizeof(hck), "after-gfxModel-node-%d", i);
			ageHeapCheck(hck);
		}

		if (lodGfx->packets.GetCount() > 0) {
			if (lodIsSkinned)
				lodGfx->SetModelRelative(true);
			lodGfx->BuildDrawOrder();
			rmcModel *rmcMdl = new rmcModel();
			rmcMdl->SetModel(0, lodGfx);
			Vector3 bmin, bmax;
			lodGfx->GetBoundingBox(bmin, bmax);
			rmcMdl->SetBoundingBox(bmin, bmax);

			if (lod < 4) {
				drawable->GetLodGroup().SetModel(lod, rmcMdl);
			}

			if (bmin.x < overallMin.x) overallMin.x = bmin.x;
			if (bmax.x > overallMax.x) overallMax.x = bmax.x;
			if (bmin.y < overallMin.y) overallMin.y = bmin.y;
			if (bmax.y > overallMax.y) overallMax.y = bmax.y;
			if (bmin.z < overallMin.z) overallMin.z = bmin.z;
			if (bmax.z > overallMax.z) overallMax.z = bmax.z;

			hasAnyLods = true;
		} else {
			delete lodGfx;
		}
	}

	if (!hasAnyLods) {
		if (!target) delete drawable;
		return nullptr;
	}
	if (ARGS.Get("texlog")) {
		char lods[128] = "";
		for (int lod = 0; lod < 4; lod++) {
			rmcModel *m = drawable->GetLodGroup().GetModel(lod);
			gfxModel *g = m ? m->GetModel(0) : 0;
			char one[32];
			formatf(one, sizeof(one), "%slod%d:%d", lod ? " " : "", lod, g ? g->packets.GetCount() : 0);
			strcat(lods, one);
		}
		Displayf("rscLoadDrawableFromResource: '%s' packets per lod %s", image.GetName(), lods);
	}

	drawable->SetBoundingBox(overallMin, overallMax);
	// The cull sphere: rmcDrawable::IsVisible tests the raw lod-group radius, and a
	// zero radius is a point test on the model origin (a chase camera 2.5 m behind
	// and above the car looked past its centre and culled the whole body).
	Vector3 ext = overallMax - overallMin;
	drawable->GetLodGroup().SetCullRadius(0.5f * ext.Mag());
	return drawable;
}
