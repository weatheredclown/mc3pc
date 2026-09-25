#ifndef ATL_BITSET_H
#define ATL_BITSET_H

#include "core/types.h"
#include <string.h>
#include <stdlib.h>

class atBitSet {
private:
	int m_NumBits;
	int m_NumWords;
	u32 *m_Words;

public:
	atBitSet() : m_NumBits(0), m_NumWords(0), m_Words(NULL) {}

	explicit atBitSet(int numBits) : m_NumBits(0), m_NumWords(0), m_Words(NULL) {
		Init(numBits);
	}

	atBitSet(const atBitSet &other) : m_NumBits(0), m_NumWords(0), m_Words(NULL) {
		*this = other;
	}

	~atBitSet() {
		Kill();
	}

	atBitSet &operator=(const atBitSet &other) {
		if (this != &other) {
			Kill();
			if (other.m_NumBits > 0 && other.m_Words != NULL) {
				Init(other.m_NumBits);
				if (m_Words && other.m_Words) {
					memcpy(m_Words, other.m_Words, m_NumWords * sizeof(u32));
				}
			}
		}
		return *this;
	}

	void Init(int numBits) {
		Kill();
		if (numBits > 0) {
			m_NumBits = numBits;
			m_NumWords = (numBits + 31) / 32;
			m_Words = (u32 *)malloc(m_NumWords * sizeof(u32));
			if (m_Words) {
				memset(m_Words, 0, m_NumWords * sizeof(u32));
			}
		}
	}

	void Kill() {
		if (m_Words) {
			free(m_Words);
			m_Words = NULL;
		}
		m_NumBits = 0;
		m_NumWords = 0;
	}

	void Reset() {
		if (m_Words && m_NumWords > 0) {
			memset(m_Words, 0, m_NumWords * sizeof(u32));
		}
	}

	void ClearAll() {
		Reset();
	}

	void SetAll() {
		if (m_Words && m_NumWords > 0) {
			memset(m_Words, 0xFF, m_NumWords * sizeof(u32));
			// Mask off unused high bits in the last word
			int rem = m_NumBits % 32;
			if (rem != 0) {
				m_Words[m_NumWords - 1] &= (1u << rem) - 1u;
			}
		}
	}

	int GetNumBits() const { return m_NumBits; }
	int GetCount() const { return m_NumBits; }
	int GetNumWords() const { return m_NumWords; }
	const u32 *GetWords() const { return m_Words; }
	u32 *GetWords() { return m_Words; }

	bool IsSet(int bit) const {
		if (bit < 0 || bit >= m_NumBits || !m_Words) return false;
		return (m_Words[bit / 32] & (1u << (bit % 32))) != 0;
	}

	bool IsClear(int bit) const {
		return !IsSet(bit);
	}

	void Set(int bit, bool val = true) {
		if (bit < 0 || bit >= m_NumBits || !m_Words) return;
		if (val) {
			m_Words[bit / 32] |= (1u << (bit % 32));
		} else {
			m_Words[bit / 32] &= ~(1u << (bit % 32));
		}
	}

	void Clear(int bit) {
		Set(bit, false);
	}

	void Toggle(int bit) {
		if (bit < 0 || bit >= m_NumBits || !m_Words) return;
		m_Words[bit / 32] ^= (1u << (bit % 32));
	}
};

template <int N>
class atFixedBitSet {
private:
	enum {
		kNumWords = (N + 31) / 32
	};
	u32 m_Words[kNumWords > 0 ? kNumWords : 1];

public:
	atFixedBitSet() {
		Reset();
	}

	void Reset() {
		memset(m_Words, 0, sizeof(m_Words));
	}

	void ClearAll() {
		Reset();
	}

	void SetAll() {
		memset(m_Words, 0xFF, sizeof(m_Words));
		int rem = N % 32;
		if (rem != 0 && kNumWords > 0) {
			m_Words[kNumWords - 1] &= (1u << rem) - 1u;
		}
	}

	int GetNumBits() const { return N; }
	int GetCount() const { return N; }
	int GetNumWords() const { return kNumWords; }
	const u32 *GetWords() const { return m_Words; }
	u32 *GetWords() { return m_Words; }

	bool IsSet(int bit) const {
		if (bit < 0 || bit >= N) return false;
		return (m_Words[bit / 32] & (1u << (bit % 32))) != 0;
	}

	bool IsClear(int bit) const {
		return !IsSet(bit);
	}

	void Set(int bit, bool val = true) {
		if (bit < 0 || bit >= N) return;
		if (val) {
			m_Words[bit / 32] |= (1u << (bit % 32));
		} else {
			m_Words[bit / 32] &= ~(1u << (bit % 32));
		}
	}

	void Clear(int bit) {
		Set(bit, false);
	}

	void Toggle(int bit) {
		if (bit < 0 || bit >= N) return;
		m_Words[bit / 32] ^= (1u << (bit % 32));
	}
};

#endif // ATL_BITSET_H
