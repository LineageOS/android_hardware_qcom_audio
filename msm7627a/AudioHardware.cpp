/*
** Copyright 2008, The Android Open-Source Project
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

#include <math.h>

//#define LOG_NDEBUG 0
#define LOG_TAG "AudioHardwareMSM76XXA"
#include <utils/Log.h>
#include <utils/String8.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <cutils/properties.h> // for property_get

// hardware specific functions

#include "AudioHardware.h"
#ifdef QCOM_FM_ENABLED
extern "C" {
#include "HardwarePinSwitching.h"
}
#endif
//#include <media/AudioRecord.h>


#define COMBO_DEVICE_SUPPORTED // Headset speaker combo device not supported on this target
#define DUALMIC_KEY "dualmic_enabled"
#define TTY_MODE_KEY "tty_mode"
#define ECHO_SUPRESSION "ec_supported"

namespace android_audio_legacy {

#ifdef SRS_PROCESSING
void*       SRSParamsG = NULL;
void*       SRSParamsW = NULL;
void*       SRSParamsC = NULL;
void*       SRSParamsHP = NULL;
void*       SRSParamsP = NULL;
void*       SRSParamsHL = NULL;

#define SRS_PARAMS_G 1
#define SRS_PARAMS_W 2
#define SRS_PARAMS_C 4
#define SRS_PARAMS_HP 8
#define SRS_PARAMS_P 16
#define SRS_PARAMS_HL 32
#define SRS_PARAMS_ALL 0xFF

#endif /*SRS_PROCESSING*/

static int audpre_index, tx_iir_index;
static void * acoustic;
const uint32_t AudioHardware::inputSamplingRates[] = {
        8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000
};

static int get_audpp_filter(void);
static int msm72xx_enable_postproc(bool state);
#ifdef SRS_PROCESSING
static void msm72xx_enable_srs(int flags, bool state);
#endif /*SRS_PROCESSING*/
static int msm72xx_enable_preproc(bool state);

// Post processing paramters
static struct rx_iir_filter iir_cfg[3];
static struct adrc_filter adrc_cfg[3];
static struct mbadrc_filter mbadrc_cfg[3];
eqalizer equalizer[3];
static uint16_t adrc_flag[3];
static uint16_t mbadrc_flag[3];
static uint16_t eq_flag[3];
static uint16_t rx_iir_flag[3];
static uint16_t agc_flag[3];
static uint16_t ns_flag[3];
static uint16_t txiir_flag[3];
static bool audpp_filter_inited = false;
static bool adrc_filter_exists[3];
static bool mbadrc_filter_exists[3];
static int post_proc_feature_mask = 0;
static int new_post_proc_feature_mask = 0;
static bool hpcm_playback_in_progress = false;
#ifdef QCOM_TUNNEL_LPA_ENABLED
static bool lpa_playback_in_progress = false;
#endif

//Pre processing parameters
static struct tx_iir tx_iir_cfg[9];
static struct ns ns_cfg[9];
static struct tx_agc tx_agc_cfg[9];
static int enable_preproc_mask[9];

static int snd_device = -1;

#define PCM_OUT_DEVICE "/dev/msm_pcm_out"
#define PCM_IN_DEVICE "/dev/msm_pcm_in"
#define PCM_CTL_DEVICE "/dev/msm_pcm_ctl"
#define PREPROC_CTL_DEVICE "/dev/msm_preproc_ctl"
#define VOICE_MEMO_DEVICE "/dev/msm_voicememo"
#ifdef QCOM_FM_ENABLED
#define FM_DEVICE  "/dev/msm_fm"
#endif
#define BTHEADSET_VGS "bt_headset_vgs"
#ifdef QCOM_VOIP_ENABLED
#define MVS_DEVICE "/dev/msm_mvs"
#endif

static uint32_t SND_DEVICE_CURRENT=-1;
static uint32_t SND_DEVICE_HANDSET=-1;
static uint32_t SND_DEVICE_SPEAKER=-1;
static uint32_t SND_DEVICE_BT=-1;
static uint32_t SND_DEVICE_BT_EC_OFF=-1;
static uint32_t SND_DEVICE_HEADSET=-1;
static uint32_t SND_DEVICE_STEREO_HEADSET_AND_SPEAKER=-1;
static uint32_t SND_DEVICE_IN_S_SADC_OUT_HANDSET=-1;
static uint32_t SND_DEVICE_IN_S_SADC_OUT_SPEAKER_PHONE=-1;
static uint32_t SND_DEVICE_TTY_HEADSET=-1;
static uint32_t SND_DEVICE_TTY_HCO=-1;
static uint32_t SND_DEVICE_TTY_VCO=-1;
static uint32_t SND_DEVICE_CARKIT=-1;
static uint32_t SND_DEVICE_FM_SPEAKER=-1;
static uint32_t SND_DEVICE_FM_HEADSET=-1;
static uint32_t SND_DEVICE_NO_MIC_HEADSET=-1;
static uint32_t SND_DEVICE_FM_DIGITAL_STEREO_HEADSET=-1;
static uint32_t SND_DEVICE_FM_DIGITAL_SPEAKER_PHONE=-1;
static uint32_t SND_DEVICE_FM_DIGITAL_BT_A2DP_HEADSET=-1;
static uint32_t SND_DEVICE_FM_ANALOG_STEREO_HEADSET=-1;
static uint32_t SND_DEVICE_FM_ANALOG_STEREO_HEADSET_CODEC=-1;
// ----------------------------------------------------------------------------

AudioHardware::AudioHardware() :
    mInit(false), mMicMute(true), mBluetoothNrec(true), mBluetoothId(0),
    mOutput(0),mBluetoothVGS(false), mSndEndpoints(NULL), mCurSndDevice(-1), mDualMicEnabled(false)
#ifdef QCOM_FM_ENABLED
    ,mFmFd(-1),FmA2dpStatus(-1)
#endif
#ifdef QCOM_VOIP_ENABLED
,mVoipFd(-1), mNumVoipStreams(0),mDirectOutput(0)
#endif
{
   if (get_audpp_filter() == 0) {
           audpp_filter_inited = true;
   }

    m7xsnddriverfd = open("/dev/msm_snd", O_RDWR);
    if (m7xsnddriverfd >= 0) {
        int rc = ioctl(m7xsnddriverfd, SND_GET_NUM_ENDPOINTS, &mNumSndEndpoints);
        if (rc >= 0) {
            mSndEndpoints = new msm_snd_endpoint[mNumSndEndpoints];
            mInit = true;
            ALOGV("constructed (%d SND endpoints)", rc);
            struct msm_snd_endpoint *ept = mSndEndpoints;
            for (int cnt = 0; cnt < mNumSndEndpoints; cnt++, ept++) {
                ept->id = cnt;
                ioctl(m7xsnddriverfd, SND_GET_ENDPOINT, ept);
                ALOGV("cnt = %d ept->name = %s ept->id = %d\n", cnt, ept->name, ept->id);
#define CHECK_FOR(desc) if (!strcmp(ept->name, #desc)) SND_DEVICE_##desc = ept->id;
                CHECK_FOR(CURRENT);
                CHECK_FOR(HANDSET);
                CHECK_FOR(SPEAKER);
                CHECK_FOR(BT);
                CHECK_FOR(BT_EC_OFF);
                CHECK_FOR(HEADSET);
                CHECK_FOR(STEREO_HEADSET_AND_SPEAKER);
                CHECK_FOR(IN_S_SADC_OUT_HANDSET);
                CHECK_FOR(IN_S_SADC_OUT_SPEAKER_PHONE);
                CHECK_FOR(TTY_HEADSET);
                CHECK_FOR(TTY_HCO);
                CHECK_FOR(TTY_VCO);
#ifdef QCOM_FM_ENABLED
                CHECK_FOR(FM_DIGITAL_STEREO_HEADSET);
                CHECK_FOR(FM_DIGITAL_SPEAKER_PHONE);
                CHECK_FOR(FM_DIGITAL_BT_A2DP_HEADSET);
                CHECK_FOR(FM_ANALOG_STEREO_HEADSET);
                CHECK_FOR(FM_ANALOG_STEREO_HEADSET_CODEC);
#endif
#undef CHECK_FOR
            }
        }
        else ALOGE("Could not retrieve number of MSM SND endpoints.");

        int AUTO_VOLUME_ENABLED = 0; // setting disabled as default

        static const char *const path = "/system/etc/AutoVolumeControl.txt";
        int txtfd;
        struct stat st;
        char *read_buf;

        txtfd = open(path, O_RDONLY);
        if (txtfd < 0) {
            ALOGE("failed to open AUTO_VOLUME_CONTROL %s: %s (%d)",
                  path, strerror(errno), errno);
        }
        else {
            if (fstat(txtfd, &st) < 0) {
                ALOGE("failed to stat %s: %s (%d)",
                      path, strerror(errno), errno);
                close(txtfd);
            }

            read_buf = (char *) mmap(0, st.st_size,
                        PROT_READ | PROT_WRITE,
                        MAP_PRIVATE,
                        txtfd, 0);

            if (read_buf == MAP_FAILED) {
                ALOGE("failed to mmap parameters file: %s (%d)",
                      strerror(errno), errno);
                close(txtfd);
            }

            if(read_buf[0] =='1')
               AUTO_VOLUME_ENABLED = 1;

            munmap(read_buf, st.st_size);
            close(txtfd);
        }
        ALOGD("Auto Volume Enabled= %d", AUTO_VOLUME_ENABLED);
        ioctl(m7xsnddriverfd, SND_AVC_CTL, &AUTO_VOLUME_ENABLED);
        ioctl(m7xsnddriverfd, SND_AGC_CTL, &AUTO_VOLUME_ENABLED);
    } else
        ALOGE("Could not open MSM SND driver.");
}

AudioHardware::~AudioHardware()
{
    for (size_t index = 0; index < mInputs.size(); index++) {
        closeInputStream((AudioStreamIn*)mInputs[index]);
    }
    mInputs.clear();
#ifdef QCOM_VOIP_ENABLED
    mVoipInputs.clear();
#endif
    closeOutputStream((AudioStreamOut*)mOutput);
    delete [] mSndEndpoints;
    if (acoustic) {
        ::dlclose(acoustic);
        acoustic = 0;
    }
    if (m7xsnddriverfd > 0)
    {
      close(m7xsnddriverfd);
      m7xsnddriverfd = -1;
    }
    for (int index = 0; index < 9; index++) {
        enable_preproc_mask[index] = 0;
    }
    mInit = false;
}

status_t AudioHardware::initCheck()
{
    return mInit ? NO_ERROR : NO_INIT;
}

AudioStreamOut* AudioHardware::openOutputStream(
        uint32_t devices, audio_output_flags_t flags, int *format, uint32_t *channels, uint32_t *sampleRate, status_t *status)
{
    { // scope for the lock
        Mutex::Autolock lock(mLock);

#ifdef QCOM_VOIP_ENABLED
        // only one output stream allowed
        if (mOutput && (devices != AudioSystem::DEVICE_OUT_DIRECTOUTPUT)) {
            if (status) {
                *status = INVALID_OPERATION;
            }
            ALOGE(" AudioHardware::openOutputStream Only one output stream allowed \n");
        }
        if(devices == AudioSystem::DEVICE_OUT_DIRECTOUTPUT) {

            if(mDirectOutput == 0) {
                // open direct output stream
                ALOGV(" AudioHardware::openOutputStream Direct output stream \n");
                AudioStreamOutDirect* out = new AudioStreamOutDirect();
                status_t lStatus = out->set(this, devices, format, channels, sampleRate);
                if (status) {
                    *status = lStatus;
                }
                if (lStatus == NO_ERROR) {
                    mDirectOutput = out;
                    ALOGV(" \n set sucessful for AudioStreamOutDirect");
                } else {
                    ALOGE(" \n set Failed for AudioStreamOutDirect");
                    delete out;
                }
            }
            else
                ALOGE(" \n AudioHardware::AudioStreamOutDirect is already open");

            return mDirectOutput;
        }
        else
#endif
        {
            // create new output stream
            AudioStreamOutMSM72xx* out = new AudioStreamOutMSM72xx();
            status_t lStatus = out->set(this, devices, format, channels, sampleRate);
            if (status) {
                *status = lStatus;
            }
            if (lStatus == NO_ERROR) {
                mOutput = out;
            } else {
                delete out;
            }
        }
    }
    return mOutput;
}

void AudioHardware::closeOutputStream(AudioStreamOut* out) {
    Mutex::Autolock lock(mLock);
    if ((mOutput == 0
#ifdef QCOM_VOIP_ENABLED
      && mDirectOutput == 0
#endif
      ) || ((mOutput != out)
#ifdef QCOM_VOIP_ENABLED
      && (mDirectOutput != out)
#endif
)) {
        ALOGW("Attempt to close invalid output stream");
    }
    else if (mOutput == out) {
        delete mOutput;
        mOutput = 0;
    }
#ifdef QCOM_VOIP_ENABLED
    else if (mDirectOutput == out) {
        ALOGV(" deleting  mDirectOutput \n");
        delete mDirectOutput;
        mDirectOutput = 0;
    }
#endif
}

AudioStreamIn* AudioHardware::openInputStream(
        uint32_t devices, int *format, uint32_t *channels, uint32_t *sampleRate, status_t *status,
        AudioSystem::audio_in_acoustics acoustic_flags)
{
    // check for valid input source
    if (!AudioSystem::isInputDevice((AudioSystem::audio_devices)devices)) {
        return 0;
    }

    mLock.lock();
#ifdef QCOM_VOIP_ENABLED
    if(devices == AudioSystem::DEVICE_IN_COMMUNICATION) {
        ALOGV("Create Audio stream Voip \n");
        AudioStreamInVoip* inVoip = new AudioStreamInVoip();
        status_t lStatus = NO_ERROR;
        lStatus =  inVoip->set(this, devices, format, channels, sampleRate, acoustic_flags);
        if (status) {
            *status = lStatus;
        }
        if (lStatus != NO_ERROR) {
            ALOGE(" Error creating voip input \n");
            mLock.unlock();
            delete inVoip;
            return 0;
        }
        mVoipInputs.add(inVoip);
        mLock.unlock();
        return inVoip;
    } else
#endif
    {
        AudioStreamInMSM72xx* in = new AudioStreamInMSM72xx();
        status_t lStatus = in->set(this, devices, format, channels, sampleRate, acoustic_flags);
        if (status) {
            *status = lStatus;
        }
        if (lStatus != NO_ERROR) {
            mLock.unlock();
            delete in;
            return 0;
        }

        mInputs.add(in);
        mLock.unlock();
        return in;
    }

}

void AudioHardware::closeInputStream(AudioStreamIn* in) {
    Mutex::Autolock lock(mLock);

    ssize_t index = -1;
    if((index = mInputs.indexOf((AudioStreamInMSM72xx *)in)) >= 0) {
        ALOGV("closeInputStream AudioStreamInMSM72xx");
        mLock.unlock();
        delete mInputs[index];
        mLock.lock();
        mInputs.removeAt(index);
    }
#ifdef QCOM_VOIP_ENABLED
    else if ((index = mVoipInputs.indexOf((AudioStreamInVoip *)in)) >= 0) {
        ALOGV("closeInputStream mVoipInputs");
        mLock.unlock();
        delete mVoipInputs[index];
        mLock.lock();
        mVoipInputs.removeAt(index);
    }
#endif
    else {
        ALOGE("Attempt to close invalid input stream");
    }
}

