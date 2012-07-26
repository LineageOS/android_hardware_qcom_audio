/* AudioHardwareALSA.h
 **
 ** Copyright 2008-2010, Wind River Systems
 ** Copyright (c) 2011-2012, Code Aurora Forum. All rights reserved.
 **
 ** Licensed under the Apache License, Version 2.0 (the "License");
 ** you may not use this file except in compliance with the License.
 ** You may obtain a copy of the License at
 **
 **     http://www.apache.org/licenses/LICENSE-2.0
 **
 ** Unless required by applicable law or agreed to in writing, software
 ** distributed under the License is distributed on an "AS IS" BASIS,
 ** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 ** See the License for the specific language governing permissions and
 ** limitations under the License.
 */

#ifndef ANDROID_AUDIO_HARDWARE_ALSA_H
#define ANDROID_AUDIO_HARDWARE_ALSA_H

#include <utils/List.h>
#include <hardware_legacy/AudioHardwareBase.h>

#include <hardware_legacy/AudioHardwareInterface.h>
#include <hardware_legacy/AudioSystemLegacy.h>
#include <system/audio.h>
#include <hardware/audio.h>
#include <utils/threads.h>
#ifdef QCOM_USBAUDIO_ENABLED
#include <AudioUsbALSA.h>
#endif
#include <sys/poll.h>
#include <sys/eventfd.h>

extern "C" {
    #include <sound/asound.h>
    #include <sound/compress_params.h>
    #include <sound/compress_offload.h>
    #include "alsa_audio.h"
    #include "msm8960_use_cases.h"
}

#include <hardware/hardware.h>

