////////////////////////////////////////
// control.h
////////////////////////////////////////

#ifndef SND_CONTROL_CONTROL_H
#define SND_CONTROL_CONTROL_H

#include "core/output.h"
#include "core/types.h"
#include "vector/vector3.h"
#include "snd_control/volumegroup.h"

class CBankEntry;
class Matrix34;
class devCamera;
struct IDirectSoundBuffer;
struct IDirectSoundFXWavesReverb;

class sndControl {
public:
    sndControl();
    sndControl(const char *name);
    virtual ~sndControl();

    void SetSound(CBankEntry *entry);
    void SetSound(const char *name);
    void SetVolume(float volume);
    void SetFrequency(float freq);
    void SetPitch(float pitch) { SetFrequency(pitch); }        // frequency multiplier, 1.0 = as recorded
    void SetReverb(float amount) { mReverb = amount; }          // 0..1 send level into the environment reverb
    float GetReverb() const { return mReverb; }
    void SetDoppler(bool on) { mDoppler = on; }
    bool GetDoppler() const { return mDoppler; }
    void SetVolumeGroup(sndControlVolumeGroup *group);
    void SetVolumeGroup(const sndControlVolumeGroup *group) { SetVolumeGroup(const_cast<sndControlVolumeGroup*>(group)); }
    void SetPan(float pan);         // -1 (left) .. +1 (right)
    void SetLoop(bool loop);

    virtual void Play();
    virtual void Stop();
    virtual void Pause();
    virtual void Resume();

    virtual bool IsPlaying() const;
    virtual bool IsPlayingOnHardware() const { return IsPlaying(); }
    virtual bool IsPaused() const;

    float CalcVolume() const;       // control volume x volume group
    float CalcFrequency() const { return m_Frequency; }

    // Per-frame service for every live control: reaps finished voices,
    // re-applies group volume changes, and lets 3D controls re-pan. Driven by
    // sndAudioManager::Update.
    static void UpdateAll();
    static void PauseAllOn();       // system-wide pause (menu)
    static void PauseAllOff();
    // PC PORT: the wet level of the one environment reverb, driven by
    // mcAudioManager from the reverb state every frame and from CutsceneStart.
    // DirectSound has no send bus, so each eligible voice carries its own reverb
    // DMO and they all read this in step (snd_control/control.cpp, "reverb").
    static void SetGlobalReverbVolume(float vol) { sm_GlobalReverbVolume = vol; }
    static float GetGlobalReverbVolume() { return sm_GlobalReverbVolume; }
    // AGE 2.72 surface.
    static void StopAll();                       // stop every live control
    static void SetDebugNames(bool on) { sm_DebugNames = on; }
    static bool GetDebugNames() { return sm_DebugNames; }
    virtual void Update() { UpdateVoice(); }     // per-control service (UpdateAll calls it)
    float CalcPan() const { return m_Pan; }
    bool IsLooped() const { return m_Loop; }
    const char *GetSoundName() const { return m_SoundName; }

    // Change flags (mChanged): what must be re-applied to the voice.
    enum { VOLUME = 0x01, PAN = 0x02, FREQUENCY = 0x04, POSITION = 0x08, LOOP = 0x10 };

protected:
    u32 mChanged;               // change flags pending for the voice
    bool mStoppedByUser;        // Stop() came from the game, not from attenuation
    float mReverb = 0.0f;       // SetReverb send level (recorded)
    bool mDoppler = true;       // SetDoppler (recorded; applied by the 3d update)
    static bool sm_DebugNames;
    static float sm_GlobalReverbVolume;   // PC PORT: wet level of the environment reverb
    // Applies CalcVolume/pan/frequency to the live DirectSound buffer.
    void ApplyBufferParams();
    virtual void UpdateVoice();     // per-frame hook (3D panning lives in the subclass)

    char m_SoundName[64];
    float m_Volume;
    float m_Frequency;
    float m_Pan;
    sndControlVolumeGroup *m_VolumeGroup;
    bool m_IsPlaying;
    bool m_IsPaused;
    bool m_PausedBySystem;
    bool m_Loop;
    float m_PlayStartTime;
    u32 m_BaseSampleRate;
    IDirectSoundBuffer *m_pBuffer;
    // PC PORT: the voice's own reverb DMO (see sndControlApplyReverb).  DX8
    // effects are per buffer, not a send bus, so a voice that wants reverb
    // carries its own; NULL when it plays dry.
    IDirectSoundFXWavesReverb *m_pReverbFX;

