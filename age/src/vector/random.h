#ifndef VECTOR_RANDOM_H
#define VECTOR_RANDOM_H

////////////////////////////////////////
// vector/random.h
//
// Random - a small, seedable, deterministic generator (each instance carries
// its own state so replay and draw randomness stay independent).
////////////////////////////////////////

class Random
{
public:
	Random(int seed = 0)					{ ForceReset(seed); }

	// Reset is what game code calls to reseed a stream, sometimes from values
	// that vary run to run (mcRaceConfig::GenerateRandomLoadScreenName reseeds
	// g_DrawRand from the frame count).  While a stream is locked - see
	// randInitFromArgs and -randseed - those reseeds are ignored so a run stays
	// reproducible; ForceReset always reseeds.
	void Reset(int seed = 0)				{ if (!m_Locked) ForceReset(seed); }
	void ForceReset(int seed);
	void Set(int seed)						{ Reset(seed); }
	int GetSeed() const						{ return m_Seed; }
	void SetLocked(bool locked)				{ m_Locked = locked; }
	bool IsLocked() const					{ return m_Locked; }

	// Uniform [0,1).
	float Float();
	// Uniform non-negative 31-bit integer.
	int Int();
	// Uniform in [lo,hi) for floats, [lo,hi] for ints.
	float Range(float lo, float hi);
	int Range(int lo, int hi);
	// Uniform in [-v, v].
	float Vary(float v);

private:
	unsigned Next();

	int m_Seed;
	unsigned m_State;
	bool m_Locked = false;
};

extern Random g_DrawRand;     // visual-only randomness (never affects simulation)
extern Random g_ReplayRand;   // simulation randomness, reseeded per race for replays

// -randseed <n>: pin both global streams to n and ignore later reseeds, so two
// runs of the same scene draw the same randomness (A/B screenshots).  Call once
// after the command line is parsed; without the argument nothing changes.
void randInitFromArgs();

#endif // VECTOR_RANDOM_H