namespace android_audio_legacy
{
using android::List;
using android::Mutex;
using android::Condition;
class AudioHardwareALSA;

/**
 * The id of ALSA module
 */
#define ALSA_HARDWARE_MODULE_ID "alsa"
#define ALSA_HARDWARE_NAME      "alsa"

#define DEFAULT_SAMPLING_RATE 48000
#define DEFAULT_CHANNEL_MODE  2
#define VOICE_SAMPLING_RATE   8000
#define VOICE_CHANNEL_MODE    1
#define PLAYBACK_LATENCY      96000
#define RECORD_LATENCY        96000
#define VOICE_LATENCY         85333
#define DEFAULT_BUFFER_SIZE   2048
#define DEFAULT_IN_BUFFER_SIZE   320
#define FM_BUFFER_SIZE        1024

#define VOIP_SAMPLING_RATE_8K 8000
#define VOIP_SAMPLING_RATE_16K 16000
#define VOIP_DEFAULT_CHANNEL_MODE  1
#define VOIP_BUFFER_SIZE_8K    320
#define VOIP_BUFFER_SIZE_16K   640
#define VOIP_BUFFER_MAX_SIZE   VOIP_BUFFER_SIZE_16K
#define VOIP_PLAYBACK_LATENCY      6400
#define VOIP_RECORD_LATENCY        6400

#define MODE_IS127              0x2
#define MODE_4GV_NB             0x3
#define MODE_4GV_WB             0x4
#define MODE_AMR                0x5
#define MODE_AMR_WB             0xD
#define MODE_PCM                0xC

#define DUALMIC_KEY         "dualmic_enabled"
#define ANC_KEY             "anc_enabled"
#define TTY_MODE_KEY        "tty_mode"
#define BT_SAMPLERATE_KEY   "bt_samplerate"
#define BTHEADSET_VGS       "bt_headset_vgs"
#define WIDEVOICE_KEY       "wide_voice_enable"
#define VOIPRATE_KEY        "voip_rate"
#define FENS_KEY            "fens_enable"
#define ST_KEY              "st_enable"
#define INCALLMUSIC_KEY     "incall_music_enabled"

#define ANC_FLAG        0x00000001
#define DMIC_FLAG       0x00000002
#define QMIC_FLAG       0x00000004
#ifdef QCOM_SSR_ENABLED
#define SSRQMIC_FLAG    0x00000008
#endif

#define TTY_OFF         0x00000010
#define TTY_FULL        0x00000020
#define TTY_VCO         0x00000040
#define TTY_HCO         0x00000080
#define TTY_CLEAR       0xFFFFFF0F

#define LPA_SESSION_ID 1
#define TUNNEL_SESSION_ID 2
#ifdef QCOM_USBAUDIO_ENABLED
static int USBPLAYBACKBIT_MUSIC = (1 << 0);
static int USBPLAYBACKBIT_VOICECALL = (1 << 1);
static int USBPLAYBACKBIT_VOIPCALL = (1 << 2);
static int USBPLAYBACKBIT_FM = (1 << 3);
static int USBPLAYBACKBIT_LPA = (1 << 4);

static int USBRECBIT_REC = (1 << 0);
static int USBRECBIT_VOICECALL = (1 << 1);
static int USBRECBIT_VOIPCALL = (1 << 2);
static int USBRECBIT_FM = (1 << 3);
#endif

#define DEVICE_SPEAKER_HEADSET "Speaker Headset"
#define DEVICE_HEADSET "Headset"
#define DEVICE_HEADPHONES "Headphones"

#ifdef QCOM_SSR_ENABLED
#define COEFF_ARRAY_SIZE          4
#define FILT_SIZE                 ((512+1)* 6)    /* # ((FFT bins)/2+1)*numOutputs */
#define SSR_FRAME_SIZE            512
#define SSR_INPUT_FRAME_SIZE      (SSR_FRAME_SIZE * 4)
#define SSR_OUTPUT_FRAME_SIZE     (SSR_FRAME_SIZE * 6)
#endif

#define MODE_CALL_KEY  "CALL_KEY"
#ifndef ALSA_DEFAULT_SAMPLE_RATE
#define ALSA_DEFAULT_SAMPLE_RATE 44100 // in Hz
#endif

#define NUM_FDS 2
#define AFE_PROXY_SAMPLE_RATE 48000
#define AFE_PROXY_CHANNEL_COUNT 2

static uint32_t FLUENCE_MODE_ENDFIRE   = 0;
static uint32_t FLUENCE_MODE_BROADSIDE = 1;
class ALSADevice;

enum {
    INCALL_REC_MONO,
    INCALL_REC_STEREO,
};

enum audio_call_mode {
    CS_INACTIVE   = 0x0,
    CS_ACTIVE     = 0x1,
    CS_HOLD       = 0x2,
    IMS_INACTIVE  = 0x0,
    IMS_ACTIVE    = 0x10,
    IMS_HOLD      = 0x20
};

class AudioSessionOutALSA;
struct alsa_handle_t {
    ALSADevice*         module;
    uint32_t            devices;
    char                useCase[MAX_STR_LEN];
    struct pcm *        handle;
    snd_pcm_format_t    format;
    uint32_t            channels;
    uint32_t            sampleRate;
    int                 mode;
    unsigned int        latency;         // Delay in usec
    unsigned int        bufferSize;      // Size of sample buffer
    unsigned int        periodSize;
    struct pcm *        rxHandle;
    snd_use_case_mgr_t  *ucMgr;
    AudioSessionOutALSA *session;
};

typedef List < alsa_handle_t > ALSAHandleList;

struct use_case_t {
    char                useCase[MAX_STR_LEN];
};

typedef List < use_case_t > ALSAUseCaseList;

class ALSADevice
{

public:

