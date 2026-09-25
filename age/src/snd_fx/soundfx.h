#ifndef SND_FX_SOUNDFX_H
#define SND_FX_SOUNDFX_H

////////////////////////////////////////
// snd_fx/soundfx.h
//
// sndSoundFxSetup - a named set of one-shot sounds ("Bank:SOUND" names)
//   with random or ordered picking.
// sndSoundFxSingleShot - plays one pick at a time through a voice.
// sndSoundFxSingleShot3d - the same, positioned (sndControl3d).
////////////////////////////////////////

#include "core/types.h"
#include "vector/vector3.h"

class bkBank;
class datTokenizer;
class sndControl;
class sndControl3d;
class sndControlVolumeGroup;

class sndSoundFxSetup {
public:
    enum { kNameLength = 48 };

    sndSoundFxSetup();
    virtual ~sndSoundFxSetup();

    // Reserve room for `maxSounds` names (optionally read from a file).
    virtual void Init(int maxSounds = 0, const char *filename = NULL);
    virtual void Add(const char *name);
    virtual void AddWidgets(bkBank &bank);
    virtual void SaveData(datTokenizer &t);
    virtual void LoadData(datTokenizer &t);

    // Random pick (or the ordered pick queued with SetNextOrderedPick).
    int Pick(int arg = 0);
    void Pick(char *&picked);
    void SetNextOrderedPick(int index) { mNextPick = index; }
    int GetNumSounds() const { return mNumSounds; }
    int GetMaxNumSounds() const { return mMaxSounds; }
    const char *GetSound(int i) const;
    static void ConvertName(char *dst, const char *src);

    int mNumSounds;

protected:
    int mMaxSounds;
    int mNextPick;
    char (*mNames)[kNameLength];
};

class sndSoundFxSingleShot : public sndSoundFxSetup {
public:
    sndSoundFxSingleShot();
    virtual ~sndSoundFxSingleShot();

    virtual void SetVolume(float v) { mVolume = v; }
    virtual void SetVolumeGroup(const sndControlVolumeGroup *grp) { mGroup = grp; }
    // index -1 = pick
    virtual void Play(int index = -1, float pitch = 1.0f, float volume = 1.0f);
    virtual void Play(const sndControlVolumeGroup *grp, int index = -1);
    virtual void Play(const Vector3 *pos, int index = -1);
    virtual void Play(const Vector3 &pos, int index = -1);
    virtual void Stop();
    virtual bool IsPlaying() const;

protected:
    virtual sndControl *GetVoice();   // created on first use
    sndControl *mVoice;
    const sndControlVolumeGroup *mGroup;
    float mVolume;
};

class sndSoundFxSingleShot3d : public sndSoundFxSingleShot {
public:
    sndSoundFxSingleShot3d();
    virtual ~sndSoundFxSingleShot3d() {}

    virtual void SetRadius(float r) { SetRadius(r * 0.25f, r); }
    virtual void SetRadius(float minR, float maxR);
    void SetPosition(const Vector3 &pos);
    void Play3D(const Vector3 &pos, const sndControlVolumeGroup *grp, int index = -1);
    virtual void Play(const Vector3 *pos, int index = -1);
    virtual void Play(const Vector3 &pos, int index = -1);

protected:
    virtual sndControl *GetVoice();
    Vector3 mPosition;
    float mInnerRadius;
    float mOuterRadius;
};

#endif // SND_FX_SOUNDFX_H
