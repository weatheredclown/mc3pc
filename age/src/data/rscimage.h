#ifndef DATA_RSCIMAGE_H
#define DATA_RSCIMAGE_H

////////////////////////////////////////
// data/rscimage.h
//
// Reading console resource packs (.pck) as DATA.
//
// A pack is a PS2 memory image of the game's objects, relocated to a fixed
// virtual base, plus a 128-byte header.  The original engine paged the
// image in and fixed its pointers up in place; that needs the PS2 class
// layouts byte for byte, so this port never does it.  Instead the image is
// kept as bytes and datResourceTokenizer reads fields out of it in the
// PS2 ABI (32-bit little-endian, 4-byte pointers, 16-byte vectors), and a
// per-type builder constructs the port's real objects from what it reads.
//
// Object layout rules seen in the packs (PS2 GCC 2.95 ABI): 4-byte
// pointers, Vector3 = 3 packed floats, Matrix34 = 4 of them; a polymorphic
// class's vtable pointer comes right after any non-polymorphic base's
// members (mcCityModel: 16 bytes of mcCullable, then the vtable), and at
// offset 0 when there is no such base (phInst, phBound, mcCity).  Vtable
// values are PS2 code addresses (0x007xxxxx) and identify the class.
//
// Container layout (no published spec exists; taken from the shipped
// SLUS-21355 packs, identical across city/bound/vehicle/fog packs):
//   0x00  u32  base      virtual address the image was relocated to (0x06800000)
//   0x04  u32  version   the *_RESOURCE_VERSION the game compares against
//   0x08  u32  count     number of images (always 1 in the shipped packs)
//   0x0c  u32  size      payload bytes
//   0x10  zeros to 0x80
//   0x80  payload: the image; the root object sits at offset 0, embedded
//         pointers are absolute virtual addresses in [base, base + size),
//         0xCD bytes are unwritten padding.
////////////////////////////////////////

#include "core/types.h"

class Vector3;
class Vector4;
class Matrix34;

class datResourceImage {
public:
	enum { kHeaderSize = 0x80 };

	datResourceImage();
	~datResourceImage();

	// Opens "<name>.pck" through the ASSET manager ("$/..." paths accepted).
	bool Load(const char *name);
	// Adopts a copy of `data` as the image, relocated to `base` (a .ppf page whose
	// objects were built at that address; see rscview -ppf).
	bool LoadFromMemory(const char *name, u32 base, const u8 *data, u32 size);
	void Kill();
	bool IsLoaded() const { return m_Data != 0; }

	u32 GetBase() const { return m_Base; }
	u32 GetVersion() const { return m_Version; }
	u32 GetSize() const { return m_Size; }
	const char *GetName() const { return m_Name; }
	const u8 *GetData() const { return m_Data; }
	const u8 *GetPayload() const { return m_Data; }   // alias used by the skeleton reader

	// Address <-> offset; addresses outside the image are invalid.
	bool IsValidAddress(u32 addr, u32 bytes = 1) const { return addr >= m_Base && addr - m_Base + bytes <= m_Size; }
	u32 ToOffset(u32 addr) const { return addr - m_Base; }
	u32 ToAddress(u32 offset) const { return m_Base + offset; }
	const u8 *At(u32 addr) const { return IsValidAddress(addr) ? m_Data + (addr - m_Base) : 0; }

	// Raw reads at an address (little-endian); out of range reads return 0.
	u32 ReadU32(u32 addr) const;
	u16 ReadU16(u32 addr) const;
	u8 ReadU8(u32 addr) const;
	float ReadFloat(u32 addr) const;
	// NUL-terminated string at an address (pointer into the image, or "" when invalid).
	const char *ReadString(u32 addr) const;

private:
	char m_Name[128];
	u8 *m_Data;
	u32 m_Base;
	u32 m_Version;
	u32 m_Count;
	u32 m_Size;
};

// Field cursor over an image.  Reads advance in declaration order with the
// PS2 ABI alignment of each type, so a class loader lists its members the
// way the header declares them (the same fixed-order discipline the
// ascii/binary tokenizers use).
class datResourceTokenizer {
public:
	datResourceTokenizer(const datResourceImage &image, u32 addr);

	const datResourceImage &GetImage() const { return m_Image; }
	u32 Tell() const { return m_Addr; }
	void Seek(u32 addr) { m_Addr = addr; }
	void Skip(u32 bytes) { m_Addr += bytes; }
	void Align(u32 alignment);
	bool IsValid(u32 bytes = 1) const { return m_Image.IsValidAddress(m_Addr, bytes); }

	u8 GetU8();
	s8 GetS8() { return (s8)GetU8(); }
	bool GetBool() { return GetU8() != 0; }
	u16 GetU16();
	s16 GetS16() { return (s16)GetU16(); }
	u32 GetU32();
	s32 GetS32() { return (s32)GetU32(); }
	int GetInt() { return (int)GetS32(); }
	float GetFloat();
	// 4-byte pointer: the target's virtual address (0 = NULL), validated.
	u32 GetPtr();
	// The vtable slot at the start of a polymorphic PS2 object.
	u32 GetVTable() { return GetU32(); }
	// Vector3 = three packed floats (12 bytes) in the shipped packs; a
	// Matrix34 is four of them (a, b, c, d).  GetVector3Quad reads a
	// 16-byte aligned quadword vector where a structure uses one.
	void GetVector3(Vector3 &v);
	void GetVector3Quad(Vector3 &v);
	void GetVector4(Vector4 &v);
	void GetMatrix34(Matrix34 &m);
	// Fixed-size char array member.
	void GetChars(char *dst, int count);
	// Pointer member that names a string: follows it and returns the text.
	const char *GetStringPtr();

	// A cursor positioned on the object a pointer refers to.
	datResourceTokenizer At(u32 addr) const { return datResourceTokenizer(m_Image, addr); }

private:
	const datResourceImage &m_Image;
	u32 m_Addr;
};

// Per-type builder contract used by datChunk::Load: specialize for every
// root object type a pack can hold and construct the real object from the
// image.  The default reports the missing loader and fails.
template <typename T>
struct datResourceBuilder {
	static T *Build(const datResourceImage &image);
};

#include "core/output.h"

template <typename T>
T *datResourceBuilder<T>::Build(const datResourceImage &image)
{
	Warningf("datResourceBuilder: no loader for the root object of '%s' (version %u, %u bytes)", image.GetName(), image.GetVersion(), image.GetSize());
	return 0;
}

// ---------------------------------------------------------------------------
// Unpack mode (stub): "-unpackresources <dir>" asks every pack the game
// loads to be written back out as the loose PC-format files the __WIN32PC
// code path reads (the inverse of the console makeresources tools).  A
// type opts in by specializing datResourceUnpacker<T>; the default only
// records the pack in <dir>/manifest.txt so the mode can be grown one root
// type at a time.
// ---------------------------------------------------------------------------
template <typename T>
struct datResourceUnpacker {
	// Returns true when loose files were written for `image` under `outDir`.
	static bool Unpack(const datResourceImage &image, const char *outDir);
};

// The directory given to -unpackresources, or NULL when the mode is off.
const char *datResourceUnpackDir();
// Appends one line for `image` to <outDir>/manifest.txt (creates the directory).
void datResourceUnpackManifest(const datResourceImage &image, const char *outDir, const char *status);

template <typename T>
bool datResourceUnpacker<T>::Unpack(const datResourceImage &image, const char *outDir)
{
	datResourceUnpackManifest(image, outDir, "no unpacker for this root type yet");
	return false;
}

#endif // DATA_RSCIMAGE_H
