#ifndef	MCCULLABLE_CULLABLE_H
#define	MCCULLABLE_CULLABLE_H

#include	"mccullable/cullabledef.h"

class	mcCullableType;

class	mcCullable
{
	private:
		mcCullable		*m_pPrev;
		mcCullable		*m_pNext;

		u8				m_bData0;
		u8				m_bData1;
		u8				m_bData2;
		u8				m_bData3;

		void			SetByte0	(const u8 bByte) { m_bData0	= bByte; }
		u8				GetByte0	(void) const { return(m_bData0);	}

	protected:
		mcCullableType	*m_pType;

	public:
						mcCullable	(void);
						mcCullable	(class datResource &rsc);
		virtual			~mcCullable	(void);
		void			SetPrev		(mcCullable *pCullable) { m_pPrev = pCullable; }
		mcCullable		*GetPrev	(void) const { return(m_pPrev); }
		void			SetNext		(mcCullable *pCullable) { m_pNext = pCullable; }
		mcCullable		*GetNext	(void) const { return(m_pNext); }
		void			SetType		(mcCullableType * pType) { m_pType = pType; }
		mcCullableType	*GetType	(void) const { return(m_pType); }

		s8				GetLOD		(void) const { return((s8)GetByte0()); }
		void			SetLOD		(s8 nLOD) { SetByte0(nLOD); }

		bool			IsEnabled	(void);
		void			Enable		(bool bOn);
		bool			SetClippingAndSphereTest	(void);

		virtual	const	Vector3	&GetCenter					(void) const;
		virtual	const	Vector3 &GetCullCenter				(void) const;
		virtual	float			GetRadius					(void) const;
		virtual	void			Render						(mcPassTypes ePassMask) = 0;

	protected:
		void					Init				(void);
		virtual	void			CalcLOD				(void);

	private:
		void			EnableRendering		(void);
		void			DisableRendering	(void);
};

#endif