#ifdef QCOM_TUNNEL_LPA_ENABLED
AudioStreamOut* AudioHardware::openOutputSession(
        uint32_t devices, int *format, status_t *status, int sessionId, uint32_t samplingRate,uint32_t channels)
{
    AudioSessionOutMSM7xxx* out;
    { // scope for the lock
        Mutex::Autolock lock(mLock);

        // create new output stream
        out = new AudioSessionOutMSM7xxx();
        status_t lStatus = out->set(this, devices, format, sessionId);
        if (status) {
            *status = lStatus;
        }
        if (lStatus != NO_ERROR) {
            delete out;
            out = NULL;
        }
    }
    return out;
}
#endif

status_t AudioHardware::setMode(int mode)
{
    status_t status = AudioHardwareBase::setMode(mode);
    if (status == NO_ERROR) {
        // make sure that doAudioRouteOrMute() is called by doRouting()
        // even if the new device selected is the same as current one.
        clearCurDevice();
    }
    return status;
}

bool AudioHardware::checkOutputStandby()
{
    if (mOutput)
        if (!mOutput->checkStandby())
            return false;

    return true;
}

status_t AudioHardware::setMicMute(bool state)
{
    Mutex::Autolock lock(mLock);
    return setMicMute_nosync(state);
}

// always call with mutex held
status_t AudioHardware::setMicMute_nosync(bool state)
{
    if (mMicMute != state) {
        mMicMute = state;
        return doAudioRouteOrMute(SND_DEVICE_CURRENT);
    }
    return NO_ERROR;
}

status_t AudioHardware::getMicMute(bool* state)
{
    *state = mMicMute;
    return NO_ERROR;
}

status_t AudioHardware::setParameters(const String8& keyValuePairs)
{
    AudioParameter param = AudioParameter(keyValuePairs);
    String8 value;
    String8 key;

    const char BT_NREC_KEY[] = "bt_headset_nrec";
    const char BT_NAME_KEY[] = "bt_headset_name";
    const char BT_NREC_VALUE_ON[] = "on";
#ifdef SRS_PROCESSING
    int to_set=0;
    ALOGV("setParameters() %s", keyValuePairs.string());
    if(strncmp("SRS_Buffer", keyValuePairs.string(), 10) == 0) {
        int SRSptr = 0;
        String8 keySRSG  = String8("SRS_BufferG"), keySRSW  = String8("SRS_BufferW"),
          keySRSC  = String8("SRS_BufferC"), keySRSHP = String8("SRS_BufferHP"),
          keySRSP  = String8("SRS_BufferP"), keySRSHL = String8("SRS_BufferHL");
        if (param.getInt(keySRSG, SRSptr) == NO_ERROR) {
            SRSParamsG = (void*)SRSptr;
            to_set |= SRS_PARAMS_G;
        } else if (param.getInt(keySRSW, SRSptr) == NO_ERROR) {
            SRSParamsW = (void*)SRSptr;
            to_set |= SRS_PARAMS_W;
        } else if (param.getInt(keySRSC, SRSptr) == NO_ERROR) {
            SRSParamsC = (void*)SRSptr;
            to_set |= SRS_PARAMS_C;
        } else if (param.getInt(keySRSHP, SRSptr) == NO_ERROR) {
            SRSParamsHP = (void*)SRSptr;
            to_set |= SRS_PARAMS_HP;
        } else if (param.getInt(keySRSP, SRSptr) == NO_ERROR) {
            SRSParamsP = (void*)SRSptr;
            to_set |= SRS_PARAMS_P;
        } else if (param.getInt(keySRSHL, SRSptr) == NO_ERROR) {
            SRSParamsHL = (void*)SRSptr;
            to_set |= SRS_PARAMS_HL;
        }

        ALOGD("SetParam SRS flags=0x%x", to_set);

        if(hpcm_playback_in_progress
#ifdef QCOM_TUNNEL_LPA_ENABLED
         || lpa_playback_in_progress
#endif
        ) {
            msm72xx_enable_srs(to_set, true);
        }

        if(SRSptr)
            return NO_ERROR;

    }
#endif /*SRS_PROCESSING*/
    if (keyValuePairs.length() == 0) return BAD_VALUE;

    key = String8(BT_NREC_KEY);
    if (param.get(key, value) == NO_ERROR) {
        if (value == BT_NREC_VALUE_ON) {
            mBluetoothNrec = true;
        } else {
            mBluetoothNrec = false;
            ALOGI("Turning noise reduction and echo cancellation off for BT "
                 "headset");
        }
    }
    key = String8(BTHEADSET_VGS);
    if (param.get(key, value) == NO_ERROR) {
        if (value == BT_NREC_VALUE_ON) {
            mBluetoothVGS = true;
        } else {
            mBluetoothVGS = false;
        }
    }
    key = String8(BT_NAME_KEY);
    if (param.get(key, value) == NO_ERROR) {
        mBluetoothId = 0;
        for (int i = 0; i < mNumSndEndpoints; i++) {
            if (!strcasecmp(value.string(), mSndEndpoints[i].name)) {
                mBluetoothId = mSndEndpoints[i].id;
                ALOGI("Using custom acoustic parameters for %s", value.string());
                break;
            }
        }
        if (mBluetoothId == 0) {
            ALOGI("Using default acoustic parameters "
                 "(%s not in acoustic database)", value.string());
            doRouting(NULL);
        }
    }

    key = String8(DUALMIC_KEY);
    if (param.get(key, value) == NO_ERROR) {
        if (value == "true") {
            mDualMicEnabled = true;
            ALOGI("DualMike feature Enabled");
        } else {
            mDualMicEnabled = false;
            ALOGI("DualMike feature Disabled");
        }
        doRouting(NULL);
    }

    key = String8(TTY_MODE_KEY);
    if (param.get(key, value) == NO_ERROR) {
        if (value == "full") {
            mTtyMode = TTY_FULL;
        } else if (value == "hco") {
            mTtyMode = TTY_HCO;
        } else if (value == "vco") {
            mTtyMode = TTY_VCO;
        } else {
            mTtyMode = TTY_OFF;
        }
        if(mMode != AudioSystem::MODE_IN_CALL){
           return NO_ERROR;
        }
        ALOGI("Changed TTY Mode=%s", value.string());
        if((mMode == AudioSystem::MODE_IN_CALL) &&
           (mCurSndDevice == SND_DEVICE_HEADSET))
           doRouting(NULL);
    }

    return NO_ERROR;
}

String8 AudioHardware::getParameters(const String8& keys)
{
    AudioParameter param = AudioParameter(keys);
    String8 value;

    String8 key = String8(DUALMIC_KEY);

    if (param.get(key, value) == NO_ERROR) {
        value = String8(mDualMicEnabled ? "true" : "false");
        param.add(key, value);
    }

    key = String8(BTHEADSET_VGS);
    if (param.get(key, value) == NO_ERROR) {
        if(mBluetoothVGS)
           param.addInt(String8("isVGS"), true);
    }

#if 0
    key = String8("tunneled-input-formats");
    if ( param.get(key,value) == NO_ERROR ) {
        param.addInt(String8("AMR"), true );
        if (mMode == AudioSystem::MODE_IN_CALL) {
            param.addInt(String8("QCELP"), true );
            param.addInt(String8("EVRC"), true );
        }
    }
#endif
#ifdef QCOM_FM_ENABLED
    key = String8("Fm-radio");
    if ( param.get(key,value) == NO_ERROR ) {
        if (IsFmon()||(mCurSndDevice == SND_DEVICE_FM_ANALOG_STEREO_HEADSET)){
            param.addInt(String8("isFMON"), true );
        }
    }
#endif
    key = String8(ECHO_SUPRESSION);
    if (param.get(key, value) == NO_ERROR) {
        value = String8("yes");
        param.add(key, value);
    }

    ALOGV("AudioHardware::getParameters() %s", param.toString().string());
    return param.toString();
}

int check_and_set_audpp_parameters(char *buf, int size)
{
    char *p, *ps;
    static const char *const seps = ",";
    int table_num;
    int i, j;
    int device_id = 0;
    int samp_index = 0;
    eq_filter_type eq[12];
    int fd;
    void *audioeq;
    void *(*eq_cal)(int32_t, int32_t, int32_t, uint16_t, int32_t, int32_t *, int32_t *, uint16_t *);
    uint16_t numerator[6];
    uint16_t denominator[4];
    uint16_t shift[2];

    if ((buf[0] == 'A') && ((buf[1] == '1') || (buf[1] == '2') || (buf[1] == '3'))) {
        /* IIR filter */
        if(buf[1] == '1') device_id=0;
        if(buf[1] == '2') device_id=1;
        if(buf[1] == '3') device_id=2;
        if (!(p = strtok(buf, ",")))
            goto token_err;

        /* Table header */
        table_num = strtol(p + 1, &ps, 10);
        if (!(p = strtok(NULL, seps)))
            goto token_err;
        /* Table description */
        if (!(p = strtok(NULL, seps)))
            goto token_err;

        for (i = 0; i < 48; i++) {
            iir_cfg[device_id].iir_params[i] = (uint16_t)strtol(p, &ps, 16);
            if (!(p = strtok(NULL, seps)))
                goto token_err;
        }
        rx_iir_flag[device_id] = (uint16_t)strtol(p, &ps, 16);
        if (!(p = strtok(NULL, seps)))
            goto token_err;
        iir_cfg[device_id].num_bands = (uint16_t)strtol(p, &ps, 16);

    } else if ((buf[0] == 'B') && ((buf[1] == '1') || (buf[1] == '2') || (buf[1] == '3'))) {
        /* This is the ADRC record we are looking for.  Tokenize it */
        if(buf[1] == '1') device_id=0;
        if(buf[1] == '2') device_id=1;
        if(buf[1] == '3') device_id=2;
        adrc_filter_exists[device_id] = true;
        if (!(p = strtok(buf, ",")))
            goto token_err;

        /* Table header */
        table_num = strtol(p + 1, &ps, 10);
        if (!(p = strtok(NULL, seps)))
            goto token_err;

        /* Table description */
        if (!(p = strtok(NULL, seps)))
            goto token_err;
        adrc_flag[device_id] = (uint16_t)strtol(p, &ps, 16);

        if (!(p = strtok(NULL, seps)))
            goto token_err;
        adrc_cfg[device_id].adrc_params[0] = (uint16_t)strtol(p, &ps, 16);

        if (!(p = strtok(NULL, seps)))
            goto token_err;
        adrc_cfg[device_id].adrc_params[1] = (uint16_t)strtol(p, &ps, 16);

        if (!(p = strtok(NULL, seps)))
            goto token_err;
        adrc_cfg[device_id].adrc_params[2] = (uint16_t)strtol(p, &ps, 16);

        if (!(p = strtok(NULL, seps)))
            goto token_err;
        adrc_cfg[device_id].adrc_params[3] = (uint16_t)strtol(p, &ps, 16);

        if (!(p = strtok(NULL, seps)))
            goto token_err;
        adrc_cfg[device_id].adrc_params[4] = (uint16_t)strtol(p, &ps, 16);

        if (!(p = strtok(NULL, seps)))
            goto token_err;
        adrc_cfg[device_id].adrc_params[5] = (uint16_t)strtol(p, &ps, 16);

        if (!(p = strtok(NULL, seps)))
            goto token_err;
        adrc_cfg[device_id].adrc_params[6] = (uint16_t)strtol(p, &ps, 16);

        if (!(p = strtok(NULL, seps)))
            goto token_err;
        adrc_cfg[device_id].adrc_params[7] = (uint16_t)strtol(p, &ps, 16);

        if (!(p = strtok(NULL, seps)))
            goto token_err;

    } else if (buf[0] == 'C' && ((buf[1] == '1') || (buf[1] == '2') || (buf[1] == '3'))) {
        /* This is the EQ record we are looking for.  Tokenize it */
        if(buf[1] == '1') device_id=0;
        if(buf[1] == '2') device_id=1;
        if(buf[1] == '3') device_id=2;
        if (!(p = strtok(buf, ",")))
            goto token_err;

        /* Table header */
        table_num = strtol(p + 1, &ps, 10);
        if (!(p = strtok(NULL, seps)))
            goto token_err;
        /* Table description */
        if (!(p = strtok(NULL, seps)))
            goto token_err;

        eq_flag[device_id] = (uint16_t)strtol(p, &ps, 16);
        if (!(p = strtok(NULL, seps)))
            goto token_err;
        ALOGI("EQ flag = %02x.", eq_flag[device_id]);

        audioeq = ::dlopen("/system/lib/libaudioeq.so", RTLD_NOW);
        if (audioeq == NULL) {
            ALOGE("audioeq library open failure");
            return -1;
        }
        eq_cal = (void *(*) (int32_t, int32_t, int32_t, uint16_t, int32_t, int32_t *, int32_t *, uint16_t *))::dlsym(audioeq, "audioeq_calccoefs");
        memset(&equalizer[device_id], 0, sizeof(eqalizer));
        /* Temp add the bands here */
        equalizer[device_id].bands = 8;
        for (i = 0; i < equalizer[device_id].bands; i++) {

            eq[i].gain = (uint16_t)strtol(p, &ps, 16);

            if (!(p = strtok(NULL, seps)))
                goto token_err;
            eq[i].freq = (uint16_t)strtol(p, &ps, 16);

            if (!(p = strtok(NULL, seps)))
                goto token_err;
            eq[i].type = (uint16_t)strtol(p, &ps, 16);

            if (!(p = strtok(NULL, seps)))
                goto token_err;
            eq[i].qf = (uint16_t)strtol(p, &ps, 16);

            if (!(p = strtok(NULL, seps)))
                goto token_err;

            eq_cal(eq[i].gain, eq[i].freq, 48000, eq[i].type, eq[i].qf, (int32_t*)numerator, (int32_t *)denominator, shift);
            for (j = 0; j < 6; j++) {
                equalizer[device_id].params[ ( i * 6) + j] = numerator[j];
            }
            for (j = 0; j < 4; j++) {
                equalizer[device_id].params[(equalizer[device_id].bands * 6) + (i * 4) + j] = denominator[j];
            }
            equalizer[device_id].params[(equalizer[device_id].bands * 10) + i] = shift[0];
        }
        ::dlclose(audioeq);

    } else if ((buf[0] == 'D') && ((buf[1] == '1') || (buf[1] == '2') || (buf[1] == '3'))) {
     /* This is the MB_ADRC record we are looking for.  Tokenize it */
        if(buf[1] == '1') device_id=0;
        if(buf[1] == '2') device_id=1;
        if(buf[1] == '3') device_id=2;
        mbadrc_filter_exists[device_id] = true;
        if (!(p = strtok(buf, ",")))
            goto token_err;
          /* Table header */
        table_num = strtol(p + 1, &ps, 10);
        if (!(p = strtok(NULL, seps)))
            goto token_err;

        /* Table description */
        if (!(p = strtok(NULL, seps)))
            goto token_err;
        mbadrc_cfg[device_id].num_bands = (uint16_t)strtol(p, &ps, 16);

        if (!(p = strtok(NULL, seps)))
            goto token_err;
        mbadrc_cfg[device_id].down_samp_level = (uint16_t)strtol(p, &ps, 16);

        if (!(p = strtok(NULL, seps)))
            goto token_err;
        mbadrc_cfg[device_id].adrc_delay = (uint16_t)strtol(p, &ps, 16);

        if (!(p = strtok(NULL, seps)))
            goto token_err;
        mbadrc_cfg[device_id].ext_buf_size = (uint16_t)strtol(p, &ps, 16);
        int ext_buf_count = mbadrc_cfg[device_id].ext_buf_size / 2;

        if (!(p = strtok(NULL, seps)))
            goto token_err;
        mbadrc_cfg[device_id].ext_partition = (uint16_t)strtol(p, &ps, 16);

        if (!(p = strtok(NULL, seps)))
            goto token_err;
        mbadrc_cfg[device_id].ext_buf_msw = (uint16_t)strtol(p, &ps, 16);

        if (!(p = strtok(NULL, seps)))
            goto token_err;
        mbadrc_cfg[device_id].ext_buf_lsw = (uint16_t)strtol(p, &ps, 16);

        for(i = 0;i < mbadrc_cfg[device_id].num_bands; i++) {
            for(j = 0; j < 10; j++) {
                if (!(p = strtok(NULL, seps)))
                    goto token_err;
                mbadrc_cfg[device_id].adrc_band[i].adrc_band_params[j] = (uint16_t)strtol(p, &ps, 16);
            }
        }

        for(i = 0;i < mbadrc_cfg[device_id].ext_buf_size/2; i++) {
            if (!(p = strtok(NULL, seps)))
                goto token_err;
            mbadrc_cfg[device_id].ext_buf.buff[i] = (uint16_t)strtol(p, &ps, 16);
        }
        if (!(p = strtok(NULL, seps)))
            goto token_err;

        mbadrc_flag[device_id] = (uint16_t)strtol(p, &ps, 16);
        ALOGV("MBADRC flag = %02x.", mbadrc_flag[device_id]);
    }else if ((buf[0] == 'E') || (buf[0] == 'F') || (buf[0] == 'G')){
     //Pre-Processing Features TX_IIR,NS,AGC
        switch (buf[1]) {
                case '1':
                        samp_index = 0;
                        break;
                case '2':
                        samp_index = 1;
                        break;
                case '3':
                        samp_index = 2;
                        break;
                case '4':
                        samp_index = 3;
                        break;
                case '5':
                        samp_index = 4;
                        break;
                case '6':
                        samp_index = 5;
                        break;
                case '7':
                        samp_index = 6;
                        break;
                case '8':
                        samp_index = 7;
                        break;
                case '9':
                        samp_index = 8;
                        break;
                default:
                        return -EINVAL;
                        break;
        }

        if (buf[0] == 'E')  {
        /* TX_IIR filter */
        if (!(p = strtok(buf, ","))){
            goto token_err;}

        /* Table header */
        table_num = strtol(p + 1, &ps, 10);
        if (!(p = strtok(NULL, seps))){
            goto token_err;}
        /* Table description */
        if (!(p = strtok(NULL, seps))){
            goto token_err;}

        for (i = 0; i < 48; i++) {
            j = (i >= 40)? i : ((i % 2)? (i - 1) : (i + 1));
            tx_iir_cfg[samp_index].iir_params[j] = (uint16_t)strtol(p, &ps, 16);
            if (!(p = strtok(NULL, seps))){
                goto token_err;}
        }

        tx_iir_cfg[samp_index].active_flag = (uint16_t)strtol(p, &ps, 16);
        if (!(p = strtok(NULL, seps))){
            goto token_err;}

        txiir_flag[device_id] = (uint16_t)strtol(p, &ps, 16);

        if (!(p = strtok(NULL, seps)))
            goto token_err;
        tx_iir_cfg[samp_index].num_bands = (uint16_t)strtol(p, &ps, 16);

        tx_iir_cfg[samp_index].cmd_id = 0;

        ALOGV("TX IIR flag = %02x.", txiir_flag[device_id]);
        if (txiir_flag[device_id] != 0)
             enable_preproc_mask[samp_index] |= TX_IIR_ENABLE;
        } else if(buf[0] == 'F')  {
        /* AGC filter */
        if (!(p = strtok(buf, ",")))
            goto token_err;

        /* Table header */
        table_num = strtol(p + 1, &ps, 10);
        if (!(p = strtok(NULL, seps)))
            goto token_err;
        /* Table description */
        if (!(p = strtok(NULL, seps)))
            goto token_err;

        tx_agc_cfg[samp_index].cmd_id = (uint16_t)strtol(p, &ps, 16);
        if (!(p = strtok(NULL, seps)))
            goto token_err;

        tx_agc_cfg[samp_index].tx_agc_param_mask = (uint16_t)strtol(p, &ps, 16);
        if (!(p = strtok(NULL, seps)))
            goto token_err;

        tx_agc_cfg[samp_index].tx_agc_enable_flag = (uint16_t)strtol(p, &ps, 16);
        if (!(p = strtok(NULL, seps)))
            goto token_err;

        tx_agc_cfg[samp_index].static_gain = (uint16_t)strtol(p, &ps, 16);
        if (!(p = strtok(NULL, seps)))
            goto token_err;

        tx_agc_cfg[samp_index].adaptive_gain_flag = (uint16_t)strtol(p, &ps, 16);
        if (!(p = strtok(NULL, seps)))
            goto token_err;

        for (i = 0; i < 19; i++) {
            tx_agc_cfg[samp_index].agc_params[i] = (uint16_t)strtol(p, &ps, 16);
            if (!(p = strtok(NULL, seps)))
                goto token_err;
            }

        agc_flag[device_id] = (uint16_t)strtol(p, &ps, 16);
        ALOGV("AGC flag = %02x.", agc_flag[device_id]);
        if (agc_flag[device_id] != 0)
            enable_preproc_mask[samp_index] |= AGC_ENABLE;
        } else if ((buf[0] == 'G')) {
        /* This is the NS record we are looking for.  Tokenize it */
        if (!(p = strtok(buf, ",")))
            goto token_err;

        /* Table header */
        table_num = strtol(p + 1, &ps, 10);
        if (!(p = strtok(NULL, seps)))
            goto token_err;

        /* Table description */
        if (!(p = strtok(NULL, seps)))
            goto token_err;
        ns_cfg[samp_index].cmd_id = (uint16_t)strtol(p, &ps, 16);

        if (!(p = strtok(NULL, seps)))
            goto token_err;
        ns_cfg[samp_index].ec_mode_new = (uint16_t)strtol(p, &ps, 16);

        if (!(p = strtok(NULL, seps)))
            goto token_err;
        ns_cfg[samp_index].dens_gamma_n = (uint16_t)strtol(p, &ps, 16);

        if (!(p = strtok(NULL, seps)))
            goto token_err;
        ns_cfg[samp_index].dens_nfe_block_size = (uint16_t)strtol(p, &ps, 16);

        if (!(p = strtok(NULL, seps)))
            goto token_err;
        ns_cfg[samp_index].dens_limit_ns = (uint16_t)strtol(p, &ps, 16);

        if (!(p = strtok(NULL, seps)))
            goto token_err;
        ns_cfg[samp_index].dens_limit_ns_d = (uint16_t)strtol(p, &ps, 16);

        if (!(p = strtok(NULL, seps)))
            goto token_err;
        ns_cfg[samp_index].wb_gamma_e = (uint16_t)strtol(p, &ps, 16);

        if (!(p = strtok(NULL, seps)))
            goto token_err;
        ns_cfg[samp_index].wb_gamma_n = (uint16_t)strtol(p, &ps, 16);

        if (!(p = strtok(NULL, seps)))
            goto token_err;
        ns_flag[device_id] = (uint16_t)strtol(p, &ps, 16);

        ALOGV("NS flag = %02x.", ns_flag[device_id]);
        if (ns_flag[device_id] != 0)
            enable_preproc_mask[samp_index] |= NS_ENABLE;
        }
    }
    return 0;

token_err:
    ALOGE("malformatted pcm control buffer");
    return -EINVAL;
}