    ALSADevice();
    virtual ~ALSADevice();
//    status_t init(alsa_device_t *module, ALSAHandleList &list);
    status_t open(alsa_handle_t *handle);
    status_t close(alsa_handle_t *handle);
    status_t standby(alsa_handle_t *handle);
    status_t route(alsa_handle_t *handle, uint32_t devices, int mode);
    status_t startVoiceCall(alsa_handle_t *handle);
    status_t startVoipCall(alsa_handle_t *handle);
    status_t startFm(alsa_handle_t *handle);
    void     setVoiceVolume(int volume);
    void     setVoipVolume(int volume);
    void     setMicMute(int state);
    void     setVoipMicMute(int state);
    void     setVoipConfig(int mode, int rate);
    status_t setFmVolume(int vol);
    void     setBtscoRate(int rate);
    status_t setLpaVolume(int vol);
    void     enableWideVoice(bool flag);
    void     enableFENS(bool flag);
    void     setFlags(uint32_t flag);
    status_t setCompressedVolume(int vol);
    void     enableSlowTalk(bool flag);
    void     setVocRecMode(uint8_t mode);
    void     setVoLTEMicMute(int state);
    void     setVoLTEVolume(int vol);
    void     setSGLTEMicMute(int state);
    void     setSGLTEVolume(int vol);
    status_t setEcrxDevice(char *device);
    void     setInChannels(int);
    //TODO:check if this needs to be public
    void     disableDevice(alsa_handle_t *handle);
    char *getUCMDeviceFromAcdbId(int acdb_id);

protected:
    friend class AudioHardwareALSA;
private:
    void     switchDevice(alsa_handle_t *handle, uint32_t devices, uint32_t mode);
    int      getUseCaseType(const char *useCase);
    status_t setHardwareParams(alsa_handle_t *handle);
    int      deviceName(alsa_handle_t *handle, unsigned flags, char **value);
    status_t setSoftwareParams(alsa_handle_t *handle);
    bool     platform_is_Fusion3();
    status_t getMixerControl(const char *name, unsigned int &value, int index = 0);
    status_t setMixerControl(const char *name, unsigned int value, int index = -1);
    status_t setMixerControl(const char *name, const char *);
    status_t setMixerControlExt(const char *name, int count, char **setValues);
    char *   getUCMDevice(uint32_t devices, int input, char *rxDevice);
    status_t  start(alsa_handle_t *handle);

    status_t   openProxyDevice();
    status_t   closeProxyDevice();
    bool       isProxyDeviceOpened();
    bool       isProxyDeviceSuspended();
    bool       suspendProxy();
    bool       resumeProxy();
    void       resetProxyVariables();
    ssize_t    readFromProxy(void **captureBuffer , ssize_t *bufferSize);
    status_t   exitReadFromProxy();
    void       initProxyParams();
    status_t   startProxy();

private:
    char mMicType[25];
    char mCurRxUCMDevice[50];
    char mCurTxUCMDevice[50];
    uint32_t mFluenceMode;
    int mFmVolume;
    uint32_t mDevSettingsFlag;
    int mBtscoSamplerate;
    bool mPflag;
    ALSAUseCaseList mUseCaseList;
    int mCallMode;
    struct mixer*  mMixer;
    int mInChannels;
//   ALSAHandleList  *mDeviceList;

    struct proxy_params {
        bool                mExitRead;
        struct pcm          *mProxyPcmHandle;
        uint32_t            mCaptureBufferSize;
        void                *mCaptureBuffer;
        enum {
            EProxyClosed    = 0,
            EProxyOpened    = 1,
            EProxySuspended = 2,
            EProxyCapture   = 3,
        };

        uint32_t mProxyState;
        struct snd_xferi mX;
        unsigned mAvail;
        struct pollfd mPfdProxy[NUM_FDS];
        long mFrames;
    };
    struct proxy_params mProxyParams;

};

// ----------------------------------------------------------------------------

class ALSAMixer
{
public:
    ALSAMixer();
    virtual                ~ALSAMixer();

    bool                    isValid() { return 1;}
    status_t                setMasterVolume(float volume);
    status_t                setMasterGain(float gain);

    status_t                setVolume(uint32_t device, float left, float right);
    status_t                setGain(uint32_t device, float gain);

    status_t                setCaptureMuteState(uint32_t device, bool state);
    status_t                getCaptureMuteState(uint32_t device, bool *state);
    status_t                setPlaybackMuteState(uint32_t device, bool state);
    status_t                getPlaybackMuteState(uint32_t device, bool *state);

};

class ALSAStreamOps
{
public:
    ALSAStreamOps(AudioHardwareALSA *parent, alsa_handle_t *handle);
    virtual            ~ALSAStreamOps();

