////////////////////////////////////////
// array.h
////////////////////////////////////////

#ifndef ATL_ARRAY_H
#define ATL_ARRAY_H

#include "core/assert.h"

#ifndef NULL
#define NULL 0
#endif

template <typename T, int AllocStep = 32>
class atArray {
public:
    atArray() : m_Data(NULL), m_Count(0), m_Allocated(0) {}
    ~atArray() {
        if (m_Data) delete[] m_Data;
    }

    // Rule-of-five: atArray OWNS m_Data (its ~atArray/Resize call delete[]), so it
    // must deep-copy / move.  The compiler's default shallow copy would double-free
    // — which bites nested atArrays (e.g. atArray<gfxModelPacket>, where a packet
    // holds atArray members: Resize copy-assigns then delete[]s the old buffer).
    atArray(const atArray &o) : m_Data(NULL), m_Count(0), m_Allocated(0) {
        if (o.m_Count > 0) {
            m_Data = new T[o.m_Count];
            for (int i = 0; i < o.m_Count; i++) m_Data[i] = o.m_Data[i];
            m_Count = m_Allocated = o.m_Count;
        }
    }
    atArray & operator=(const atArray &o) {
        if (this == &o) return *this;
        T *nb = NULL;
        if (o.m_Count > 0) {
            nb = new T[o.m_Count];
            for (int i = 0; i < o.m_Count; i++) nb[i] = o.m_Data[i];
        }
        if (m_Data) delete[] m_Data;
        m_Data = nb;
        m_Count = m_Allocated = o.m_Count;
        return *this;
    }
    atArray(atArray &&o) : m_Data(o.m_Data), m_Count(o.m_Count), m_Allocated(o.m_Allocated) {
        o.m_Data = NULL; o.m_Count = 0; o.m_Allocated = 0;
    }
    atArray & operator=(atArray &&o) {
        if (this != &o) {
            if (m_Data) delete[] m_Data;
            m_Data = o.m_Data; m_Count = o.m_Count; m_Allocated = o.m_Allocated;
            o.m_Data = NULL; o.m_Count = 0; o.m_Allocated = 0;
        }
        return *this;
    }

    int GetAllocCount() const { return m_Allocated; }
    int GetSize() const { return m_Count; }
    int GetCount() const { return m_Count; }
    // the original shorthand: arr() == arr.GetCount()
    int operator()() const { return GetCount(); }
    T* data() { return m_Data; }
    const T* data() const { return m_Data; }
    bool IsEmpty() const { return m_Count == 0; }
    // Index of the first element equal to item, or -1.
    int Find(const T &item) const { for (int i = 0; i < m_Count; i++) if (m_Data[i] == item) return i; return -1; }
    T& Last() { Assert(m_Count > 0); return m_Data[m_Count - 1]; }
    const T& Last() const { Assert(m_Count > 0); return m_Data[m_Count - 1]; }

    void Append(const T &item) {
        Resize(m_Count + 1);
        m_Data[m_Count - 1] = item;
    }

    T & Append() {
        Resize(m_Count + 1);
        return m_Data[m_Count - 1];
    }

    // Remove one element, shifting the tail down (editor graph node delete).
    void Delete(int index) {
        Assert(index >= 0 && index < m_Count);
        for (int i = index; i < m_Count - 1; ++i)
            m_Data[i] = m_Data[i + 1];
        m_Count--;
    }

    void Push(const T &item) {
        Append(item);
    }
    T& Top() { return Last(); }
    const T& Top() const { return Last(); }
    T Pop() {
        Assert(m_Count > 0);
        T item = m_Data[m_Count - 1];
        m_Count--;
        return item;
    }

    T & Insert(int index) {
        Assert(index >= 0 && index <= m_Count);
        Resize(m_Count + 1);
        for (int i = m_Count - 1; i > index; i--) {
            m_Data[i] = m_Data[i - 1];
        }
        return m_Data[index];
    }

    void DeleteFast(int index) {
        Assert(index >= 0 && index < m_Count);
        m_Data[index] = m_Data[m_Count - 1];
        m_Count--;
    }

    void Reallocate(int size) {
        Reserve(size);
    }

    // Ensure capacity for at least `capacity` elements (never shrinks storage).
    void Reserve(int capacity) {
        if (capacity <= m_Allocated) return;
        T* newBuffer = new T[capacity];
        for (int i = 0; i < m_Count; i++) newBuffer[i] = static_cast<T&&>(m_Data[i]);
        if (m_Data) delete[] m_Data;
        m_Data = newBuffer;
        m_Allocated = capacity;
    }

