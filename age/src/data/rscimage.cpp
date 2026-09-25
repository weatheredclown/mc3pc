#include "data/rscimage.h"

#include "core/output.h"
#include "core/stream.h"
#include "data/assetcfg.h"
#include "vector/vector3.h"
#include "vector/Vector4.h"
#include "vector/Matrix34.h"

#include "data/args.h"
#include <string.h>
#include <stdio.h>
#include <direct.h>

////////////////////////////////////////
// datResourceImage

datResourceImage::datResourceImage()
	: m_Data(0), m_Base(0), m_Version(0), m_Count(0), m_Size(0)
{
	m_Name[0] = 0;
}

datResourceImage::~datResourceImage()
{
	Kill();
}

void datResourceImage::Kill()
{
	delete [] m_Data;
	m_Data = 0;
	m_Base = m_Version = m_Count = m_Size = 0;
}

static u32 sRd32(const u8 *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

bool datResourceImage::Load(const char *name)
{
	Kill();
	if (!name) return false;
	strncpy(m_Name, name, sizeof(m_Name) - 1);
	m_Name[sizeof(m_Name) - 1] = 0;

	Stream *s = ASSET.Open(name, "pck");
	if (!s) {
		const char *chnk = strstr(name, ".chnk");
		if (chnk) {
			char clean[256];
			int len = (int)(chnk - name);
			if (len > 0 && len < 250) {
				strncpy(clean, name, len);
				clean[len] = 0;
				s = ASSET.Open(clean, "pck");
			}
		}
	}
	if (!s) {
		Warningf("datResourceImage: cannot open '%s.pck'", name);
		return false;
	}

	u8 header[kHeaderSize];
	if (s->Read(header, kHeaderSize) != kHeaderSize) {
		Warningf("datResourceImage: '%s.pck' is shorter than its header", name);
		s->Close();
		return false;
	}
	m_Base = sRd32(header);
	m_Version = sRd32(header + 4);
	m_Count = sRd32(header + 8);
	m_Size = sRd32(header + 12);
	if (m_Count != 1 || m_Size == 0 || m_Size > 512u * 1024u * 1024u) {
		Warningf("datResourceImage: '%s.pck' has an unexpected header (base %08x version %u count %u size %u)", name, m_Base, m_Version, m_Count, m_Size);
		s->Close();
		Kill();
		return false;
	}

	m_Data = new u8[m_Size];
	u32 got = 0;
	while (got < m_Size) {
		int n = s->Read(m_Data + got, (int)(m_Size - got));
		if (n <= 0) break;
		got += (u32)n;
	}
	s->Close();
	if (got != m_Size) {
		Warningf("datResourceImage: '%s.pck' payload truncated (%u of %u bytes)", name, got, m_Size);
		Kill();
		return false;
	}
	Displayf("datResourceImage: '%s.pck' base %08x version %u, %u bytes", name, m_Base, m_Version, m_Size);
	return true;
}

bool datResourceImage::LoadFromMemory(const char *name, u32 base, const u8 *data, u32 size)
{
	Kill();
	if (!name || !data || size == 0) return false;
	strncpy(m_Name, name, sizeof(m_Name) - 1);
	m_Name[sizeof(m_Name) - 1] = 0;
	m_Base = base;
	m_Version = 0;
	m_Count = 1;
	m_Size = size;
	m_Data = new u8[size];
	memcpy(m_Data, data, size);
	return true;
}

u32 datResourceImage::ReadU32(u32 addr) const
{
	return IsValidAddress(addr, 4) ? sRd32(m_Data + (addr - m_Base)) : 0;
}

u16 datResourceImage::ReadU16(u32 addr) const
{
	if (!IsValidAddress(addr, 2)) return 0;
	const u8 *p = m_Data + (addr - m_Base);
	return (u16)((u16)p[0] | ((u16)p[1] << 8));
}

u8 datResourceImage::ReadU8(u32 addr) const
{
	return IsValidAddress(addr, 1) ? m_Data[addr - m_Base] : 0;
}

float datResourceImage::ReadFloat(u32 addr) const
{
	u32 bits = ReadU32(addr);
	float f;
	memcpy(&f, &bits, sizeof(f));
	return f;
}

const char *datResourceImage::ReadString(u32 addr) const
{
	if (!IsValidAddress(addr, 1)) return "";
	// only hand out strings that terminate inside the image
	u32 off = addr - m_Base;
	for (u32 i = off; i < m_Size; i++)
		if (m_Data[i] == 0) return (const char *)(m_Data + off);
	return "";
}

////////////////////////////////////////
// datResourceTokenizer

datResourceTokenizer::datResourceTokenizer(const datResourceImage &image, u32 addr)
	: m_Image(image), m_Addr(addr)
{
}

void datResourceTokenizer::Align(u32 alignment)
{
	if (alignment <= 1) return;
	u32 off = m_Addr - m_Image.GetBase();
	u32 rem = off % alignment;
	if (rem) m_Addr += alignment - rem;
}

u8 datResourceTokenizer::GetU8()
{
	u8 v = m_Image.ReadU8(m_Addr);
	m_Addr += 1;
	return v;
}

u16 datResourceTokenizer::GetU16()
{
	Align(2);
	u16 v = m_Image.ReadU16(m_Addr);
	m_Addr += 2;
	return v;
}

u32 datResourceTokenizer::GetU32()
{
	Align(4);
	u32 v = m_Image.ReadU32(m_Addr);
	m_Addr += 4;
	return v;
}

float datResourceTokenizer::GetFloat()
{
	Align(4);
	float v = m_Image.ReadFloat(m_Addr);
	m_Addr += 4;
	return v;
}

u32 datResourceTokenizer::GetPtr()
{
	u32 addr = GetU32();
	if (addr == 0) return 0;
	if (!m_Image.IsValidAddress(addr)) {
		Warningf("datResourceTokenizer: '%s' pointer %08x at %08x points outside the image", m_Image.GetName(), addr, m_Addr - 4);
		return 0;
	}
	return addr;
}

// The shipped packs store Vector3 as three packed floats (12 bytes, 4-byte
// aligned) - phInst and mcInstCityModel matrices are four of them in a row.
void datResourceTokenizer::GetVector3(Vector3 &v)
{
	v.x = GetFloat();
	v.y = GetFloat();
	v.z = GetFloat();
}

void datResourceTokenizer::GetVector3Quad(Vector3 &v)
{
	Align(16);
	v.x = GetFloat();
	v.y = GetFloat();
	v.z = GetFloat();
	GetFloat();     // the quadword's w
}

void datResourceTokenizer::GetVector4(Vector4 &v)
{
	Align(16);
	v.x = GetFloat();
	v.y = GetFloat();
	v.z = GetFloat();
	v.w = GetFloat();
}

void datResourceTokenizer::GetMatrix34(Matrix34 &m)
{
	GetVector3(m.a);
	GetVector3(m.b);
	GetVector3(m.c);
	GetVector3(m.d);
}

void datResourceTokenizer::GetChars(char *dst, int count)
{
	for (int i = 0; i < count; i++)
		dst[i] = (char)GetU8();
}

const char *datResourceTokenizer::GetStringPtr()
{
	u32 addr = GetPtr();
	return addr ? m_Image.ReadString(addr) : "";
}

////////////////////////////////////////
// unpack mode

const char *datResourceUnpackDir()
{
	static const char *s_Dir = 0;
	static bool s_Checked = false;
	if (!s_Checked) {
		s_Checked = true;
		const char *dir = 0;
		if (args::sm_Instance && ARGS.Get("unpackresources", 0, &dir) && dir && dir[0])
			s_Dir = dir;
	}
	return s_Dir;
}

void datResourceUnpackManifest(const datResourceImage &image, const char *outDir, const char *status)
{
	if (!outDir || !outDir[0]) return;
	_mkdir(outDir);
	char path[512];
	snprintf(path, sizeof(path), "%s/manifest.txt", outDir);
	FILE *f = fopen(path, "a");
	if (!f) {
		Warningf("datResourceUnpack: cannot write '%s'", path);
		return;
	}
	fprintf(f, "%s\tversion %u\tbase %08x\t%u bytes\t%s\n", image.GetName(), image.GetVersion(), image.GetBase(), image.GetSize(), status ? status : "");
	fclose(f);
	Displayf("datResourceUnpack: %s -> %s (%s)", image.GetName(), outDir, status ? status : "");
}