    status_t            set(int *format, uint32_t *channels, uint32_t *rate, uint32_t device);

    status_t            setParameters(const String8& keyValuePairs);
    String8             getParameters(const String8& keys);

    uint32_t            sampleRate() const;
    size_t              bufferSize() const;
    int                 format() const;
    uint32_t            channels() const;

    status_t            open(int mode);
    void                close();

protected:
    friend class AudioHardwareALSA;

    AudioHardwareALSA *     mParent;
    alsa_handle_t *         mHandle;
    uint32_t                mDevices;
};

// ----------------------------------------------------------------------------

class AudioStreamOutALSA : public AudioStreamOut, public ALSAStreamOps
{
public:
    AudioStreamOutALSA(AudioHardwareALSA *parent, alsa_handle_t *handle);
    virtual            ~AudioStreamOutALSA();

    virtual uint32_t    sampleRate() const
    {
        return ALSAStreamOps::sampleRate();
    }

    virtual size_t      bufferSize() const
    {
        return ALSAStreamOps::bufferSize();
    }

    virtual uint32_t    channels() const;

    virtual int         format() const
    {
        return ALSAStreamOps::format();
    }

    virtual uint32_t    latency() const;

    virtual ssize_t     write(const void *buffer, size_t bytes);
    virtual status_t    dump(int fd, const Vector<String16>& args);

    status_t            setVolume(float left, float right);

    virtual status_t    standby();

    virtual status_t    setParameters(const String8& keyValuePairs) {
        return ALSAStreamOps::setParameters(keyValuePairs);
    }

    virtual String8     getParameters(const String8& keys) {
        return ALSAStreamOps::getParameters(keys);
    }

    // return the number of audio frames written by the audio dsp to DAC since
    // the output has exited standby
    virtual status_t    getRenderPosition(uint32_t *dspFrames);

    status_t            open(int mode);
    status_t            close();

private:
    uint32_t            mFrameCount;
    uint32_t            mUseCase;

protected:
    AudioHardwareALSA *     mParent;
};

// ----------------------------------------------------------------------------

class AudioSessionOutALSA : public AudioStreamOut
{
public:
    AudioSessionOutALSA(AudioHardwareALSA *parent,
                        uint32_t   devices,
                        int        format,
                        uint32_t   channels,
                        uint32_t   samplingRate,
                        int        type,
                        status_t   *status);
    virtual            ~AudioSessionOutALSA();

    virtual uint32_t    sampleRate() const
    {
        return mSampleRate;
    }

    virtual size_t      bufferSize() const
    {
        return mBufferSize;
    }

    virtual uint32_t    channels() const
    {
        return mChannels;
    }

    virtual int         format() const
    {
        return mFormat;
    }

    virtual uint32_t    latency() const;

    virtual ssize_t     write(const void *buffer, size_t bytes);

    virtual status_t    start();
    virtual status_t    pause();
    virtual status_t    flush();
    virtual status_t    stop();

    virtual status_t    dump(int fd, const Vector<String16>& args);

    status_t            setVolume(float left, float right);

    virtual status_t    standby();

    virtual status_t    setParameters(const String8& keyValuePairs);

    virtual String8     getParameters(const String8& keys);


    // return the number of audio frames written by the audio dsp to DAC since
    // the output has exited standby
    virtual status_t    getRenderPosition(uint32_t *dspFrames);

    virtual status_t    getNextWriteTimestamp(int64_t *timestamp);

    virtual status_t    setObserver(void *observer);

    virtual status_t    getBufferInfo(buf_info **buf);
    virtual status_t    isBufferAvailable(int *isAvail);

    status_t            pause_l();
    status_t            resume_l();

private:
    Mutex               mLock;
    uint32_t            mFrameCount;
    uint32_t            mSampleRate;
    uint32_t            mChannels;
    size_t              mBufferSize;
    int                 mFormat;
    uint32_t            mStreamVol;