    // Set the live element count.  Growth is amortized O(1): capacity grows in
    // AllocStep chunks then doubles, so Append() doesn't reallocate every call
    // (loading a 100-packet model was O(n^2) reallocs before this).  Shrinking
    // keeps the buffer (cheap pop); use Reset() to release storage.
    void Resize(int newSize) {
        if (newSize < 0) newSize = 0;
        if (newSize > m_Allocated) {
            int cap = m_Allocated > 0 ? m_Allocated : AllocStep;
            while (cap < newSize) cap *= 2;
            Reserve(cap);
        }
        m_Count = newSize;
    }

    void Reset() {
        if (m_Data) {
            delete[] m_Data;
            m_Data = NULL;
        }
        m_Count = 0;
        m_Allocated = 0;
    }

    void Reuse(int newSize) {
        Reset();
        Resize(newSize);
    }

    void Init(int size = 0) {
        Resize(size);
    }
    // AGE 2.72 form: reserve `count` slots (src, when given, seeds them).
    void Init(const T *src, int count) {
        Reset();
        if (src) { for (int i = 0; i < count; i++) Append(src[i]); }
        else Resize(count);
    }

    void DeleteMatches(const T &value) {
        int writeIndex = 0;
        for (int readIndex = 0; readIndex < m_Count; readIndex++) {
            if (!(m_Data[readIndex] == value)) {
                m_Data[writeIndex] = m_Data[readIndex];
                writeIndex++;
            }
        }
        m_Count = writeIndex;
    }

    // AGE idiom: game code takes &array[0] of an empty array to get a
    // (never dereferenced) element pointer, so index 0 of an empty array
    // yields a stable dummy instead of asserting.
    T& operator[](int index) {
        if (index == 0 && m_Count == 0) return EmptyElement();
        Assert(index >= 0 && index < m_Count);
        return m_Data[index];
    }

    const T& operator[](int index) const {
        if (index == 0 && m_Count == 0) return EmptyElement();
        Assert(index >= 0 && index < m_Count);
        return m_Data[index];
    }

    static T& EmptyElement() {
        static unsigned char s_Zero[sizeof(T)] = {0};
        return *reinterpret_cast<T*>(s_Zero);
    }

    // Range-for support only (T* iterators over the live [0, m_Count) span).
    // Everything else uses native atArray semantics at the call site:
    //   .empty() -> GetCount()==0    .back() -> a[GetCount()-1]    .data() -> &a[0]
    T*       begin()       { return m_Data; }
    T*       end()         { return m_Data + m_Count; }
    const T* begin() const { return m_Data; }
    const T* end()   const { return m_Data + m_Count; }

private:
    T *m_Data;
    int m_Count;
    int m_Allocated;
};

template <typename T, int N>
class atFixedArray {
private:
    T m_Data[N];
    int m_Count;
public:
    atFixedArray() : m_Count(0) {}
    int GetCount() const { return m_Count; }
    int GetSize() const { return m_Count; }
    int operator()() const { return m_Count; }
    bool IsEmpty() const { return m_Count == 0; }
    // Index of the first element equal to item, or -1.
    int Find(const T &item) const { for (int i = 0; i < m_Count; i++) if (m_Data[i] == item) return i; return -1; }
    void Append(const T &item) {
        Assert(m_Count < N);
        m_Data[m_Count++] = item;
    }
    T & Append() {
        Assert(m_Count < N);
        m_Count++;
        return m_Data[m_Count - 1];
    }
    T& Top() {
        Assert(m_Count > 0);
        return m_Data[m_Count - 1];
    }
    const T& Top() const {
        Assert(m_Count > 0);
        return m_Data[m_Count - 1];
    }
    T Pop() {
        Assert(m_Count > 0);
        m_Count--;
        return m_Data[m_Count];
    }
    void Delete(int index) {
        Assert(index >= 0 && index < m_Count);
        for (int i = index; i < m_Count - 1; ++i) {
            m_Data[i] = m_Data[i + 1];
        }
        m_Count--;
    }
    void DeleteFast(int index) {
        Assert(index >= 0 && index < m_Count);
        m_Data[index] = m_Data[m_Count - 1];
        m_Count--;
    }
    void Resize(int newSize) {
        Assert(newSize >= 0 && newSize <= N);
        m_Count = newSize;
    }
    // AGE 2.72 alias of Resize (elements are already storage-backed).
    void SetCount(int newCount) { Resize(newCount); }
    void Reset() { m_Count = 0; }
    void Push(const T &item) {
        Append(item);
    }
    int GetMaxCount() const {
        return N;
    }
    T& operator[](int index) {
        Assert(index >= 0 && index < m_Count);
        return m_Data[index];
    }
    const T& operator[](int index) const {
        Assert(index >= 0 && index < m_Count);
        return m_Data[index];
    }
    T* begin() { return m_Data; }
    T* end() { return m_Data + m_Count; }
    const T* begin() const { return m_Data; }
    const T* end() const { return m_Data + m_Count; }
};

#endif // ATL_ARRAY_H
