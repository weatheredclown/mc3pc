#ifndef __STARFIELD_H__
#define __STARFIELD_H__

#include "vector/vector3.h"
#include "vector/vector4.h"

#include "parse/fileio.h"
#include "gfx/ptsprite.h"

#define NUM_STARS 110

struct star
{
	Vector3 pos;
	float size;
	Vector4 color;
};

class mcStarField : public parFileIO
{
public:
	mcStarField() { m_tex=NULL;}
	mcStarField	(class datResource &rsc);
	~mcStarField();
	void Draw(Matrix34 &mat);

	void FileIO(datParser&);
	const char *GetDirName() { return "tune/effects"; }
	const char *GetTypeName() {return "mcStarfield";}

private:

	gfxPS_PosScale pointSpritePositions[NUM_STARS] ALIGNED(64);
	gfxPS_RotFrame pointSpriteRotations[NUM_STARS] ALIGNED(64);
	unsigned pointSpriteColors[NUM_STARS] ALIGNED(64);

	star m_stars[NUM_STARS];
	gfxTexture *m_tex;

	float m_sizeMod;
	Vector4 m_colorMod;
};

#endif