////////////////////////////////////////
// resource.h
////////////////////////////////////////

class rbHitTypeManager;
#ifdef HITTYPEMGR
#undef HITTYPEMGR
#endif
#define HITTYPEMGR (*rbHitTypeManager::smInstance)

#ifndef DATA_RESOURCE_H
#define DATA_RESOURCE_H

#include "core/output.h"
#include "core/types.h"
#include "data/memory.h"
#include "data/rscimage.h"

#include <new>
#include <string.h>
#include <type_traits>
#include <typeinfo>

class Vector3;
class Vector4;
class Matrix34;

// Reports (once per type) a class that a console page-in body constructs but
// that has no T(datResource&) on PC; the pointer is left NULL.
void datResourceNoPageIn(const char *typeName, const char *imageName);

// ---------------------------------------------------------------------------
// datResource - the block of resource memory an object is being constructed
// from.
//
// On the consoles a pack was paged in and every class inside it ran its
// "page-in" constructor T(datResource&) over memory that already held its
// members; the constructor only had to fix up pointers (PointerFixup) and
// re-run the constructors of the objects it owns (ObjectFixup).
//
// The PC port keeps that constructor and that call pattern, but the image is
// PS2 data (see data/rscimage.h) and cannot be used as live memory.  So here
// datResource is a cursor over the image: T(datResource&) reads its members
// out of the image in declaration order with the typed Get* calls (the same
// fixed-order discipline the ascii/binary tokenizers use), and
//   ObjectFixup(rsc, ptr)   reads the next pointer and does ptr = new T(rsc.At(addr))
//   ObjectFixup(rsc, obj)   re-constructs an embedded object from the cursor
//   rsc.PointerFixup(ptr)   reads the next pointer and keeps the image address
//                           in ptr (tagged) for VirtualConstructFromPtr / rsc.Construct
// A class therefore owns one constructor body for PS2 and one for PC (the PC
// body under __WIN32PC lists every member; the PS2 body only the pointers).
// Strings returned by GetStringPtr point into the image, which is freed when
// datChunk::Load returns: copy them.
// ---------------------------------------------------------------------------
class datResource
{
public:
	datResource() : m_Image(0), m_Addr(0) {}
	explicit datResource(const datResourceImage &image) : m_Image(&image), m_Addr(image.GetBase()) {}
	datResource(const datResourceImage &image, u32 addr) : m_Image(&image), m_Addr(addr) {}

	bool IsValid(u32 bytes = 1) const { return m_Image != 0 && m_Image->IsValidAddress(m_Addr, bytes); }
	const datResourceImage *GetImage() const { return m_Image; }
	const char *GetName() const { return m_Image ? m_Image->GetName() : ""; }

	// A cursor positioned on the object a pointer refers to.
	datResource At(u32 addr) const { return datResource(*m_Image, addr); }

	u32 Tell() const { return m_Addr; }
	void Seek(u32 addr) { m_Addr = addr; }
	void Skip(u32 bytes) { m_Addr += bytes; }
	// Advance to the next multiple of `alignment` bytes from the image base
	// (PS2 objects holding a Vector4 / quadword are 16-byte aligned).
	void Align(u32 alignment);

	// Typed reads; each advances the cursor with the PS2 alignment of the type.
	// Out-of-image reads return 0 (and warn once per image) so a partially
	// mapped class degrades to zeros instead of garbage.
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
	void GetVector3(Vector3 &v);        // three packed floats (12 bytes)
	void GetVector3Quad(Vector3 &v);    // a 16-byte aligned quadword vector
	void GetVector4(Vector4 &v);
	void GetMatrix34(Matrix34 &m);
	void GetChars(char *dst, int count);
	// Pointer member naming a string: follows it; "" when NULL.  Points into the image.
	const char *GetStringPtr();
	// Same, copied into a fixed buffer.
	void GetString(char *dst, int maxLen);

	// Pointer members.  PointerFixup reads the pointer and leaves the image
	// address in `ptr` (tagged, not dereferenceable); AddressOf recovers it
	// and Construct / VirtualConstructFromPtr turn it into a real object.
	template <typename T>
	void PointerFixup(T *&ptr) { ptr = (T *)(uintptr_t)GetPtr(); }
	void PointerFixup(void *&ptr) { ptr = (void *)(uintptr_t)GetPtr(); }
	template <typename T>
	u32 AddressOf(const T *ptr) const { return (u32)(uintptr_t)ptr; }
	template <typename T>
	void Construct(T *&ptr)
	{
		u32 addr = AddressOf(ptr);
		if constexpr (std::is_constructible<T, datResource &>::value) {
			ptr = (addr && m_Image) ? new T(At(addr)) : 0;
		} else {
			if (addr) datResourceNoPageIn(typeid(T).name(), GetName());
			ptr = 0;
		}
	}

	// Console-era accessors kept for source compatibility: there is no live
	// base address on PC.
	void *GetBase() const { return 0; }
	size_t GetFixup() const { return 0; }

private:
	const datResourceImage *m_Image;
	u32 m_Addr;
};

