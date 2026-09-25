#ifndef DATA_CALLBACK_H
#define DATA_CALLBACK_H

// crAnimList (cranimation/animlist.h) derives from Base but reaches it only
// through this header in its include set; pull it in here so that resolves.
#include "data/base.h"

// ---------------------------------------------------------------------------
// datCallback — AGE deferred/bound callback.
//
// Wraps a free function, static member, or non-static member function together
// with up to two bound "context" pointers, so it can be stored now and invoked
// later via Call(param).  Adapter macros build the type-erased thunk:
//
//   CFA (f)   free/static fn taking NO args:            f()
//   CFA1(f)   free/static fn taking ONE ptr arg:        f(ctx1)      (bound)
//             ...or, when built as datCallback(CFA1(f),0,true):
//                                                       f(param)     (call-time)
//   MFA (f)   member fn taking NO args:                 (obj->*f)()
//   MFA1(f)   member fn taking ONE ptr arg:             (obj->*f)(param)
//   MFA2(f)   member fn taking TWO ptr args:            (obj->*f)(ctx2, param)
//
//   NullCB    an empty callback; Call() is a no-op.
//
// Construction:
//   datCallback()                          -> empty (== NullCB)
//   datCallback(CFA(f))                    -> f()
//   datCallback(CFA1(f), ctx)              -> f(ctx)
//   datCallback(CFA1(f), 0, true)          -> f(<call param>)
//   datCallback(MFA2(&C::m), obj, data)    -> (obj->*m)(data, <call param>)
//
// Only the runtime class + NullCB are exercised by the testanim2 milestone
// (cranimation default-constructs these and fires Call() with no arg at
// animation end); the adapter macros exist for the debug-UI / editor call
// sites compiled in other targets.
// ---------------------------------------------------------------------------

// List of callbacks run at a fixed point (g_datEndFrameList: after every frame).
class datCallbackList {
public:
	void Add(const class datCallback &cb);
	void Remove(const class datCallback &cb);
	void Call(void *param = 0) const;
	int GetCount() const { return m_Count; }
private:
	enum { kMax = 32 };
	class datCallback *m_Items[kMax] = {};
	int m_Count = 0;
};
extern datCallbackList g_datEndFrameList;

// Opaque per-callback payload (the third datCallback argument).
typedef void *CallbackData;

class datCallback
{
public:
	// Unified thunk shape: (bound ctx1, bound ctx2, call-time param).
	typedef void (*Thunk)(void* ctx1, void* ctx2, void* param);

	datCallback()
		: m_thunk(0), m_ctx1(0), m_ctx2(0), m_useParam(false) {}

	// Bound-context form: datCallback(CFA(f)), datCallback(CFA1(f), ctx),
	// datCallback(MFA2(&C::m), obj, data).
	datCallback(Thunk thunk, void* ctx1 = 0, void* ctx2 = 0)
		: m_thunk(thunk), m_ctx1(ctx1), m_ctx2(ctx2), m_useParam(false) {}

	// Call-param forms: with a null bound context the thunks forward the
	// Call() argument into that slot instead (see the c1?c1:p fallbacks).
	datCallback(Thunk thunk, void* ctx1, bool useParam)
		: m_thunk(thunk), m_ctx1(ctx1), m_ctx2(0), m_useParam(useParam) {}

	// datCallback(MFA1(&C::m), obj, 0, true): member callback whose argument
	// arrives at Call() time (editor menus pass the item index this way).
	datCallback(Thunk thunk, void* ctx1, void* ctx2, bool useParam)
		: m_thunk(thunk), m_ctx1(ctx1), m_ctx2(ctx2), m_useParam(useParam) {}

	datCallback(void (*f)())
		: m_ctx2(0), m_useParam(false)
	{
		struct Helper {
			static void Thunk(void* c1, void*, void*) {
				typedef void (*Func)();
				((Func)c1)();
			}
		};
		m_thunk = Helper::Thunk;
		m_ctx1 = (void*)f;
	}

	// RAGE's SetClient sets the callback's CLIENT DATA - the value the target receives -
	// not the receiver object.  The menus rebind it before Call() on left/right
	// (mcScreenBase::StandardEventHandler: SetClient((CallbackData)-1 / 1) on rows built as
	// datCallback(MFA1(&C::m), this, data)).  Writing m_ctx1 there replaced `this` with
	// +-1 and every left/right row crashed on its first member access (vehicle select:
	// read of 0x1 in mcMenuVehicle::DoAdvanceCar).  Member thunks read their argument from
	// ctx2; a callback with no bound argument slot keeps working the same way.
	void SetClient(void *client) { m_ctx2 = client; }
	void Call(void* param = 0) const
	{
		if (!m_thunk) return;
		// Bound contexts win; a null slot lets the adapter thunk fall back to
		// the call-time parameter (the ", true" construction forms).
		m_thunk(m_ctx1 ? m_ctx1 : param, m_ctx2, param);
	}