    // Intrusive registry of all live controls (for UpdateAll / pause).
    sndControl *m_RegNext;
    sndControl *m_RegPrev;
    static sndControl *sm_RegHead;

private:
    void Register();
    void Unregister();
};

class sndControl3d : public sndControl {
public:
    sndControl3d(const char *name = NULL);
    virtual ~sndControl3d() {}

    static void AddFrameCamera(const Matrix34 *cam);
    static void AddFrameCamera(const devCamera *cam);
    // Head matrix plus an explicit ear position; `cutting` marks a camera cut (no doppler this frame).
    static void AddFrameCamera(const Matrix34 *cam, const Vector3 *earPos, bool cutting);
    static void AddFrameCamera(const Matrix34 *cam, const Vector3 *earPos) { AddFrameCamera(cam, earPos, false); }

    // Listener "heads": one per camera fed this frame (split screen).
    struct sndControl3dHead {
        Matrix34 m_Matrix;
        Vector3 m_AttenPosition;    // where distance attenuation is measured from
        Vector3 m_Velocity;
    };
    enum { MAX_HEADS = 4 };
    static sndControl3dHead sm_Heads[MAX_HEADS];
    static int sm_HeadCount;
    // Called once per frame before cameras are added; resets the head list.
    static void StartFrameUpdate();
    static void SetDefaultDopplerScale(float scale) { sm_DopplerScale = scale; }
    static float GetDefaultDopplerScale() { return sm_DopplerScale; }
    static void DebugDraw();
    virtual void PanOptionChanged() { Quitf("sndControl3d::PanOptionChanged - not implemented"); }

    void SetFlag3d(u32 flag) { mFlags |= flag; }
    void ClearFlag3d(u32 flag) { mFlags &= ~flag; }
    void SetPosition(const Vector3 &pos);
    void SetRadius(float inner, float outer);
    void GetRadius(float &inner, float &outer) const { inner = m_InnerRadius; outer = m_OuterRadius; }
    float GetRadius() const { return m_OuterRadius; }
    // How sharply this voice loses volume with distance.  1 is the plain
    // inverse-distance law; above 1 the sound goes quiet faster than the
    // geometry alone would say, below 1 it carries further.  The game turns it
    // down on sounds that have to be audible across a whole street.
    void SetRolloffFactor(float factor) { m_RolloffFactor = factor > 0.0f ? factor : 1.0f; }
    float GetRolloffFactor() const { return m_RolloffFactor; }

    // Recompute distance attenuation/pan from the current listener without
    // needing a live buffer (testatten drives this on an unplayed voice).
    virtual void Update3DCalc();
    float GetDistanceAtten() const { return mVolumeDistAtten; }

    virtual void Play();

    enum {
        PAN_3D = 0x01,
        DOPPLER_ENABLE = 0x02,
        HALF_DOPPLER = 0x04,
        DOUBLE_DOPPLER = 0x08,
        PAN_2D = 0x10,
        PAN_NONE = 0x20,
        PAN_MASK = PAN_3D | PAN_2D | PAN_NONE
    };
    enum { RADIUS = 0x100 };   // change flag: radii changed (with sndControl::VOLUME etc.)
    static float sm_DefaultInnerRadius;
    static float sm_DefaultOuterRadius;
    const Vector3 &GetPosition() const { return mPosition; }

protected:
    virtual void UpdateVoice();     // distance attenuation + stereo pan
    void Calc3d(float *atten, float *pan) const;
    void Apply3d();

    u32 mFlags;
    bool m_HasPosition;
    Vector3 mPosition;
    Vector3 mLastPosition;
    float m_InnerRadius;
    float m_RolloffFactor = 1.0f;   // see SetRolloffFactor
    float m_OuterRadius;
    float mVolumeDistAtten;         // last computed distance attenuation (0..1)
    u32 mEditPanFlags;              // bank: pan option under edit
    static float sm_DopplerScale;
};

typedef sndControl3d::sndControl3dHead sndControl3dHead;

// TEMP DEBUG: callers of Stop() may tag why they stopped a live voice; the
// tag is logged by the CUT diagnostic and cleared after each Stop().
void sndSetStopReason(const char *reason);

// Hard-stops any buffers still in the KeyOff release fade. Must be called
// before the DirectSound device is released.
void sndControlFlushReleases();

#ifndef DOPPLER_ENABLE
#define DOPPLER_ENABLE sndControl3d::DOPPLER_ENABLE
#endif

#endif // SND_CONTROL_CONTROL_H