    bool                mPaused;
    bool                mSeeking;
    bool                mReachedEOS;
    bool                mSkipWrite;
    bool                mEosEventReceived;
    AudioHardwareALSA  *mParent;
    alsa_handle_t *     mAlsaHandle;
    ALSADevice *     mAlsaDevice;
    snd_use_case_mgr_t *mUcMgr;
    AudioEventObserver *mObserver;

    uint32_t            mUseCase;
    status_t            openDevice(char *pUseCase, bool bIsUseCase, int devices);

    status_t            closeDevice(alsa_handle_t *pDevice);
    void                createEventThread();
    void                bufferAlloc(alsa_handle_t *handle);
    void                bufferDeAlloc();
    bool                isReadyToPostEOS(int errPoll, void *fd);
    status_t            drain();
    status_t            openAudioSessionDevice(int type, int devices);
    // make sure the event thread also exited
    void                requestAndWaitForEventThreadExit();
    int32_t             writeToDriver(char *buffer, int bytes);
    static void *       eventThreadWrapper(void *me);
    void                eventThreadEntry();
    void                reset();

    //Structure to hold mem buffer information
    class BuffersAllocated {
    public:
        BuffersAllocated(void *buf1, int32_t nSize) :
        memBuf(buf1), memBufsize(nSize), bytesToWrite(0)
        {}
        void* memBuf;
        int32_t memBufsize;
        uint32_t bytesToWrite;
    };
    List<BuffersAllocated> mEmptyQueue;
    List<BuffersAllocated> mFilledQueue;
    List<BuffersAllocated> mBufPool;

    //Declare all the threads
    pthread_t mEventThread;

    //Declare the condition Variables and Mutex
    Mutex mEmptyQueueMutex;
    Mutex mFilledQueueMutex;

    Condition mWriteCv;
    Condition mEventCv;
    bool mKillEventThread;
    bool mEventThreadAlive;
    int mInputBufferSize;
    int mInputBufferCount;

    //event fd to signal the EOS and Kill from the userspace
    int mEfd;

#ifdef USE_A2220
    int mA2220Fd = -1;
    int mA2220Mode = A2220_PATH_INCALL_RECEIVER_NSOFF;
    Mutex mA2220Lock;

    int a2220_ctl(int mode);
#endif

public:
    bool mRouteAudioToA2dp;
};


class AudioStreamInALSA : public AudioStreamIn, public ALSAStreamOps
{
public:
    AudioStreamInALSA(AudioHardwareALSA *parent,
            alsa_handle_t *handle,
            AudioSystem::audio_in_acoustics audio_acoustics);
    virtual            ~AudioStreamInALSA();

    virtual uint32_t    sampleRate() const
    {
        return ALSAStreamOps::sampleRate();
    }

    virtual size_t      bufferSize() const
    {
        return ALSAStreamOps::bufferSize();
    }

    virtual uint32_t    channels() const
    {
        return ALSAStreamOps::channels();
    }

    virtual int         format() const
    {
        return ALSAStreamOps::format();
    }

    virtual ssize_t     read(void* buffer, ssize_t bytes);
    virtual status_t    dump(int fd, const Vector<String16>& args);

    virtual status_t    setGain(float gain);

    virtual status_t    standby();

    virtual status_t    setParameters(const String8& keyValuePairs)
    {
        return ALSAStreamOps::setParameters(keyValuePairs);
    }

    virtual String8     getParameters(const String8& keys)
    {
        return ALSAStreamOps::getParameters(keys);
    }

    // Return the amount of input frames lost in the audio driver since the last call of this function.
    // Audio driver is expected to reset the value to 0 and restart counting upon returning the current value by this function call.
    // Such loss typically occurs when the user space process is blocked longer than the capacity of audio driver buffers.
    // Unit: the number of input audio frames
    virtual unsigned int  getInputFramesLost() const;

    virtual status_t addAudioEffect(effect_handle_t effect)
    {
        return BAD_VALUE;
    }
   