static int get_audpp_filter(void)
{
    struct stat st;
    char *read_buf;
    char *next_str, *current_str;
    int csvfd;

    ALOGI("get_audpp_filter");
    static const char *const path =
        "/system/etc/AudioFilter.csv";
    csvfd = open(path, O_RDONLY);
    if (csvfd < 0) {
        /* failed to open normal acoustic file ... */
        ALOGE("failed to open AUDIO_NORMAL_FILTER %s: %s (%d).",
             path, strerror(errno), errno);
        return -1;
    } else ALOGI("open %s success.", path);

    if (fstat(csvfd, &st) < 0) {
        ALOGE("failed to stat %s: %s (%d).",
             path, strerror(errno), errno);
        close(csvfd);
        return -1;
    }

    read_buf = (char *) mmap(0, st.st_size,
                    PROT_READ | PROT_WRITE,
                    MAP_PRIVATE,
                    csvfd, 0);

    if (read_buf == MAP_FAILED) {
        ALOGE("failed to mmap parameters file: %s (%d)",
             strerror(errno), errno);
        close(csvfd);
        return -1;
    }

    current_str = read_buf;

    while (*current_str != (char)EOF)  {
        int len;
        next_str = strchr(current_str, '\n');
        if (!next_str)
           break;
        len = next_str - current_str;
        *next_str++ = '\0';
        if (check_and_set_audpp_parameters(current_str, len)) {
            ALOGI("failed to set audpp parameters, exiting.");
            munmap(read_buf, st.st_size);
            close(csvfd);
            return -1;
        }
        current_str = next_str;
    }

    munmap(read_buf, st.st_size);
    close(csvfd);
    return 0;
}
#ifdef SRS_PROCESSING
static void msm72xx_enable_srs(int flags, bool state)
{
    int fd = open(PCM_CTL_DEVICE, O_RDWR);
    if (fd < 0) {
        ALOGE("Cannot open PCM Ctl device for srs params");
        return;
    }

    ALOGD("Enable SRS flags=0x%x state= %d",flags,state);
    if (state == false) {
        if(post_proc_feature_mask & SRS_ENABLE) {
            new_post_proc_feature_mask &= SRS_DISABLE;
            post_proc_feature_mask &= SRS_DISABLE;
        }
        if(SRSParamsG) {
            unsigned short int backup = ((unsigned short int*)SRSParamsG)[2];
            ((unsigned short int*)SRSParamsG)[2] = 0;
            ioctl(fd, AUDIO_SET_SRS_TRUMEDIA_PARAM, SRSParamsG);
            ((unsigned short int*)SRSParamsG)[2] = backup;
        }
    } else {
        new_post_proc_feature_mask |= SRS_ENABLE;
        post_proc_feature_mask |= SRS_ENABLE;
        if(SRSParamsW && (flags & SRS_PARAMS_W))
            ioctl(fd, AUDIO_SET_SRS_TRUMEDIA_PARAM, SRSParamsW);
        if(SRSParamsC && (flags & SRS_PARAMS_C))
            ioctl(fd, AUDIO_SET_SRS_TRUMEDIA_PARAM, SRSParamsC);
        if(SRSParamsHP && (flags & SRS_PARAMS_HP))
            ioctl(fd, AUDIO_SET_SRS_TRUMEDIA_PARAM, SRSParamsHP);
        if(SRSParamsP && (flags & SRS_PARAMS_P))
            ioctl(fd, AUDIO_SET_SRS_TRUMEDIA_PARAM, SRSParamsP);
        if(SRSParamsHL && (flags & SRS_PARAMS_HL))
            ioctl(fd, AUDIO_SET_SRS_TRUMEDIA_PARAM, SRSParamsHL);
        if(SRSParamsG && (flags & SRS_PARAMS_G))
            ioctl(fd, AUDIO_SET_SRS_TRUMEDIA_PARAM, SRSParamsG);
    }

    if (ioctl(fd, AUDIO_ENABLE_AUDPP, &post_proc_feature_mask) < 0) {
        ALOGE("enable audpp error");
    }

    close(fd);
}

#endif /*SRS_PROCESSING*/
static int msm72xx_enable_postproc(bool state)
{
    int fd;
    int device_id=0;

    char postProc[128];
    property_get("audio.legacy.postproc",postProc,"0");

    if(!(strcmp("true",postProc) == 0)){
        post_proc_feature_mask &= MBADRC_DISABLE;
        post_proc_feature_mask &= ADRC_DISABLE;
        post_proc_feature_mask &= EQ_DISABLE;
        post_proc_feature_mask &= RX_IIR_DISABLE;
        ALOGV("Legacy Post Proc disabled.");
        return 0;
    }

    if (!audpp_filter_inited)
    {
        ALOGE("Parsing error in AudioFilter.csv.");
        post_proc_feature_mask &= MBADRC_DISABLE;
        post_proc_feature_mask &= ADRC_DISABLE;
        post_proc_feature_mask &= EQ_DISABLE;
        post_proc_feature_mask &= RX_IIR_DISABLE;
        return -EINVAL;
    }
    if(snd_device < 0) {
        ALOGE("Enabling/Disabling post proc features for device: %d", snd_device);
        post_proc_feature_mask &= MBADRC_DISABLE;
        post_proc_feature_mask &= ADRC_DISABLE;
        post_proc_feature_mask &= EQ_DISABLE;
        post_proc_feature_mask &= RX_IIR_DISABLE;
        return -EINVAL;
    }

    if(snd_device == SND_DEVICE_SPEAKER)
    {
        device_id = 0;
        ALOGI("set device to SND_DEVICE_SPEAKER device_id=0");
    }
    if(snd_device == SND_DEVICE_HANDSET)
    {
        device_id = 1;
        ALOGI("set device to SND_DEVICE_HANDSET device_id=1");
    }
    if(snd_device == SND_DEVICE_HEADSET)
    {
        device_id = 2;
        ALOGI("set device to SND_DEVICE_HEADSET device_id=2");
    }

    fd = open(PCM_CTL_DEVICE, O_RDWR);
    if (fd < 0) {
        ALOGE("Cannot open PCM Ctl device");
        return -EPERM;
    }

    if(mbadrc_filter_exists[device_id] && state)
    {
        ALOGV("MBADRC Enabled");
        post_proc_feature_mask &= ADRC_DISABLE;
        if ((mbadrc_flag[device_id] == 0) && (post_proc_feature_mask & MBADRC_ENABLE))
        {
            ALOGV("MBADRC Disable");
            post_proc_feature_mask &= MBADRC_DISABLE;
        }
        else if(post_proc_feature_mask & MBADRC_ENABLE)
        {
            ALOGV("MBADRC Enabled %d", post_proc_feature_mask);

            if (ioctl(fd, AUDIO_SET_MBADRC, &mbadrc_cfg[device_id]) < 0)
            {
                ALOGE("set mbadrc filter error");
            }
        }
    }
    else if (adrc_filter_exists[device_id] && state)
    {
        post_proc_feature_mask &= MBADRC_DISABLE;
        ALOGV("ADRC Enabled %d", post_proc_feature_mask);

        if (adrc_flag[device_id] == 0 && (post_proc_feature_mask & ADRC_ENABLE))
            post_proc_feature_mask &= ADRC_DISABLE;
        else if(post_proc_feature_mask & ADRC_ENABLE)
        {
            ALOGI("ADRC Filter ADRC FLAG = %02x.", adrc_flag[device_id]);
            ALOGI("ADRC Filter COMP THRESHOLD = %02x.", adrc_cfg[device_id].adrc_params[0]);
            ALOGI("ADRC Filter COMP SLOPE = %02x.", adrc_cfg[device_id].adrc_params[1]);
            ALOGI("ADRC Filter COMP RMS TIME = %02x.", adrc_cfg[device_id].adrc_params[2]);
            ALOGI("ADRC Filter COMP ATTACK[0] = %02x.", adrc_cfg[device_id].adrc_params[3]);
            ALOGI("ADRC Filter COMP ATTACK[1] = %02x.", adrc_cfg[device_id].adrc_params[4]);
            ALOGI("ADRC Filter COMP RELEASE[0] = %02x.", adrc_cfg[device_id].adrc_params[5]);
            ALOGI("ADRC Filter COMP RELEASE[1] = %02x.", adrc_cfg[device_id].adrc_params[6]);
            ALOGI("ADRC Filter COMP DELAY = %02x.", adrc_cfg[device_id].adrc_params[7]);
            if (ioctl(fd, AUDIO_SET_ADRC, &adrc_cfg[device_id]) < 0)
            {
                ALOGE("set adrc filter error.");
            }
        }
    }
    else
    {
        ALOGV("MBADRC and ADRC Disabled");
        post_proc_feature_mask &= (MBADRC_DISABLE | ADRC_DISABLE);
    }

    if (eq_flag[device_id] == 0 && (post_proc_feature_mask & EQ_ENABLE))
        post_proc_feature_mask &= EQ_DISABLE;
    else if ((post_proc_feature_mask & EQ_ENABLE) && state)
    {
        ALOGI("Setting EQ Filter");
        if (ioctl(fd, AUDIO_SET_EQ, &equalizer[device_id]) < 0) {
            ALOGE("set Equalizer error.");
        }
    }

    if (rx_iir_flag[device_id] == 0 && (post_proc_feature_mask & RX_IIR_ENABLE))
        post_proc_feature_mask &= RX_IIR_DISABLE;
    else if ((post_proc_feature_mask & RX_IIR_ENABLE)&& state)
    {
        ALOGI("IIR Filter FLAG = %02x.", rx_iir_flag[device_id]);
        ALOGI("IIR NUMBER OF BANDS = %02x.", iir_cfg[device_id].num_bands);
        ALOGI("IIR Filter N1 = %02x.", iir_cfg[device_id].iir_params[0]);
        ALOGI("IIR Filter N2 = %02x.",  iir_cfg[device_id].iir_params[1]);
        ALOGI("IIR Filter N3 = %02x.",  iir_cfg[device_id].iir_params[2]);
        ALOGI("IIR Filter N4 = %02x.",  iir_cfg[device_id].iir_params[3]);
        ALOGI("IIR FILTER M1 = %02x.",  iir_cfg[device_id].iir_params[24]);
        ALOGI("IIR FILTER M2 = %02x.", iir_cfg[device_id].iir_params[25]);
        ALOGI("IIR FILTER M3 = %02x.",  iir_cfg[device_id].iir_params[26]);
        ALOGI("IIR FILTER M4 = %02x.",  iir_cfg[device_id].iir_params[27]);
        ALOGI("IIR FILTER M16 = %02x.",  iir_cfg[device_id].iir_params[39]);
        ALOGI("IIR FILTER SF1 = %02x.",  iir_cfg[device_id].iir_params[40]);
         if (ioctl(fd, AUDIO_SET_RX_IIR, &iir_cfg[device_id]) < 0)
        {
            ALOGE("set rx iir filter error.");
        }
    }

    if(state){
        ALOGI("Enabling post proc features with mask 0x%04x", post_proc_feature_mask);
        if (ioctl(fd, AUDIO_ENABLE_AUDPP, &post_proc_feature_mask) < 0) {
            ALOGE("enable audpp error");
            close(fd);
            return -EPERM;
        }
    } else{
        if(post_proc_feature_mask & MBADRC_ENABLE) post_proc_feature_mask &= MBADRC_DISABLE;
        if(post_proc_feature_mask & ADRC_ENABLE) post_proc_feature_mask &= ADRC_DISABLE;
        if(post_proc_feature_mask & EQ_ENABLE) post_proc_feature_mask &= EQ_DISABLE;
        if(post_proc_feature_mask & RX_IIR_ENABLE) post_proc_feature_mask &= RX_IIR_DISABLE;

        ALOGI("disabling post proc features with mask 0x%04x", post_proc_feature_mask);
        if (ioctl(fd, AUDIO_ENABLE_AUDPP, &post_proc_feature_mask) < 0) {
            ALOGE("enable audpp error");
            close(fd);
            return -EPERM;
        }
   }

   close(fd);
   return 0;
}

