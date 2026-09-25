#ifndef SND_MANAGER_H
#define SND_MANAGER_H

#include "core/output.h"
#include "core/types.h"

struct IDirectSound8;

enum {
    ERRORS = 0,
    WARNINGS = 1,
    STATUS = 2,
    TMI = 3
};

// Speaker configuration.
enum {
    SYSTEM_OUTPUT_MONO = 0,
    SYSTEM_OUTPUT_STEREO = 1,
    SYSTEM_OUTPUT_SURROUND = 2,
    SYSTEM_OUTPUT_HEADPHONES = 3,
    SYSTEM_OUTPUT_HEADPHONE = SYSTEM_OUTPUT_HEADPHONES,
    SYSTEM_OUTPUT_DEFAULT = SYSTEM_OUTPUT_STEREO
};

// Default stream buffer size (bytes) the game sizes its stream pool from.
#define AGESND_STREAM_SIZE (64 * 1024)

class sndManager {
public:
    // PC PORT: recorded here, read per frame by the voices' I3DL2 reverb DMOs
    // (snd_control/control.cpp).  The PC backend has no debug print level and
    // no DSP send bus, but mcAudioManager drives both from ordinary play -
    // SetDspFXType from the reverb block in Update(), SetDebugPrintLevel from the
    // audio bank - so neither may fault.  (See sndControl::SetGlobalReverbVolume,
    // the send level into the bus these set up.)
    static void SetDebugPrintLevel(int level) { sm_DebugPrintLevel = level; }
    static int GetDebugPrintLevel() { return sm_DebugPrintLevel; }
    // PC PORT: the driver layer.  On PS2 this class is the IOP interface - it
    // queues commands into sm_SendBuff and ships them over RPC.  Here the driver
    // is DirectSound and sndControl writes its buffers directly, so what survives
    // is the lifecycle, which the alpha drives from:
    //     sndAudioManager::Start        -> Begin()
    //     sndAudioManager::Update       -> Update()
    //     ~sndAudioManager              -> ShutdownClass(), End()
    // Our sndAudioManager folds Start into its constructor, so Begin/End hang off
    // the constructor and destructor instead.
    static void Begin();
    static void End();
    static bool IsInitialized() { return sm_Initialized; }
    // The alpha body is `if (sm_Initialized) { SendCommands(false); UpdateStatus(); }`
    // - flush the queued command buffer to the IOP and read its status block back.
    // Nothing to flush here: every setter reaches the voice immediately
    // (sndControl::ApplyBufferParams), and DirectSound mixes on its own thread.
    // Kept as a call so the alpha's 15 call sites can be restored verbatim.
    static void Update() {}
    // DSP effect (reverb) on a bus: type, feedback, delay.
    static void SetDspFXType(int bus, int type, float feedback, float delay)
    {
        (void)bus;
        sm_DspFXType = type; sm_DspFXFeedback = feedback; sm_DspFXDelay = delay;
    }
    // The reverb environment, read per frame by sndControl's I3DL2 DMOs.
    // type is 0..8, feedback and delay 0..127 (mcAudioManager::AddReverb).
    static int GetDspFXType() { return sm_DspFXType; }
    static float GetDspFXFeedback() { return sm_DspFXFeedback; }
    static float GetDspFXDelay() { return sm_DspFXDelay; }

private:
    static bool sm_Initialized;
    static int sm_DebugPrintLevel;
    static int sm_DspFXType;
    static float sm_DspFXFeedback;
    static float sm_DspFXDelay;
};

class sndAudioManager {
public:
    static sndAudioManager *smInstance;
    static bool AudioEnabled() { return true; }

    sndAudioManager(bool AudioEnabled = true);
    sndAudioManager(bool AudioEnabled, bool streaming, int streamPoolSize) : sndAudioManager(AudioEnabled) { (void)streaming; (void)streamPoolSize; }
    virtual ~sndAudioManager();

    static sndAudioManager* GetInstance() { return smInstance; }
    static void UpdateInstance();
    static void CreateInstance(bool audioEnabled = true) { if (!smInstance) new sndAudioManager(audioEnabled); }
    static void SetOutputMode(int mode) { sm_OutputMode = mode; }
    static int GetOutputMode() { return sm_OutputMode; }
    static void Stop();                                  // stop every voice
    static void AddWidgets(class bkBank &bank);

    virtual void PauseOn();
    virtual void PauseOff();
    virtual void Update();
    virtual int AmbientRoomPlay(const char* name, float volume = -1.0f);
    virtual int AmbientRoomPlayBlend(const char* name, float blendTime, float volume);

    struct IDirectSound8* GetDSound() const { return m_pDS; }
    bool IsAudioEnabled() const { return mAudioEnabled; }

protected:
    bool mAudioEnabled;
    struct IDirectSound8* m_pDS;
    static int sm_OutputMode;
};

#endif // SND_MANAGER_H
