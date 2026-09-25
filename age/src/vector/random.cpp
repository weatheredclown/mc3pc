#include "vector/random.h"

#include "data/args.h"
#include "core/output.h"
#include <stdlib.h>

Random g_DrawRand(1);
Random g_ReplayRand(1);

void Random::ForceReset(int seed)
{
	m_Seed = seed;
	m_State = (unsigned)seed * 2654435761u + 0x9E3779B9u;
	if (m_State == 0) m_State = 0x9E3779B9u;
}

unsigned Random::Next()
{
	// xorshift32
	unsigned x = m_State;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	m_State = x;
	return x;
}

float Random::Float()
{
	return (float)(Next() >> 8) * (1.0f / 16777216.0f);
}

int Random::Int()
{
	return (int)(Next() >> 1);
}

float Random::Range(float lo, float hi)
{
	return lo + (hi - lo) * Float();
}

int Random::Range(int lo, int hi)
{
	if (hi <= lo) return lo;
	return lo + (int)(Next() % (unsigned)(hi - lo + 1));
}

float Random::Vary(float v)
{
	return Range(-v, v);
}

void randInitFromArgs()
{
	// Hosts differ in where they start up (mcmain.h for the game, tester_main /
	// rscview for the tools), so this is called from more than one place.
	static bool s_done = false;
	if (s_done)
		return;
	s_done = true;

	const char *seedText = NULL;
	if (!ARGS.Get("randseed", 0, &seedText) || !seedText)
		return;

	const int seed = atoi(seedText);
	g_DrawRand.ForceReset(seed);
	g_ReplayRand.ForceReset(seed);
	g_DrawRand.SetLocked(true);
	g_ReplayRand.SetLocked(true);
	Displayf("-randseed %d: g_DrawRand and g_ReplayRand pinned (later Reset calls ignored)", seed);
}