static unsigned calculate_audpre_table_index(unsigned index)
{
    switch (index) {
        case 48000:    return SAMP_RATE_INDX_48000;
        case 44100:    return SAMP_RATE_INDX_44100;
        case 32000:    return SAMP_RATE_INDX_32000;
        case 24000:    return SAMP_RATE_INDX_24000;
        case 22050:    return SAMP_RATE_INDX_22050;
        case 16000:    return SAMP_RATE_INDX_16000;
        case 12000:    return SAMP_RATE_INDX_12000;
        case 11025:    return SAMP_RATE_INDX_11025;
        case 8000:    return SAMP_RATE_INDX_8000;
        default:     return -1;
    }
}
size_t AudioHardware::getInputBufferSize(uint32_t sampleRate, int format, int channelCount)
{
    if ( (format != AudioSystem::PCM_16_BIT) &&
         (format != AudioSystem::AMR_NB)     &&
         (format != AudioSystem::AAC)){
        ALOGW("getInputBufferSize bad format: 0x%x", format);
        return 0;
    }
    if (channelCount < 1 || channelCount > 2) {
        ALOGW("getInputBufferSize bad channel count: %d", channelCount);
        return 0;
    }

    if(format == AudioSystem::AMR_NB)
       return 320*channelCount;
    else if (format == AudioSystem::AAC)
       return 2048;
#ifdef QCOM_VOIP_ENABLED
    else if (sampleRate == AUDIO_HW_VOIP_SAMPLERATE_8K)
       return 320*channelCount;
    else if (sampleRate == AUDIO_HW_VOIP_SAMPLERATE_16K)
       return 640*channelCount;
#endif
    else
       return 2048*channelCount;
}

static status_t set_volume_rpc(uint32_t device,
                               uint32_t method,
                               uint32_t volume,
                               int m7xsnddriverfd)
{

    ALOGD("rpc_snd_set_volume(%d, %d, %d)\n", device, method, volume);

    if (device == -1UL) return NO_ERROR;

    if (m7xsnddriverfd < 0) {
        ALOGE("Can not open snd device");
        return -EPERM;
    }
    /* rpc_snd_set_volume(
     *     device,            # Any hardware device enum, including
     *                        # SND_DEVICE_CURRENT
     *     method,            # must be SND_METHOD_VOICE to do anything useful
     *     volume,            # integer volume level, in range [0,5].
     *                        # note that 0 is audible (not quite muted)
     *  )
     * rpc_snd_set_volume only works for in-call sound volume.
     */
     struct msm_snd_volume_config args;
     args.device = device;
     args.method = method;
     args.volume = volume;

     if (ioctl(m7xsnddriverfd, SND_SET_VOLUME, &args) < 0) {
         ALOGE("snd_set_volume error.");
         return -EIO;
     }
     return NO_ERROR;
}

status_t AudioHardware::setVoiceVolume(float v)
{
    if (v < 0.0) {
        ALOGW("setVoiceVolume(%f) under 0.0, assuming 0.0\n", v);
        v = 0.0;
    } else if (v > 1.0) {
        ALOGW("setVoiceVolume(%f) over 1.0, assuming 1.0\n", v);
        v = 1.0;
    }
    // Added 0.4 to current volume, as in voice call Mute cannot be set as minimum volume(0.00)
    // setting Rx volume level as 2 for minimum and 7 as max level.
    v = 0.4 + v;

    int vol = lrint(v * 5.0);
    ALOGD("setVoiceVolume(%f)\n", v);
    ALOGI("Setting in-call volume to %d (available range is 2 to 7)\n", vol);

    if ((mCurSndDevice != -1) && ((mCurSndDevice == SND_DEVICE_TTY_HEADSET) || (mCurSndDevice == SND_DEVICE_TTY_VCO)))
    {
        vol = 1;
        ALOGI("For TTY device in FULL or VCO mode, the volume level is set to: %d \n", vol);
    }

    Mutex::Autolock lock(mLock);
    set_volume_rpc(SND_DEVICE_CURRENT, SND_METHOD_VOICE, vol, m7xsnddriverfd);
    return NO_ERROR;
}

#ifdef QCOM_FM_ENABLED
status_t AudioHardware::setFmVolume(float v)
{
    if (v < 0.0) {
        ALOGW("setFmVolume(%f) under 0.0, assuming 0.0\n", v);
        v = 0.0;
    } else if (v > 1.0) {
        ALOGW("setFmVolume(%f) over 1.0, assuming 1.0\n", v);
        v = 1.0;
    }

    int vol = lrint(v * 7.5);
    if (vol > 7)
        vol = 7;
    ALOGD("setFmVolume(%f)\n", v);
    Mutex::Autolock lock(mLock);
    set_volume_rpc(SND_DEVICE_CURRENT, SND_METHOD_VOICE, vol, m7xsnddriverfd);
    return NO_ERROR;
}
#endif

status_t AudioHardware::setMasterVolume(float v)
{
    Mutex::Autolock lock(mLock);
    int vol = ceil(v * 7.0);
    ALOGI("Set master volume to %d.\n", vol);
    set_volume_rpc(SND_DEVICE_HANDSET, SND_METHOD_VOICE, vol, m7xsnddriverfd);
    set_volume_rpc(SND_DEVICE_SPEAKER, SND_METHOD_VOICE, vol, m7xsnddriverfd);
    set_volume_rpc(SND_DEVICE_BT,      SND_METHOD_VOICE, vol, m7xsnddriverfd);
    set_volume_rpc(SND_DEVICE_HEADSET, SND_METHOD_VOICE, vol, m7xsnddriverfd);
    set_volume_rpc(SND_DEVICE_IN_S_SADC_OUT_HANDSET, SND_METHOD_VOICE, vol, m7xsnddriverfd);
    set_volume_rpc(SND_DEVICE_IN_S_SADC_OUT_SPEAKER_PHONE, SND_METHOD_VOICE, vol, m7xsnddriverfd);
    set_volume_rpc(SND_DEVICE_TTY_HEADSET, SND_METHOD_VOICE, 1, m7xsnddriverfd);
    set_volume_rpc(SND_DEVICE_TTY_VCO, SND_METHOD_VOICE, 1, m7xsnddriverfd);
    // We return an error code here to let the audioflinger do in-software
    // volume on top of the maximum volume that we set through the SND API.
    // return error - software mixer will handle it
    return -1;
}

static status_t do_route_audio_rpc(uint32_t device,
                                   bool ear_mute, bool mic_mute, int m7xsnddriverfd)
{
    if (device == -1UL)
        return NO_ERROR;

    ALOGW("rpc_snd_set_device(%d, %d, %d)\n", device, ear_mute, mic_mute);

    if (m7xsnddriverfd < 0) {
        ALOGE("Can not open snd device");
        return -EPERM;
    }
    // RPC call to switch audio path
    /* rpc_snd_set_device(
     *     device,            # Hardware device enum to use
     *     ear_mute,          # Set mute for outgoing voice audio
     *                        # this should only be unmuted when in-call
     *     mic_mute,          # Set mute for incoming voice audio
     *                        # this should only be unmuted when in-call or
     *                        # recording.
     *  )
     */
    struct msm_snd_device_config args;
    args.device = device;
    args.ear_mute = ear_mute ? SND_MUTE_MUTED : SND_MUTE_UNMUTED;
    if((device != SND_DEVICE_CURRENT) && (!mic_mute)
#ifdef QCOM_FM_ENABLED
      &&(device != SND_DEVICE_FM_DIGITAL_STEREO_HEADSET)
      &&(device != SND_DEVICE_FM_DIGITAL_SPEAKER_PHONE)
      &&(device != SND_DEVICE_FM_DIGITAL_BT_A2DP_HEADSET)
#endif
       ) {
        //Explicitly mute the mic to release DSP resources
        args.mic_mute = SND_MUTE_MUTED;
        if (ioctl(m7xsnddriverfd, SND_SET_DEVICE, &args) < 0) {
            ALOGE("snd_set_device error.");
            return -EIO;
        }
    }
    args.mic_mute = mic_mute ? SND_MUTE_MUTED : SND_MUTE_UNMUTED;
    if (ioctl(m7xsnddriverfd, SND_SET_DEVICE, &args) < 0) {
        ALOGE("snd_set_device error.");
        return -EIO;
    }

    return NO_ERROR;
}

// always call with mutex held
status_t AudioHardware::doAudioRouteOrMute(uint32_t device)
{
    int rc;
    int nEarmute=true;
#if 0
    if (device == (uint32_t)SND_DEVICE_BT || device == (uint32_t)SND_DEVICE_CARKIT) {
        if (mBluetoothId) {
            device = mBluetoothId;
        } else if (!mBluetoothNrec) {
            device = SND_DEVICE_BT_EC_OFF;
        }
    }
#endif
#ifdef QCOM_FM_ENABLED
    if(IsFmon()){
        /* FM needs both Rx path and Tx path to be unmuted */
        nEarmute = false;
        mMicMute = false;
    } else
#endif
    if (mMode == AudioSystem::MODE_IN_CALL)
        nEarmute = false;
#ifdef QCOM_VOIP_ENABLED
    else if(mMode == AudioSystem::MODE_IN_COMMUNICATION){
        nEarmute = false;
        ALOGW("VoipCall in MODE_IN_COMMUNICATION");
    }
#endif
    rc = do_route_audio_rpc(device,
                              nEarmute , mMicMute, m7xsnddriverfd);
#ifdef QCOM_FM_ENABLED
    if ((
        (device == SND_DEVICE_FM_DIGITAL_STEREO_HEADSET) ||
        (device == SND_DEVICE_FM_DIGITAL_SPEAKER_PHONE)  ||
        (device == SND_DEVICE_FM_DIGITAL_BT_A2DP_HEADSET)) &&
        (device != mCurSndDevice)) {
        ALOGV("doAudioRouteOrMute():switch to FM mode");
        switch_mode(MODE_FM);
    } else if (((mCurSndDevice == SND_DEVICE_FM_DIGITAL_STEREO_HEADSET) ||
        (mCurSndDevice == SND_DEVICE_FM_DIGITAL_SPEAKER_PHONE)  ||
        (mCurSndDevice == SND_DEVICE_FM_DIGITAL_BT_A2DP_HEADSET)) &&
        (device != mCurSndDevice)) {
        ALOGV("doAudioRouteOrMute():switch to AUX PCM mode");
        switch_mode(MODE_BTSCO);
    }
#endif
    return rc;
}

