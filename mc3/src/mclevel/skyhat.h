#ifndef	MCLEVEL_SKYHAT_H
#define	MCLEVEL_SKYHAT_H

#include	"parse/fileio.h"

#include	"mccullable/cullableclass.h"
#include	"mccullable/cullable.h"
#include	"mclevel/conditions.h"

class	mcSkyHatClass;

typedef	CullableClass	< mcSkyHatClass, mcCullableClass, (dtREFLECTING_GROUND), SKYHAT_PRIORITY >	SkyHatClassBase;

class	rmcModel;
class	mcStarField;
class	sndSoundFxSingleShot;

class	mcSkyHatClass	:	public	SkyHatClassBase,	public	parFileIO
{
	private:
		enum					{ MAX_LAYERS = 8 };
		int						m_nNumLayers;
		rmcModel				*m_pModels[MAX_LAYERS];
		float					m_fYRotations[MAX_LAYERS];
		float					m_fYRotationSpeed[MAX_LAYERS];
		mcStarField				*m_pStars;
		sndSoundFxSingleShot	*m_pLightningSound;
		bool					m_triggerLightning1;
		bool					m_triggerLightning2;
		bool					m_triggerLightning3;
		float					m_lightningStartTime;
		float					m_lightningNextStart;
		int						m_lightningNum;
		bool					m_lightningGoing;
		int						m_lightningSeed;
		float					m_lightningAudioDelay;

		Vector3					m_clearColor;
		float					m_lightningStrength;
		float					m_boltDirection[3];
		float					m_boltTop;
		float					m_boltBottom;
		float					m_boltDist;
		Vector3					m_glowColor;
		Vector3					m_vOffset;
		Vector4					m_topColorMod;
		Vector4					m_bottomColorMod;

		public:
								mcSkyHatClass		(void);
								mcSkyHatClass		(class datResource &rsc);
								~mcSkyHatClass		(void);

		void					Reset				(void);
		void					Update				(float fDelta);
		void					SetModel			(int layer, rmcModel *m) { if (layer >= 0 && layer < MAX_LAYERS) { m_pModels[layer] = m; if (layer >= m_nNumLayers) m_nNumLayers = layer + 1; } }

		void					SetRenderStates		(const mcPassTypes ePassMask)	const;
		void					RestoreRenderStates	(const mcPassTypes ePassMask)	const;
		void					RenderAllTypes		(mcPassTypes ePassMask);

		float					GetLightningColor	(Vector3 &color)	const;

		void					Draw				(const Matrix34 *cam, const Vector4 &colorMod, bool bottom)	const;
	private:
		void					FileIO				(datParser &Parser);
};

#endif
