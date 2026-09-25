#ifndef SND_CONTROL_VOLUMEGROUP_H
#define SND_CONTROL_VOLUMEGROUP_H

#include "core/types.h"

class datParser;

class sndControlVolumeGroup {
public:
    static sndControlVolumeGroup* Create(bool dynamic, const char *name);
    static void LoadTuning();
    static sndControlVolumeGroup* GetFirst() { return sm_First; }

    sndControlVolumeGroup(bool muteOnPause = false, const char *name = nullptr, bool pauseOnSilence = false);
    virtual ~sndControlVolumeGroup();

    sndControlVolumeGroup* GetNext() const { return mNext; }
    void SetVolume(float volume) { mVolume = volume; }
    float GetVolume() const { return mVolume; }
    void SetFrequency(float f) { mFrequency = f; }
    float GetFrequency() const { return mFrequency; }
    virtual float CalcVolume() const { return mVolume; }
    virtual void FileIOInternal(datParser &dat) {}
    virtual void Pause(bool pause = true) {}
    void SetChanged(int flags = 1) {}

    enum { VOLUME = 1, FREQUENCY = 2 };

    // Bank hooks (subclasses extend the group's widgets).
    virtual void AddWidgets(class bkBank &bank);
    virtual void VolumeWidgetChangedCallback() {}
    virtual void FrequencyWidgetChangedCallback() {}
    void Release();

public:
    union {
        float m_Volume;
        float mVolume;
    };
    union {
        float m_Frequency;
        float mFrequency;
    };
    union {
        bool m_Dynamic;
        bool mDynamic;
    };
    char m_Name[64];
    union {
        sndControlVolumeGroup* m_Next;
        sndControlVolumeGroup* mNext;
    };
    bool mMuteOnPause;
    bool mPauseOnSilence;

    static sndControlVolumeGroup* sm_First;
    static sndControlVolumeGroup* smFirst;
};

#endif // SND_CONTROL_VOLUMEGROUP_H
