#include "drawable.h"

#include "phbound/boundsphere.h"

#include "crskeleton/skeldata.h"

rmcShaderGroup *drwShaderModel::s_ParentShaderGroup = 0;
int drwShaderModel::s_ParentShaderCount = 0;

drwShaderModel::drwShaderModel()
{
	m_RefCount = 0;
	m_BonyData = 0;

	SetBound(0);
}

drwShaderModel::~drwShaderModel()
{
	if( m_Bound )
	{
		if( m_Bound->GetRefCount() )
		{
			m_Bound->Release();
			m_Bound = 0;
		}
		else 
		{
			delete m_Bound;
		}
	}

	if ( m_BonyData )
	{
		delete m_BonyData->m_pTagHash;
		delete m_BonyData->m_pAnimHash;
	}

	delete m_BonyData;
	m_BonyData = 0;
}

void drwShaderModel::Release()
{
	if ( m_RefCount > 0 )
	{
		--m_RefCount;
	}
}

bool drwShaderModel::Load( const char *name, rmcTypeFileParser *parser, bool configParser)
{
	// PC port: Load geometry via rmcDrawable, then parse bound/skel/bonetag sections
	static rmcTypeFileParser defaultParser;
	if ( !parser )
	{
		parser = &defaultParser;
		parser->Reset();
	}
	if ( configParser )
	{
		parser->RegisterLoader( "animation", "anim", datCallback(MFA1(drwShaderModel::LoadAnimations), this, 0, true) );
		parser->RegisterLoader( "bound", "bound", datCallback(MFA1(drwShaderModel::LoadBounds), this, 0, true) );
		parser->RegisterLoader( "skel", "skel", datCallback(MFA1(rmcDrawable::LoadSkel), this, 0, true) );
		parser->RegisterLoader( "bonetag", "all", datCallback(MFA1(drwShaderModel::LoadBoneTags), this, 0, true) );
	}
	parser->SetWarningSpew(false);

	char base[256];
	strncpy(base, name, sizeof(base) - 1);
	base[sizeof(base) - 1] = 0;
	size_t n = strlen(base);
	if (n > 5 && _stricmp(base + n - 5, ".type") == 0) base[n - 5] = 0;
	Stream *s = ASSET.Open(base, "type");
	if (!s) {
		if (m_SkeletonData && m_SkeletonData->GetNumBones() > 0) {
			// PC port: In resource mode (-resources), model geometry is resourced.
			m_Name = base;
			if (!m_Bound) {
				phBound *fallback = new phBoundSphere(m_Radius > 0.0f ? m_Radius : 2.0f);
				SetBound(fallback);
			}
			return true;
		}
		Warningf("drwShaderModel: can't open '%s.type'", base);
		return false;
	}
	datTokenizer tok;
	tok.Init(base, s);
	m_Name = base;
	bool ok = rmcDrawable::Load(tok);

	s->Seek(0);
	tok.Init(base, s);
	parser->ProcessTypeFile(tok);
	s->Close();

	// PC port: Ensure model has a valid bound
	if (!m_Bound) {
		phBound *fallback = new phBoundSphere(m_Radius > 0.0f ? m_Radius : 2.0f);
		SetBound(fallback);
	}

	return ok;
}

bool drwShaderModel::PreLoadBonetags(const char *basename, rmcTypeFileParser *parser)
{	
	static rmcTypeFileParser defaultParser;
	if ( !parser )
	{
		parser = &defaultParser;
		parser->Reset();
	}
	parser->RegisterLoader( "bonetag", "all", datCallback(MFA1(drwShaderModel::LoadBoneTags), this, 0, true) );
	parser->SetWarningSpew(false);

	bool ret = false;

	Stream *S = ASSET.Open(basename, "type");
	if (S) {
		datTokenizer T;
		T.Init(basename,S);
		
		ret = parser->ProcessTypeFile( T );

		S->Close();
		return ret;
	}
	else if (m_SkeletonData && m_SkeletonData->GetNumBones() > 0)
	{
		// PC port: In resource mode (-resources), bone tags and vehicle skeleton
		// are sourced from the resource pack. Populate the tag hash from skeleton bones.
		if (!m_BonyData)
			m_BonyData = new drwBonyData;
		if (!m_BonyData->m_pTagHash)
		{
			m_BonyData->m_pTagHash = HashTable::Create();
			m_BonyData->m_pTagHash->MakePermanent();
		}
		for (int i = 0; i < m_SkeletonData->GetNumBones(); i++)
		{
			const crBoneData *b = m_SkeletonData->GetBone(i);
			if (b && b->GetName()[0] && strncmp(b->GetName(), "bone_", 5) != 0)
			{
				m_BonyData->m_pTagHash->Insert(b->GetName(), (void*)(intptr_t)(i + 1));
			}
		}
		return true;
	}
	else
		return false;
}