// Owned object reached through a pointer member: read the pointer, construct
// the object from its own place in the image.
template <typename T>
inline void ObjectFixup(datResource &rsc, T *&ptr)
{
	u32 addr = rsc.GetPtr();
	if constexpr (std::is_constructible<T, datResource &>::value) {
		ptr = (addr && rsc.GetImage()) ? new T(rsc.At(addr)) : 0;
	} else {
		// A class without a page-in constructor: console bodies still name it.
		if (addr) datResourceNoPageIn(typeid(T).name(), rsc.GetName());
		ptr = 0;
	}
}

// Owned array of `count` objects laid out inline at the pointer.  Each
// element's T(datResource&) must consume exactly the element's PS2 size
// (end with rsc.Align(<class alignment>) when the class is padded).
template <typename T>
inline void ObjectFixup(datResource &rsc, T *&ptr, int count)
{
	u32 addr = rsc.GetPtr();
	if (!addr || count <= 0 || !rsc.GetImage()) {
		ptr = 0;
		return;
	}
	if constexpr (std::is_constructible<T, datResource &>::value) {
		ptr = new T[count];
		datResource sub = rsc.At(addr);
		for (int i = 0; i < count; i++) {
			ptr[i].~T();
			::new ((void *)&ptr[i]) T(sub);
		}
	} else {
		datResourceNoPageIn(typeid(T).name(), rsc.GetName());
		ptr = 0;
	}
}

// Embedded object: re-run its constructor from the parent's cursor.
template <typename T>
inline void ObjectFixup(datResource &rsc, T &obj)
{
	if constexpr (std::is_constructible<T, datResource &>::value) {
		obj.~T();
		::new ((void *)&obj) T(rsc);
	} else {
		datResourceNoPageIn(typeid(T).name(), rsc.GetName());
	}
}

// Array of `count` pointers, each to an owned object (T is the pointer type).
template <typename T>
inline void PtrArrayObjectFixup(datResource &rsc, T *&ptr, int count)
{
	u32 addr = rsc.GetPtr();
	if (!addr || count <= 0 || !rsc.GetImage()) {
		ptr = 0;
		return;
	}
	ptr = new T[count];
	datResource sub = rsc.At(addr);
	for (int i = 0; i < count; i++) {
		sub.PointerFixup(ptr[i]);
		sub.Construct(ptr[i]);
	}
}

// A resource's page-in entry point: reconstruct the object in place.
typedef void (*datResourcePageInFunc)(datResource &);

// ---------------------------------------------------------------------------
// datResourceRegister — a static registrar object each resource-backed class
// instantiates once (e.g. `datResourceRegister crAnimation::Register(MAGIC,
// crAnimation::ResourcePageIn, 0)`) to associate a magic number with its
// page-in function.
// ---------------------------------------------------------------------------
class datResourceRegister
{
public:
	datResourceRegister(u32 magic, datResourcePageInFunc pageIn, int flags = 0)
		: m_Magic(magic), m_PageIn(pageIn), m_Flags(flags) {}

	u32                   m_Magic;
	datResourcePageInFunc m_PageIn;
	int                   m_Flags;
};

// ---------------------------------------------------------------------------
// ResourceId — a hashed identifier used to key entities stored in a package.
// ---------------------------------------------------------------------------
class ResourceId
{
public:
	ResourceId() : m_Id(0) {}
	ResourceId(u32 id) : m_Id(id) {}
#ifdef TESTANIM3_BUILD
	ResourceId(const char *name);
#else
	ResourceId(const char *name) : m_Id(Hash(name)) {}
#endif

	operator u32() const { return m_Id; }
	bool operator==(const ResourceId &o) const { return m_Id == o.m_Id; }
	bool operator!=(const ResourceId &o) const { return m_Id != o.m_Id; }

	u32 Get() const { return m_Id; }

private:
	static u32 Hash(const char *s)
	{
		u32 h = 5381;
		while (s && *s) h = ((h << 5) + h) + (unsigned char)*s++;
		return h;
	}

	u32 m_Id;
};

#ifndef DECLARE_PLACE
#define DECLARE_PLACE(cls) \
    void* operator new(size_t, void* ptr) { return ptr; } \
    void* operator new(size_t sz) { return ::operator new(sz); } \
    void operator delete(void*, void*) {} \
    void operator delete(void* ptr) { ::operator delete(ptr); }
#endif

#ifndef IMPLEMENT_PLACE
#define IMPLEMENT_PLACE(cls)
#endif

class datResourceHeader {
};

class datResourceManager {
public:
    datResourceHeader* Load(const char* name, int flags) { Quitf("datResourceManager::Load - not implemented"); return nullptr; }
};

class datResourceNameTable {
public:
    static void Normalize(char *dest, const char *src) {
        if (dest && src) {
            strcpy(dest, src);
        }
    }
    static datResourceNameTable* Load(const char* name, datResourceHeader* hdr) { Quitf("datResourceNameTable::Load - not implemented"); return nullptr; }
    template <typename T>
    static datResourceNameTable* Load(const char* name, T* hdr) { Quitf("datResourceNameTable::Load - not implemented"); return nullptr; }
    static void* Translate(const char* name, u32 magic) { Quitf("datResourceNameTable::Translate - not implemented"); return nullptr; }
    static const char* ReverseTranslate(const void* ptr, u32 type) { return ""; }
    void Free() { Quitf("datResourceNameTable::Free - not implemented"); }
};

// The pack loader travels with the resource declarations (game code reaches
// datChunk / the page manager through the rmcore and physics headers).
#include "data/chunk.h"

#endif // DATA_RESOURCE_H