	bool IsSet() const { return m_thunk != 0; }

	// Editor call sites test callback.GetType() != datCallback::NULL_CALLBACK.
	enum { NULL_CALLBACK = 0, VALID_CALLBACK };
	int GetType() const { return m_thunk ? VALID_CALLBACK : NULL_CALLBACK; }

	bool operator==(const datCallback& o) const
	{
		return m_thunk == o.m_thunk && m_ctx1 == o.m_ctx1 &&
		       m_ctx2 == o.m_ctx2 && m_useParam == o.m_useParam;
	}
	bool operator!=(const datCallback& o) const { return !(*this == o); }

private:
	Thunk m_thunk;
	void* m_ctx1;
	void* m_ctx2;
	bool  m_useParam;
};

// An empty callback usable as a default argument / reset value.
// File-scope const => internal linkage, one per TU; only ever used by value.
static const datCallback NullCB;
static const datCallback NullCallback;

// ---------------------------------------------------------------------------
// Adapter thunk generators (function pointer carried as a non-type template
// argument, so each target function gets its own compile-time thunk).
// ---------------------------------------------------------------------------

// --- free / static functions ---
template <class FP, FP F> struct datFreeThunk;

// void f()
template <void (__cdecl *F)()>
struct datFreeThunk<void (__cdecl *)(), F>
{
	static void Fn(void*, void*, void*) { F(); }
};

// void f(A*)   (bound ctx1, or Call() param via the ",true" ctor)
template <class A, void (__cdecl *F)(A*)>
struct datFreeThunk<void (__cdecl *)(A*), F>
{
	static void Fn(void* c1, void*, void* p) { F(reinterpret_cast<A*>(c1 ? c1 : p)); }
};

// void f(A&)
template <class A, void (__cdecl *F)(A&)>
struct datFreeThunk<void (__cdecl *)(A&), F>
{
	static void Fn(void* c1, void*, void* p) { F(*reinterpret_cast<A*>(c1 ? c1 : p)); }
};

// void f(void*, void*)   (ctx2 bound, param at call time)
template <void (__cdecl *F)(void*, void*)>
struct datFreeThunk<void (__cdecl *)(void*, void*), F>
{
	static void Fn(void*, void* c2, void* p) { F(c2, p); }
};

// void f(float)
template <void (__cdecl *F)(float)>
struct datFreeThunk<void (__cdecl *)(float), F>
{
	static void Fn(void* c1, void*, void* p) { void* v = c1 ? c1 : p; F(v ? *reinterpret_cast<float*>(v) : 0.0f); }
};

// void f(bool)
template <void (__cdecl *F)(bool)>
struct datFreeThunk<void (__cdecl *)(bool), F>
{
	static void Fn(void* c1, void*, void* p) { void* v = c1 ? c1 : p; F(v ? *reinterpret_cast<bool*>(v) : false); }
};

// void f(int)
template <void (__cdecl *F)(int)>
struct datFreeThunk<void (__cdecl *)(int), F>
{
	static void Fn(void* c1, void*, void* p) { F((int)(intptr_t)(c1 ? c1 : p)); }
};

// --- member functions (obj bound in ctx1) ---
template <class MP, MP M> struct datMemThunk;

// void C::m()
template <class C, void (C::*M)()>
struct datMemThunk<void (C::*)(), M>
{
	static void Fn(void* c1, void*, void*) { (reinterpret_cast<C*>(c1)->*M)(); }
};

// bool C::m()  /  int C::m()   (result discarded: bank buttons bound to
// Save/Load-style members that report success)
template <class C, bool (C::*M)()>
struct datMemThunk<bool (C::*)(), M>
{
	static void Fn(void* c1, void*, void*) { (void)(reinterpret_cast<C*>(c1)->*M)(); }
};
template <class C, int (C::*M)()>
struct datMemThunk<int (C::*)(), M>
{
	static void Fn(void* c1, void*, void*) { (void)(reinterpret_cast<C*>(c1)->*M)(); }
};

// bool C::m(A*)
template <class C, class A, bool (C::*M)(A*)>
struct datMemThunk<bool (C::*)(A*), M>
{
	static void Fn(void* c1, void* c2, void* p) { (void)(reinterpret_cast<C*>(c1)->*M)(reinterpret_cast<A*>(c2 ? c2 : p)); }
};