#ifdef QCOM_FM_ENABLED
bool AudioHardware::isFMAnalog()
{
    char value[PROPERTY_VALUE_MAX];
    bool isAfm = false;

    if (property_get("hw.fm.isAnalog", value, NULL)
    && !strcasecmp(value, "true")){
        isAfm = true;
    }

    return isAfm;
}
#endif
status_t AudioHardware::doRouting(AudioStreamInMSM72xx *input)
{
    /* currently this code doesn't work without the htc libacoustic */

    Mutex::Autolock lock(mLock);
    uint32_t outputDevices = mOutput->devices();
    status_t ret = NO_ERROR;
    int new_snd_device = -1;
#ifdef QCOM_FM_ENABLED
    bool enableDgtlFmDriver = false;
#endif


    //int (*msm72xx_enable_audpp)(int);
    //msm72xx_enable_audpp = (int (*)(int))::dlsym(acoustic, "msm72xx_enable_audpp");

    if (input != NULL) {
        uint32_t inputDevice = input->devices();
        ALOGI("do input routing device %x\n", inputDevice);
        // ignore routing device information when we start a recording in voice
        // call
        // Recording will happen through currently active tx device
        if(inputDevice == AudioSystem::DEVICE_IN_VOICE_CALL)
            return NO_ERROR;
        if (inputDevice != 0) {
            if (inputDevice & AudioSystem::DEVICE_IN_BLUETOOTH_SCO_HEADSET) {
                ALOGI("Routing audio to Bluetooth PCM\n");
                new_snd_device = SND_DEVICE_BT;
            } else if (inputDevice & AudioSystem::DEVICE_IN_WIRED_HEADSET) {
                    ALOGI("Routing audio to Wired Headset\n");
                    new_snd_device = SND_DEVICE_HEADSET;
#ifdef QCOM_FM_ENABLED
            } else if (inputDevice & AudioSystem::DEVICE_IN_FM_RX_A2DP) {
                    ALOGI("Routing audio from FM to Bluetooth A2DP\n");
                    new_snd_device = SND_DEVICE_FM_DIGITAL_BT_A2DP_HEADSET;
                    FmA2dpStatus=true;
            } else if (inputDevice & AudioSystem::DEVICE_IN_FM_RX) {
                    ALOGI("Routing audio to FM\n");
                    enableDgtlFmDriver = true;
#endif
            } else {
                if (outputDevices & AudioSystem::DEVICE_OUT_SPEAKER) {
                    ALOGI("Routing audio to Speakerphone\n");
                    new_snd_device = SND_DEVICE_SPEAKER;
                    new_post_proc_feature_mask = (ADRC_ENABLE | EQ_ENABLE | RX_IIR_ENABLE | MBADRC_ENABLE);
                } else {
                    ALOGI("Routing audio to Handset\n");
                    new_snd_device = SND_DEVICE_HANDSET;
                }
            }
        }
    }

    // if inputDevice == 0, restore output routing
    if (new_snd_device == -1) {
        if (outputDevices & (outputDevices - 1)) {
            if ((outputDevices & AudioSystem::DEVICE_OUT_SPEAKER) == 0) {
                ALOGV("Hardware does not support requested route combination (%#X),"
                     " picking closest possible route...", outputDevices);
            }
        }

        if ((mTtyMode != TTY_OFF) && (mMode == AudioSystem::MODE_IN_CALL) &&
                (outputDevices & AudioSystem::DEVICE_OUT_WIRED_HEADSET)) {
            if (mTtyMode == TTY_FULL) {
                ALOGI("Routing audio to TTY FULL Mode\n");
                new_snd_device = SND_DEVICE_TTY_HEADSET;
            } else if (mTtyMode == TTY_VCO) {
                ALOGI("Routing audio to TTY VCO Mode\n");
                new_snd_device = SND_DEVICE_TTY_VCO;
            } else if (mTtyMode == TTY_HCO) {
                ALOGI("Routing audio to TTY HCO Mode\n");
                new_snd_device = SND_DEVICE_TTY_HCO;
            }
#ifdef COMBO_DEVICE_SUPPORTED
        } else if ((outputDevices & AudioSystem::DEVICE_OUT_WIRED_HEADSET) &&
                   (outputDevices & AudioSystem::DEVICE_OUT_SPEAKER)) {
            ALOGI("Routing audio to Wired Headset and Speaker\n");
            new_snd_device = SND_DEVICE_STEREO_HEADSET_AND_SPEAKER;
            new_post_proc_feature_mask = (ADRC_ENABLE | EQ_ENABLE | RX_IIR_ENABLE | MBADRC_ENABLE);
        } else if ((outputDevices & AudioSystem::DEVICE_OUT_WIRED_HEADPHONE) &&
                   (outputDevices & AudioSystem::DEVICE_OUT_SPEAKER)) {
            ALOGI("Routing audio to No microphone Wired Headset and Speaker (%d,%x)\n", mMode, outputDevices);
            new_snd_device = SND_DEVICE_STEREO_HEADSET_AND_SPEAKER;
            new_post_proc_feature_mask = (ADRC_ENABLE | EQ_ENABLE | RX_IIR_ENABLE | MBADRC_ENABLE);
#endif
#ifdef QCOM_FM_ENABLED
        } else if ((outputDevices & AudioSystem::DEVICE_OUT_WIRED_HEADSET) &&
                   (outputDevices & AudioSystem::DEVICE_OUT_FM)) {
            if( !isFMAnalog() ){
                ALOGI("Routing FM to Wired Headset\n");
                new_snd_device = SND_DEVICE_FM_DIGITAL_STEREO_HEADSET;
                enableDgtlFmDriver = true;
                new_post_proc_feature_mask = (ADRC_ENABLE | EQ_ENABLE | RX_IIR_ENABLE | MBADRC_ENABLE);
            } else{
                ALOGW("Enabling Anlg FM + codec device\n");
                new_snd_device = SND_DEVICE_FM_ANALOG_STEREO_HEADSET_CODEC;
                enableDgtlFmDriver = false;
            }
        } else if ((outputDevices & AudioSystem::DEVICE_OUT_SPEAKER) &&
                   (outputDevices & AudioSystem::DEVICE_OUT_FM)) {
            ALOGI("Routing FM to Speakerphone\n");
            new_snd_device = SND_DEVICE_FM_DIGITAL_SPEAKER_PHONE;
            new_post_proc_feature_mask = (ADRC_ENABLE | EQ_ENABLE | RX_IIR_ENABLE | MBADRC_ENABLE);
            enableDgtlFmDriver = true;
        } else if ( (outputDevices & AudioSystem::DEVICE_OUT_FM) && isFMAnalog()) {
            ALOGW("Enabling Anlg FM on wired headset\n");
            new_snd_device = SND_DEVICE_FM_ANALOG_STEREO_HEADSET;
            enableDgtlFmDriver = false;
#endif
        } else if (outputDevices &
                   (AudioSystem::DEVICE_OUT_BLUETOOTH_SCO | AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_HEADSET)) {
            ALOGI("Routing audio to Bluetooth PCM\n");
            new_snd_device = SND_DEVICE_BT;
        } else if (outputDevices & AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_CARKIT) {
            ALOGI("Routing audio to Bluetooth PCM\n");
            new_snd_device = SND_DEVICE_CARKIT;
        } else if (outputDevices & AudioSystem::DEVICE_OUT_WIRED_HEADSET) {
            ALOGI("Routing audio to Wired Headset\n");
            new_snd_device = SND_DEVICE_HEADSET;
            new_post_proc_feature_mask = (ADRC_ENABLE | EQ_ENABLE | RX_IIR_ENABLE | MBADRC_ENABLE);
        } else if (outputDevices & AudioSystem::DEVICE_OUT_SPEAKER) {
            ALOGI("Routing audio to Speakerphone\n");
            new_snd_device = SND_DEVICE_SPEAKER;
            new_post_proc_feature_mask = (ADRC_ENABLE | EQ_ENABLE | RX_IIR_ENABLE | MBADRC_ENABLE);
        } else if (outputDevices & AudioSystem::DEVICE_OUT_EARPIECE) {
            ALOGI("Routing audio to Handset\n");
            new_snd_device = SND_DEVICE_HANDSET;
            new_post_proc_feature_mask = (ADRC_ENABLE | EQ_ENABLE | RX_IIR_ENABLE | MBADRC_ENABLE);
        }
    }

    if (mDualMicEnabled && mMode == AudioSystem::MODE_IN_CALL) {
        if (new_snd_device == SND_DEVICE_HANDSET) {
            ALOGI("Routing audio to handset with DualMike enabled\n");
            new_snd_device = SND_DEVICE_IN_S_SADC_OUT_HANDSET;
        } else if (new_snd_device == SND_DEVICE_SPEAKER) {
            ALOGI("Routing audio to speakerphone with DualMike enabled\n");
            new_snd_device = SND_DEVICE_IN_S_SADC_OUT_SPEAKER_PHONE;
        }
    }
#ifdef QCOM_FM_ENABLED
    if ((mFmFd == -1) && enableDgtlFmDriver ) {
        enableFM();
    } else if ((mFmFd != -1) && !enableDgtlFmDriver ) {
        disableFM();
    }

    if((outputDevices  == 0) && (FmA2dpStatus == true))
       new_snd_device = SND_DEVICE_FM_DIGITAL_BT_A2DP_HEADSET;
#endif

    if (new_snd_device != -1 && new_snd_device != mCurSndDevice) {
        ret = doAudioRouteOrMute(new_snd_device);

        //disable post proc first for previous session
        if(hpcm_playback_in_progress
#ifdef QCOM_TUNNEL_LPA_ENABLED
         || lpa_playback_in_progress
#endif
         ) {
            msm72xx_enable_postproc(false);
#ifdef SRS_PROCESSING
            msm72xx_enable_srs(SRS_PARAMS_ALL, false);
#endif /*SRS_PROCESSING*/
        }

        //enable post proc for new device
        snd_device = new_snd_device;
        post_proc_feature_mask = new_post_proc_feature_mask;

        if(hpcm_playback_in_progress
#ifdef QCOM_TUNNEL_LPA_ENABLED
         || lpa_playback_in_progress
#endif
         ){
            msm72xx_enable_postproc(true);
#ifdef SRS_PROCESSING
            msm72xx_enable_srs(SRS_PARAMS_ALL, true);
#endif /*SRS_PROCESSING*/
        }

        mCurSndDevice = new_snd_device;
    }

    return ret;
}

#ifdef QCOM_FM_ENABLED
status_t AudioHardware::enableFM()
{
    ALOGD("enableFM");
    status_t status = NO_INIT;
    status = ::open(FM_DEVICE, O_RDWR);
    if (status < 0) {
           ALOGE("Cannot open FM_DEVICE errno: %d", errno);
           goto Error;
    }
    mFmFd = status;

    status = ioctl(mFmFd, AUDIO_START, 0);

    if (status < 0) {
            ALOGE("Cannot do AUDIO_START");
            goto Error;
    }
    return NO_ERROR;

    Error:
    if (mFmFd >= 0) {
        ::close(mFmFd);
        mFmFd = -1;
    }
    return NO_ERROR;
}


status_t AudioHardware::disableFM()
{
    int status;
    ALOGD("disableFM");
    if (mFmFd >= 0) {
        status = ioctl(mFmFd, AUDIO_STOP, 0);
        if (status < 0) {
                ALOGE("Cannot do AUDIO_STOP");
        }
        ::close(mFmFd);
        mFmFd = -1;
    }

    return NO_ERROR;
}
#endif
status_t AudioHardware::checkMicMute()
{
    Mutex::Autolock lock(mLock);
    if (mMode != AudioSystem::MODE_IN_CALL) {
        setMicMute_nosync(true);
    }

    return NO_ERROR;
}

status_t AudioHardware::dumpInternals(int fd, const Vector<String16>& args)
{
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;
    result.append("AudioHardware::dumpInternals\n");
    snprintf(buffer, SIZE, "\tmInit: %s\n", mInit? "true": "false");
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmMicMute: %s\n", mMicMute? "true": "false");
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmBluetoothNrec: %s\n", mBluetoothNrec? "true": "false");
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmBluetoothId: %d\n", mBluetoothId);
    result.append(buffer);
    ::write(fd, result.string(), result.size());
    return NO_ERROR;
}

status_t AudioHardware::dump(int fd, const Vector<String16>& args)
{
    dumpInternals(fd, args);
    for (size_t index = 0; index < mInputs.size(); index++) {
        mInputs[index]->dump(fd, args);
    }

    if (mOutput) {
        mOutput->dump(fd, args);
    }
    return NO_ERROR;
}

uint32_t AudioHardware::getInputSampleRate(uint32_t sampleRate)
{
    uint32_t i;
    uint32_t prevDelta;
    uint32_t delta;

    for (i = 0, prevDelta = 0xFFFFFFFF; i < sizeof(inputSamplingRates)/sizeof(uint32_t); i++, prevDelta = delta) {
        delta = abs(sampleRate - inputSamplingRates[i]);
        if (delta > prevDelta) break;
    }
    // i is always > 0 here
    return inputSamplingRates[i-1];
}

// getActiveInput_l() must be called with mLock held
AudioHardware::AudioStreamInMSM72xx *AudioHardware::getActiveInput_l()
{
    for (size_t i = 0; i < mInputs.size(); i++) {
        // return first input found not being in standby mode
        // as only one input can be in this state
        if (mInputs[i]->state() > AudioStreamInMSM72xx::AUDIO_INPUT_CLOSED) {
            return mInputs[i];
        }
    }

    return NULL;
}

// ----------------------------------------------------------------------------


//  VOIP stream class
//.----------------------------------------------------------------------------
#ifdef QCOM_VOIP_ENABLED
AudioHardware::AudioStreamInVoip::AudioStreamInVoip() :
    mHardware(0), mFd(-1), mState(AUDIO_INPUT_CLOSED), mRetryCount(0),
    mFormat(AUDIO_HW_IN_FORMAT), mChannels(AUDIO_HW_IN_CHANNELS),
    mSampleRate(AUDIO_HW_VOIP_SAMPLERATE_8K), mBufferSize(AUDIO_HW_VOIP_BUFFERSIZE_8K),
    mAcoustics((AudioSystem::audio_in_acoustics)0), mDevices(0)
{
}


status_t AudioHardware::AudioStreamInVoip::set(
        AudioHardware* hw, uint32_t devices, int *pFormat, uint32_t *pChannels, uint32_t *pRate,
        AudioSystem::audio_in_acoustics acoustic_flags)
{
    ALOGE("AudioStreamInVoip::set devices = %u format = %d pChannels = %u Rate = %u \n",
         devices, *pFormat, *pChannels, *pRate);
    if ((pFormat == 0) ||(*pFormat != AUDIO_HW_IN_FORMAT))
    {
        *pFormat = AUDIO_HW_IN_FORMAT;
        ALOGE("Audio Format (%d)not supported \n",*pFormat);
        return BAD_VALUE;
    }

    if (pRate == 0) {
        return BAD_VALUE;
    }
    uint32_t rate = hw->getInputSampleRate(*pRate);
    if (rate != *pRate) {
        *pRate = rate;
        ALOGE(" sample rate does not match\n");
        return BAD_VALUE;
    }

    if (pChannels == 0 || (*pChannels & (AudioSystem::CHANNEL_IN_MONO)) == 0) {
        *pChannels = AUDIO_HW_IN_CHANNELS;
        ALOGE(" Channle count does not match\n");
        return BAD_VALUE;
    }

    mHardware = hw;

    ALOGV("AudioStreamInVoip::set(%d, %d, %u)", *pFormat, *pChannels, *pRate);
    if (mFd >= 0) {
        ALOGE("Audio record already open");
        return -EPERM;
    }

    status_t status = NO_INIT;
    // open driver
    ALOGV("Check if driver is open");
    if(mHardware->mVoipFd >= 0) {
        mFd = mHardware->mVoipFd;
        // Increment voip stream count
        mHardware->mNumVoipStreams++;
        ALOGV("MVS driver is already opened, mHardware->mNumVoipStreams = %d \n",
            mHardware->mNumVoipStreams);
    }
    else {
        ALOGE("open mvs driver");
        status = ::open(MVS_DEVICE, /*O_WRONLY*/ O_RDWR);
        if (status < 0) {
            ALOGE("Cannot open %s errno: %d",MVS_DEVICE, errno);
            goto Error;
        }
        mFd = status;
        ALOGV("VOPIstreamin : Save the fd %d \n",mFd);
        mHardware->mVoipFd = mFd;
        // Increment voip stream count
        mHardware->mNumVoipStreams++;
        ALOGV(" input stream set mHardware->mNumVoipStreams = %d \n", mHardware->mNumVoipStreams);

        // configuration
        ALOGV("get mvs config");
        struct msm_audio_mvs_config mvs_config;
        status = ioctl(mFd, AUDIO_GET_MVS_CONFIG, &mvs_config);
        if (status < 0) {
           ALOGE("Cannot read mvs config");
           goto Error;
        }

        ALOGV("set mvs config");
        mvs_config.mvs_mode = MVS_MODE_PCM;
        status = ioctl(mFd, AUDIO_SET_MVS_CONFIG, &mvs_config);
        if (status < 0) {
            ALOGE("Cannot set mvs config");
            goto Error;
        }

        ALOGV("start mvs");
        status = ioctl(mFd, AUDIO_START, 0);
        if (status < 0) {
            ALOGE("Cannot start mvs driver");
            goto Error;
        }
    }
    mFormat =  *pFormat;
    mChannels = *pChannels;
    mSampleRate = *pRate;
    if(mSampleRate == AUDIO_HW_VOIP_SAMPLERATE_8K)
       mBufferSize = 320;
    else if(mSampleRate == AUDIO_HW_VOIP_SAMPLERATE_16K)
       mBufferSize = 640;
    else
    {
       ALOGE(" unsupported sample rate");
       return -1;
    }

    ALOGV(" AudioHardware::AudioStreamInVoip::set after configuring devices\
            = %u format = %d pChannels = %u Rate = %u \n",
             devices, mFormat, mChannels, mSampleRate);

    ALOGV(" Set state  AUDIO_INPUT_OPENED\n");
    mState = AUDIO_INPUT_OPENED;

    if (!acoustic)
        return NO_ERROR;

     return NO_ERROR;

Error:
    if (mFd >= 0) {
        ::close(mFd);
        mFd = -1;
        mHardware->mVoipFd = -1;
    }
    ALOGE("Error : ret status \n");
    return status;
}