    virtual status_t removeAudioEffect(effect_handle_t effect)
    {
        return BAD_VALUE;
    }
    status_t            setAcousticParams(void* params);

    status_t            open(int mode);
    status_t            close();
#ifdef QCOM_SSR_ENABLED
    // Helper function to initialize the Surround Sound library.
    status_t initSurroundSoundLibrary(unsigned long buffersize);
#endif

private:
    void                resetFramesLost();

    unsigned int        mFramesLost;
    AudioSystem::audio_in_acoustics mAcoustics;

#ifdef QCOM_SSR_ENABLED
    // Function to read coefficients from files.
    status_t            readCoeffsFromFile();

    FILE                *mFp_4ch;
    FILE                *mFp_6ch;
    int16_t             **mRealCoeffs;
    int16_t             **mImagCoeffs;
    void                *mSurroundObj;

    int16_t             *mSurroundInputBuffer;
    int16_t             *mSurroundOutputBuffer;
    int                 mSurroundInputBufferIdx;
    int                 mSurroundOutputBufferIdx;
#endif

protected:
    AudioHardwareALSA *     mParent;
};

class AudioHardwareALSA : public AudioHardwareBase
{
public:
    AudioHardwareALSA();
    virtual            ~AudioHardwareALSA();

    /**
     * check to see if the audio hardware interface has been initialized.
     * return status based on values defined in include/utils/Errors.h
     */
    virtual status_t    initCheck();

    /** set the audio volume of a voice call. Range is between 0.0 and 1.0 */
    virtual status_t    setVoiceVolume(float volume);

    /**
     * set the audio volume for all audio activities other than voice call.
     * Range between 0.0 and 1.0. If any value other than NO_ERROR is returned,
     * the software mixer will emulate this capability.
     */
    virtual status_t    setMasterVolume(float volume);
#ifdef QCOM_FM_ENABLED
    virtual status_t    setFmVolume(float volume);
#endif
    /**
     * setMode is called when the audio mode changes. NORMAL mode is for
     * standard audio playback, RINGTONE when a ringtone is playing, and IN_CALL
     * when a call is in progress.
     */
    virtual status_t    setMode(int mode);

    // mic mute
    virtual status_t    setMicMute(bool state);
    virtual status_t    getMicMute(bool* state);

    // set/get global audio parameters
    virtual status_t    setParameters(const String8& keyValuePairs);
    virtual String8     getParameters(const String8& keys);

    // Returns audio input buffer size according to parameters passed or 0 if one of the
    // parameters is not supported
    virtual size_t    getInputBufferSize(uint32_t sampleRate, int format, int channels);

#ifdef QCOM_TUNNEL_LPA_ENABLED
    /** This method creates and opens the audio hardware output
      *  session for LPA */
    virtual AudioStreamOut* openOutputSession(
            uint32_t devices,
            int *format,
            status_t *status,
            int sessionId,
            uint32_t samplingRate=0,
            uint32_t channels=0);
    virtual void closeOutputSession(AudioStreamOut* out);
#endif

    /** This method creates and opens the audio hardware output stream */
    virtual AudioStreamOut* openOutputStream(
            uint32_t devices,
            audio_output_flags_t flags,
            int *format=0,
            uint32_t *channels=0,
            uint32_t *sampleRate=0,
            status_t *status=0);
    virtual    void        closeOutputStream(AudioStreamOut* out);

    /** This method creates and opens the audio hardware input stream */
    virtual AudioStreamIn* openInputStream(
            uint32_t devices,
            int *format,
            uint32_t *channels,
            uint32_t *sampleRate,
            status_t *status,
            AudioSystem::audio_in_acoustics acoustics);
    virtual    void        closeInputStream(AudioStreamIn* in);

    status_t    startA2dpPlayback(uint32_t activeUsecase);
    status_t    stopA2dpPlayback(uint32_t activeUsecase);
    bool        suspendA2dpPlayback(uint32_t activeUsecase);

