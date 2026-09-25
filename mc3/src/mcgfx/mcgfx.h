#ifndef	MC_GFX_H
#define	MC_GFX_H

#define CITY_PAGE_FILE_SLOT (123)

class	rmcModelFactory;
class	rmcShaderFactory;
class	rmcTextureFactory;

class	mcGfx
{
	private:
		rmcModelFactory		*m_pModelFactory;
		rmcShaderFactory	*m_pShaderFactory;
		rmcTextureFactory	*m_pTextureFactory;
		rmcShaderFactory	*m_pCityShaderFactory;
		rmcTextureFactory	*m_pCityTextureFactory;
		rmcModelFactory		*m_pCarModelFactory;
		rmcShaderFactory	*m_pCarShaderFactory;
		rmcTextureFactory	*m_pCarTextureFactory;
		const	char		*m_pShaderPath;

public:
							mcGfx				(void);
							~mcGfx				(void);
		void				Init				(void);

		rmcTextureFactory	*GetCityTextureFactory	(void) const { return(m_pCityTextureFactory); }

};

extern	mcGfx	*MCGFX;

#endif