AudioHardware::AudioStreamInVoip::~AudioStreamInVoip()
{
    ALOGV("AudioStreamInVoip destructor");
    standby();
    if (mHardware->mNumVoipStreams)
        mHardware->mNumVoipStreams--;
}



ssize_t AudioHardware::AudioStreamInVoip::read( void* buffer, ssize_t bytes)
{
    ALOGV("AudioStreamInVoip::read(%p, %ld)", buffer, bytes);
    if (!mHardware) return -1;

    size_t count = bytes;
    size_t totalBytesRead = 0;

    if (mState < AUDIO_INPUT_OPENED) {
       ALOGE(" reopen the device \n");
        AudioHardware *hw = mHardware;
        hw->mLock.lock();
        status_t status = set(hw, mDevices, &mFormat, &mChannels, &mSampleRate, mAcoustics);
        if (status != NO_ERROR) {
            hw->mLock.unlock();
            return -1;
        }
        hw->mLock.unlock();
        mState = AUDIO_INPUT_STARTED;
        bytes = 0;
  } else {
      ALOGV("AudioStreamInVoip::read : device is already open \n");
  }


  if(mFormat == AUDIO_HW_IN_FORMAT)
  {
      if(count < mBufferSize) {
          ALOGE("read:: read size requested is less than min input buffer size");
          return 0;
      }

      struct msm_audio_mvs_frame audio_mvs_frame;
      audio_mvs_frame.frame_type = 0;
      while (count >= mBufferSize) {
          audio_mvs_frame.len = mBufferSize;
          ALOGV("Calling read count = %u mBufferSize = %u \n",count, mBufferSize);
          int bytesRead = ::read(mFd, &audio_mvs_frame, sizeof(audio_mvs_frame));
          ALOGV("read_bytes = %d mvs\n", bytesRead);
          if (bytesRead > 0) {
                  memcpy(buffer+totalBytesRead,&audio_mvs_frame.voc_pkt, mBufferSize);
                  count -= mBufferSize;
                  totalBytesRead += mBufferSize;
                  if(!mFirstread) {
                      mFirstread = true;
                      break;
                  }
              } else {
                  ALOGE("retry read count = %d buffersize = %d\n", count, mBufferSize);
                  if (errno != EAGAIN) return bytesRead;
                  mRetryCount++;
                  ALOGW("EAGAIN - retrying");
              }
      }
  }
  return totalBytesRead;
}

status_t AudioHardware::AudioStreamInVoip::standby()
{
    bool isDriverClosed = false;
    if (!mHardware) return -1;
    ALOGV(" AudioStreamInVoip::standby = %d \n", mHardware->mNumVoipStreams);
    if (mState > AUDIO_INPUT_CLOSED && (mHardware->mNumVoipStreams == 1)) {
         int ret = 0;
         if (mFd >= 0) {
            ret = ioctl(mFd, AUDIO_STOP, NULL);
            ALOGV("MVS stop returned %d \n", ret);
            ::close(mFd);
            mFd = mHardware->mVoipFd = -1;
            ALOGE("MVS driver closed");
            isDriverClosed = true;
        }
        mState = AUDIO_INPUT_CLOSED;
    }
    return NO_ERROR;
}

status_t AudioHardware::AudioStreamInVoip::dump(int fd, const Vector<String16>& args)
{
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;
    result.append("AudioStreamInVoip::dump\n");
    snprintf(buffer, SIZE, "\tsample rate: %d\n", sampleRate());
    result.append(buffer);
    snprintf(buffer, SIZE, "\tbuffer size: %d\n", bufferSize());
    result.append(buffer);
    snprintf(buffer, SIZE, "\tchannels: %d\n", channels());
    result.append(buffer);
    snprintf(buffer, SIZE, "\tformat: %d\n", format());
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmHardware: %p\n", mHardware);
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmFd count: %d\n", mFd);
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmState: %d\n", mState);
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmRetryCount: %d\n", mRetryCount);
    result.append(buffer);
    ::write(fd, result.string(), result.size());
    return NO_ERROR;
}

status_t AudioHardware::AudioStreamInVoip::setParameters(const String8& keyValuePairs)
{
    AudioParameter param = AudioParameter(keyValuePairs);
    String8 key = String8(AudioParameter::keyRouting);
    status_t status = NO_ERROR;
    int device;
    ALOGV("AudioStreamInVoip::setParameters() %s", keyValuePairs.string());

    if (param.getInt(key, device) == NO_ERROR) {
        ALOGV("set input routing %x", device);
        if (device & (device - 1)) {
            status = BAD_VALUE;
        } else {
            mDevices = device;
            status = mHardware->doRouting(this);
        }
        param.remove(key);
    }

    if (param.size()) {
        status = BAD_VALUE;
    }
    return status;
}

String8 AudioHardware::AudioStreamInVoip::getParameters(const String8& keys)
{
    AudioParameter param = AudioParameter(keys);
    String8 value;
    String8 key = String8(AudioParameter::keyRouting);

    if (param.get(key, value) == NO_ERROR) {
        ALOGV("get routing %x", mDevices);
        param.addInt(key, (int)mDevices);
    }

    ALOGV("AudioStreamInVoip::getParameters() %s", param.toString().string());
    return param.toString();
}

// getActiveInput_l() must be called with mLock held
AudioHardware::AudioStreamInVoip*AudioHardware::getActiveVoipInput_l()
{
    for (size_t i = 0; i < mVoipInputs.size(); i++) {
        // return first input found not being in standby mode
        // as only one input can be in this state
        if (mVoipInputs[i]->state() > AudioStreamInVoip::AUDIO_INPUT_CLOSED) {
            return mVoipInputs[i];
        }
    }

    return NULL;
}
#endif
// ---------------------------------------------------------------------------
//  VOIP stream class end


// ----------------------------------------------------------------------------

AudioHardware::AudioStreamOutMSM72xx::AudioStreamOutMSM72xx() :
    mHardware(0), mFd(-1), mStartCount(0), mRetryCount(0), mStandby(true), mDevices(0)
{
}

status_t AudioHardware::AudioStreamOutMSM72xx::set(
        AudioHardware* hw, uint32_t devices, int *pFormat, uint32_t *pChannels, uint32_t *pRate)
{
    int lFormat = pFormat ? *pFormat : 0;
    uint32_t lChannels = pChannels ? *pChannels : 0;
    uint32_t lRate = pRate ? *pRate : 0;

    mHardware = hw;

    // fix up defaults
    if (lFormat == 0) lFormat = format();
    if (lChannels == 0) lChannels = channels();
    if (lRate == 0) lRate = sampleRate();

    // check values
    if ((lFormat != format()) ||
        (lChannels != channels()) ||
        (lRate != sampleRate())) {
        if (pFormat) *pFormat = format();
        if (pChannels) *pChannels = channels();
        if (pRate) *pRate = sampleRate();
        return BAD_VALUE;
    }

    if (pFormat) *pFormat = lFormat;
    if (pChannels) *pChannels = lChannels;
    if (pRate) *pRate = lRate;

    mDevices = devices;

    return NO_ERROR;
}

AudioHardware::AudioStreamOutMSM72xx::~AudioStreamOutMSM72xx()
{
    if (mFd >= 0) close(mFd);
}

ssize_t AudioHardware::AudioStreamOutMSM72xx::write(const void* buffer, size_t bytes)
{
    //ALOGE("AudioStreamOutMSM72xx::write(%p, %u)", buffer, bytes);
    status_t status = NO_INIT;
    size_t count = bytes;
    const uint8_t* p = static_cast<const uint8_t*>(buffer);

    if (mStandby) {

        // open driver
        ALOGV("open driver");
        status = ::open("/dev/msm_pcm_out", O_RDWR);
        if (status < 0) {
            ALOGE("Cannot open /dev/msm_pcm_out errno: %d", errno);
            goto Error;
        }
        mFd = status;

        // configuration
        ALOGV("get config");
        struct msm_audio_config config;
        status = ioctl(mFd, AUDIO_GET_CONFIG, &config);
        if (status < 0) {
            ALOGE("Cannot read config");
            goto Error;
        }

        ALOGV("set config");
        config.channel_count = AudioSystem::popCount(channels());
        config.sample_rate = sampleRate();
        config.buffer_size = bufferSize();
        config.buffer_count = AUDIO_HW_NUM_OUT_BUF;
        config.type = CODEC_TYPE_PCM;
        status = ioctl(mFd, AUDIO_SET_CONFIG, &config);
        if (status < 0) {
            ALOGE("Cannot set config");
            goto Error;
        }

        ALOGV("buffer_size: %u", config.buffer_size);
        ALOGV("buffer_count: %u", config.buffer_count);
        ALOGV("channel_count: %u", config.channel_count);
        ALOGV("sample_rate: %u", config.sample_rate);

        // fill 2 buffers before AUDIO_START
        mStartCount = AUDIO_HW_NUM_OUT_BUF;
        mStandby = false;
    }

    while (count) {
        ssize_t written = ::write(mFd, p, count);
        if (written >= 0) {
            count -= written;
            p += written;
        } else {
            if (errno != EAGAIN) return written;
            mRetryCount++;
            ALOGW("EAGAIN - retry");
        }
    }

    // start audio after we fill 2 buffers
    if (mStartCount) {
        if (--mStartCount == 0) {
            ioctl(mFd, AUDIO_START, 0);
            hpcm_playback_in_progress = true;
            post_proc_feature_mask = new_post_proc_feature_mask;
            //enable post processing
            msm72xx_enable_postproc(true);
#ifdef SRS_PROCESSING
            msm72xx_enable_srs(SRS_PARAMS_ALL, true);
#endif /*SRS_PROCESSING*/
        }
    }
    return bytes;

Error:
    if (mFd >= 0) {
        ::close(mFd);
        mFd = -1;
    }
    // Simulate audio output timing in case of error
    usleep(bytes * 1000000 / frameSize() / sampleRate());

    return status;
}

status_t AudioHardware::AudioStreamOutMSM72xx::standby()
{
    status_t status = NO_ERROR;
    if (!mStandby && mFd >= 0) {
        //disable post processing
        hpcm_playback_in_progress = false;
#ifdef QCOM_TUNNEL_LPA_ENABLED
        if(!lpa_playback_in_progress)
#endif
        {
            msm72xx_enable_postproc(false);
#ifdef SRS_PROCESSING
            msm72xx_enable_srs(SRS_PARAMS_ALL, false);
#endif /*SRS_PROCESSING*/
        }
        ::close(mFd);
        mFd = -1;
    }
    mStandby = true;
    return status;
}

status_t AudioHardware::AudioStreamOutMSM72xx::dump(int fd, const Vector<String16>& args)
{
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;
    result.append("AudioStreamOutMSM72xx::dump\n");
    snprintf(buffer, SIZE, "\tsample rate: %d\n", sampleRate());
    result.append(buffer);
    snprintf(buffer, SIZE, "\tbuffer size: %d\n", bufferSize());
    result.append(buffer);
    snprintf(buffer, SIZE, "\tchannels: %d\n", channels());
    result.append(buffer);
    snprintf(buffer, SIZE, "\tformat: %d\n", format());
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmHardware: %p\n", mHardware);
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmFd: %d\n", mFd);
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmStartCount: %d\n", mStartCount);
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmRetryCount: %d\n", mRetryCount);
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmStandby: %s\n", mStandby? "true": "false");
    result.append(buffer);
    ::write(fd, result.string(), result.size());
    return NO_ERROR;
}

bool AudioHardware::AudioStreamOutMSM72xx::checkStandby()
{
    return mStandby;
}


status_t AudioHardware::AudioStreamOutMSM72xx::setParameters(const String8& keyValuePairs)
{
    AudioParameter param = AudioParameter(keyValuePairs);
    String8 key = String8(AudioParameter::keyRouting);
    status_t status = NO_ERROR;
    int device;
    ALOGV("AudioStreamOutMSM72xx::setParameters() %s", keyValuePairs.string());

    if (param.getInt(key, device) == NO_ERROR) {
        mDevices = device;
        ALOGV("set output routing %x", mDevices);
        status = mHardware->doRouting(NULL);
        param.remove(key);
    }

    if (param.size()) {
        status = BAD_VALUE;
    }
    return status;
}

String8 AudioHardware::AudioStreamOutMSM72xx::getParameters(const String8& keys)
{
    AudioParameter param = AudioParameter(keys);
    String8 value;
    String8 key = String8(AudioParameter::keyRouting);

    if (param.get(key, value) == NO_ERROR) {
        ALOGV("get routing %x", mDevices);
        param.addInt(key, (int)mDevices);
    }

    ALOGV("AudioStreamOutMSM72xx::getParameters() %s", param.toString().string());
    return param.toString();
}

status_t AudioHardware::AudioStreamOutMSM72xx::getRenderPosition(uint32_t *dspFrames)
{
    //TODO: enable when supported by driver
    return INVALID_OPERATION;
}

#ifdef QCOM_VOIP_ENABLED
AudioHardware::AudioStreamOutDirect::AudioStreamOutDirect() :
    mHardware(0), mFd(-1), mStartCount(0), mRetryCount(0), mStandby(true), mDevices(0),mChannels(AudioSystem::CHANNEL_OUT_MONO),
    mSampleRate(AUDIO_HW_VOIP_SAMPLERATE_8K), mBufferSize(AUDIO_HW_VOIP_BUFFERSIZE_8K)
{
}

status_t AudioHardware::AudioStreamOutDirect::set(
        AudioHardware* hw, uint32_t devices, int *pFormat, uint32_t *pChannels, uint32_t *pRate)
{
    int lFormat = pFormat ? *pFormat : 0;
    uint32_t lChannels = pChannels ? *pChannels : 0;
    uint32_t lRate = pRate ? *pRate : 0;

    ALOGE("AudioStreamOutDirect::set  lFormat = %d lChannels= %u lRate = %u\n", lFormat, lChannels, lRate );
    mHardware = hw;

    // fix up defaults
    if (lFormat == 0) lFormat = format();
    if (lChannels == 0) lChannels = channels();
    if (lRate == 0) lRate = sampleRate();

    // check values
    if ((lFormat != format()) ||
        (lChannels != channels()) ||
        (lRate != sampleRate())) {
        if (pFormat) *pFormat = format();
        if (pChannels) *pChannels = channels();
        if (pRate) *pRate = sampleRate();
        ALOGE("  AudioStreamOutDirect::set return bad values\n");
        return BAD_VALUE;
    }

    if (pFormat) *pFormat = lFormat;
    if (pChannels) *pChannels = lChannels;
    if (pRate) *pRate = lRate;


    // check values
    mFormat =  lFormat;
    mChannels = lChannels;
    mSampleRate = lRate;
    if(mSampleRate == AUDIO_HW_VOIP_SAMPLERATE_8K) {
        mBufferSize = AUDIO_HW_VOIP_BUFFERSIZE_8K;
    } else if(mSampleRate == AUDIO_HW_VOIP_SAMPLERATE_16K) {
        mBufferSize = AUDIO_HW_VOIP_BUFFERSIZE_16K;
    } else {
        ALOGE("  AudioStreamOutDirect::set return bad values\n");
        return BAD_VALUE;
    }


    mDevices = devices;
    mHardware->mNumVoipStreams++;
    return NO_ERROR;
}

