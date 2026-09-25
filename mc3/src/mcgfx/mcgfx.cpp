

#include	"mcGfx.h"

#include	"rmcore/texture.h"

mcGfx	*MCGFX = NULL;

// PC port: the game built these factories while it started up, out of code that is not part of
// this tree.  The city draw path only ever asks for the texture factory, and on PC that is the
// default gfx-backed one, so this is the whole of it.
mcGfx::mcGfx(void)
	: m_pModelFactory(NULL), m_pShaderFactory(NULL), m_pTextureFactory(NULL),
	  m_pCityShaderFactory(NULL), m_pCityTextureFactory(NULL), m_pCarModelFactory(NULL),
	  m_pCarShaderFactory(NULL), m_pCarTextureFactory(NULL), m_pShaderPath(NULL)
{
}

mcGfx::~mcGfx(void)
{
}

void	mcGfx::Init(void)
{
	rmcTextureFactory::InitClass();
	m_pTextureFactory = &rmcTextureFactory::GetInstance();
	m_pCityTextureFactory = m_pTextureFactory;
}
