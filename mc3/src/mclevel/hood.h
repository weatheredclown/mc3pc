#ifndef	LEVEL_HOOD_H
#define	LEVEL_HOOD_H



class	mcCityModel;
class	mcInstCityModel;



class	mcHood
{
	friend class mcCity;
	template <typename T> friend struct datResourceBuilder;

	private:
		char			*m_pName;
		int				m_nNumCityModels;
		mcCityModel		**m_ppCityModels;
		int				m_nNumInstCityModels;
		mcInstCityModel	**m_ppInstCityModels;

	public:
						mcHood					(void);
						mcHood					(class datResource &rsc);
						~mcHood					(void);
		void			Init					(const char *pName);
		void			LoadModels				(void);
		void			DeleteModels			(void);
		const	char	*GetName				(void) const { return(m_pName); }
		int				GetNumCityModels		(void) const { return(m_nNumCityModels); }
		int				GetNumInstCityModels	(void) const { return(m_nNumInstCityModels); }
		mcCityModel		*GetCityModel			(const int nModelIndex) const { return(m_ppCityModels[nModelIndex]); }
		mcInstCityModel	*GetInstCityModel		(const int nModelIndex) const { return(m_ppInstCityModels[nModelIndex]); }

	private:
		void			AllocCityModels			(const int nNumModels);
		void			AllocInstCityModels		(const int nNumModels);
};



#endif