AudioHardware::AudioStreamOutDirect::~AudioStreamOutDirect()
{
    ALOGV("AudioStreamOutDirect destructor");
    standby();
    if (mHardware->mNumVoipStreams)
        mHardware->mNumVoipStreams--;
}

ssize_t AudioHardware::AudioStreamOutDirect::write(const void* buffer, size_t bytes)
{
    status_t status = NO_INIT;
    size_t count = bytes;
    const uint8_t* p = static_cast<const uint8_t*>(buffer);

    if (mStandby) {
        if(mHardware->mVoipFd >= 0) {
            mFd = mHardware->mVoipFd;
        }
        else {
            // open driver
            ALOGE("open mvs driver");
            status = ::open(MVS_DEVICE, /*O_WRONLY*/ O_RDWR);
            if (status < 0) {
                ALOGE("Cannot open %s errno: %d",MVS_DEVICE, errno);
                goto Error;
            }
            mFd = status;
            mHardware->mVoipFd = mFd;
            // configuration
            ALOGV("get mvs config");
            struct msm_audio_mvs_config mvs_config;
            status = ioctl(mFd, AUDIO_GET_MVS_CONFIG, &mvs_config);
            if (status < 0) {
               ALOGE("Cannot read mvs config");
               goto Error;
            }

            ALOGV("set mvs config");
            mvs_config.mvs_mode = MVS_MODE_PCM;
            status = ioctl(mFd, AUDIO_SET_MVS_CONFIG, &mvs_config);
            if (status < 0) {
                ALOGE("Cannot set mvs config");
                goto Error;
            }

            ALOGV("start mvs config");
            status = ioctl(mFd, AUDIO_START, 0);
            if (status < 0) {
                ALOGE("Cannot start mvs driver");
                goto Error;
            }

            mStandby = false;
        }
    }
    struct msm_audio_mvs_frame audio_mvs_frame;
    audio_mvs_frame.frame_type = 0;
    while (count) {
        audio_mvs_frame.len = mBufferSize;
        memcpy(&audio_mvs_frame.voc_pkt, p, mBufferSize);
        // TODO - this memcpy is rendundant can be removed.
        ALOGV("write mvs bytes");
        size_t written = ::write(mFd, &audio_mvs_frame, sizeof(audio_mvs_frame));
        ALOGV(" mvs bytes written : %d \n", written);
        if (written == 0) {
            count -= mBufferSize;
            p += mBufferSize;
        } else {
            if (errno != EAGAIN) return written;
            mRetryCount++;
            ALOGW("EAGAIN - retry");
        }
    }

    return bytes;

Error:
ALOGE("  write Error \n");
    if (mFd >= 0) {
        ::close(mFd);
        mFd = -1;
        mHardware->mVoipFd = -1;
    }
    // Simulate audio output timing in case of error
//    usleep(bytes * 1000000 / frameSize() / sampleRate());

    return status;
}



status_t AudioHardware::AudioStreamOutDirect::standby()
{
    ALOGD("AudioStreamOutDirect::standby()");
    status_t status = NO_ERROR;
    int ret = 0;

    ALOGV(" AudioStreamOutDirect::standby mHardware->mNumVoipStreams = %d mFd = %d mStandby %d\n", mHardware->mNumVoipStreams, mFd,mStandby);
    if (mFd >= 0 && (mHardware->mNumVoipStreams == 1)) {
       ret = ioctl(mFd, AUDIO_STOP, NULL);
       ALOGV("MVS stop returned %d \n", ret);
       ::close(mFd);
       mFd = mHardware->mVoipFd = -1;
       ALOGE("MVS driver closed");
   }

    mStandby = true;
    return status;
}

status_t AudioHardware::AudioStreamOutDirect::dump(int fd, const Vector<String16>& args)
{
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;
    result.append("AudioStreamOutDirect::dump\n");
    snprintf(buffer, SIZE, "\tsample rate: %d\n", sampleRate());
    result.append(buffer);
    snprintf(buffer, SIZE, "\tbuffer size: %d\n", bufferSize());
    result.append(buffer);
    snprintf(buffer, SIZE, "\tchannels: %d\n", channels());
    result.append(buffer);
    snprintf(buffer, SIZE, "\tformat: %d\n", format());
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmHardware: %p\n", mHardware);
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmFd: %d\n", mFd);
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmStartCount: %d\n", mStartCount);
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmRetryCount: %d\n", mRetryCount);
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmStandby: %s\n", mStandby? "true": "false");
    result.append(buffer);
    ::write(fd, result.string(), result.size());
    return NO_ERROR;
}

bool AudioHardware::AudioStreamOutDirect::checkStandby()
{
    return mStandby;
}


status_t AudioHardware::AudioStreamOutDirect::setParameters(const String8& keyValuePairs)
{
    AudioParameter param = AudioParameter(keyValuePairs);
    String8 key = String8(AudioParameter::keyRouting);
    status_t status = NO_ERROR;
    int device;
    ALOGV("AudioStreamOutDirect::setParameters() %s", keyValuePairs.string());

    if (param.getInt(key, device) == NO_ERROR) {
        mDevices = device;
        ALOGV("set output routing %x", mDevices);
        status = mHardware->doRouting(NULL);
        param.remove(key);
    }

    if (param.size()) {
        status = BAD_VALUE;
    }
    return status;
}

String8 AudioHardware::AudioStreamOutDirect::getParameters(const String8& keys)
{
    AudioParameter param = AudioParameter(keys);
    String8 value;
    String8 key = String8(AudioParameter::keyRouting);

    if (param.get(key, value) == NO_ERROR) {
        ALOGV("get routing %x", mDevices);
        param.addInt(key, (int)mDevices);
    }

    ALOGV("AudioStreamOutDirect::getParameters() %s", param.toString().string());
    return param.toString();
}

status_t AudioHardware::AudioStreamOutDirect::getRenderPosition(uint32_t *dspFrames)
{
    //TODO: enable when supported by driver
    return INVALID_OPERATION;
}
#endif

// End AudioStreamOutDirect

AudioHardware::AudioSessionOutMSM7xxx::AudioSessionOutMSM7xxx() :
    mHardware(0), mStartCount(0), mRetryCount(0), mStandby(true), mDevices(0)
#ifdef QCOM_TUNNEL_LPA_ENABLED
      ,mLPADriverFd(-1)
#endif
{
}

status_t AudioHardware::AudioSessionOutMSM7xxx::set(
        AudioHardware* hw, uint32_t devices, int *pFormat
#ifdef QCOM_TUNNEL_LPA_ENABLED
        , int32_t LPADriverFd
#endif
        )
{
    int lFormat = pFormat ? *pFormat : 0;

    mHardware = hw;
    mDevices = devices;
#ifdef QCOM_TUNNEL_LPA_ENABLED
    if(LPADriverFd >= 0) {
        mLPADriverFd = LPADriverFd;
        lpa_playback_in_progress = true;
        post_proc_feature_mask = new_post_proc_feature_mask;
        msm72xx_enable_postproc(true);
#ifdef SRS_PROCESSING
        msm72xx_enable_srs(SRS_PARAMS_ALL, true);
#endif /*SRS_PROCESSING*/

    }
#endif
    return NO_ERROR;
}

AudioHardware::AudioSessionOutMSM7xxx::~AudioSessionOutMSM7xxx()
{
}


status_t AudioHardware::AudioSessionOutMSM7xxx::standby()
{

    ALOGD("AudioSessionOutMSM7xxx::standby()");
    mStandby = true;
#ifdef QCOM_TUNNEL_LPA_ENABLED
    lpa_playback_in_progress = false;
#endif
    if(!hpcm_playback_in_progress ){
        msm72xx_enable_postproc(false);
#ifdef SRS_PROCESSING
        msm72xx_enable_srs(SRS_PARAMS_ALL, false);
#endif /*SRS_PROCESSING*/
    }
    return NO_ERROR;
}

bool AudioHardware::AudioSessionOutMSM7xxx::checkStandby()
{
    return mStandby;
}

status_t AudioHardware::AudioSessionOutMSM7xxx::setParameters(const String8& keyValuePairs)
{
    AudioParameter param = AudioParameter(keyValuePairs);
    String8 key = String8(AudioParameter::keyRouting);
    status_t status = NO_ERROR;
    int device;
    ALOGV("AudioSessionOutMSM7xxx::setParameters() %s", keyValuePairs.string());

    if (param.getInt(key, device) == NO_ERROR) {
        mDevices = device;
        ALOGV("set output routing %x", mDevices);
        status = mHardware->doRouting(NULL);
        param.remove(key);
    }

    if (param.size()) {
        status = BAD_VALUE;
    }
    return status;
}
String8 AudioHardware::AudioSessionOutMSM7xxx::getParameters(const String8& keys)
{
    AudioParameter param = AudioParameter(keys);
    String8 value;
    String8 key = String8(AudioParameter::keyRouting);

    if (param.get(key, value) == NO_ERROR) {
        ALOGV("get routing %x", mDevices);
        param.addInt(key, (int)mDevices);
    }

    ALOGV("AudioSessionOutMSM7xxx::getParameters() %s", param.toString().string());
    return param.toString();
}

status_t AudioHardware::AudioSessionOutMSM7xxx::getRenderPosition(uint32_t *dspFrames)
{
    //TODO: enable when supported by driver
    return INVALID_OPERATION;
}

status_t AudioHardware::AudioSessionOutMSM7xxx::setVolume(float left, float right)
{
    float v = (left + right) / 2;
    if (v < 0.0) {
        ALOGW("AudioSessionOutMSM7xxx::setVolume(%f) under 0.0, assuming 0.0\n", v);
        v = 0.0;
    } else if (v > 1.0) {
        ALOGW("AudioSessionOutMSM7xxx::setVolume(%f) over 1.0, assuming 1.0\n", v);
        v = 1.0;
    }

    // Ensure to convert the log volume back to linear for LPA
    long vol = v * 10000;
    ALOGV("AudioSessionOutMSM7xxx::setVolume(%f)\n", v);
    ALOGV("Setting session volume to %ld (available range is 0 to 100)\n", vol);

#ifdef QCOM_TUNNEL_LPA_ENABLED
    if (ioctl(mLPADriverFd,AUDIO_SET_VOLUME, vol)< 0)
        ALOGE("LPA volume set failed");

    ALOGV("LPA volume set failed(%f) succeeded",vol);
#endif
    return NO_ERROR;
}


//.----------------------------------------------------------------------------


//.----------------------------------------------------------------------------
int AudioHardware::AudioStreamInMSM72xx::InstanceCount = 0;
AudioHardware::AudioStreamInMSM72xx::AudioStreamInMSM72xx() :
    mHardware(0), mFd(-1), mState(AUDIO_INPUT_CLOSED), mRetryCount(0),
    mFormat(AUDIO_HW_IN_FORMAT), mChannels(AUDIO_HW_IN_CHANNELS),
    mSampleRate(AUDIO_HW_IN_SAMPLERATE), mBufferSize(AUDIO_HW_IN_BUFFERSIZE),
    mAcoustics((AudioSystem::audio_in_acoustics)0), mDevices(0)
{
    AudioStreamInMSM72xx::InstanceCount++;
}