void drwShaderModel::LoadBounds( rmcTypeFileCbData *data )
{
	char fileName[128];

	datTokenizer *T = data->m_T;

	T->GetToken(fileName, sizeof(fileName));
	phBound *pBound = phBound::LoadFromFile( fileName );

	if( !pBound )
	{
		Displayf( "bound file = %s - not found, creating fallback bound", fileName );
		pBound = new phBoundSphere(m_Radius > 0.0f ? m_Radius : 2.0f);
	}
	delete m_Bound;
	this->SetBound( pBound );
}

bool drwShaderModel::Load( datTokenizer &T, rmcTypeFileParser *parser, bool configParser)
{
	static rmcTypeFileParser defaultParser;
	if ( !parser )
	{
		parser = &defaultParser;
		parser->Reset();
	}
	if ( configParser )
	{
		parser->RegisterLoader( "bonetag", "all", datCallback(MFA1(drwShaderModel::LoadBoneTags), this, 0, true) );
		parser->RegisterLoader( "animation", "anim", datCallback(MFA1(drwShaderModel::LoadAnimations), this, 0, true) );

		parser->RegisterLoader( "bound", "bound", datCallback(MFA1(drwShaderModel::LoadBounds), this, 0, true) );

		parser->RegisterLoader( "skel", "skel", datCallback(MFA1(rmcDrawable::LoadSkel), this, 0, true) );
	}
	else
	{
		s_ParentShaderGroup = m_ShaderGroups;
	}
	parser->SetWarningSpew(false);

	bool retVal = rmcDrawable::Load( T, parser, configParser );

	if (!m_Bound) {
		SetBound(new phBoundSphere(m_Radius > 0.0f ? m_Radius : 2.0f));
	}

	s_ParentShaderGroup = 0;
	s_ParentShaderCount = 0;
	return retVal;
}

void drwShaderModel::LoadSkel( rmcTypeFileCbData *data )
{
	datTokenizer *T = data->m_T;
	char buf[128];
	T->GetToken(buf,sizeof(buf)); 
	if (m_SkeletonData)
	{
		return;
	}
	
	m_SkeletonData = NULL;
	if (stricmp(buf,"none")) {
		Stream *S = ASSET.Open(buf, "skel" );
		if (S) {
			m_SkeletonData = crSkeletonData::Create(S, buf);
			Assert(m_SkeletonData);
			S->Close();
		}
	}
}

void drwShaderModel::LoadMesh( rmcTypeFileCbData *data, bool useCpv)
{
	if ( useCpv == false )
	{
	}
	if ( s_ParentShaderGroup )
	{
		s_ShaderGroup = s_ParentShaderGroup;
		s_ShaderGroupCount = (s8) s_ParentShaderCount;
	}
	rmcDrawable::LoadMesh(data);
}

void drwShaderModel::Draw(const rmcShaderData *data,const Matrix34 &mtx,int bucket,int lod,atBitSet *enables, int ) const
{
	
	GetLodGroup().Draw(GetShaderGroup(0),data,&mtx,bucket,lod,enables);
	
}

void drwShaderModel::DrawCpv(const rmcShaderData *data,const Matrix34 &mtx,int bucket,int lod,int cpvIndex,int ) const
{
	while ( GetLodGroup().GetLod(lod).GetModel(0) == 0 && lod <= drwDrawable::LOD_L )
		lod++;

	rmcDrawable::DrawCpv( data, mtx, bucket, lod, cpvIndex );
	
}

void drwShaderModel::LoadBoneTags( rmcTypeFileCbData *data )
{
	if ( m_BonyData == 0 )
		m_BonyData = new drwBonyData;

	if ( m_BonyData->m_pTagHash == 0 )
	{
		m_BonyData->m_pTagHash = HashTable::Create();
		m_BonyData->m_pTagHash->MakePermanent();
	}

	int tagId = data->m_T->GetInt();
	m_BonyData->m_pTagHash->Insert( data->m_EntityName, (void *) (tagId + 1));
}

void drwShaderModel::LoadAnimations( rmcTypeFileCbData *data )
{
	if ( m_BonyData == 0 )
		m_BonyData = new drwBonyData;

	if ( m_BonyData->m_pAnimHash == 0 )
	{
		m_BonyData->m_pAnimHash = HashTable::Create();
		m_BonyData->m_pAnimHash->MakePermanent();
	}

	datTokenizer *T = data->m_T;
	T->MatchToken("{");
	T->MatchToken("Count");
	int animCount = T->GetInt();
	
	char animName[64];
	char fileName[128];
	for (int i = 0; i < animCount; ++i)
	{
		T->GetToken(animName, sizeof(animName));
		T->GetToken(fileName, sizeof(fileName));
		AddAnim( fileName, animName);
	}
	T->MatchToken("}");
}

void drwShaderModel::AddAnim( const char *, const char * )
{
}