// void C::m(A*)   (either bound ctx2 or call-time param)
template <class C, class A, void (C::*M)(A*)>
struct datMemThunk<void (C::*)(A*), M>
{
	static void Fn(void* c1, void* c2, void* p) { (reinterpret_cast<C*>(c1)->*M)(reinterpret_cast<A*>(c2 ? c2 : p)); }
};

// void C::m(A&)   (bound ctx2 or call-time param, passed by reference)
template <class C, class A, void (C::*M)(A&)>
struct datMemThunk<void (C::*)(A&), M>
{
	static void Fn(void* c1, void* c2, void* p) { (reinterpret_cast<C*>(c1)->*M)(*reinterpret_cast<A*>(c2 ? c2 : p)); }
};

// void C::m(int)
template <class C, void (C::*M)(int)>
struct datMemThunk<void (C::*)(int), M>
{
	static void Fn(void* c1, void* c2, void* p) { (reinterpret_cast<C*>(c1)->*M)((int)(intptr_t)(c2 ? c2 : p)); }
};

// void C::m(bool)
template <class C, void (C::*M)(bool)>
struct datMemThunk<void (C::*)(bool), M>
{
	static void Fn(void* c1, void* c2, void* p) { void* v = c2 ? c2 : p; (reinterpret_cast<C*>(c1)->*M)(v ? *reinterpret_cast<bool*>(v) : false); }
};

// void C::m(float)
template <class C, void (C::*M)(float)>
struct datMemThunk<void (C::*)(float), M>
{
	static void Fn(void* c1, void* c2, void* p) { union { void* ptr; float f; } u = { c2 ? c2 : p }; (reinterpret_cast<C*>(c1)->*M)(u.f); }
};

// void C::m(bool, float)   (bool bound in ctx2, float at call time: bank sliders)
template <class C, void (C::*M)(bool, float)>
struct datMemThunk<void (C::*)(bool, float), M>
{
	static void Fn(void* c1, void* c2, void* p) { (reinterpret_cast<C*>(c1)->*M)(c2 != 0, p ? *reinterpret_cast<float*>(p) : 0.0f); }
};

// void C::m(A*, B*)   (ctx2 bound, param at call time)
template <class C, class A, class B, void (C::*M)(A*, B*)>
struct datMemThunk<void (C::*)(A*, B*), M>
{
	static void Fn(void* c1, void* c2, void* p) { (reinterpret_cast<C*>(c1)->*M)(reinterpret_cast<A*>(c2), reinterpret_cast<B*>(p)); }
};

// void C::m() const
template <class C, void (C::*M)() const>
struct datMemThunk<void (C::*)() const, M>
{
	static void Fn(void* c1, void*, void*) { (reinterpret_cast<const C*>(c1)->*M)(); }
};

// void C::m(A*) const
template <class C, class A, void (C::*M)(A*) const>
struct datMemThunk<void (C::*)(A*) const, M>
{
	static void Fn(void* c1, void* c2, void* p) { (reinterpret_cast<const C*>(c1)->*M)(reinterpret_cast<A*>(c2 ? c2 : p)); }
};

// void C::m(const A*) const
template <class C, class A, void (C::*M)(const A*) const>
struct datMemThunk<void (C::*)(const A*) const, M>
{
	static void Fn(void* c1, void* c2, void* p) { (reinterpret_cast<const C*>(c1)->*M)(reinterpret_cast<const A*>(c2 ? c2 : p)); }
};

// void C::m(A*, B*) const
template <class C, class A, class B, void (C::*M)(A*, B*) const>
struct datMemThunk<void (C::*)(A*, B*) const, M>
{
	static void Fn(void* c1, void* c2, void* p) { (reinterpret_cast<const C*>(c1)->*M)(reinterpret_cast<A*>(c2), reinterpret_cast<B*>(p)); }
};

// void C::m(float) const
template <class C, void (C::*M)(float) const>
struct datMemThunk<void (C::*)(float) const, M>
{
	static void Fn(void* c1, void* c2, void* p) { union { void* ptr; float f; } u = { c2 ? c2 : p }; (reinterpret_cast<const C*>(c1)->*M)(u.f); }
};

#define CFA(f)   (&datFreeThunk<decltype(&f), &f>::Fn)
#define CFA1(f)  (&datFreeThunk<decltype(&f), &f>::Fn)
#define CFA2(f)  (&datFreeThunk<decltype(&f), &f>::Fn)
#define MFA(f)   (&datMemThunk<decltype(&f), &f>::Fn)
#define MFA1(f)  (&datMemThunk<decltype(&f), &f>::Fn)
#define MFA2(f)  (&datMemThunk<decltype(&f), &f>::Fn)

#endif // DATA_CALLBACK_H