status_t AudioHardware::AudioStreamInMSM72xx::set(
        AudioHardware* hw, uint32_t devices, int *pFormat, uint32_t *pChannels, uint32_t *pRate,
        AudioSystem::audio_in_acoustics acoustic_flags)
{
    if(AudioStreamInMSM72xx::InstanceCount > 1)
    {
        ALOGE("More than one instance of recording not supported");
        return -EBUSY;
    }

    if ((pFormat == 0) ||
        ((*pFormat != AUDIO_HW_IN_FORMAT) &&
         (*pFormat != AudioSystem::AMR_NB) &&
         (*pFormat != AudioSystem::AAC)))
    {
        *pFormat = AUDIO_HW_IN_FORMAT;
        ALOGE("audio format bad value");
        return BAD_VALUE;
    }
    if (pRate == 0) {
        return BAD_VALUE;
    }
    uint32_t rate = hw->getInputSampleRate(*pRate);
    if (rate != *pRate) {
        *pRate = rate;
        ALOGE(" sample rate does not match\n");
        return BAD_VALUE;
    }

    if (pChannels == 0 || (*pChannels & (AudioSystem::CHANNEL_IN_MONO | AudioSystem::CHANNEL_IN_STEREO)) == 0)
    {
        *pChannels = AUDIO_HW_IN_CHANNELS;
        ALOGE(" Channel count does not match\n");
        return BAD_VALUE;
    }

    mHardware = hw;

    ALOGV("AudioStreamInMSM72xx::set(%d, %d, %u)", *pFormat, *pChannels, *pRate);
    if (mFd >= 0) {
        ALOGE("Audio record already open");
        return -EPERM;
    }

    struct msm_audio_config config;
    struct msm_audio_voicememo_config gcfg;
    memset(&gcfg,0,sizeof(gcfg));
    status_t status = 0;
    if(*pFormat == AUDIO_HW_IN_FORMAT)
    {
    // open audio input device
        status = ::open(PCM_IN_DEVICE, O_RDWR);
        if (status < 0) {
            ALOGE("Cannot open %s errno: %d", PCM_IN_DEVICE, errno);
            goto Error;
        }
        mFd = status;

        // configuration
        status = ioctl(mFd, AUDIO_GET_CONFIG, &config);
        if (status < 0) {
            ALOGE("Cannot read config");
           goto Error;
        }

    ALOGV("set config");
    config.channel_count = AudioSystem::popCount(*pChannels);
    config.sample_rate = *pRate;
    config.buffer_size = bufferSize();
    config.buffer_count = 2;
        config.type = CODEC_TYPE_PCM;
    status = ioctl(mFd, AUDIO_SET_CONFIG, &config);
    if (status < 0) {
        ALOGE("Cannot set config");
        if (ioctl(mFd, AUDIO_GET_CONFIG, &config) == 0) {
            if (config.channel_count == 1) {
                *pChannels = AudioSystem::CHANNEL_IN_MONO;
            } else {
                *pChannels = AudioSystem::CHANNEL_IN_STEREO;
            }
            *pRate = config.sample_rate;
        }
        goto Error;
    }

    ALOGV("confirm config");
    status = ioctl(mFd, AUDIO_GET_CONFIG, &config);
    if (status < 0) {
        ALOGE("Cannot read config");
        goto Error;
    }
    ALOGV("buffer_size: %u", config.buffer_size);
    ALOGV("buffer_count: %u", config.buffer_count);
    ALOGV("channel_count: %u", config.channel_count);
    ALOGV("sample_rate: %u", config.sample_rate);
    ALOGV("input device: %x", devices);

    mDevices = devices;
    mFormat = AUDIO_HW_IN_FORMAT;
    mChannels = *pChannels;
    mSampleRate = config.sample_rate;
    mBufferSize = config.buffer_size;
    }
    else if( (*pFormat == AudioSystem::AMR_NB))
           {

      // open vocie memo input device
      status = ::open(VOICE_MEMO_DEVICE, O_RDWR);
      if (status < 0) {
          ALOGE("Cannot open Voice Memo device for read");
          goto Error;
      }
      mFd = status;
      /* Config param */
      if(ioctl(mFd, AUDIO_GET_CONFIG, &config))
      {
        ALOGE(" Error getting buf config param AUDIO_GET_CONFIG \n");
        goto  Error;
      }

      ALOGV("The Config buffer size is %d", config.buffer_size);
      ALOGV("The Config buffer count is %d", config.buffer_count);
      ALOGV("The Config Channel count is %d", config.channel_count);
      ALOGV("The Config Sample rate is %d", config.sample_rate);

      mDevices = devices;
      mChannels = *pChannels;
      mSampleRate = config.sample_rate;

      if (mDevices == AudioSystem::DEVICE_IN_VOICE_CALL)
      {
        if ((mChannels & AudioSystem::CHANNEL_IN_VOICE_DNLINK) &&
            (mChannels & AudioSystem::CHANNEL_IN_VOICE_UPLINK)) {
          ALOGI("Recording Source: Voice Call Both Uplink and Downlink");
          gcfg.rec_type = RPC_VOC_REC_BOTH;
        } else if (mChannels & AudioSystem::CHANNEL_IN_VOICE_DNLINK) {
          ALOGI("Recording Source: Voice Call DownLink");
          gcfg.rec_type = RPC_VOC_REC_FORWARD;
        } else if (mChannels & AudioSystem::CHANNEL_IN_VOICE_UPLINK) {
          ALOGI("Recording Source: Voice Call UpLink");
          gcfg.rec_type = RPC_VOC_REC_REVERSE;
        }
      }
      else {
        ALOGI("Recording Source: Mic/Headset");
        gcfg.rec_type = RPC_VOC_REC_REVERSE;
      }

      gcfg.rec_interval_ms = 0; // AV sync
      gcfg.auto_stop_ms = 0;

      switch (*pFormat)
      {
        case AudioSystem::AMR_NB:
        {
          ALOGI("Recording Format: AMR_NB");
          gcfg.capability = RPC_VOC_CAP_AMR; // RPC_VOC_CAP_AMR (64)
          gcfg.max_rate = RPC_VOC_AMR_RATE_1220; // Max rate (Fixed frame)
          gcfg.min_rate = RPC_VOC_AMR_RATE_1220; // Min rate (Fixed frame length)
          gcfg.frame_format = RPC_VOC_PB_AMR; // RPC_VOC_PB_AMR
          mFormat = AudioSystem::AMR_NB;
          mBufferSize = 320;
          break;
        }

        default:
        break;
      }

      gcfg.dtx_enable = 0;
      gcfg.data_req_ms = 20;

      /* Set Via  config param */
      if (ioctl(mFd, AUDIO_SET_VOICEMEMO_CONFIG, &gcfg))
      {
        ALOGE("Error: AUDIO_SET_VOICEMEMO_CONFIG failed\n");
        goto  Error;
      }

      if (ioctl(mFd, AUDIO_GET_VOICEMEMO_CONFIG, &gcfg))
      {
        ALOGE("Error: AUDIO_GET_VOICEMEMO_CONFIG failed\n");
        goto  Error;
      }

      ALOGV("After set rec_type = 0x%8x\n",gcfg.rec_type);
      ALOGV("After set rec_interval_ms = 0x%8x\n",gcfg.rec_interval_ms);
      ALOGV("After set auto_stop_ms = 0x%8x\n",gcfg.auto_stop_ms);
      ALOGV("After set capability = 0x%8x\n",gcfg.capability);
      ALOGV("After set max_rate = 0x%8x\n",gcfg.max_rate);
      ALOGV("After set min_rate = 0x%8x\n",gcfg.min_rate);
      ALOGV("After set frame_format = 0x%8x\n",gcfg.frame_format);
      ALOGV("After set dtx_enable = 0x%8x\n",gcfg.dtx_enable);
      ALOGV("After set data_req_ms = 0x%8x\n",gcfg.data_req_ms);
    }
    else if(*pFormat == AudioSystem::AAC) {
      // open AAC input device
               status = ::open(PCM_IN_DEVICE, O_RDWR);
               if (status < 0) {
                     ALOGE("Cannot open AAC input  device for read");
                     goto Error;
               }
               mFd = status;

      /* Config param */
               if(ioctl(mFd, AUDIO_GET_CONFIG, &config))
               {
                     ALOGE(" Error getting buf config param AUDIO_GET_CONFIG \n");
                     goto  Error;
               }

      ALOGV("The Config buffer size is %d", config.buffer_size);
      ALOGV("The Config buffer count is %d", config.buffer_count);
      ALOGV("The Config Channel count is %d", config.channel_count);
      ALOGV("The Config Sample rate is %d", config.sample_rate);

      mDevices = devices;
      mChannels = *pChannels;
      mSampleRate = *pRate;
      mBufferSize = 2048;
      mFormat = *pFormat;

      config.channel_count = AudioSystem::popCount(*pChannels);
      config.sample_rate = *pRate;
      config.type = 1; // Configuring PCM_IN_DEVICE to AAC format

      if (ioctl(mFd, AUDIO_SET_CONFIG, &config)) {
             ALOGE(" Error in setting config of msm_pcm_in device \n");
                   goto Error;
        }
    }

    //mHardware->setMicMute_nosync(false);
    mState = AUDIO_INPUT_OPENED;

    //if (!acoustic)
    //    return NO_ERROR;

    audpre_index = calculate_audpre_table_index(mSampleRate);
    if(audpre_index < 0) {
        ALOGE("wrong sampling rate");
        status = -EINVAL;
        goto Error;
    }
    return NO_ERROR;

Error:
    if (mFd >= 0) {
        ::close(mFd);
        mFd = -1;
    }
    return status;
}

static int msm72xx_enable_preproc(bool state)
{
    uint16_t mask = 0x0000;

    if (audpp_filter_inited)
    {
        int fd;

        fd = open(PREPROC_CTL_DEVICE, O_RDWR);
        if (fd < 0) {
             ALOGE("Cannot open PreProc Ctl device");
             return -EPERM;
        }

        if (enable_preproc_mask[audpre_index] & AGC_ENABLE) {
            /* Setting AGC Params */
            ALOGI("AGC Filter Param1= %02x.", tx_agc_cfg[audpre_index].cmd_id);
            ALOGI("AGC Filter Param2= %02x.", tx_agc_cfg[audpre_index].tx_agc_param_mask);
            ALOGI("AGC Filter Param3= %02x.", tx_agc_cfg[audpre_index].tx_agc_enable_flag);
            ALOGI("AGC Filter Param4= %02x.", tx_agc_cfg[audpre_index].static_gain);
            ALOGI("AGC Filter Param5= %02x.", tx_agc_cfg[audpre_index].adaptive_gain_flag);
            ALOGI("AGC Filter Param6= %02x.", tx_agc_cfg[audpre_index].agc_params[0]);
            ALOGI("AGC Filter Param7= %02x.", tx_agc_cfg[audpre_index].agc_params[18]);
            if ((enable_preproc_mask[audpre_index] & AGC_ENABLE) &&
                (ioctl(fd, AUDIO_SET_AGC, &tx_agc_cfg[audpre_index]) < 0))
            {
                ALOGE("set AGC filter error.");
            }
        }

        if (enable_preproc_mask[audpre_index] & NS_ENABLE) {
            /* Setting NS Params */
            ALOGI("NS Filter Param1= %02x.", ns_cfg[audpre_index].cmd_id);
            ALOGI("NS Filter Param2= %02x.", ns_cfg[audpre_index].ec_mode_new);
            ALOGI("NS Filter Param3= %02x.", ns_cfg[audpre_index].dens_gamma_n);
            ALOGI("NS Filter Param4= %02x.", ns_cfg[audpre_index].dens_nfe_block_size);
            ALOGI("NS Filter Param5= %02x.", ns_cfg[audpre_index].dens_limit_ns);
            ALOGI("NS Filter Param6= %02x.", ns_cfg[audpre_index].dens_limit_ns_d);
            ALOGI("NS Filter Param7= %02x.", ns_cfg[audpre_index].wb_gamma_e);
            ALOGI("NS Filter Param8= %02x.", ns_cfg[audpre_index].wb_gamma_n);
            if ((enable_preproc_mask[audpre_index] & NS_ENABLE) &&
                (ioctl(fd, AUDIO_SET_NS, &ns_cfg[audpre_index]) < 0))
            {
                ALOGE("set NS filter error.");
            }
        }

        if (enable_preproc_mask[audpre_index] & TX_IIR_ENABLE) {
            /* Setting TX_IIR Params */
            ALOGI("TX_IIR Filter Param1= %02x.", tx_iir_cfg[audpre_index].cmd_id);
            ALOGI("TX_IIR Filter Param2= %02x.", tx_iir_cfg[audpre_index].active_flag);
            ALOGI("TX_IIR Filter Param3= %02x.", tx_iir_cfg[audpre_index].num_bands);
            ALOGI("TX_IIR Filter Param4= %02x.", tx_iir_cfg[audpre_index].iir_params[0]);
            ALOGI("TX_IIR Filter Param5= %02x.", tx_iir_cfg[audpre_index].iir_params[1]);
            ALOGI("TX_IIR Filter Param6 %02x.", tx_iir_cfg[audpre_index].iir_params[47]);
            if ((enable_preproc_mask[audpre_index] & TX_IIR_ENABLE) &&
                (ioctl(fd, AUDIO_SET_TX_IIR, &tx_iir_cfg[audpre_index]) < 0))
            {
               ALOGE("set TX IIR filter error.");
            }
        }

        if (state == true) {
            /*Setting AUDPRE_ENABLE*/
            if (ioctl(fd, AUDIO_ENABLE_AUDPRE, &enable_preproc_mask[audpre_index]) < 0) {
                ALOGE("set AUDPRE_ENABLE error.");
            }
        } else {
            /*Setting AUDPRE_ENABLE*/
            if (ioctl(fd, AUDIO_ENABLE_AUDPRE, &mask) < 0) {
                ALOGE("set AUDPRE_ENABLE error.");
            }
        }
        close(fd);
    }

    return NO_ERROR;
}

AudioHardware::AudioStreamInMSM72xx::~AudioStreamInMSM72xx()
{
    ALOGV("AudioStreamInMSM72xx destructor");
    AudioStreamInMSM72xx::InstanceCount--;
    standby();
}

ssize_t AudioHardware::AudioStreamInMSM72xx::read( void* buffer, ssize_t bytes)
{
    ALOGV("AudioStreamInMSM72xx::read(%p, %ld)", buffer, bytes);
    if (!mHardware) return -1;

    size_t count = bytes;
    size_t  aac_framesize= bytes;
    uint8_t* p = static_cast<uint8_t*>(buffer);
    uint32_t* recogPtr = (uint32_t *)p;
    uint16_t* frameCountPtr;
    uint16_t* frameSizePtr;

    if (mState < AUDIO_INPUT_OPENED) {
        AudioHardware *hw = mHardware;
        hw->mLock.lock();
        status_t status = set(hw, mDevices, &mFormat, &mChannels, &mSampleRate, mAcoustics);
        hw->mLock.unlock();
        if (status != NO_ERROR) {
            return -1;
        }
        mFirstread = false;
    }

    if (mState < AUDIO_INPUT_STARTED) {
        mState = AUDIO_INPUT_STARTED;
        // force routing to input device
#ifdef QCOM_FM_ENABLED
        if (mDevices != AudioSystem::DEVICE_IN_FM_RX) {
            mHardware->clearCurDevice();
            mHardware->doRouting(this);
        }
#endif
        if (ioctl(mFd, AUDIO_START, 0)) {
            ALOGE("Error starting record");
            standby();
            return -1;
        }
        msm72xx_enable_preproc(true);
    }

    // Resetting the bytes value, to return the appropriate read value
    bytes = 0;
    if (mFormat == AudioSystem::AAC)
    {
        *((uint32_t*)recogPtr) = 0x51434F4D ;// ('Q','C','O', 'M') Number to identify format as AAC by higher layers
        recogPtr++;
        frameCountPtr = (uint16_t*)recogPtr;
        *frameCountPtr = 0;
        p += 3*sizeof(uint16_t);
        count -= 3*sizeof(uint16_t);
    }
    while (count > 0) {

        if (mFormat == AudioSystem::AAC) {
            frameSizePtr = (uint16_t *)p;
            p += sizeof(uint16_t);
            if(!(count > 2)) break;
            count -= sizeof(uint16_t);
        }

        ssize_t bytesRead = ::read(mFd, p, count);
        if (bytesRead > 0) {
            count -= bytesRead;
            p += bytesRead;
            bytes += bytesRead;

            if (mFormat == AudioSystem::AAC){
                *frameSizePtr =  bytesRead;
                (*frameCountPtr)++;
            }

            if(!mFirstread)
            {
               mFirstread = true;
               break;
            }

        }
        else if(bytesRead == 0)
        {
         ALOGI("Bytes Read = %d ,Buffer no longer sufficient",bytesRead);
         break;
        } else {
            if (errno != EAGAIN) return bytesRead;
            mRetryCount++;
            ALOGW("EAGAIN - retrying");
        }
    }
    if (mFormat == AudioSystem::AAC)
         return aac_framesize;

    return bytes;
}

status_t AudioHardware::AudioStreamInMSM72xx::standby()
{
    if (mState > AUDIO_INPUT_CLOSED) {
        msm72xx_enable_preproc(false);
        if (mFd >= 0) {
            ::close(mFd);
            mFd = -1;
        }
        mState = AUDIO_INPUT_CLOSED;
    }
    if (!mHardware) return -1;
    // restore output routing if necessary
#ifdef QCOM_FM_ENABLED
    if (!mHardware->IsFmon())
#endif
    {
        mHardware->clearCurDevice();
        mHardware->doRouting(this);
    }
#ifdef QCOM_FM_ENABLED
    if(mHardware->IsFmA2dpOn())
        mHardware->SwitchOffFmA2dp();
#endif

    return NO_ERROR;
}

status_t AudioHardware::AudioStreamInMSM72xx::dump(int fd, const Vector<String16>& args)
{
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;
    result.append("AudioStreamInMSM72xx::dump\n");
    snprintf(buffer, SIZE, "\tsample rate: %d\n", sampleRate());
    result.append(buffer);
    snprintf(buffer, SIZE, "\tbuffer size: %d\n", bufferSize());
    result.append(buffer);
    snprintf(buffer, SIZE, "\tchannels: %d\n", channels());
    result.append(buffer);
    snprintf(buffer, SIZE, "\tformat: %d\n", format());
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmHardware: %p\n", mHardware);
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmFd count: %d\n", mFd);
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmState: %d\n", mState);
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmRetryCount: %d\n", mRetryCount);
    result.append(buffer);
    ::write(fd, result.string(), result.size());
    return NO_ERROR;
}

status_t AudioHardware::AudioStreamInMSM72xx::setParameters(const String8& keyValuePairs)
{
    AudioParameter param = AudioParameter(keyValuePairs);
    String8 key = String8(AudioParameter::keyRouting);
    status_t status = NO_ERROR;
    int device;
    ALOGV("AudioStreamInMSM72xx::setParameters() %s", keyValuePairs.string());

    if (param.getInt(key, device) == NO_ERROR) {
        ALOGD("set input routing %x", device);
        if (device & (device - 1)) {
            status = BAD_VALUE;
        } else {
            mDevices = device;
            status = mHardware->doRouting(this);
        }
        param.remove(key);
    }

    if (param.size()) {
        status = BAD_VALUE;
    }
    return status;
}

String8 AudioHardware::AudioStreamInMSM72xx::getParameters(const String8& keys)
{
    AudioParameter param = AudioParameter(keys);
    String8 value;
    String8 key = String8(AudioParameter::keyRouting);

    if (param.get(key, value) == NO_ERROR) {
        ALOGV("get routing %x", mDevices);
        param.addInt(key, (int)mDevices);
    }

    ALOGV("AudioStreamInMSM72xx::getParameters() %s", param.toString().string());
    return param.toString();
}

// ----------------------------------------------------------------------------

extern "C" AudioHardwareInterface* createAudioHardware(void) {
    return new AudioHardware();
}

}; // namespace android
