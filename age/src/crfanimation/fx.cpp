#include "crfanimation/fx.h"

#include "data/token.h"

crfAnimFXDataCreateInstanceFunc crfAnimFXData::sm_CreateFunction = NULL;

// Base data has no fields; the game's subclass parses the .animfx blocks.
void crfAnimFXData::Load(datTokenizer & /*tok*/)
{
}
