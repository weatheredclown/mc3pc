////////////////////////////////////////
// material.cpp
//
// phMaterial implementation.
////////////////////////////////////////

#include "phcore/material.h"
#include "data/resource.h"
#include "core/output.h"

// Friction here is a multiplier on the tyre's own grip, so 1 is "ordinary
// ground" and 0 is a frictionless plane.  A material that never had values
// loaded into it has to read as ordinary ground: it is what an unknown
// material id resolves to (phBoundLoadFromResource substitutes the default
// surface) and what phSurfaceMgr::GetDefaultSurface hands out.  Defaulting it
// to zero turned every such surface into ice, because vehWheel floors a
// non-positive surface friction at 0.05 and the car then has no grip at all.
const float phMaterial::kDefaultFriction = 1.0f;

const char * phMaterial::ClassTypeString = "BASE";
const char * phMaterialSnd::ClassTypeString = "SOUND";

phMaterial::phMaterial(datResource &rsc)
    : Type(MATERIAL)
    , Elasticity(0.0f)
    , Friction(kDefaultFriction)
{
    Name[0] = 0; Effect[0] = 0; SoundName[0] = 0;
    rsc.GetVTable();
    Type = rsc.GetInt();
    rsc.GetChars(Name, sizeof(Name));
    Name[sizeof(Name) - 1] = 0;
    Elasticity = rsc.GetFloat();
    Friction = rsc.GetFloat();
}

phMaterial::phMaterial()
    : Type(MATERIAL)
    , Elasticity(0.0f)
    , Friction(kDefaultFriction)
{
    Name[0] = '\0';
    Effect[0] = '\0';
    SoundName[0] = '\0';
}

phMaterial::~phMaterial()
{
}

void phMaterial::Load(datAsciiTokenizer &token)
{
    if (token.CheckToken("type:")) {
        char typeBuf[64];
        token.GetToken(typeBuf, sizeof(typeBuf));
        if (_stricmp(typeBuf, phMaterialSnd::ClassTypeString) == 0) {
            Type = SOUND;
        } else {
            Type = MATERIAL;
        }
    }
    if (token.CheckToken("mtl")) {
        token.GetToken(Name, sizeof(Name));
        token.CheckToken("{");
    }
    LoadData(token);
    if (Type == MATERIAL) {
        // Canonical blocks end right after the fields LoadData reads; tolerate
        // (but flag) stray fields so an odd asset can't Quitf the whole load.
        char buf[64];
        while (!token.CheckToken("}")) {
            if (token.GetToken(buf, sizeof(buf)) <= 0)
                break;
            Warningf("phMaterial '%s': skipping unknown field '%s'", Name, buf);
        }
    }
}

// Reads ONLY the base fields, in file order, and stops before the closing '}'
// (or before any subclass fields).  This is the composition contract
// rbPhysMaterial::LoadData depends on: it consumes "mtl <name> {" itself,
// chains here, then parses its own fields up to '}'.
//
// Canonical block (Entity/*/Bound.bnd, layout.bnds):
//   mtl default {
//   \telasticity: 0.100000 <trailing space>
//   \tfriction: 0.500000 <trailing space>
//   \teffect: none
//   \tsound: none
//   }
void phMaterial::LoadData(datAsciiTokenizer &token)
{
    if (token.CheckToken("elasticity:")) {
        Elasticity = token.GetFloat();
    }
    if (token.CheckToken("friction:")) {
        Friction = token.GetFloat();
    }
    // Optional: absent in the rbPhysMat flavor (whose blocks go straight to
    // the subclass fields after friction).  Stored so Save reproduces them.
    if (token.CheckToken("effect:")) {
        token.GetToken(Effect, sizeof(Effect));
    }
    if (token.CheckToken("sound:")) {
        token.GetToken(SoundName, sizeof(SoundName));
    }
}

// Exact inverse of LoadData: same fields, same order, same formatting (tab
// indent, trailing space after the numeric fields) so load->save round trips
// the source block byte-for-byte.
void phMaterial::SaveData(datAsciiTokenizer &token)
{
    token.PutDelimiter("\telasticity: ");
    token.Put(Elasticity);
    token.PutDelimiter(" \n");
    token.PutDelimiter("\tfriction: ");
    token.Put(Friction);
    token.PutDelimiter(" \n");
    if (Effect[0]) {
        token.PutDelimiter("\teffect: ");
        token.Put(Effect);
        token.PutDelimiter("\n");
    }
    if (SoundName[0]) {
        token.PutDelimiter("\tsound: ");
        token.Put(SoundName);
        token.PutDelimiter("\n");
    }
}

// Full-block save, the exact inverse of Load.
void phMaterial::Save(datAsciiTokenizer &token)
{
    token.PutDelimiter("mtl ");
    token.Put(Name);
    token.PutDelimiter(" {\n");
    SaveData(token);
    token.PutDelimiter("}\n");
}

phMaterialSnd::phMaterialSnd()
{
    Type = SOUND;
}

phMaterialSnd::~phMaterialSnd()
{
}