    status_t    startA2dpPlayback_l(uint32_t activeUsecase);
    status_t    stopA2dpPlayback_l(uint32_t activeUsecase);
    bool        suspendA2dpPlayback_l(uint32_t activeUsecase);

    /**This method dumps the state of the audio hardware */
    //virtual status_t dumpState(int fd, const Vector<String16>& args);

    static AudioHardwareInterface* create();

    int                 mode()
    {
        return mMode;
    }

    void pauseIfUseCaseTunnelOrLPA();
    void resumeIfUseCaseTunnelOrLPA();

private:
    status_t     openA2dpOutput();
    status_t     closeA2dpOutput();
    status_t     stopA2dpThread();
    void       a2dpThreadFunc();
    static void*        a2dpThreadWrapper(void *context);
    void        setA2DPActiveUseCases_l(uint32_t activeUsecase);
    uint32_t    getA2DPActiveUseCases_l();
    void        clearA2DPActiveUseCases_l(uint32_t activeUsecase);
    uint32_t    useCaseStringToEnum(const char *usecase);

protected:
    virtual status_t    dump(int fd, const Vector<String16>& args);
    virtual uint32_t    getVoipMode(int format);
    status_t            doRouting(int device);
#ifdef QCOM_FM_ENABLED
    void                handleFm(int device);
#endif
#ifdef QCOM_USBAUDIO_ENABLED
    void                closeUSBPlayback();
    void                closeUSBRecording();
    void                closeUsbRecordingIfNothingActive();
    void                closeUsbPlaybackIfNothingActive();
    void                startUsbPlaybackIfNotStarted();
    void                startUsbRecordingIfNotStarted();
#endif
    void                setInChannels(int device);

    void                disableVoiceCall(char* verb, char* modifier, int mode, int device);
    void                enableVoiceCall(char* verb, char* modifier, int mode, int device);
    bool                routeVoiceCall(int device, int	newMode);
    bool                routeVoLTECall(int device, int newMode);
    friend class AudioSessionOutALSA;
    friend class AudioStreamOutALSA;
    friend class AudioStreamInALSA;
    friend class ALSAStreamOps;

    ALSADevice*     mALSADevice;

    ALSAHandleList      mDeviceList;

#ifdef QCOM_USBAUDIO_ENABLED
    AudioUsbALSA        *mAudioUsbALSA;
#endif

    Mutex                   mLock;

    snd_use_case_mgr_t *mUcMgr;

    int32_t            mCurDevice;
    /* The flag holds all the audio related device settings from
     * Settings and Qualcomm Settings applications */
    uint32_t            mDevSettingsFlag;
    uint32_t            mVoipStreamCount;
    bool                mVoipMicMute;
    uint32_t            mVoipBitRate;
    uint32_t            mIncallMode;

    bool                mMicMute;
    int mCSCallActive;
    int mVolteCallActive;
    int mCallState;
    int mIsFmActive;
    bool mBluetoothVGS;
    bool mFusion3Platform;
#ifdef QCOM_USBAUDIO_ENABLED
    int musbPlaybackState;
    int musbRecordingState;
#endif

    //A2DP variables
    audio_stream_out   *mA2dpStream;
    audio_hw_device_t  *mA2dpDevice;

    bool                mKillA2DPThread;
    bool                mA2dpThreadAlive;
    pthread_t           mA2dpThread;
    Mutex               mA2dpMutex;
    Condition           mA2dpCv;
    volatile bool       mIsA2DPEnabled;

    enum {
      USECASE_NONE = 0x0,
      USECASE_HIFI = 0x1,
      USECASE_HIFI_LOWLATENCY = 0x2,
      USECASE_HIFI_LOW_POWER = 0x4,
      USECASE_HIFI_TUNNEL = 0x8,
      USECASE_FM = 0x10,
    };
    uint32_t mA2DPActiveUseCases;

public:
    bool mRouteAudioToA2dp;
};

// ----------------------------------------------------------------------------

};        // namespace android_audio_legacy
#endif    // ANDROID_AUDIO_HARDWARE_ALSA_H
