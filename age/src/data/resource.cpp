#include "data/resource.h"

#include "core/output.h"
#include "vector/vector3.h"
#include "vector/Vector4.h"
#include "vector/Matrix34.h"

#include <string.h>

////////////////////////////////////////
// datResource - see resource.h.  Every read goes through a
// datResourceTokenizer positioned at the cursor so the PS2 ABI alignment
// rules live in one place (data/rscimage.cpp).

static void sWarnOnce(const datResourceImage *image, u32 addr)
{
	static const datResourceImage *sLast = 0;
	if (image == sLast) return;
	sLast = image;
	Warningf("datResource: read at %08x is outside '%s'; the class layout is longer than the object", addr, image ? image->GetName() : "(no image)");
}

void datResourceNoPageIn(const char *typeName, const char *imageName)
{
	static const char *sReported[64];
	static int sCount = 0;
	for (int i = 0; i < sCount; i++)
		if (strcmp(sReported[i], typeName) == 0) return;
	if (sCount < 64) sReported[sCount++] = typeName;
	Warningf("datResource: '%s' has no page-in constructor on PC; left NULL (first seen in '%s')", typeName, imageName);
}

void datResource::Align(u32 alignment)
{
	if (!m_Image) return;
	datResourceTokenizer t(*m_Image, m_Addr);
	t.Align(alignment);
	m_Addr = t.Tell();
}

u8 datResource::GetU8()
{
	if (!IsValid(1)) { sWarnOnce(m_Image, m_Addr); m_Addr += 1; return 0; }
	datResourceTokenizer t(*m_Image, m_Addr);
	u8 v = t.GetU8();
	m_Addr = t.Tell();
	return v;
}

u16 datResource::GetU16()
{
	if (!m_Image) return 0;
	datResourceTokenizer t(*m_Image, m_Addr);
	t.Align(2);
	if (!m_Image->IsValidAddress(t.Tell(), 2)) { sWarnOnce(m_Image, t.Tell()); m_Addr = t.Tell() + 2; return 0; }
	u16 v = t.GetU16();
	m_Addr = t.Tell();
	return v;
}

u32 datResource::GetU32()
{
	if (!m_Image) return 0;
	datResourceTokenizer t(*m_Image, m_Addr);
	t.Align(4);
	if (!m_Image->IsValidAddress(t.Tell(), 4)) { sWarnOnce(m_Image, t.Tell()); m_Addr = t.Tell() + 4; return 0; }
	u32 v = t.GetU32();
	m_Addr = t.Tell();
	return v;
}

float datResource::GetFloat()
{
	u32 bits = GetU32();
	float f;
	memcpy(&f, &bits, sizeof(f));
	return f;
}

u32 datResource::GetPtr()
{
	if (!m_Image) return 0;
	datResourceTokenizer t(*m_Image, m_Addr);
	t.Align(4);
	if (!m_Image->IsValidAddress(t.Tell(), 4)) { sWarnOnce(m_Image, t.Tell()); m_Addr = t.Tell() + 4; return 0; }
	u32 v = t.GetPtr();
	m_Addr = t.Tell();
	return v;
}

void datResource::GetVector3(Vector3 &v)
{
	v.x = GetFloat();
	v.y = GetFloat();
	v.z = GetFloat();
}

void datResource::GetVector3Quad(Vector3 &v)
{
	Align(16);
	GetVector3(v);
	Skip(4);
}

void datResource::GetVector4(Vector4 &v)
{
	Align(16);
	v.x = GetFloat();
	v.y = GetFloat();
	v.z = GetFloat();
	v.w = GetFloat();
}

void datResource::GetMatrix34(Matrix34 &m)
{
	GetVector3(m.a);
	GetVector3(m.b);
	GetVector3(m.c);
	GetVector3(m.d);
}

void datResource::GetChars(char *dst, int count)
{
	for (int i = 0; i < count; i++)
		dst[i] = (char)GetU8();
}

const char *datResource::GetStringPtr()
{
	u32 addr = GetPtr();
	return (addr && m_Image) ? m_Image->ReadString(addr) : "";
}

void datResource::GetString(char *dst, int maxLen)
{
	if (maxLen <= 0) return;
	const char *s = GetStringPtr();
	strncpy(dst, s, (size_t)maxLen - 1);
	dst[maxLen - 1] = 0;
}
