/*
** Copyright 2008, The Android Open-Source Project
** Copyright (c) 2011-2013, The Linux Foundation. All rights reserved.
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

#define LOG_NDEBUG 0
#define LOG_TAG "AudioHardwareMSM8660"
#include <utils/Log.h>
#include <utils/String8.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <fcntl.h>
#include "AudioHardware.h"
#include <media/AudioSystem.h>
#include <cutils/properties.h>

#include <linux/android_pmem.h>
#ifdef QCOM_ACDB_ENABLED
#include <linux/msm_audio_acdb.h>
#endif
#ifdef QCOM_VOIP_ENABLED
#include <linux/msm_audio_mvs.h>
#endif
#include <sys/mman.h>
#include "control.h"
#include "acdb.h"

#ifdef HTC_ACOUSTIC_AUDIO
    extern "C" {
    #include <linux/spi_aic3254.h>
    #include <linux/tpa2051d3.h>
    }
    #define DSP_EFFECT_KEY "dolby_srs_eq"
#endif

#define VOICE_SESSION_NAME "Voice session"
#define VOIP_SESSION_NAME "VoIP session"

// hardware specific functions

#define LOG_SND_RPC 0  // Set to 1 to log sound RPC's

#define DUALMIC_KEY "dualmic_enabled"
#define BTHEADSET_VGS "bt_headset_vgs"
#define ANC_KEY "anc_enabled"
#define TTY_MODE_KEY "tty_mode"
#define ECHO_SUPRESSION "ec_supported"

#define VOIPRATE_KEY "voip_rate"

#define MVS_DEVICE "/dev/msm_mvs"

#ifdef QCOM_FM_ENABLED
#define FM_DEVICE  "/dev/msm_fm"
#define FM_A2DP_REC 1
#define FM_FILE_REC 2
#endif

#ifdef QCOM_ACDB_ENABLED
#define INVALID_ACDB_ID -1
#endif

namespace android_audio_legacy {

Mutex   mDeviceSwitchLock;
#ifdef HTC_ACOUSTIC_AUDIO
Mutex   mAIC3254ConfigLock;
#endif
static int audpre_index, tx_iir_index;
static void * acoustic;
const uint32_t AudioHardware::inputSamplingRates[] = {
        8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000
};
static const uint32_t INVALID_DEVICE                        = 65535;
static const uint32_t SND_DEVICE_CURRENT                    = -1;
static const uint32_t SND_DEVICE_HANDSET                    = 0;
static const uint32_t SND_DEVICE_SPEAKER                    = 1;
static const uint32_t SND_DEVICE_HEADSET                    = 2;
static const uint32_t SND_DEVICE_FM_HANDSET                 = 3;
static const uint32_t SND_DEVICE_FM_SPEAKER                 = 4;
static const uint32_t SND_DEVICE_FM_HEADSET                 = 5;
static const uint32_t SND_DEVICE_BT                         = 6;
static const uint32_t SND_DEVICE_HEADSET_AND_SPEAKER        = 7;
static const uint32_t SND_DEVICE_NO_MIC_HEADSET             = 8;
static const uint32_t SND_DEVICE_IN_S_SADC_OUT_HANDSET      = 9;
static const uint32_t SND_DEVICE_IN_S_SADC_OUT_SPEAKER_PHONE= 10;
static const uint32_t SND_DEVICE_TTY_HEADSET                = 11;
static const uint32_t SND_DEVICE_TTY_HCO                    = 12;
static const uint32_t SND_DEVICE_TTY_VCO                    = 13;
static const uint32_t SND_DEVICE_TTY_FULL                   = 14;
static const uint32_t SND_DEVICE_HDMI                       = 15;
static const uint32_t SND_DEVICE_CARKIT                     = -1;
static const uint32_t SND_DEVICE_ANC_HEADSET                = 16;
static const uint32_t SND_DEVICE_NO_MIC_ANC_HEADSET         = 17;
static const uint32_t SND_DEVICE_HEADPHONE_AND_SPEAKER      = 18;
static const uint32_t SND_DEVICE_FM_TX                      = 19;
static const uint32_t SND_DEVICE_FM_TX_AND_SPEAKER          = 20;
static const uint32_t SND_DEVICE_SPEAKER_TX                 = 21;
static const uint32_t SND_DEVICE_BACK_MIC_CAMCORDER         = 33;
#ifdef HTC_ACOUSTIC_AUDIO
static const uint32_t SND_DEVICE_SPEAKER_BACK_MIC           = 26;
static const uint32_t SND_DEVICE_HANDSET_BACK_MIC           = 27;
static const uint32_t SND_DEVICE_NO_MIC_HEADSET_BACK_MIC    = 28;
static const uint32_t SND_DEVICE_HEADSET_AND_SPEAKER_BACK_MIC = 30;
static const uint32_t SND_DEVICE_I2S_SPEAKER                = 32;
static const uint32_t SND_DEVICE_BT_EC_OFF                  = 45;
static const uint32_t SND_DEVICE_HAC                        = 252;
static const uint32_t SND_DEVICE_USB_HEADSET                = 253;
#else
static const uint32_t SND_DEVICE_BT_EC_OFF                  = -1;
#endif
#ifdef SAMSUNG_AUDIO
static uint32_t SND_DEVICE_VOIP_HANDSET               = 50;
static uint32_t SND_DEVICE_VOIP_SPEAKER               = 51;
static uint32_t SND_DEVICE_VOIP_HEADSET               = 52;
static uint32_t SND_DEVICE_CALL_HANDSET               = 60;
static uint32_t SND_DEVICE_CALL_SPEAKER               = 61;
static uint32_t SND_DEVICE_CALL_HEADSET               = 62;
static uint32_t SND_DEVICE_VR_SPEAKER                 = 70;
static uint32_t SND_DEVICE_VR_HEADSET                 = 71;
static uint32_t SND_DEVICE_HAC                        = 252;
static uint32_t SND_DEVICE_USB_HEADSET                = 253;
#endif
static const uint32_t DEVICE_HANDSET_RX            = 0; // handset_rx
static const uint32_t DEVICE_HANDSET_TX            = 1;//handset_tx
static const uint32_t DEVICE_SPEAKER_RX            = 2; //speaker_stereo_rx
static const uint32_t DEVICE_SPEAKER_TX            = 3;//speaker_mono_tx
static const uint32_t DEVICE_HEADSET_RX            = 4; //headset_stereo_rx
static const uint32_t DEVICE_HEADSET_TX            = 5; //headset_mono_tx
static const uint32_t DEVICE_FMRADIO_HANDSET_RX    = 6; //fmradio_handset_rx
static const uint32_t DEVICE_FMRADIO_HEADSET_RX    = 7; //fmradio_headset_rx
static const uint32_t DEVICE_FMRADIO_SPEAKER_RX    = 8; //fmradio_speaker_rx
static const uint32_t DEVICE_DUALMIC_HANDSET_TX    = 9; //handset_dual_mic_endfire_tx
static const uint32_t DEVICE_DUALMIC_SPEAKER_TX    = 10; //speaker_dual_mic_endfire_tx
static const uint32_t DEVICE_TTY_HEADSET_MONO_RX   = 11; //tty_headset_mono_rx
static const uint32_t DEVICE_TTY_HEADSET_MONO_TX   = 12; //tty_headset_mono_tx
static const uint32_t DEVICE_SPEAKER_HEADSET_RX    = 13; //headset_stereo_speaker_stereo_rx
static const uint32_t DEVICE_FMRADIO_STEREO_TX     = 14;
static const uint32_t DEVICE_HDMI_STERO_RX         = 15; //hdmi_stereo_rx
static const uint32_t DEVICE_ANC_HEADSET_STEREO_RX = 16; //ANC RX
static const uint32_t DEVICE_BT_SCO_RX             = 17; //bt_sco_rx
static const uint32_t DEVICE_BT_SCO_TX             = 18; //bt_sco_tx
static const uint32_t DEVICE_FMRADIO_STEREO_RX     = 19;
#ifdef SAMSUNG_AUDIO
// Samsung devices
static uint32_t DEVICE_HANDSET_VOIP_RX       = 40; // handset_voip_rx
static uint32_t DEVICE_HANDSET_VOIP_TX       = 41; // handset_voip_tx
static uint32_t DEVICE_SPEAKER_VOIP_RX       = 42; // speaker_voip_rx
static uint32_t DEVICE_SPEAKER_VOIP_TX       = 43; // speaker_voip_tx
static uint32_t DEVICE_HEADSET_VOIP_RX       = 44; // headset_voip_rx
static uint32_t DEVICE_HEADSET_VOIP_TX       = 45; // headset_voip_tx
static uint32_t DEVICE_HANDSET_CALL_RX       = 60; // handset_call_rx
static uint32_t DEVICE_HANDSET_CALL_TX       = 61; // handset_call_tx
static uint32_t DEVICE_SPEAKER_CALL_RX       = 62; // speaker_call_rx
static uint32_t DEVICE_SPEAKER_CALL_TX       = 63; // speaker_call_tx
static uint32_t DEVICE_HEADSET_CALL_RX       = 64; // headset_call_rx
static uint32_t DEVICE_HEADSET_CALL_TX       = 65; // headset_call_tx
static uint32_t DEVICE_SPEAKER_VR_TX         = 82; // speaker_vr_tx
static uint32_t DEVICE_HEADSET_VR_TX         = 83; // headset_vr_tx
#endif
static uint32_t DEVICE_CAMCORDER_TX          = 105; // camcoder_tx (misspelled by Samsung)
                                                    // secondary_mic_tx (sony)

static uint32_t FLUENCE_MODE_ENDFIRE   = 0;
static uint32_t FLUENCE_MODE_BROADSIDE = 1;
static int vr_enable = 0;

int dev_cnt = 0;
const char ** name = NULL;
int mixer_cnt = 0;
static uint32_t cur_tx = INVALID_DEVICE;
static uint32_t cur_rx = INVALID_DEVICE;
#ifdef QCOM_VOIP_ENABLED
int voip_session_id = 0;
int voip_session_mute = 0;
#endif
int voice_session_id = 0;
int voice_session_mute = 0;
static bool dualmic_enabled = false;
static bool anc_running = false;
static bool anc_setting = false;
// This flag is used for avoiding multiple init/deinit of ANC driver.
static bool anc_enabled = false;
bool vMicMute = false;

#ifdef QCOM_ACDB_ENABLED
static bool bInitACDB = false;
#endif
#ifdef HTC_ACOUSTIC_AUDIO
int rx_htc_acdb = 0;
int tx_htc_acdb = 0;
static bool support_aic3254 = true;
static bool aic3254_enabled = true;
int (*set_sound_effect)(const char* effect);
static bool support_tpa2051 = true;
static bool support_htc_backmic = true;
static bool fm_enabled = false;
static int alt_enable = 0;
static int hac_enable = 0;
static uint32_t cur_aic_tx = UPLINK_OFF;
static uint32_t cur_aic_rx = DOWNLINK_OFF;
static int cur_tpa_mode = 0;
#endif

typedef struct routing_table
{
    unsigned short dec_id;
    int dev_id;
    int dev_id_tx;
    int stream_type;
    bool active;
    struct routing_table *next;
} Routing_table;
Routing_table* head;
Mutex       mRoutingTableLock;

typedef struct device_table
{
    int dev_id;
    int acdb_id;
    int class_id;
    int capability;
}Device_table;
Device_table* device_list;

enum STREAM_TYPES {
    PCM_PLAY=1,
    PCM_REC,
#ifdef QCOM_TUNNEL_LPA_ENABLED
    LPA_DECODE,
#endif
    VOICE_CALL,
#ifdef QCOM_VOIP_ENABLED
    VOIP_CALL,
#endif
#ifdef QCOM_FM_ENABLED
    FM_RADIO,
    FM_REC,
    FM_A2DP,
#endif
    INVALID_STREAM
};

typedef struct ComboDeviceType
{
    uint32_t DeviceId;
    STREAM_TYPES StreamType;
}CurrentComboDeviceStruct;
CurrentComboDeviceStruct CurrentComboDeviceData;
Mutex   mComboDeviceLock;

#ifdef QCOM_FM_ENABLED
enum FM_STATE {
    FM_INVALID=1,
    FM_OFF,
    FM_ON
};

FM_STATE fmState = FM_INVALID;
#endif

static uint32_t fmDevice = INVALID_DEVICE;

#define MAX_DEVICE_COUNT 200
#define DEV_ID(X) device_list[X].dev_id
#ifdef QCOM_ACDB_ENABLED
#define ACDB_ID(X) device_list[X].acdb_id
#endif
#define CAPABILITY(X) device_list[X].capability

void addToTable(int decoder_id,int device_id,int device_id_tx,int stream_type,bool active) {
    Routing_table* temp_ptr;
    ALOGD("addToTable stream %d",stream_type);
    Mutex::Autolock lock(mRoutingTableLock);
    temp_ptr = (Routing_table* ) malloc(sizeof(Routing_table));
    temp_ptr->next = NULL;
    temp_ptr->dec_id = decoder_id;
    temp_ptr->dev_id = device_id;
    temp_ptr->dev_id_tx = device_id_tx;
    temp_ptr->stream_type = stream_type;
    temp_ptr->active = active;
    //make sure Voice node is always on top.
    //For voice call device Switching, there a limitation
    //Routing must happen before disabling/Enabling device.
    if(head->next != NULL){
       if(head->next->stream_type == VOICE_CALL){
          temp_ptr->next = head->next->next;
          head->next->next = temp_ptr;
          return;
       }
    }
    //add new Node to head.
    temp_ptr->next =head->next;
    head->next = temp_ptr;
}

bool isStreamOn(int Stream_type) {
    Routing_table* temp_ptr;
    Mutex::Autolock lock(mRoutingTableLock);
    temp_ptr = head->next;
    while(temp_ptr!=NULL) {
        if(temp_ptr->stream_type == Stream_type)
                return true;
        temp_ptr=temp_ptr->next;
    }
    return false;
}

bool isStreamOnAndActive(int Stream_type) {
    Routing_table* temp_ptr;
    Mutex::Autolock lock(mRoutingTableLock);
    temp_ptr = head->next;
    while(temp_ptr!=NULL) {
        if(temp_ptr->stream_type == Stream_type) {
            if(temp_ptr->active == true) {
                return true;
            }
            else {
                return false;
            }
        }
        temp_ptr=temp_ptr->next;
    }
    return false;
}

bool isStreamOnAndInactive(int Stream_type) {
    Routing_table* temp_ptr;
    Mutex::Autolock lock(mRoutingTableLock);
    temp_ptr = head->next;
    while(temp_ptr!=NULL) {
        if(temp_ptr->stream_type == Stream_type) {
            if(temp_ptr->active == false) {
                return true;
            }
            else {
                return false;
            }
        }
        temp_ptr=temp_ptr->next;
    }
    return false;
}

Routing_table*  getNodeByStreamType(int Stream_type) {
    Routing_table* temp_ptr;
    Mutex::Autolock lock(mRoutingTableLock);
    temp_ptr = head->next;
    while(temp_ptr!=NULL) {
        if(temp_ptr->stream_type == Stream_type) {
            return temp_ptr;
        }
        temp_ptr=temp_ptr->next;
    }
    return NULL;
}

void modifyActiveStateOfStream(int Stream_type, bool Active) {
    Routing_table* temp_ptr;
    Mutex::Autolock lock(mRoutingTableLock);
    temp_ptr = head->next;
    while(temp_ptr!=NULL) {
        if(temp_ptr->stream_type == Stream_type) {
            temp_ptr->active = Active;
        }
        temp_ptr=temp_ptr->next;
    }
}

void modifyActiveDeviceOfStream(int Stream_type,int Device_id,int Device_id_tx) {
    Routing_table* temp_ptr;
    temp_ptr = head->next;
    while(temp_ptr!=NULL) {
        if(temp_ptr->stream_type == Stream_type) {
            temp_ptr->dev_id = Device_id;
            temp_ptr->dev_id_tx = Device_id_tx;
        }
        temp_ptr=temp_ptr->next;
    }
}

void printTable()
{
    Routing_table * temp_ptr;
    Mutex::Autolock lock(mRoutingTableLock);
    temp_ptr = head->next;
    while(temp_ptr!=NULL) {
        printf("%d %d %d %d %d\n",temp_ptr->dec_id,temp_ptr->dev_id,temp_ptr->dev_id_tx,temp_ptr->stream_type,temp_ptr->active);
        temp_ptr = temp_ptr->next;
    }
}

void deleteFromTable(int Stream_type) {
    Routing_table *temp_ptr,*temp1;
    ALOGD("deleteFromTable stream %d",Stream_type);
    Mutex::Autolock lock(mRoutingTableLock);
    temp_ptr = head;
    while(temp_ptr->next!=NULL) {
        if(temp_ptr->next->stream_type == Stream_type) {
            temp1 = temp_ptr->next;
            temp_ptr->next = temp_ptr->next->next;
            free(temp1);
            return;
        }
        temp_ptr=temp_ptr->next;
    }

}

bool isDeviceListEmpty() {
    if(head->next == NULL)
        return true;
    else
        return false;
}

#ifdef QCOM_ANC_HEADSET_ENABLED
//NEEDS to be called with device already enabled
#define ANC_ACDB_STEREO_FF_ID 26
int enableANC(int enable, uint32_t device)
{
    int rc;
    device = 16;
    ALOGD("%s: enable=%d, device=%d", __func__, enable, device);

    // If anc is already enabled/disabled, then don't initalize the driver again.
    if (enable == anc_enabled)
    {
        ALOGV("ANC driver is already in state %d. Not calling anc driver", enable);
        return -EPERM;
    }

    if (enable) {
#ifdef QCOM_ACDB_ENABLED
        rc = acdb_loader_send_anc_cal(ANC_ACDB_STEREO_FF_ID);
        if (rc) {
            ALOGE("Error processing ANC ACDB data\n");
            return rc;
        }
#endif
    }
    rc = msm_enable_anc(DEV_ID(device),enable);

    if ( rc == 0 )
    {
        ALOGV("msm_enable_anc was successful");
        anc_enabled = enable;
    } else
    {
        ALOGV("msm_enable_anc failed");

    }

    return rc;
}
#endif

#ifdef QCOM_ACDB_ENABLED
static void initACDB() {
    while(bInitACDB == false) {
        ALOGD("Calling acdb_loader_init_ACDB()");
        if(acdb_loader_init_ACDB() == 0){
            ALOGD("acdb_loader_init_ACDB() successful");
            bInitACDB = true;
        }
    }
}
#endif

int enableDevice(int device,short enable) {

    // prevent disabling of a device if it doesn't exist
    //Temporaray hack till speaker_tx device is mainlined
    if(DEV_ID(device) == INVALID_DEVICE) {
        return 0;
    }
#ifdef QCOM_ACDB_ENABLED
    if(bInitACDB == false) {
        initACDB();
    }
#endif
    ALOGV("value of device and enable is %d %d ALSA dev id:%d",device,enable,DEV_ID(device));
    if( msm_en_device(DEV_ID(device), enable)) {
        ALOGE("msm_en_device(%d,%d) failed errno = %d",DEV_ID(device),enable, errno);
        return -1;
    }
    return 0;
}

static status_t updateDeviceInfo(int rx_device,int tx_device) {
    bool isRxDeviceEnabled = false,isTxDeviceEnabled = false;
    Routing_table *temp_ptr,*temp_head;
    int tx_dev_prev = INVALID_DEVICE;
    temp_head = head;

    ALOGD("updateDeviceInfo: E");
    Mutex::Autolock lock(mDeviceSwitchLock);

    if(temp_head->next == NULL) {
        ALOGD("simple device switch");
        if(cur_rx!=INVALID_DEVICE)
            enableDevice(cur_rx,0);
        if(cur_tx != INVALID_DEVICE)
            enableDevice(cur_tx,0);
        cur_rx = rx_device;
        cur_tx = tx_device;
        if(cur_rx == DEVICE_ANC_HEADSET_STEREO_RX) {
            enableDevice(cur_rx,1);
            enableDevice(cur_tx,1);
        }
        return NO_ERROR;
    }

    Mutex::Autolock lock_1(mRoutingTableLock);

    while(temp_head->next != NULL) {
        temp_ptr = temp_head->next;
        switch(temp_ptr->stream_type) {
            case PCM_PLAY:
#ifdef QCOM_TUNNEL_LPA_ENABLED
            case LPA_DECODE:
#endif
#ifdef QCOM_FM_ENABLED
            case FM_RADIO:
            case FM_A2DP:
#endif
                if(rx_device == INVALID_DEVICE)
                    return -1;
                ALOGD("The node type is %d", temp_ptr->stream_type);
                ALOGV("rx_device = %d,temp_ptr->dev_id = %d",rx_device,temp_ptr->dev_id);
                if(rx_device != temp_ptr->dev_id) {
                    enableDevice(temp_ptr->dev_id,0);
                }
                if(msm_route_stream(PCM_PLAY,temp_ptr->dec_id,DEV_ID(temp_ptr->dev_id),0)) {
                     ALOGV("msm_route_stream(PCM_PLAY,%d,%d,0) failed",temp_ptr->dec_id,DEV_ID(temp_ptr->dev_id));
                }
                if(isRxDeviceEnabled == false) {
                    enableDevice(rx_device,1);
#ifdef QCOM_ACDB_ENABLED
                    acdb_loader_send_audio_cal(ACDB_ID(rx_device), CAPABILITY(rx_device));
#endif
                    isRxDeviceEnabled = true;
                }
                if(msm_route_stream(PCM_PLAY,temp_ptr->dec_id,DEV_ID(rx_device),1)) {
                    ALOGV("msm_route_stream(PCM_PLAY,%d,%d,1) failed",temp_ptr->dec_id,DEV_ID(rx_device));
                }
                modifyActiveDeviceOfStream(temp_ptr->stream_type,rx_device,INVALID_DEVICE);
                cur_tx = tx_device ;
                cur_rx = rx_device ;
                break;

            case PCM_REC:

                if(tx_device == INVALID_DEVICE)
                    return -1;

                // If dual  mic is enabled in QualComm settings then that takes preference.
                if ( dualmic_enabled && (DEV_ID(DEVICE_DUALMIC_SPEAKER_TX) != INVALID_DEVICE))
                {
                   tx_device = DEVICE_DUALMIC_SPEAKER_TX;
                }

                ALOGD("case PCM_REC");
                if(isTxDeviceEnabled == false) {
                    enableDevice(temp_ptr->dev_id,0);
                    enableDevice(tx_device,1);
#ifdef QCOM_ACDB_ENABLED
                    acdb_loader_send_audio_cal(ACDB_ID(tx_device), CAPABILITY(tx_device));
#endif
                    isTxDeviceEnabled = true;
                }
                if(msm_route_stream(PCM_REC,temp_ptr->dec_id,DEV_ID(temp_ptr->dev_id),0)) {
                    ALOGV("msm_route_stream(PCM_PLAY,%d,%d,0) failed",temp_ptr->dec_id,DEV_ID(temp_ptr->dev_id));
                }
                if(msm_route_stream(PCM_REC,temp_ptr->dec_id,DEV_ID(tx_device),1)) {
                    ALOGV("msm_route_stream(PCM_REC,%d,%d,1) failed",temp_ptr->dec_id,DEV_ID(tx_device));
                }
                modifyActiveDeviceOfStream(PCM_REC,tx_device,INVALID_DEVICE);
                tx_dev_prev = cur_tx;
                cur_tx = tx_device ;
                cur_rx = rx_device ;
                if((vMicMute == true) && (tx_dev_prev != cur_tx)) {
                    ALOGD("REC:device switch with mute enabled :tx_dev_prev %d cur_tx: %d",tx_dev_prev, cur_tx);
                    msm_device_mute(DEV_ID(cur_tx), true);
                }
                break;
            case VOICE_CALL:
#ifdef QCOM_VOIP_ENABLED
            case VOIP_CALL:
#endif
                if(rx_device == INVALID_DEVICE || tx_device == INVALID_DEVICE)
                    return -1;
                ALOGD("case VOICE_CALL/VOIP CALL %d",temp_ptr->stream_type);
#ifdef QCOM_ACDB_ENABLED
    #ifdef HTC_ACOUSTIC_AUDIO
                if (rx_htc_acdb == 0)
                    rx_htc_acdb = ACDB_ID(rx_device);
                if (tx_htc_acdb == 0)
                    tx_htc_acdb = ACDB_ID(tx_device);
                ALOGD("acdb_loader_send_voice_cal acdb_rx = %d, acdb_tx = %d", rx_htc_acdb, tx_htc_acdb);
                acdb_loader_send_voice_cal(rx_htc_acdb, tx_htc_acdb);
    #else
                acdb_loader_send_voice_cal(ACDB_ID(rx_device),ACDB_ID(tx_device));
    #endif
#endif
                msm_route_voice(DEV_ID(rx_device),DEV_ID(tx_device),1);

                // Temporary work around for Speaker mode. The driver is not
                // supporting Speaker Rx and Handset Tx combo
                if(isRxDeviceEnabled == false) {
                    if (rx_device != temp_ptr->dev_id)
                    {
                        enableDevice(temp_ptr->dev_id,0);
                    }
                    isRxDeviceEnabled = true;
                }
                if(isTxDeviceEnabled == false) {
                    if (tx_device != temp_ptr->dev_id_tx)
                    {
                        enableDevice(temp_ptr->dev_id_tx,0);
                    }
                    isTxDeviceEnabled = true;
                }

                if (rx_device != temp_ptr->dev_id)
                {
                    enableDevice(rx_device,1);
                }

                if (tx_device != temp_ptr->dev_id_tx)
                {
                    enableDevice(tx_device,1);
                }

                cur_rx = rx_device;
                cur_tx = tx_device;
                modifyActiveDeviceOfStream(temp_ptr->stream_type,cur_rx,cur_tx);
                break;
            default:
                break;
        }
        temp_head = temp_head->next;
    }
    ALOGV("updateDeviceInfo: X");
    return NO_ERROR;
}

void freeMemory() {
    Routing_table *temp;
    while(head != NULL) {
        temp = head->next;
        free(head);
        head = temp;
    }
free(device_list);
}

//
// ----------------------------------------------------------------------------

AudioHardware::AudioHardware() :
    mInit(false), mMicMute(true), mBluetoothNrec(true), mBluetoothId(0),
#ifdef HTC_ACOUSTIC_AUDIO
    mHACSetting(false), mBluetoothIdTx(0), mBluetoothIdRx(0),
#endif
    mOutput(0),mBluetoothVGS(false),
    mCurSndDevice(-1),
    mTtyMode(TTY_OFF), mFmFd(-1), mNumPcmRec(0)
#ifdef QCOM_VOIP_ENABLED
    ,mVoipFd(-1), mVoipInActive(false), mVoipOutActive(false), mDirectOutput(0), mVoipBitRate(0),
    mDirectOutrefCnt(0)
#endif
#ifdef HTC_ACOUSTIC_AUDIO
    , mRecordState(false), mEffectEnabled(false)
#endif
{

    int control;
    int i = 0,index = 0;
#ifdef QCOM_ACDB_ENABLED
    int acdb_id = INVALID_ACDB_ID;
#endif
    int fluence_mode = FLUENCE_MODE_ENDFIRE;
    char value[128];
#ifdef HTC_ACOUSTIC_AUDIO
    int (*snd_get_num)();
    int (*snd_get_bt_endpoint)(msm_bt_endpoint *);
    int (*set_acoustic_parameters)();
    int (*set_tpa2051_parameters)();
    int (*set_aic3254_parameters)();
    int (*support_back_mic)();

    struct msm_bt_endpoint *ept;
#endif
        head = (Routing_table* ) malloc(sizeof(Routing_table));
        head->next = NULL;

#ifdef HTC_ACOUSTIC_AUDIO
        acoustic =:: dlopen("/system/lib/libhtc_acoustic.so", RTLD_NOW);
        if (acoustic == NULL ) {
            ALOGD("Could not open libhtc_acoustic.so");
            /* this is not really an error on non-htc devices... */
            mNumBTEndpoints = 0;
            support_aic3254 = false;
            support_tpa2051 = false;
            support_htc_backmic = false;
        }
#endif

        ALOGD("msm_mixer_open: Opening the device");
        control = msm_mixer_open("/dev/snd/controlC0", 0);
        if(control< 0)
                ALOGE("ERROR opening the device");


        mixer_cnt = msm_mixer_count();
        ALOGD("msm_mixer_count:mixer_cnt =%d",mixer_cnt);

        dev_cnt = msm_get_device_count();
        ALOGV("got device_count %d",dev_cnt);
        if (dev_cnt <= 0) {
           ALOGE("NO devices registered\n");
           return;
        }

        //End any voice call if it exists. This is to ensure the next request
        //to voice call after a mediaserver crash or sub system restart
        //is not ignored by the voice driver.
        if (msm_end_voice() < 0)
            ALOGE("msm_end_voice() failed");

        if(msm_reset_all_device() < 0)
            ALOGE("msm_reset_all_device() failed");

        name = msm_get_device_list();
        device_list = (Device_table* )malloc(sizeof(Device_table)*MAX_DEVICE_COUNT);
        if(device_list == NULL) {
            ALOGE("malloc failed for device list");
            return;
        }
        property_get("persist.audio.fluence.mode",value,"0");
        if (!strcmp("broadside", value)) {
              fluence_mode = FLUENCE_MODE_BROADSIDE;
        }

    property_get("persist.audio.vr.enable",value,"Unknown");
    if (!strcmp("true", value))
        vr_enable = 1;

        for(i = 0;i<MAX_DEVICE_COUNT;i++)
            device_list[i].dev_id = INVALID_DEVICE;

        for(i = 0; i < dev_cnt;i++) {
            if(strcmp((char* )name[i],"handset_rx") == 0) {
                index = DEVICE_HANDSET_RX;
            }
            else if(strcmp((char* )name[i],"handset_tx") == 0) {
                index = DEVICE_HANDSET_TX;
            }
            else if((strcmp((char* )name[i],"speaker_stereo_rx") == 0) || 
                    (strcmp((char* )name[i],"speaker_stereo_rx_playback") == 0) ||
                    (strcmp((char* )name[i],"speaker_rx") == 0)) {
                index = DEVICE_SPEAKER_RX;
            }
            else if((strcmp((char* )name[i],"speaker_mono_tx") == 0) || (strcmp((char* )name[i],"speaker_tx") == 0)) {
                index = DEVICE_SPEAKER_TX;
            }
            else if((strcmp((char* )name[i],"headset_stereo_rx") == 0) || (strcmp((char* )name[i],"headset_rx") == 0)) {
                index = DEVICE_HEADSET_RX;
            }
            else if((strcmp((char* )name[i],"headset_mono_tx") == 0) || (strcmp((char* )name[i],"headset_tx") == 0)) {
                index = DEVICE_HEADSET_TX;
            }
            else if(strcmp((char* )name[i],"fmradio_handset_rx") == 0) {
                index = DEVICE_FMRADIO_HANDSET_RX;
            }
            else if((strcmp((char* )name[i],"fmradio_headset_rx") == 0) || (strcmp((char* )name[i],"fm_radio_headset_rx") == 0)) {
                index = DEVICE_FMRADIO_HEADSET_RX;
            }
            else if((strcmp((char* )name[i],"fmradio_speaker_rx") == 0) || (strcmp((char* )name[i],"fm_radio_speaker_rx") == 0)) {
                index = DEVICE_FMRADIO_SPEAKER_RX;
            }
            else if((strcmp((char* )name[i],"handset_dual_mic_endfire_tx") == 0) || (strcmp((char* )name[i],"dualmic_handset_ef_tx") == 0)) {
                if (fluence_mode == FLUENCE_MODE_ENDFIRE) {
                     index = DEVICE_DUALMIC_HANDSET_TX;
                } else {
                     ALOGV("Endfire handset found but user request for %d\n", fluence_mode);
                     continue;
                }
            }
            else if((strcmp((char* )name[i],"speaker_dual_mic_endfire_tx") == 0)|| (strcmp((char* )name[i],"dualmic_speaker_ef_tx") == 0)) {
                if (fluence_mode == FLUENCE_MODE_ENDFIRE) {
                     index = DEVICE_DUALMIC_SPEAKER_TX;
                } else {
                     ALOGV("Endfire speaker found but user request for %d\n", fluence_mode);
                     continue;
                }
            }
            else if(strcmp((char* )name[i],"handset_dual_mic_broadside_tx") == 0) {
                if (fluence_mode == FLUENCE_MODE_BROADSIDE) {
                     index = DEVICE_DUALMIC_HANDSET_TX;
                } else {
                     ALOGV("Broadside handset found but user request for %d\n", fluence_mode);
                     continue;
                }
            }
            else if(strcmp((char* )name[i],"speaker_dual_mic_broadside_tx") == 0) {
                if (fluence_mode == FLUENCE_MODE_BROADSIDE) {
                     index = DEVICE_DUALMIC_SPEAKER_TX;
                } else {
                     ALOGV("Broadside speaker found but user request for %d\n", fluence_mode);
                     continue;
                }
            }
            else if((strcmp((char* )name[i],"tty_headset_mono_rx") == 0) || (strcmp((char* )name[i],"tty_headset_rx") == 0)) {
                index = DEVICE_TTY_HEADSET_MONO_RX;
            }
            else if((strcmp((char* )name[i],"tty_headset_mono_tx") == 0) || (strcmp((char* )name[i],"tty_headset_tx") == 0)) {
                index = DEVICE_TTY_HEADSET_MONO_TX;
            }
            else if((strcmp((char* )name[i],"bt_sco_rx") == 0) || (strcmp((char* )name[i],"bt_sco_mono_rx") == 0)) {
                index = DEVICE_BT_SCO_RX;
            }
            else if((strcmp((char* )name[i],"bt_sco_tx") == 0) || (strcmp((char* )name[i],"bt_sco_mono_tx") == 0)) {
                index = DEVICE_BT_SCO_TX;
            }
            else if((strcmp((char*)name[i],"headset_stereo_speaker_stereo_rx") == 0) ||
                    (strcmp((char*)name[i],"headset_stereo_rx_playback") == 0) ||
                    (strcmp((char*)name[i],"headset_speaker_stereo_rx") == 0) || (strcmp((char*)name[i],"speaker_headset_rx") == 0)) {
                index = DEVICE_SPEAKER_HEADSET_RX;
            }
            else if((strcmp((char*)name[i],"fmradio_stereo_tx") == 0) || (strcmp((char*)name[i],"fm_radio_tx") == 0)) {
                index = DEVICE_FMRADIO_STEREO_TX;
            }
            else if((strcmp((char*)name[i],"hdmi_stereo_rx") == 0) || (strcmp((char*)name[i],"hdmi_rx") == 0)) {
                index = DEVICE_HDMI_STERO_RX;
            }
            //to check for correct name and ACDB number for ANC
            else if(strcmp((char*)name[i],"anc_headset_stereo_rx") == 0) {
                index = DEVICE_ANC_HEADSET_STEREO_RX;
            }
            else if(strcmp((char*)name[i],"fmradio_stereo_rx") == 0)
                index = DEVICE_FMRADIO_STEREO_RX;
#ifdef SAMSUNG_AUDIO
            else if(strcmp((char* )name[i], "handset_voip_rx") == 0)
                index = DEVICE_HANDSET_VOIP_RX;
            else if(strcmp((char* )name[i], "handset_voip_tx") == 0)
                index = DEVICE_HANDSET_VOIP_TX;
            else if(strcmp((char* )name[i], "speaker_voip_rx") == 0)
                index = DEVICE_SPEAKER_VOIP_RX;
            else if(strcmp((char* )name[i], "speaker_voip_tx") == 0)
                index = DEVICE_SPEAKER_VOIP_TX;
            else if(strcmp((char* )name[i], "headset_voip_rx") == 0)
                index = DEVICE_HEADSET_VOIP_RX;
            else if(strcmp((char* )name[i], "headset_voip_tx") == 0)
                index = DEVICE_HEADSET_VOIP_TX;
            else if(strcmp((char* )name[i], "handset_call_rx") == 0)
                index = DEVICE_HANDSET_CALL_RX;
            else if(strcmp((char* )name[i], "handset_call_tx") == 0)
                index = DEVICE_HANDSET_CALL_TX;
            else if(strcmp((char* )name[i], "speaker_call_rx") == 0)
                index = DEVICE_SPEAKER_CALL_RX;
            else if(strcmp((char* )name[i], "speaker_call_tx") == 0)
                index = DEVICE_SPEAKER_CALL_TX;
            else if(strcmp((char* )name[i], "headset_call_rx") == 0)
                index = DEVICE_HEADSET_CALL_RX;
            else if(strcmp((char* )name[i], "headset_call_tx") == 0)
                index = DEVICE_HEADSET_CALL_TX;
            else if(strcmp((char* )name[i], "speaker_vr_tx") == 0)
                index = DEVICE_SPEAKER_VR_TX;
            else if(strcmp((char* )name[i], "headset_vr_tx") == 0)
                index = DEVICE_HEADSET_VR_TX;
#endif
            else if((strcmp((char* )name[i], "camcoder_tx") == 0) ||
#ifdef SONY_AUDIO
                    (strcmp((char* )name[i], "secondary_mic_tx") == 0))
#else
                    (strcmp((char* )name[i], "camcorder_tx") == 0) ||
                    (strcmp((char* )name[i], "handset_lgcam_tx") == 0))
#endif
                index = DEVICE_CAMCORDER_TX;
            else {
                ALOGI("Not used device: %s", ( char* )name[i]);
                continue;
            }
            ALOGI("index = %d",index);

            device_list[index].dev_id = msm_get_device((char* )name[i]);
            if(device_list[index].dev_id >= 0) {
                    ALOGI("Found device: %s:index = %d,dev_id: %d",( char* )name[i], index,device_list[index].dev_id);
            }
#ifdef QCOM_ACDB_ENABLED
            acdb_mapper_get_acdb_id_from_dev_name((char* )name[i], &device_list[index].acdb_id);
            device_list[index].class_id = msm_get_device_class(device_list[index].dev_id);
            device_list[index].capability = msm_get_device_capability(device_list[index].dev_id);
            ALOGI("acdb ID = %d,class ID = %d,capablity = %d for device %d",device_list[index].acdb_id,
            device_list[index].class_id,device_list[index].capability,device_list[index].dev_id);
#endif
        }

        CurrentComboDeviceData.DeviceId = INVALID_DEVICE;
        CurrentComboDeviceData.StreamType = INVALID_STREAM;
#ifdef HTC_ACOUSTIC_AUDIO
    set_acoustic_parameters = (int (*)(void))::dlsym(acoustic, "set_acoustic_parameters");
    if ((*set_acoustic_parameters) == 0 ) {
        ALOGE("Could not open set_acoustic_parameters()");
        return;
    }

    int rc = set_acoustic_parameters();
    if (rc < 0) {
        ALOGD("Could not set acoustic parameters to share memory: %d", rc);
    }

    /* Check the system property for enable or not the ALT function */
    property_get("htc.audio.alt.enable", value, "0");
    alt_enable = atoi(value);
    ALOGV("Enable ALT function: %d", alt_enable);

    /* Check the system property for enable or not the HAC function */
    property_get("htc.audio.hac.enable", value, "0");
    hac_enable = atoi(value);
    ALOGV("Enable HAC function: %d", hac_enable);

    set_tpa2051_parameters = (int (*)(void))::dlsym(acoustic, "set_tpa2051_parameters");
    if ((*set_tpa2051_parameters) == 0) {
        ALOGI("set_tpa2051_parameters() not present");
        support_tpa2051 = false;
    }

    if (support_tpa2051) {
        if (set_tpa2051_parameters() < 0) {
            ALOGI("Speaker amplifies tpa2051 is not supported");
            support_tpa2051 = false;
        }
    }

    set_aic3254_parameters = (int (*)(void))::dlsym(acoustic, "set_aic3254_parameters");
    if ((*set_aic3254_parameters) == 0 ) {
        ALOGI("set_aic3254_parameters() not present");
        support_aic3254 = false;
    }

    if (support_aic3254) {
        if (set_aic3254_parameters() < 0) {
            ALOGI("AIC3254 DSP is not supported");
            support_aic3254 = false;
        }
    }

    if (support_aic3254) {
        set_sound_effect = (int (*)(const char*))::dlsym(acoustic, "set_sound_effect");
        if ((*set_sound_effect) == 0 ) {
            ALOGI("set_sound_effect() not present");
            ALOGI("AIC3254 DSP is not supported");
            support_aic3254 = false;
        } else
            strcpy(mEffect, "\0");
    }

    support_back_mic = (int (*)(void))::dlsym(acoustic, "support_back_mic");
    if ((*support_back_mic) == 0 ) {
        ALOGI("support_back_mic() not present");
        support_htc_backmic = false;
    }

    if (support_htc_backmic) {
        if (support_back_mic() != 1) {
            ALOGI("HTC DualMic is not supported");
            support_htc_backmic = false;
        }
    }

    snd_get_num = (int (*)(void))::dlsym(acoustic, "snd_get_num");
    if ((*snd_get_num) == 0 ) {
        ALOGD("Could not open snd_get_num()");
    }

    mNumBTEndpoints = snd_get_num();
    ALOGV("mNumBTEndpoints = %d", mNumBTEndpoints);
    mBTEndpoints = new msm_bt_endpoint[mNumBTEndpoints];
    ALOGV("constructed %d SND endpoints)", mNumBTEndpoints);
    ept = mBTEndpoints;
    snd_get_bt_endpoint = (int (*)(msm_bt_endpoint *))::dlsym(acoustic, "snd_get_bt_endpoint");
    if ((*snd_get_bt_endpoint) == 0 ) {
        mInit = true;
        ALOGE("Could not open snd_get_bt_endpoint()");
        return;
    }
    snd_get_bt_endpoint(mBTEndpoints);

    for (int i = 0; i < mNumBTEndpoints; i++) {
        ALOGV("BT name %s (tx,rx)=(%d,%d)", mBTEndpoints[i].name, mBTEndpoints[i].tx, mBTEndpoints[i].rx);
    }
#endif
    mInit = true;
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
    if (acoustic) {
        ::dlclose(acoustic);
        acoustic = 0;
    }
    msm_mixer_close();
#ifdef QCOM_ACDB_ENABLED
    acdb_loader_deallocate_ACDB();
#endif
    freeMemory();

    mInit = false;
}

status_t AudioHardware::initCheck()
{
    return mInit ? NO_ERROR : NO_INIT;
}

AudioStreamOut* AudioHardware::openOutputStream(
        uint32_t devices, int *format, uint32_t *channels, uint32_t *sampleRate, status_t *status)
{
     audio_output_flags_t flags = static_cast<audio_output_flags_t> (*status);

     ALOGD("AudioHardware::openOutputStream devices %x format %d channels %d samplerate %d flags %d",
        devices, *format, *channels, *sampleRate, flags);

    { // scope for the lock
        status_t lStatus;

        Mutex::Autolock lock(mLock);
#ifdef QCOM_VOIP_ENABLED
        // only one output stream allowed
        if (mOutput && !((flags & AUDIO_OUTPUT_FLAG_DIRECT) && (flags & AUDIO_OUTPUT_FLAG_VOIP_RX))
#ifdef QCOM_TUNNEL_LPA_ENABLED
                    && !(flags & AUDIO_OUTPUT_FLAG_TUNNEL)
#endif /* QCOM_TUNNEL_LPA_ENABLED */
                    && !(flags & AUDIO_OUTPUT_FLAG_LPA)) {

            if (status) {
                *status = INVALID_OPERATION;
            }
            ALOGE(" AudioHardware::openOutputStream Only one output stream allowed \n");
            return 0;
        }
        if ((flags & AUDIO_OUTPUT_FLAG_DIRECT) && (flags & AUDIO_OUTPUT_FLAG_VOIP_RX)) {
            // open direct output stream
            if(mDirectOutput == 0) {
               ALOGV(" AudioHardware::openOutputStream Direct output stream \n");
               AudioStreamOutDirect* out = new AudioStreamOutDirect();
               lStatus = out->set(this, devices, format, channels, sampleRate);
               if (status) {
                   *status = lStatus;
               }
               if (lStatus == NO_ERROR) {
                   mDirectOutput = out;
                   mDirectOutrefCnt++;
                   mLock.unlock();
                   if (mVoipInActive)
                       setupDeviceforVoipCall(true);
                   mLock.lock();
                   ALOGV(" \n set sucessful for AudioStreamOutDirect");
               } else {
                   ALOGE(" \n set Failed for AudioStreamOutDirect");
                   delete out;
               }
            }
            else {
                mDirectOutrefCnt++;
                ALOGE(" \n AudioHardware::AudioStreamOutDirect is already open refcnt %d", mDirectOutrefCnt);
            }
            return mDirectOutput;
        } else
#endif
#ifdef QCOM_TUNNEL_LPA_ENABLED
	    if (flags & AUDIO_OUTPUT_FLAG_LPA) {
			status_t err = BAD_VALUE;
            // create new output LPA stream
            AudioSessionOutLPA* out = new AudioSessionOutLPA(this, devices, *format, *channels,*sampleRate,0,&err);
            if(err != NO_ERROR) {
                delete out;
                out = NULL;
            }
            if (status) *status = err;
            mOutputLPA = out;
            return mOutputLPA;

        } else
#endif
#ifdef TUNNEL_PLAYBACK
        if (flags & AUDIO_OUTPUT_FLAG_TUNNEL) {
            status_t err = BAD_VALUE;
            // create new Tunnel output stream
            AudioSessionOutTunnel* out = new AudioSessionOutTunnel(this, devices, *format, *channels,*sampleRate,0,&err);
            if(err != NO_ERROR) {
                delete out;
                out = NULL;
            }
            if (status) *status = err;
            mOutputTunnel = out;
            return mOutputTunnel;
        } else
#endif /*TUNNEL_PLAYBACK*/
        {
            // create new output stream
            AudioStreamOutMSM8x60* out = new AudioStreamOutMSM8x60();
            lStatus = out->set(this, devices, format, channels, sampleRate);
            if (status) {
                *status = lStatus;
            }
            if (lStatus == NO_ERROR) {
                mOutput = out;
            } else {
                delete out;
            }
            return mOutput;
        }
    }
    return NULL;
}


void AudioHardware::closeOutputStream(AudioStreamOut* out) {
    ALOGD("closeOutputStream called");

    Mutex::Autolock lock(mLock);
    if ((mOutput == 0
#ifdef QCOM_VOIP_ENABLED
        && mDirectOutput == 0
#endif
        && mOutputLPA == 0) || ((mOutput != out)
#ifdef QCOM_VOIP_ENABLED
         && (mDirectOutput != out)
#endif
#ifdef TUNNEL_PLAYBACK
        && (mOutputTunnel!= out)
#endif /*TUNNEL_PLAYBACK*/
       && (mOutputLPA != out))) {
        ALOGW("Attempt to close invalid output stream");
    }
    else if (mOutput == out) {
        delete mOutput;
        mOutput = 0;
    }
#ifdef QCOM_VOIP_ENABLED
    else if (mDirectOutput == out) {
        mDirectOutrefCnt--;
        if (mDirectOutrefCnt <= 0) {
            ALOGV(" deleting  mDirectOutput \n");
            delete mDirectOutput;
            mDirectOutput = 0;
        }
    }
#endif
    else if (mOutputLPA == out) {
	    ALOGV(" deleting  mOutputLPA \n");
        delete mOutputLPA;
        mOutputLPA = 0;
	}
#ifdef TUNNEL_PLAYBACK
    else if (mOutputTunnel == out) {
        ALOGD("Closing Tunnel Output");
        delete mOutputTunnel;
        mOutputTunnel = 0;
    }
#endif /*TUNNEL_PLAYBACK*/
}

AudioStreamIn* AudioHardware::openInputStream(
        uint32_t devices, int *format, uint32_t *channels, uint32_t *sampleRate, status_t *status,
        AudioSystem::audio_in_acoustics acoustic_flags)
{
    ALOGD("AudioHardware::openInputStream devices %x format %d channels %d samplerate %d in_p=%x lin_p=%x in_v=%x lin_v=%x",
        devices, *format, *channels, *sampleRate, AUDIO_DEVICE_IN_VOICE_CALL, AudioSystem::DEVICE_IN_VOICE_CALL, AUDIO_DEVICE_IN_COMMUNICATION, AudioSystem::DEVICE_IN_COMMUNICATION);

    // check for valid input source
    if (!AudioSystem::isInputDevice((AudioSystem::audio_devices)devices)) {
        return 0;
    }

    mLock.lock();
#ifdef QCOM_VOIP_ENABLED
    if((devices == AudioSystem::DEVICE_IN_COMMUNICATION) && (*sampleRate <= AUDIO_HW_VOIP_SAMPLERATE_16K)) {
        ALOGE("Create Audio stream Voip \n");
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
        if (mVoipOutActive) {
            inVoip->mSetupDevice = true;
            setupDeviceforVoipCall(true);
        }
        return inVoip;
    } else
#endif /*QCOM_VOIP_ENABLED*/
    {
       if ( (mMode == AudioSystem::MODE_IN_CALL) &&
            (getInputSampleRate(*sampleRate) > AUDIO_HW_IN_SAMPLERATE) &&
            (*format == AUDIO_HW_IN_FORMAT) )
        {
              ALOGE("PCM recording, in a voice call, with sample rate more than 8K not supported \
                   re-configure with 8K and try software re-sampler ");
              *status = -EINVAL;
              *sampleRate = AUDIO_HW_IN_SAMPLERATE;
              mLock.unlock();
              return 0;
        }
        AudioStreamInMSM8x60* in8x60 = new AudioStreamInMSM8x60();
        status_t lStatus = in8x60->set(this, devices, format, channels, sampleRate, acoustic_flags);
        if (status) {
            *status = lStatus;
        }
        if (lStatus != NO_ERROR) {
            ALOGE("Error creating Audio stream AudioStreamInMSM8x60 \n");
            mLock.unlock();
            delete in8x60;
            return 0;
        }
        mInputs.add(in8x60);
        mLock.unlock();
        return in8x60;
    }
}

void AudioHardware::closeInputStream(AudioStreamIn* in) {
    Mutex::Autolock lock(mLock);

    ssize_t index = -1;
    if((index = mInputs.indexOf((AudioStreamInMSM8x60 *)in)) >= 0) {
        ALOGV("closeInputStream AudioStreamInMSM8x60");
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
        mInputs.removeAt(index);
    }
#endif /*QCOM_VOIP_ENABLED*/
    else {
        ALOGE("Attempt to close invalid input stream");
    }
}

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
    int session_id = 0;
    if (mMicMute != state) {
        mMicMute = state;
        ALOGD("setMicMute_nosync calling voice mute with the mMicMute %d", mMicMute);
        if(isStreamOnAndActive(VOICE_CALL)) {
             session_id = voice_session_id;
             voice_session_mute = mMicMute;
#ifdef QCOM_VOIP_ENABLED
        } else if (isStreamOnAndActive(VOIP_CALL)) {
            session_id = voip_session_id;
            voip_session_mute = mMicMute;
#endif
        } else {
            ALOGE(" unknown voice stream");
            return -1;
        }
#ifdef LEGACY_QCOM_VOICE
        msm_set_voice_tx_mute(mMicMute);
#else
        msm_set_voice_tx_mute_ext(mMicMute,session_id);
#endif
    }
    return NO_ERROR;
}

status_t AudioHardware::getMicMute(bool* state)
{
    int session_id = 0;
    if(isStreamOnAndActive(VOICE_CALL)) {
          session_id = voice_session_id;
          *state = mMicMute = voice_session_mute;
#ifdef QCOM_VOIP_ENABLED
    } else if (isStreamOnAndActive(VOIP_CALL)) {
           session_id = voip_session_id;
           *state = mMicMute = voip_session_mute;
#endif
    } else
         *state = mMicMute;
    return NO_ERROR;
}

status_t AudioHardware::setParameters(const String8& keyValuePairs)
{
    AudioParameter param = AudioParameter(keyValuePairs);
    int rc = 0;
    String8 value;
    String8 key;
    const char BT_NREC_KEY[] = "bt_headset_nrec";
    const char BT_NAME_KEY[] = "bt_headset_name";
    const char BT_NREC_VALUE_ON[] = "on";
#ifdef QCOM_FM_ENABLED
    const char FM_NAME_KEY[] = "FMRadioOn";
    const char FM_VALUE_HANDSET[] = "handset";
    const char FM_VALUE_SPEAKER[] = "speaker";
    const char FM_VALUE_HEADSET[] = "headset";
    const char FM_VALUE_FALSE[] = "false";
#endif
#ifdef HTC_ACOUSTIC_AUDIO
    const char ACTIVE_AP[] = "active_ap";
    const char EFFECT_ENABLED[] = "sound_effect_enable";
#endif

    ALOGV("setParameters() %s", keyValuePairs.string());

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
#ifdef HTC_ACOUSTIC_AUDIO
        mBluetoothIdTx = 0;
        mBluetoothIdRx = 0;
        for (int i = 0; i < mNumBTEndpoints; i++) {
            if (!strcasecmp(value.string(), mBTEndpoints[i].name)) {
                mBluetoothIdTx = mBTEndpoints[i].tx;
                mBluetoothIdRx = mBTEndpoints[i].rx;
                ALOGD("Using custom acoustic parameters for %s", value.string());
                break;
            }
        }
        if (mBluetoothIdTx == 0) {
            ALOGD("Using default acoustic parameters "
                 "(%s not in acoustic database)", value.string());
        }
#endif
        doRouting(NULL, 0);
    }

    key = String8(DUALMIC_KEY);
    if (param.get(key, value) == NO_ERROR) {
        if (value == "true") {
            dualmic_enabled = true;
            ALOGI("DualMic feature Enabled");
        } else {
            dualmic_enabled = false;
            ALOGI("DualMic feature Disabled");
        }
        doRouting(NULL, 0);
    }
#ifdef QCOM_ANC_HEADSET_ENABLED
    key = String8(ANC_KEY);
    if (param.get(key, value) == NO_ERROR) {
        if (value == "true") {
          ALOGE("Enabling ANC setting in the setparameter\n");
          anc_setting= true;
        } else {
           ALOGE("Disabling ANC setting in the setparameter\n");
           anc_setting= false;
           //disabling ANC feature.
           enableANC(0,cur_rx);
           anc_running = false;
        }
     doRouting(NULL, 0);
    }
#endif

    key = String8(TTY_MODE_KEY);
    if (param.get(key, value) == NO_ERROR) {
        if (value == "full" || value == "tty_full") {
            mTtyMode = TTY_FULL;
        } else if (value == "hco" || value == "tty_hco") {
            mTtyMode = TTY_HCO;
        } else if (value == "vco" || value == "tty_vco") {
            mTtyMode = TTY_VCO;
        } else {
            mTtyMode = TTY_OFF;
        }
        if(mMode != AUDIO_MODE_IN_CALL){
           return NO_ERROR;
        }
        ALOGI("Changed TTY Mode=%s", value.string());
        if((mMode == AUDIO_MODE_IN_CALL) &&
           (cur_rx == DEVICE_HEADSET_RX) &&
           (cur_tx == DEVICE_HEADSET_TX))
           doRouting(NULL, 0);
    }
#ifdef HTC_ACOUSTIC_AUDIO
    key = String8(ACTIVE_AP);
    if (param.get(key, value) == NO_ERROR) {
        const char* active_ap = value.string();
        ALOGD("Active AP = %s", active_ap);
        strcpy(mActiveAP, active_ap);

        const char* dsp_effect = "\0";
        key = String8(DSP_EFFECT_KEY);
        if (param.get(key, value) == NO_ERROR) {
            ALOGD("DSP Effect = %s", value.string());
            dsp_effect = value.string();
            strcpy(mEffect, dsp_effect);
        }

        key = String8(EFFECT_ENABLED);
        if (param.get(key, value) == NO_ERROR) {
            const char* sound_effect_enable = value.string();
            ALOGD("Sound Effect Enabled = %s", sound_effect_enable);
            if (value == "on") {
                mEffectEnabled = true;
                if (support_aic3254)
                    aic3254_config(get_snd_dev());
            } else {
                strcpy(mEffect, "\0");
                mEffectEnabled = false;
            }
        }
    }
#endif

#ifdef QCOM_VOIP_ENABLED
    key = String8(VOIPRATE_KEY);
    if (param.get(key, value) == NO_ERROR) {
        mVoipBitRate = atoi(value);
        ALOGI("VOIP Bitrate =%d", mVoipBitRate);
        param.remove(key);
    }
#endif /*QCOM_VOIP_ENABLED*/
    return NO_ERROR;
}
#ifdef QCOM_VOIP_ENABLED

uint32_t AudioHardware::getMvsMode(int format, int rate)
{
    switch(format) {
    case AudioSystem::PCM_16_BIT:
        if(rate == AUDIO_HW_VOIP_SAMPLERATE_8K) {
            return MVS_MODE_PCM;
        } else if(rate== AUDIO_HW_VOIP_SAMPLERATE_16K) {
            return MVS_MODE_PCM_WB;
        } else {
            return MVS_MODE_PCM;
        }
        break;
    case AudioSystem::AMR_NB:
        return MVS_MODE_AMR;
        break;
    case AudioSystem::AMR_WB:
        return MVS_MODE_AMR_WB;
        break;
    case AudioSystem::EVRC:
        return   MVS_MODE_IS127;
        break;
    case AudioSystem::EVRCB:
        return MVS_MODE_4GV_NB;
        break;
    case AudioSystem::EVRCWB:
        return MVS_MODE_4GV_WB;
        break;
    default:
        return BAD_INDEX;
    }
}

uint32_t AudioHardware::getMvsRateType(uint32_t mvsMode, uint32_t *rateType)
{
    int ret = 0;

    switch (mvsMode) {
    case MVS_MODE_AMR: {
        switch (mVoipBitRate) {
        case 4750:
            *rateType = MVS_AMR_MODE_0475;
            break;
        case 5150:
            *rateType = MVS_AMR_MODE_0515;
            break;
        case 5900:
            *rateType = MVS_AMR_MODE_0590;
            break;
        case 6700:
            *rateType = MVS_AMR_MODE_0670;
            break;
        case 7400:
            *rateType = MVS_AMR_MODE_0740;
            break;
        case 7950:
            *rateType = MVS_AMR_MODE_0795;
            break;
        case 10200:
            *rateType = MVS_AMR_MODE_1020;
            break;
        case 12200:
            *rateType = MVS_AMR_MODE_1220;
            break;
        default:
            ALOGD("wrong rate for AMR NB.\n");
            ret = -EINVAL;
        break;
        }
        break;
    }
    case MVS_MODE_AMR_WB: {
        switch (mVoipBitRate) {
        case 6600:
            *rateType = MVS_AMR_MODE_0660;
            break;
        case 8850:
            *rateType = MVS_AMR_MODE_0885;
            break;
        case 12650:
            *rateType = MVS_AMR_MODE_1265;
            break;
        case 14250:
            *rateType = MVS_AMR_MODE_1425;
            break;
        case 15850:
            *rateType = MVS_AMR_MODE_1585;
            break;
        case 18250:
            *rateType = MVS_AMR_MODE_1825;
            break;
        case 19850:
            *rateType = MVS_AMR_MODE_1985;
            break;
        case 23050:
            *rateType = MVS_AMR_MODE_2305;
            break;
        case 23850:
            *rateType = MVS_AMR_MODE_2385;
            break;
        default:
            ALOGD("wrong rate for AMR_WB.\n");
            ret = -EINVAL;
            break;
        }
    break;
    }
    case MVS_MODE_PCM:
    case MVS_MODE_PCM_WB:
        *rateType = 0;
        break;
    case MVS_MODE_IS127:
    case MVS_MODE_4GV_NB:
    case MVS_MODE_4GV_WB: {
        switch (mVoipBitRate) {
        case MVS_VOC_0_RATE:
        case MVS_VOC_8_RATE:
        case MVS_VOC_4_RATE:
        case MVS_VOC_2_RATE:
        case MVS_VOC_1_RATE:
            *rateType = mVoipBitRate;
            break;
        default:
            ALOGE("wrong rate for IS127/4GV_NB/WB.\n");
            ret = -EINVAL;
            break;
        }
        break;
    }
        default:
        ALOGE("wrong mode type.\n");
        ret = -EINVAL;
    }
    ALOGD("mode=%d, rate=%u, rateType=%d\n",
        mvsMode, mVoipBitRate, *rateType);
    return ret;
}
#endif /*QCOM_VOIP_ENABLED*/
String8 AudioHardware::getParameters(const String8& keys)
{
    AudioParameter param = AudioParameter(keys);
    String8 value;

    String8 key = String8(DUALMIC_KEY);
    if (param.get(key, value) == NO_ERROR) {
        value = String8(dualmic_enabled ? "true" : "false");
        param.add(key, value);
    }
#ifdef QCOM_FM_ENABLED
    key = String8("Fm-radio");
    if ( param.get(key,value) == NO_ERROR ) {
        if ( getNodeByStreamType(FM_RADIO) ) {
            param.addInt(String8("isFMON"), true );
        }
    }
#endif
    key = String8(BTHEADSET_VGS);
    if (param.get(key, value) == NO_ERROR) {
        if(mBluetoothVGS)
           param.addInt(String8("isVGS"), true);
    }
    key = String8(ECHO_SUPRESSION);
    if (param.get(key, value) == NO_ERROR) {
        value = String8("yes");
        param.add(key, value);
    }

#ifdef HTC_ACOUSTIC_AUDIO
    key = String8(DSP_EFFECT_KEY);
    if (param.get(key, value) == NO_ERROR) {
        value = String8(mCurDspProfile);
        param.add(key, value);
    }
#endif
    ALOGV("AudioHardware::getParameters() %s", param.toString().string());
    return param.toString();
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
    ALOGD("AudioHardware::getInputBufferSize sampleRate %d format %d channelCount %d"
            ,sampleRate, format, channelCount);
    if ( (format != AudioSystem::PCM_16_BIT) &&
         (format != AudioSystem::AMR_NB)     &&
         (format != AudioSystem::AMR_WB)     &&
         (format != AudioSystem::EVRC)       &&
         (format != AudioSystem::EVRCB)      &&
         (format != AudioSystem::EVRCWB)     &&
         (format != AudioSystem::QCELP)      &&
         (format != AudioSystem::AAC)){
        ALOGW("getInputBufferSize bad format: 0x%x", format);
        return 0;
    }
    if (channelCount < 1 || channelCount > 2) {
        ALOGW("getInputBufferSize bad channel count: %d", channelCount);
        return 0;
    }

    size_t bufferSize = 0;

    if (format == AudioSystem::AMR_NB) {
       bufferSize = 320*channelCount;
    } else if (format == AudioSystem::EVRC) {
       bufferSize = 230*channelCount;
    } else if (format == AudioSystem::QCELP) {
       bufferSize = 350*channelCount;
    } else if (format == AudioSystem::AAC) {
       bufferSize = 2048;
    } else if (sampleRate == 8000 || sampleRate == 16000 || sampleRate == 32000) {
       bufferSize = (sampleRate * channelCount * 20 * sizeof(int16_t)) / 1000;
    }
    else if (sampleRate == 11025 || sampleRate == 12000) {
       bufferSize = 256 * sizeof(int16_t) * channelCount;
    }
    else if (sampleRate == 22050 || sampleRate == 24000) {
       bufferSize = 512 * sizeof(int16_t) * channelCount;
    }
    else if (sampleRate == 44100 || sampleRate == 48000) {
       bufferSize = 1024 * sizeof(int16_t) * channelCount;
    }

    ALOGD("getInputBufferSize: sampleRate: %d channelCount: %d bufferSize: %d", sampleRate, channelCount, bufferSize);

    return bufferSize;
}

static status_t set_volume_rpc(uint32_t device,
                               uint32_t method,
                               uint32_t volume)
{
    ALOGV("set_volume_rpc(%d, %d, %d)\n", device, method, volume);

    if (device == -1UL) return NO_ERROR;
     return NO_ERROR;
}

status_t AudioHardware::setVoiceVolume(float v)
{
    int session_id = 0;
    if (v < 0.0) {
        ALOGW("setVoiceVolume(%f) under 0.0, assuming 0.0\n", v);
        v = 0.0;
    } else if (v > 1.0) {
        ALOGW("setVoiceVolume(%f) over 1.0, assuming 1.0\n", v);
        v = 1.0;
    }

#ifdef HTC_ACOUSTIC_AUDIO
    mVoiceVolume = v;
#endif

    if(isStreamOnAndActive(VOICE_CALL)) {
        session_id = voice_session_id;
    }
#ifdef QCOM_VOIP_ENABLED
    else if (isStreamOnAndActive(VOIP_CALL)) {
        session_id = voip_session_id;
    }
#endif
    else {
        ALOGE(" unknown stream ");
        return -1;
    }
    int vol = lrint(v * 100.0);
    // Voice volume levels from android are mapped to driver volume levels as follows.
    // 0 -> 5, 20 -> 4, 40 ->3, 60 -> 2, 80 -> 1, 100 -> 0
    vol = 5 - (vol/20);
    ALOGD("setVoiceVolume(%f)\n", v);
    ALOGI("Setting in-call volume to %d (available range is 5(MIN VOLUME)  to 0(MAX VOLUME)\n", vol);

#ifdef LEGACY_QCOM_VOICE
    if (msm_set_voice_rx_vol(vol)) {
        ALOGE("msm_set_voice_rx_vol(%d) failed errno = %d", vol, errno);
        return -1;
    }
#else
    if(msm_set_voice_rx_vol_ext(vol,session_id)) {
        ALOGE("msm_set_voice_rx_vol(%d) failed errno = %d",vol,errno);
        return -1;
    }
#endif
    ALOGV("msm_set_voice_rx_vol(%d) succeeded session_id %d",vol,session_id);
    return NO_ERROR;
}

#ifdef QCOM_FM_ENABLED
status_t AudioHardware::setFmVolume(float v)
{
    int vol = android::AudioSystem::logToLinear( (v?(v + 0.005):v) );
    if ( vol > 100 ) {
        vol = 100;
    }
    else if ( vol < 0 ) {
        vol = 0;
    }
    ALOGV("setFmVolume(%f)\n", v);
    ALOGV("Setting FM volume to %d (available range is 0 to 100)\n", vol);
    Routing_table* temp = NULL;
    temp = getNodeByStreamType(FM_RADIO);
    if(temp == NULL){
        ALOGV("No Active FM stream is running");
        return NO_ERROR;
    }
    if(msm_set_volume(temp->dec_id, vol)) {
        ALOGE("msm_set_volume(%d) failed for FM errno = %d",vol,errno);
        return -1;
    }
    ALOGV("msm_set_volume(%d) for FM succeeded",vol);
    return NO_ERROR;
}
#endif

status_t AudioHardware::setMasterVolume(float v)
{
    Mutex::Autolock lock(mLock);
    int vol = ceil(v * 7.0);
    ALOGI("Set master volume to %d.\n", vol);

    set_volume_rpc(SND_DEVICE_HANDSET, SND_METHOD_VOICE, vol);
    set_volume_rpc(SND_DEVICE_SPEAKER, SND_METHOD_VOICE, vol);
    set_volume_rpc(SND_DEVICE_BT,      SND_METHOD_VOICE, vol);
    set_volume_rpc(SND_DEVICE_HEADSET, SND_METHOD_VOICE, vol);
    //TBD - does HDMI require this handling

    // We return an error code here to let the audioflinger do in-software
    // volume on top of the maximum volume that we set through the SND API.
    // return error - software mixer will handle it
    return -1;
}

#ifdef HTC_ACOUSTIC_AUDIO
status_t get_batt_temp(int *batt_temp) {
    ALOGD("Enable ALT for speaker");

    int i, fd, len;
    char get_batt_temp[6] = { 0 };
    const char *fn = "/sys/class/power_supply/battery/batt_temp";

    if ((fd = open(fn, O_RDONLY)) < 0) {
        ALOGE("Couldn't open sysfs file batt_temp");
        return UNKNOWN_ERROR;
    }

    if ((len = read(fd, get_batt_temp, sizeof(get_batt_temp))) <= 1) {
        ALOGE("read battery temp fail: %s", strerror(errno));
        close(fd);
        return BAD_VALUE;
    }

    *batt_temp = strtol(get_batt_temp, NULL, 10);
    ALOGD("ALT batt_temp = %d", *batt_temp);

    close(fd);
    return NO_ERROR;
}

status_t do_tpa2051_control(int mode)
{
    int fd, rc;
    int tpa_mode = 0;

    if (mode) {
        if (cur_rx == DEVICE_HEADSET_RX)
            tpa_mode = TPA2051_MODE_VOICECALL_HEADSET;
        else if (cur_rx == DEVICE_SPEAKER_RX)
            tpa_mode = TPA2051_MODE_VOICECALL_SPKR;
    } else {
        switch (cur_rx) {
            case DEVICE_FMRADIO_HEADSET_RX:
                tpa_mode = TPA2051_MODE_FM_HEADSET;
                break;
            case DEVICE_FMRADIO_SPEAKER_RX:
                tpa_mode = TPA2051_MODE_FM_SPKR;
                break;
            case DEVICE_SPEAKER_HEADSET_RX:
                tpa_mode = TPA2051_MODE_RING;
                break;
            case DEVICE_HEADSET_RX:
                tpa_mode = TPA2051_MODE_PLAYBACK_HEADSET;
                break;
            case DEVICE_SPEAKER_RX:
                tpa_mode = TPA2051_MODE_PLAYBACK_SPKR;
                break;
            default:
                break;
        }
    }

    fd = open("/dev/tpa2051d3", O_RDWR);
    if (fd < 0) {
        ALOGE("can't open /dev/tpa2051d3 %d", fd);
        return -1;
    }

    if (tpa_mode != cur_tpa_mode) {
        cur_tpa_mode = tpa_mode;
        rc = ioctl(fd, TPA2051_SET_MODE, &tpa_mode);
        if (rc < 0)
            ALOGE("ioctl TPA2051_SET_MODE failed: %s", strerror(errno));
        else
            ALOGD("update TPA2051_SET_MODE to mode %d success", tpa_mode);
    }

    close(fd);
    return 0;
}
#endif

static status_t do_route_audio_rpc(uint32_t device,
                                   int mode, bool mic_mute)
{
    if(device == -1)
        return 0;

    int new_rx_device = INVALID_DEVICE,new_tx_device = INVALID_DEVICE,fm_device = INVALID_DEVICE;
    Routing_table* temp = NULL;
    ALOGV("do_route_audio_rpc(%d, %d, %d)", device, mode, mic_mute);

    if(device == SND_DEVICE_HANDSET) {
        new_rx_device = DEVICE_HANDSET_RX;
        new_tx_device = DEVICE_HANDSET_TX;
        ALOGV("In HANDSET");
    }
    else if(device == SND_DEVICE_SPEAKER) {
        new_rx_device = DEVICE_SPEAKER_RX;
        new_tx_device = DEVICE_SPEAKER_TX;
        ALOGV("In SPEAKER");
    }
    else if(device == SND_DEVICE_HEADSET) {
        new_rx_device = DEVICE_HEADSET_RX;
        new_tx_device = DEVICE_HEADSET_TX;
        ALOGV("In HEADSET");
    }
    else if(device == SND_DEVICE_NO_MIC_HEADSET) {
        new_rx_device = DEVICE_HEADSET_RX;
        new_tx_device = DEVICE_HANDSET_TX;
        ALOGV("In NO MIC HEADSET");
    }
#ifdef QCOM_FM_ENABLED
    else if (device == SND_DEVICE_FM_HANDSET) {
        fm_device = DEVICE_FMRADIO_HANDSET_RX;
        ALOGV("In FM HANDSET");
    }
    else if(device == SND_DEVICE_FM_SPEAKER) {
        fm_device = DEVICE_FMRADIO_SPEAKER_RX;
        ALOGV("In FM SPEAKER");
    }
    else if(device == SND_DEVICE_FM_HEADSET) {
        fm_device = DEVICE_FMRADIO_HEADSET_RX;
        ALOGV("In FM HEADSET");
    }
#endif
#ifdef SAMSUNG_AUDIO
    else if(device == SND_DEVICE_IN_S_SADC_OUT_HANDSET) {
        new_rx_device = DEVICE_HANDSET_CALL_RX;
        new_tx_device = DEVICE_DUALMIC_HANDSET_TX;
        ALOGV("In DUALMIC_CALL_HANDSET");
        if(DEV_ID(new_tx_device) == INVALID_DEVICE) {
            new_tx_device = DEVICE_HANDSET_CALL_TX;
            ALOGV("Falling back to HANDSET_CALL_RX AND HANDSET_CALL_TX as no DUALMIC_HANDSET_TX support found");
        }
    }
#else
    else if(device == SND_DEVICE_IN_S_SADC_OUT_HANDSET) {
        new_rx_device = DEVICE_HANDSET_RX;
        new_tx_device = DEVICE_DUALMIC_HANDSET_TX;
        ALOGV("In DUALMIC_HANDSET");
        if(DEV_ID(new_tx_device) == INVALID_DEVICE) {
            new_tx_device = DEVICE_HANDSET_TX;
            ALOGV("Falling back to HANDSET_RX AND HANDSET_TX as no DUALMIC_HANDSET_TX support found");
        }
    }
#endif
    else if(device == SND_DEVICE_IN_S_SADC_OUT_SPEAKER_PHONE) {
        new_rx_device = DEVICE_SPEAKER_RX;
        new_tx_device = DEVICE_DUALMIC_SPEAKER_TX;
        ALOGV("In DUALMIC_SPEAKER");
        if(DEV_ID(new_tx_device) == INVALID_DEVICE) {
            new_tx_device = DEVICE_SPEAKER_TX;
            ALOGV("Falling back to SPEAKER_RX AND SPEAKER_TX as no DUALMIC_SPEAKER_TX support found");
        }
    }
    else if(device == SND_DEVICE_TTY_FULL) {
        new_rx_device = DEVICE_TTY_HEADSET_MONO_RX;
        new_tx_device = DEVICE_TTY_HEADSET_MONO_TX;
        ALOGV("In TTY_FULL");
    }
    else if(device == SND_DEVICE_TTY_VCO) {
        new_rx_device = DEVICE_TTY_HEADSET_MONO_RX;
        new_tx_device = DEVICE_HANDSET_TX;
        ALOGV("In TTY_VCO");
    }
    else if(device == SND_DEVICE_TTY_HCO) {
        new_rx_device = DEVICE_HANDSET_RX;
        new_tx_device = DEVICE_TTY_HEADSET_MONO_TX;
        ALOGV("In TTY_HCO");
    }
#ifdef HTC_ACOUSTIC_AUDIO
    else if((device == SND_DEVICE_BT) ||
            (device == SND_DEVICE_BT_EC_OFF)) {
#else
    else if(device == SND_DEVICE_BT) {
#endif
        new_rx_device = DEVICE_BT_SCO_RX;
        new_tx_device = DEVICE_BT_SCO_TX;
        ALOGV("In BT_HCO");
    }
    else if(device == SND_DEVICE_HEADSET_AND_SPEAKER) {
        new_rx_device = DEVICE_SPEAKER_HEADSET_RX;
        new_tx_device = DEVICE_HEADSET_TX;
        ALOGV("In DEVICE_SPEAKER_HEADSET_RX and DEVICE_HEADSET_TX");
        if(DEV_ID(new_rx_device) == INVALID_DEVICE) {
             new_rx_device = DEVICE_HEADSET_RX;
             ALOGV("Falling back to HEADSET_RX AND HEADSET_TX as no combo device support found");
        }
    }
    else if(device == SND_DEVICE_HEADPHONE_AND_SPEAKER) {
        new_rx_device = DEVICE_SPEAKER_HEADSET_RX;
        new_tx_device = DEVICE_HANDSET_TX;
        ALOGV("In DEVICE_SPEAKER_HEADSET_RX and DEVICE_HANDSET_TX");
        if(DEV_ID(new_rx_device) == INVALID_DEVICE) {
             new_rx_device = DEVICE_HEADSET_RX;
             ALOGV("Falling back to HEADSET_RX AND HANDSET_TX as no combo device support found");
        }
    }
    else if (device == SND_DEVICE_HDMI) {
        new_rx_device = DEVICE_HDMI_STERO_RX;
        new_tx_device = cur_tx;
        ALOGI("In DEVICE_HDMI_STERO_RX and cur_tx");
    }
#ifdef QCOM_ANC_HEADSET_ENABLED
    else if(device == SND_DEVICE_ANC_HEADSET) {
        new_rx_device = DEVICE_ANC_HEADSET_STEREO_RX;
        new_tx_device = DEVICE_HEADSET_TX;
        ALOGI("In ANC HEADSET");
    }
    else if(device == SND_DEVICE_NO_MIC_ANC_HEADSET) {
        new_rx_device = DEVICE_ANC_HEADSET_STEREO_RX;
        new_tx_device = DEVICE_HANDSET_TX;
        ALOGI("In ANC HEADPhone");
    }
#endif
#ifdef QCOM_FM_ENABLED
    else if(device == SND_DEVICE_FM_TX){
        new_rx_device = DEVICE_FMRADIO_STEREO_RX;
        ALOGI("In DEVICE_FMRADIO_STEREO_RX and cur_tx");
    }
#endif
    else if(device == SND_DEVICE_SPEAKER_TX) {
        new_rx_device = cur_rx;
        new_tx_device = DEVICE_SPEAKER_TX;
        ALOGI("In SPEAKER_TX cur_rx = %d\n", cur_rx);
    }
#ifdef SAMSUNG_AUDIO
    else if (device == SND_DEVICE_VOIP_HANDSET) {
        new_rx_device = DEVICE_HANDSET_VOIP_RX;
        new_tx_device = DEVICE_HANDSET_VOIP_TX;
        ALOGD("In VOIP HANDSET");
    }
    else if (device == SND_DEVICE_VOIP_SPEAKER) {
        new_rx_device = DEVICE_SPEAKER_VOIP_RX;
        new_tx_device = DEVICE_SPEAKER_VOIP_TX;
        ALOGD("In VOIP SPEAKER");
    }
    else if (device == SND_DEVICE_VOIP_HEADSET) {
        new_rx_device = DEVICE_HEADSET_VOIP_RX;
        new_tx_device = DEVICE_HEADSET_VOIP_TX;
        ALOGD("In VOIP HEADSET");
    }
    else if (device == SND_DEVICE_CALL_HANDSET) {
        new_rx_device = DEVICE_HANDSET_CALL_RX;
        new_tx_device = DEVICE_HANDSET_CALL_TX;
        ALOGD("In CALL HANDSET");
    }
    else if (device == SND_DEVICE_CALL_SPEAKER) {
        new_rx_device = DEVICE_SPEAKER_CALL_RX;
        new_tx_device = DEVICE_SPEAKER_CALL_TX;
        ALOGD("In CALL SPEAKER");
    }
    else if (device == SND_DEVICE_CALL_HEADSET) {
        new_rx_device = DEVICE_HEADSET_CALL_RX;
        new_tx_device = DEVICE_HEADSET_CALL_TX;
        ALOGD("In CALL HEADSET");
    }
    else if(device == SND_DEVICE_VR_SPEAKER) {
        new_rx_device = DEVICE_SPEAKER_RX;
        new_tx_device = DEVICE_SPEAKER_VR_TX;
        ALOGV("In VR SPEAKER");
    }
    else if(device == SND_DEVICE_VR_HEADSET) {
        new_rx_device = DEVICE_HEADSET_RX;
        new_tx_device = DEVICE_HEADSET_VR_TX;
        ALOGV("In VR HEADSET");
    }
#endif
#ifdef BACK_MIC_CAMCORDER
    else if (device == SND_DEVICE_BACK_MIC_CAMCORDER) {
        new_rx_device = cur_rx;
        new_tx_device = DEVICE_CAMCORDER_TX;
        ALOGV("In BACK_MIC_CAMCORDER");
    }
#endif
    if(new_rx_device != INVALID_DEVICE)
        ALOGD("new_rx = %d", DEV_ID(new_rx_device));
    if(new_tx_device != INVALID_DEVICE)
        ALOGD("new_tx = %d", DEV_ID(new_tx_device));

    if ((mode == AUDIO_MODE_IN_CALL) && !isStreamOn(VOICE_CALL)) {
#ifdef LEGACY_QCOM_VOICE
        msm_start_voice();
#endif
        ALOGV("Going to enable RX/TX device for voice stream");
            // Routing Voice
            if ( (new_rx_device != INVALID_DEVICE) && (new_tx_device != INVALID_DEVICE))
            {
#ifdef QCOM_ACDB_ENABLED
                initACDB();
                acdb_loader_send_voice_cal(ACDB_ID(new_rx_device),ACDB_ID(new_tx_device));
#endif
                ALOGD("Starting voice on Rx %d and Tx %d device", DEV_ID(new_rx_device), DEV_ID(new_tx_device));
                msm_route_voice(DEV_ID(new_rx_device),DEV_ID(new_tx_device), 1);
            }
            else
            {
                return -1;
            }

            if(cur_rx != INVALID_DEVICE && (enableDevice(cur_rx,0) == -1))
                    return -1;

            if(cur_tx != INVALID_DEVICE&&(enableDevice(cur_tx,0) == -1))
                    return -1;

           //Enable RX device
           if(new_rx_device !=INVALID_DEVICE && (enableDevice(new_rx_device,1) == -1))
               return -1;
            //Enable TX device
           if(new_tx_device !=INVALID_DEVICE && (enableDevice(new_tx_device,1) == -1))
               return -1;
#ifdef LEGACY_QCOM_VOICE
           msm_set_voice_tx_mute(0);
#else
           voice_session_id = msm_get_voc_session(VOICE_SESSION_NAME);
           if(voice_session_id <=0) {
                ALOGE("voice session invalid");
                return 0;
           }
           msm_start_voice_ext(voice_session_id);
           msm_set_voice_tx_mute_ext(voice_session_mute,voice_session_id);
#endif

           if(!isDeviceListEmpty())
               updateDeviceInfo(new_rx_device,new_tx_device);
            cur_rx = new_rx_device;
            cur_tx = new_tx_device;
            addToTable(0,cur_rx,cur_tx,VOICE_CALL,true);
    }
    else if ((mode == AUDIO_MODE_NORMAL) && isStreamOnAndActive(VOICE_CALL)) {
        ALOGV("Going to disable RX/TX device during end of voice call");
        temp = getNodeByStreamType(VOICE_CALL);
        if(temp == NULL)
            return 0;

        // Ending voice call
        ALOGD("Ending Voice call");
#ifdef LEGACY_QCOM_VOICE
        msm_end_voice();
#else
        msm_end_voice_ext(voice_session_id);
        voice_session_id = 0;
        voice_session_mute = 0;
#endif

        if((temp->dev_id != INVALID_DEVICE && temp->dev_id_tx != INVALID_DEVICE)
#ifdef QCOM_VOIP_ENABLED
        && (!isStreamOn(VOIP_CALL))
#endif
        ) {
           enableDevice(temp->dev_id,0);
           enableDevice(temp->dev_id_tx,0);
        }
        deleteFromTable(VOICE_CALL);
        updateDeviceInfo(new_rx_device,new_tx_device);
        if(new_rx_device != INVALID_DEVICE && new_tx_device != INVALID_DEVICE) {
            cur_rx = new_rx_device;
            cur_tx = new_tx_device;
        }
    }
    else {
        ALOGD("updateDeviceInfo() called for default case");
        updateDeviceInfo(new_rx_device,new_tx_device);
    }
#ifdef HTC_ACOUSTIC_AUDIO
    if (support_tpa2051)
        do_tpa2051_control(mode ^1);
#endif
    return NO_ERROR;
}

// always call with mutex held
status_t AudioHardware::doAudioRouteOrMute(uint32_t device)
{
// BT acoustics is not supported. This might be used by OEMs. Hence commenting
// the code and not removing it.
#if 0
    if (device == (uint32_t)SND_DEVICE_BT || device == (uint32_t)SND_DEVICE_CARKIT) {
        if (mBluetoothId) {
            device = mBluetoothId;
        } else if (!mBluetoothNrec) {
            device = SND_DEVICE_BT_EC_OFF;
        }
    }
#endif

#ifdef HTC_ACOUSTIC_AUDIO
    if (device == SND_DEVICE_BT) {
        if (!mBluetoothNrec)
            device = SND_DEVICE_BT_EC_OFF;
    }

    if (support_aic3254) {
        aic3254_config(device);
        do_aic3254_control(device);
    }

    getACDB(device);
#endif

    if (isStreamOnAndActive(VOICE_CALL) && mMicMute == false)
        msm_set_voice_tx_mute(0);

#ifdef HTC_ACOUSTIC_AUDIO
    if (isInCall())
        setVoiceVolume(mVoiceVolume);
#endif
    ALOGV("doAudioRouteOrMute() device %x, mMode %d, mMicMute %d", device, mMode, mMicMute);
    return do_route_audio_rpc(device, mMode, mMicMute);
}

#ifdef HTC_ACOUSTIC_AUDIO
status_t AudioHardware::set_mRecordState(bool onoff) {
    mRecordState = onoff;
    return 0;
}

status_t AudioHardware::get_mRecordState(void) {
    return mRecordState;
}

status_t AudioHardware::get_snd_dev(void) {
    return mCurSndDevice;
}

void AudioHardware::getACDB(uint32_t device) {
    rx_htc_acdb = 0;
    tx_htc_acdb = 0;

    if (device == SND_DEVICE_BT) {
        if (mBluetoothIdTx != 0) {
            rx_htc_acdb = mBluetoothIdRx;
            tx_htc_acdb = mBluetoothIdTx;
        } else {
            /* use default BT entry defined in AudioBTID.csv */
            rx_htc_acdb = mBTEndpoints[0].rx;
            tx_htc_acdb = mBTEndpoints[0].tx;
        }
    } else if (device == SND_DEVICE_CARKIT ||
               device == SND_DEVICE_BT_EC_OFF) {
        if (mBluetoothIdTx != 0) {
            rx_htc_acdb = mBluetoothIdRx;
            tx_htc_acdb = mBluetoothIdTx;
        } else {
            /* use default carkit entry defined in AudioBTID.csv */
            rx_htc_acdb = mBTEndpoints[1].rx;
            tx_htc_acdb = mBTEndpoints[1].tx;
        }
    }

    ALOGV("getACDB: device = %d, HTC RX ACDB ID = %d, HTC TX ACDB ID = %d",
         device, rx_htc_acdb, tx_htc_acdb);
}

status_t AudioHardware::do_aic3254_control(uint32_t device) {
    ALOGD("do_aic3254_control device: %d mode: %d record: %d", device, mMode, mRecordState);

    uint32_t new_aic_txmode = UPLINK_OFF;
    uint32_t new_aic_rxmode = DOWNLINK_OFF;

    Mutex::Autolock lock(mAIC3254ConfigLock);

    if (isInCall()) {
        switch (device) {
            case SND_DEVICE_HEADSET:
                new_aic_rxmode = CALL_DOWNLINK_EMIC_HEADSET;
                new_aic_txmode = CALL_UPLINK_EMIC_HEADSET;
                break;
            case SND_DEVICE_SPEAKER:
            case SND_DEVICE_SPEAKER_BACK_MIC:
                new_aic_rxmode = CALL_DOWNLINK_IMIC_SPEAKER;
                new_aic_txmode = CALL_UPLINK_IMIC_SPEAKER;
                break;
            case SND_DEVICE_HEADSET_AND_SPEAKER:
            case SND_DEVICE_HEADSET_AND_SPEAKER_BACK_MIC:
                new_aic_rxmode = RING_HEADSET_SPEAKER;
                break;
            case SND_DEVICE_NO_MIC_HEADSET:
            case SND_DEVICE_NO_MIC_HEADSET_BACK_MIC:
                new_aic_rxmode = CALL_DOWNLINK_IMIC_HEADSET;
                new_aic_txmode = CALL_UPLINK_IMIC_HEADSET;
                break;
            case SND_DEVICE_HANDSET:
            case SND_DEVICE_HANDSET_BACK_MIC:
                new_aic_rxmode = CALL_DOWNLINK_IMIC_RECEIVER;
                new_aic_txmode = CALL_UPLINK_IMIC_RECEIVER;
                break;
            default:
                break;
        }
    } else {
#ifdef QCOM_TUNNEL_LPA_ENABLED
        if (checkOutputStandby() && !isStreamOnAndActive(LPA_DECODE)) {
#else
        if (checkOutputStandby()) {
#endif
            if (device == SND_DEVICE_FM_HEADSET) {
                new_aic_rxmode = FM_OUT_HEADSET;
                new_aic_txmode = FM_IN_HEADSET;
            } else if (device == SND_DEVICE_FM_SPEAKER) {
                new_aic_rxmode = FM_OUT_SPEAKER;
                new_aic_txmode = FM_IN_SPEAKER;
            }
        } else {
            switch (device) {
                case SND_DEVICE_HEADSET_AND_SPEAKER:
                case SND_DEVICE_HEADSET_AND_SPEAKER_BACK_MIC:
                case SND_DEVICE_HEADPHONE_AND_SPEAKER:
                    new_aic_rxmode = RING_HEADSET_SPEAKER;
                    break;
                case SND_DEVICE_SPEAKER:
                case SND_DEVICE_SPEAKER_BACK_MIC:
                    new_aic_rxmode = PLAYBACK_SPEAKER;
                    break;
                case SND_DEVICE_HANDSET:
                case SND_DEVICE_HANDSET_BACK_MIC:
                    new_aic_rxmode = PLAYBACK_RECEIVER;
                    break;
                case SND_DEVICE_HEADSET:
                case SND_DEVICE_NO_MIC_HEADSET:
                case SND_DEVICE_NO_MIC_HEADSET_BACK_MIC:
                    new_aic_rxmode = PLAYBACK_HEADSET;
                    break;
                default:
                    break;
            }
        }

        if (mRecordState) {
            switch (device) {
                case SND_DEVICE_HEADSET:
                    new_aic_txmode = VOICERECORD_EMIC;
                    break;
                case SND_DEVICE_HANDSET_BACK_MIC:
                case SND_DEVICE_SPEAKER_BACK_MIC:
                case SND_DEVICE_NO_MIC_HEADSET_BACK_MIC:
                case SND_DEVICE_HEADSET_AND_SPEAKER_BACK_MIC:
                    new_aic_txmode = VIDEORECORD_IMIC;
                    break;
                case SND_DEVICE_HANDSET:
                case SND_DEVICE_SPEAKER:
                case SND_DEVICE_NO_MIC_HEADSET:
                case SND_DEVICE_HEADSET_AND_SPEAKER:
                    new_aic_txmode = VOICERECORD_IMIC;
                    break;
                default:
                    break;
            }
        }
    }

    if (new_aic_rxmode != cur_aic_rx)
        ALOGD("do_aic3254_control: set aic3254 rx to %d", new_aic_rxmode);
    if (aic3254_ioctl(AIC3254_CONFIG_RX, new_aic_rxmode) >= 0)
        cur_aic_rx = new_aic_rxmode;

    if (new_aic_txmode != cur_aic_tx)
        ALOGD("do_aic3254_control: set aic3254 tx to %d", new_aic_txmode);
    if (aic3254_ioctl(AIC3254_CONFIG_TX, new_aic_txmode) >= 0)
        cur_aic_tx = new_aic_txmode;

    if (cur_aic_tx == UPLINK_OFF && cur_aic_rx == DOWNLINK_OFF && aic3254_enabled) {
        strcpy(mCurDspProfile, "\0");
        aic3254_enabled = false;
        aic3254_powerdown();
    } else if (cur_aic_tx != UPLINK_OFF || cur_aic_rx != DOWNLINK_OFF)
        aic3254_enabled = true;

    return NO_ERROR;
}

bool AudioHardware::isAic3254Device(uint32_t device) {
    switch(device) {
        case SND_DEVICE_HANDSET:
        case SND_DEVICE_SPEAKER:
        case SND_DEVICE_HEADSET:
        case SND_DEVICE_NO_MIC_HEADSET:
        case SND_DEVICE_FM_HEADSET:
        case SND_DEVICE_HEADSET_AND_SPEAKER:
        case SND_DEVICE_FM_SPEAKER:
        case SND_DEVICE_HEADPHONE_AND_SPEAKER:
        case SND_DEVICE_HANDSET_BACK_MIC:
        case SND_DEVICE_SPEAKER_BACK_MIC:
        case SND_DEVICE_NO_MIC_HEADSET_BACK_MIC:
        case SND_DEVICE_HEADSET_AND_SPEAKER_BACK_MIC:
            return true;
            break;
        default:
            return false;
            break;
    }
}

status_t AudioHardware::aic3254_config(uint32_t device) {
    ALOGD("aic3254_config: device %d enabled %d", device, aic3254_enabled);
    char name[22] = "\0";
    char aap[9] = "\0";

    if ((!isAic3254Device(device) ||
         !aic3254_enabled) &&
        strlen(mCurDspProfile) != 0)
        return NO_ERROR;

    Mutex::Autolock lock(mAIC3254ConfigLock);

    if (mMode == AUDIO_MODE_IN_CALL) {
        strcpy(name, "Phone_Default");
        switch (device) {
            case SND_DEVICE_HANDSET:
            case SND_DEVICE_HANDSET_BACK_MIC:
                strcpy(name, "Phone_Handset_Dualmic");
                break;
            case SND_DEVICE_HEADSET:
            case SND_DEVICE_HEADSET_AND_SPEAKER:
            case SND_DEVICE_HEADSET_AND_SPEAKER_BACK_MIC:
            case SND_DEVICE_NO_MIC_HEADSET:
                strcpy(name, "Phone_Headset");
                break;
            case SND_DEVICE_SPEAKER:
                strcpy(name, "Phone_Speaker_Dualmic");
                break;
            default:
                break;
        }
    } else {
        if ((strcasecmp(mActiveAP, "Camcorder") == 0)) {
            if (strlen(mEffect) != 0) {
                strcpy(name, "Recording_");
                strcat(name, mEffect);
            } else
                strcpy(name, "Playback_Default");
        } else if (mRecordState) {
            strcpy(name, "Record_Default");
        } else if (strlen(mEffect) == 0 && !mEffectEnabled)
            strcpy(name, "Playback_Default");
        else {
            if (mEffectEnabled)
                strcpy(name, mEffect);

            if ((strcasecmp(name, "Srs") == 0) ||
                (strcasecmp(name, "Dolby") == 0)) {
                strcpy(mEffect, name);
                if (strcasecmp(mActiveAP, "Music") == 0)
                    strcat(name, "_a");
                else if (strcasecmp(mActiveAP, "Video") == 0)
                    strcat(name, "_v");
                if (device == SND_DEVICE_SPEAKER)
                    strcat(name, "_spk");
                else
                    strcat(name, "_hp");
            }
        }
    }

    if (strcasecmp(mCurDspProfile, name)) {
        ALOGD("aic3254_config: loading effect %s", name);
        strcpy(mCurDspProfile, name);
    } else {
        ALOGD("aic3254_config: effect %s already loaded", name);
        return NO_ERROR;
    }

    int rc = set_sound_effect(name);
    if (rc < 0) {
        ALOGE("Could not set sound effect %s: %d", name, rc);
        return rc;
    }

    return NO_ERROR;
}

int AudioHardware::aic3254_ioctl(int cmd, const int argc) {
    int rc = -1;
    int (*set_aic3254_ioctl)(int, const int*);

    set_aic3254_ioctl = (int (*)(int, const int*))::dlsym(acoustic, "set_aic3254_ioctl");
    if ((*set_aic3254_ioctl) == 0) {
        ALOGE("Could not open set_aic3254_ioctl()");
        return rc;
    }

    rc = set_aic3254_ioctl(cmd, &argc);
    if (rc < 0)
        ALOGE("aic3254_ioctl failed");

    return rc;
}

void AudioHardware::aic3254_powerdown() {
    int rc = aic3254_ioctl(AIC3254_POWERDOWN, 0);
    if (rc < 0)
        ALOGE("aic3254_powerdown failed");
    else
        ALOGI("aic3254 powered off");
}
#endif

status_t AudioHardware::doRouting(AudioStreamInMSM8x60 *input, uint32_t outputDevices)
{
    Mutex::Autolock lock(mLock);
    status_t ret = NO_ERROR;
    int audProcess = (ADRC_DISABLE | EQ_DISABLE | RX_IIR_DISABLE);
    int sndDevice = -1;

    if (!outputDevices)
        outputDevices = mOutput->devices();

    ALOGD("outputDevices = %x", outputDevices);

    if (input != NULL) {
        uint32_t inputDevice = input->devices();
        ALOGI("do input routing device %x\n", inputDevice);
        // ignore routing device information when we start a recording in voice
        // call
        // Recording will happen through currently active tx device
        if((inputDevice == AUDIO_DEVICE_IN_VOICE_CALL)
#ifdef QCOM_FM_ENABLED
           || (inputDevice == AUDIO_DEVICE_IN_FM_RX)
           || (inputDevice == AUDIO_DEVICE_IN_FM_RX_A2DP)
#endif
        )
            return NO_ERROR;
        if (inputDevice != 0) {
            if (inputDevice & AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET) {
                ALOGI("Routing audio to Bluetooth PCM\n");
                sndDevice = SND_DEVICE_BT;
#ifdef BACK_MIC_CAMCORDER
            } else if (inputDevice & AUDIO_DEVICE_IN_BACK_MIC) {
                ALOGI("Routing audio to back mic (camcorder)");
                sndDevice = SND_DEVICE_BACK_MIC_CAMCORDER;
#endif
            } else if (inputDevice & AUDIO_DEVICE_IN_WIRED_HEADSET) {
                if ((outputDevices & AUDIO_DEVICE_OUT_WIRED_HEADSET) &&
                    (outputDevices & AUDIO_DEVICE_OUT_SPEAKER)) {
                    ALOGI("Routing audio to Wired Headset and Speaker\n");
                    sndDevice = SND_DEVICE_HEADSET_AND_SPEAKER;
                    audProcess = (ADRC_ENABLE | EQ_ENABLE | RX_IIR_ENABLE | MBADRC_ENABLE);
                } else {
                    ALOGI("Routing audio to Wired Headset\n");
                    sndDevice = SND_DEVICE_HEADSET;
                }
            }
#ifdef QCOM_ANC_HEADSET_ENABLED
            else if (inputDevice & AUDIO_DEVICE_IN_ANC_HEADSET) {
                    ALOGI("Routing audio to ANC Headset\n");
                    sndDevice = SND_DEVICE_ANC_HEADSET;
                }
#endif
#if defined(SAMSUNG_AUDIO) && defined(QCOM_VOIP_ENABLED)
            else if (isStreamOnAndActive(VOIP_CALL)) {
                if (outputDevices & AUDIO_DEVICE_OUT_EARPIECE) {
                        ALOGD("Routing audio to VOIP handset\n");
                        sndDevice = SND_DEVICE_VOIP_HANDSET;
                }
                else if (outputDevices & AUDIO_DEVICE_OUT_SPEAKER) {
                        ALOGD("Routing audio to VOIP speaker\n");
                        sndDevice = SND_DEVICE_VOIP_HANDSET;
                }
            }
#endif
            else if (isStreamOnAndActive(PCM_PLAY)
#ifdef QCOM_TUNNEL_LPA_ENABLED
                     || isStreamOnAndActive(LPA_DECODE)
#endif
              ) {
                if (outputDevices & AUDIO_DEVICE_OUT_EARPIECE) {
                    ALOGI("Routing audio to Handset\n");
                    sndDevice = SND_DEVICE_HANDSET;
                } else if (outputDevices & AUDIO_DEVICE_OUT_WIRED_HEADPHONE) {
                    ALOGI("Routing audio to Speakerphone (LPA headphone case)\n");
                    sndDevice = SND_DEVICE_NO_MIC_HEADSET;
                }
#ifdef QCOM_FM_ENABLED
                 else if (outputDevices & AUDIO_DEVICE_OUT_FM_TX) {
                    ALOGE("Routing audio_rx to Speaker\n");
                    sndDevice = SND_DEVICE_SPEAKER_TX;
                }
#endif
                else {
                    ALOGI("Routing audio to Speaker (LPA case)\n");
                    sndDevice = SND_DEVICE_SPEAKER;
                }
            } else {
                ALOGI("Routing audio to Speaker (default)\n");
                sndDevice = SND_DEVICE_SPEAKER;
            }
#ifdef SAMSUNG_AUDIO
            if (input->isForVR()) {
                if (sndDevice == SND_DEVICE_SPEAKER)
                    sndDevice = SND_DEVICE_VR_SPEAKER;
                else if (sndDevice == SND_DEVICE_HEADSET)
                    sndDevice = SND_DEVICE_VR_HEADSET;
            }
#endif
        }
        // if inputDevice == 0, restore output routing
    }

    if (sndDevice == -1) {
        if (outputDevices & (outputDevices - 1)) {
            if ((outputDevices & AUDIO_DEVICE_OUT_SPEAKER) == 0) {
                ALOGW("Hardware does not support requested route combination (%#X),"
                     " picking closest possible route...", outputDevices);
            }
        }
        if ((mTtyMode != TTY_OFF) && (mMode == AUDIO_MODE_IN_CALL) &&
                ((outputDevices & AUDIO_DEVICE_OUT_WIRED_HEADSET)
#ifdef QCOM_ANC_HEADSET_ENABLED
                 ||(outputDevices & AUDIO_DEVICE_OUT_ANC_HEADSET)
#endif
            )) {
            if (mTtyMode == TTY_FULL) {
                ALOGI("Routing audio to TTY FULL Mode\n");
                sndDevice = SND_DEVICE_TTY_FULL;
            } else if (mTtyMode == TTY_VCO) {
                ALOGI("Routing audio to TTY VCO Mode\n");
                sndDevice = SND_DEVICE_TTY_VCO;
            } else if (mTtyMode == TTY_HCO) {
                ALOGI("Routing audio to TTY HCO Mode\n");
                sndDevice = SND_DEVICE_TTY_HCO;
            }
        } else if (outputDevices &
                   (AUDIO_DEVICE_OUT_BLUETOOTH_SCO | AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET)) {
            ALOGI("Routing audio to Bluetooth PCM\n");
            sndDevice = SND_DEVICE_BT;
        } else if (outputDevices & AUDIO_DEVICE_OUT_BLUETOOTH_SCO_CARKIT) {
            ALOGI("Routing audio to Bluetooth PCM\n");
            sndDevice = SND_DEVICE_CARKIT;
        } else if (outputDevices & AUDIO_DEVICE_OUT_AUX_DIGITAL) {
            ALOGI("Routing audio to HDMI\n");
            sndDevice = SND_DEVICE_HDMI;
        } else if ((outputDevices & AUDIO_DEVICE_OUT_WIRED_HEADSET) &&
                   (outputDevices & AUDIO_DEVICE_OUT_SPEAKER)) {
            ALOGI("Routing audio to Wired Headset and Speaker\n");
            sndDevice = SND_DEVICE_HEADSET_AND_SPEAKER;
            audProcess = (ADRC_ENABLE | EQ_ENABLE | RX_IIR_ENABLE | MBADRC_ENABLE);
        } else 
#ifdef QCOM_FM_ENABLED
          if ((outputDevices & AUDIO_DEVICE_OUT_FM_TX) &&
                   (outputDevices & AUDIO_DEVICE_OUT_SPEAKER)) {
            ALOGI("Routing audio to FM Tx and Speaker\n");
            sndDevice = SND_DEVICE_FM_TX_AND_SPEAKER;
            enableComboDevice(sndDevice,1);
            audProcess = (ADRC_ENABLE | EQ_ENABLE | RX_IIR_ENABLE | MBADRC_ENABLE);
        }  else
#endif
          if (outputDevices & AUDIO_DEVICE_OUT_WIRED_HEADPHONE) {
            if (outputDevices & AUDIO_DEVICE_OUT_SPEAKER) {
                ALOGI("Routing audio to No microphone Wired Headset and Speaker (%d,%x)\n", mMode, outputDevices);
                sndDevice = SND_DEVICE_HEADPHONE_AND_SPEAKER;
                audProcess = (ADRC_ENABLE | EQ_ENABLE | RX_IIR_ENABLE | MBADRC_ENABLE);
            } else {
                ALOGI("Routing audio to No microphone Wired Headset (%d,%x)\n", mMode, outputDevices);
                sndDevice = SND_DEVICE_NO_MIC_HEADSET;
                audProcess = (ADRC_ENABLE | EQ_ENABLE | RX_IIR_ENABLE | MBADRC_ENABLE);
            }
        }
#ifdef QCOM_ANC_HEADSET_ENABLED
             else if (outputDevices & AUDIO_DEVICE_OUT_ANC_HEADPHONE) {
                ALOGI("Routing audio to No microphone ANC Headset (%d,%x)\n", mMode, outputDevices);
                sndDevice = SND_DEVICE_NO_MIC_ANC_HEADSET;
                audProcess = (ADRC_ENABLE | EQ_ENABLE | RX_IIR_ENABLE | MBADRC_ENABLE);
        }
#endif
         else if (outputDevices & AUDIO_DEVICE_OUT_WIRED_HEADSET) {
             ALOGI("Routing audio to Wired Headset\n");
             sndDevice = SND_DEVICE_HEADSET;
             audProcess = (ADRC_ENABLE | EQ_ENABLE | RX_IIR_ENABLE | MBADRC_ENABLE);
        }
#ifdef QCOM_ANC_HEADSET_ENABLED
          else if (outputDevices & AUDIO_DEVICE_OUT_ANC_HEADSET) {
            ALOGI("Routing audio to ANC Headset\n");
            sndDevice = SND_DEVICE_ANC_HEADSET;
            audProcess = (ADRC_ENABLE | EQ_ENABLE | RX_IIR_ENABLE | MBADRC_ENABLE);
        }
#endif
          else if (outputDevices & AUDIO_DEVICE_OUT_SPEAKER) {
#ifdef SAMSUNG_AUDIO
            if (mMode == AUDIO_MODE_IN_CALL) {
              ALOGD("Routing audio to Call Speaker\n");
              sndDevice = SND_DEVICE_CALL_SPEAKER;
            }
#if defined(SAMSUNG_AUDIO) && defined(QCOM_VOIP_ENABLED)
            else if (mMode == AUDIO_MODE_IN_COMMUNICATION) {
              ALOGD("Routing audio to VOIP speaker\n");
              sndDevice = SND_DEVICE_VOIP_SPEAKER;
            }
#endif
            else {
#endif
            ALOGI("Routing audio to Speakerphone (out_speaker case)\n");
            sndDevice = SND_DEVICE_SPEAKER;
            audProcess = (ADRC_ENABLE | EQ_ENABLE | RX_IIR_ENABLE | MBADRC_ENABLE);
#ifdef SAMSUNG_AUDIO
            }
#endif
        } else
#ifdef QCOM_FM_ENABLED
         if (outputDevices & AUDIO_DEVICE_OUT_FM_TX){
            ALOGI("Routing audio to FM Tx Device\n");
            sndDevice = SND_DEVICE_FM_TX;
            audProcess = (ADRC_ENABLE | EQ_ENABLE | RX_IIR_ENABLE | MBADRC_ENABLE);
        } else
#endif
          if(outputDevices & AUDIO_DEVICE_OUT_EARPIECE){
#ifdef SAMSUNG_AUDIO
            if (mMode == AUDIO_MODE_IN_CALL) {
              if (dualmic_enabled) {
                ALOGI("Routing audio to Handset with DualMike enabled\n");
                sndDevice = SND_DEVICE_IN_S_SADC_OUT_HANDSET;
              }
              else {
                ALOGD("Routing audio to Call Handset\n");
                sndDevice = SND_DEVICE_CALL_HANDSET;
              }
            }
#if defined(SAMSUNG_AUDIO) && defined(QCOM_VOIP_ENABLED)
            else if (mMode == AUDIO_MODE_IN_COMMUNICATION) {
              ALOGD("Routing audio to VOIP handset\n");
              sndDevice = SND_DEVICE_VOIP_HANDSET;
            }
#endif
            else {
#endif
            ALOGI("Routing audio to Handset\n");
            sndDevice = SND_DEVICE_HANDSET;
            audProcess = (ADRC_ENABLE | EQ_ENABLE | RX_IIR_ENABLE | MBADRC_ENABLE);
#ifdef SAMSUNG_AUDIO
            }
#endif
        }
    }

#ifndef SAMSUNG_AUDIO
    if (dualmic_enabled) {
        if (sndDevice == SND_DEVICE_HANDSET) {
            ALOGI("Routing audio to Handset with DualMike enabled\n");
            sndDevice = SND_DEVICE_IN_S_SADC_OUT_HANDSET;
        } else if (sndDevice == SND_DEVICE_SPEAKER) {
            ALOGI("Routing audio to Speakerphone with DualMike enabled\n");
            sndDevice = SND_DEVICE_IN_S_SADC_OUT_SPEAKER_PHONE;
        }
    }
#endif

#ifdef SAMSUNG_AUDIO
    if ((mMode == AUDIO_MODE_IN_CALL) && (sndDevice == SND_DEVICE_HEADSET)) {
            ALOGD("Routing audio to Call Headset\n");
            sndDevice = SND_DEVICE_CALL_HEADSET;
#ifdef QCOM_VOIP_ENABLED
    } else if ((mMode == AUDIO_MODE_IN_COMMUNICATION) && (sndDevice == SND_DEVICE_HEADSET)) {
            ALOGD("Routing audio to VOIP headset\n");
            sndDevice = SND_DEVICE_VOIP_HEADSET;
#endif
    }
#endif

#ifdef QCOM_FM_ENABLED
    if ((outputDevices & AUDIO_DEVICE_OUT_FM) && (mFmFd == -1)){
        enableFM(sndDevice);
    }
    if ((mFmFd != -1) && !(outputDevices & AUDIO_DEVICE_OUT_FM)){
        disableFM();
    }

    if ((CurrentComboDeviceData.DeviceId == INVALID_DEVICE) &&
        (sndDevice == SND_DEVICE_FM_TX_AND_SPEAKER )){
        /* speaker rx is already enabled change snd device to the fm tx
         * device and let the flow take the regular route to
         * updatedeviceinfo().
         */
        Mutex::Autolock lock_1(mComboDeviceLock);

        CurrentComboDeviceData.DeviceId = SND_DEVICE_FM_TX_AND_SPEAKER;
        sndDevice = DEVICE_FMRADIO_STEREO_RX;
    }
    else
#endif
    if(CurrentComboDeviceData.DeviceId != INVALID_DEVICE){
        /* time to disable the combo device */
        enableComboDevice(CurrentComboDeviceData.DeviceId,0);
        Mutex::Autolock lock_2(mComboDeviceLock);
        CurrentComboDeviceData.DeviceId = INVALID_DEVICE;
        CurrentComboDeviceData.StreamType = INVALID_STREAM;
    }

    if (sndDevice != -1 && sndDevice != mCurSndDevice) {
        ret = doAudioRouteOrMute(sndDevice);
        mCurSndDevice = sndDevice;
    }
#ifdef QCOM_ANC_HEADSET_ENABLED
    //check if ANC setting is ON
    if (anc_setting == true
                && (sndDevice == SND_DEVICE_ANC_HEADSET
                || sndDevice ==SND_DEVICE_NO_MIC_ANC_HEADSET)) {
        enableANC(1,sndDevice);
        anc_running = true;
    } else {
        //disconnection case
        anc_running = false;
    }
#endif
    return ret;
}

status_t AudioHardware::enableComboDevice(uint32_t sndDevice, bool enableOrDisable)
{
    ALOGD("enableComboDevice %u",enableOrDisable);
    status_t status = NO_ERROR;
#ifdef QCOM_TUNNEL_LPA_ENABLED
    Routing_table *LpaNode = getNodeByStreamType(LPA_DECODE);
#endif
    Routing_table *PcmNode = getNodeByStreamType(PCM_PLAY);

    if(enableDevice(DEVICE_SPEAKER_RX, enableOrDisable)) {
         ALOGE("enableDevice failed for device %d", DEVICE_SPEAKER_RX);
         return -1;
    }
#ifdef QCOM_FM_ENABLED
    if(SND_DEVICE_FM_TX_AND_SPEAKER == sndDevice){

        if(getNodeByStreamType(VOICE_CALL) || getNodeByStreamType(FM_RADIO) ||
           getNodeByStreamType(FM_A2DP)){
            ALOGE("voicecall/FM radio active bailing out");
            return NO_ERROR;
        }

        if(
#ifdef QCOM_TUNNEL_LPA_ENABLED
         !LpaNode &&
#endif
         !PcmNode) {
            ALOGE("No active playback session active bailing out ");
            cur_rx = DEVICE_FMRADIO_STEREO_RX;
            return NO_ERROR;
        }

        Mutex::Autolock lock_1(mComboDeviceLock);

        Routing_table* temp = NULL;

        if (enableOrDisable == 1) {
            if(CurrentComboDeviceData.StreamType == INVALID_STREAM){
                if (PcmNode){
                    temp = PcmNode;
                    CurrentComboDeviceData.StreamType = PCM_PLAY;
                    ALOGD("PCM_PLAY session Active ");
#ifdef QCOM_TUNNEL_LPA_ENABLED
                }else if(LpaNode){
                    temp = LpaNode;
                    CurrentComboDeviceData.StreamType = LPA_DECODE;
                    ALOGD("LPA_DECODE session Active ");
#endif
                } else {
                    ALOGE("no PLAYback session Active ");
                    return -1;
                }
            }else
                temp = getNodeByStreamType(CurrentComboDeviceData.StreamType);

            if(temp == NULL){
                ALOGE("speaker de-route not possible");
                return -1;
            }

            ALOGD("combo:msm_route_stream(%d,%d,1)",temp->dec_id,
                DEV_ID(DEVICE_SPEAKER_RX));
            if(msm_route_stream(PCM_PLAY, temp->dec_id, DEV_ID(DEVICE_SPEAKER_RX),
                1)) {
                ALOGE("msm_route_stream failed");
                return -1;
            }

        }else if(enableOrDisable == 0) {
            temp = getNodeByStreamType(CurrentComboDeviceData.StreamType);


            if(temp == NULL){
                ALOGE("speaker de-route not possible");
                return -1;
            }

            ALOGD("combo:de-route msm_route_stream(%d,%d,0)",temp->dec_id,
                DEV_ID(DEVICE_SPEAKER_RX));
            if(msm_route_stream(PCM_PLAY, temp->dec_id,
                DEV_ID(DEVICE_SPEAKER_RX), 0)) {
                ALOGE("msm_route_stream failed");
                return -1;
            }
        }

    }
#endif

    return status;
}
#ifdef QCOM_FM_ENABLED
status_t AudioHardware::enableFM(int sndDevice)
{
    ALOGD("enableFM");
    status_t status = NO_INIT;
    unsigned short session_id = INVALID_DEVICE;
    status = ::open(FM_DEVICE, O_RDWR);
    if (status < 0) {
           ALOGE("Cannot open FM_DEVICE errno: %d", errno);
           goto Error;
    }
    mFmFd = status;
    if(ioctl(mFmFd, AUDIO_GET_SESSION_ID, &session_id)) {
           ALOGE("AUDIO_GET_SESSION_ID failed*********");
           goto Error;
    }

    if(enableDevice(DEVICE_FMRADIO_STEREO_TX, 1)) {
           ALOGE("enableDevice failed for device %d", DEVICE_FMRADIO_STEREO_TX);
           goto Error;
    }
#ifdef QCOM_ACDB_ENABLED
    acdb_loader_send_audio_cal(ACDB_ID(DEVICE_FMRADIO_STEREO_TX),
    CAPABILITY(DEVICE_FMRADIO_STEREO_TX));
#endif
    if(msm_route_stream(PCM_PLAY, session_id, DEV_ID(DEVICE_FMRADIO_STEREO_TX), 1)) {
           ALOGE("msm_route_stream failed");
           goto Error;
    }
    addToTable(session_id,cur_rx,INVALID_DEVICE,FM_RADIO,true);
    if(sndDevice == mCurSndDevice || mCurSndDevice == -1) {
        enableDevice(cur_rx, 1);
#ifdef QCOM_ACDB_ENABLED
        acdb_loader_send_audio_cal(ACDB_ID(cur_rx), CAPABILITY(cur_rx));
#endif
        msm_route_stream(PCM_PLAY,session_id,DEV_ID(cur_rx),1);
    }
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
    ALOGD("disableFM");
    Routing_table* temp = NULL;
    temp = getNodeByStreamType(FM_RADIO);
    if(temp == NULL)
        return 0;
    if (mFmFd >= 0) {
            ::close(mFmFd);
            mFmFd = -1;
    }
    if(msm_route_stream(PCM_PLAY, temp->dec_id, DEV_ID(DEVICE_FMRADIO_STEREO_TX), 0)) {
           ALOGE("msm_route_stream failed");
           return 0;
    }
    if(!getNodeByStreamType(FM_A2DP)) {
       if(enableDevice(DEVICE_FMRADIO_STEREO_TX, 0)) {
          ALOGE("Device disable failed for device %d", DEVICE_FMRADIO_STEREO_TX);
       }
    }
    deleteFromTable(FM_RADIO);
    if(!getNodeByStreamType(VOICE_CALL)
#ifdef QCOM_TUNNEL_LPA_ENABLED
      && !getNodeByStreamType(LPA_DECODE)
#endif
        && !getNodeByStreamType(PCM_PLAY)
#ifdef QCOM_VOIP_ENABLED
        && !getNodeByStreamType(VOIP_CALL)
#endif
        ) {
        if(enableDevice(cur_rx, 0)) {
            ALOGV("Disable device[%d] failed errno = %d",DEV_ID(cur_rx),errno);
            return 0;
        }
    }
    return NO_ERROR;
}
#endif

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

#ifdef QCOM_VOIP_ENABLED
status_t AudioHardware::setupDeviceforVoipCall(bool value)
{
    ALOGV("setupDeviceforVoipCall value %d",value);
    if (mMode == AudioSystem::MODE_IN_CALL && value == false) {
        ALOGE("mode already set for voice call, do not change to normal");
        return NO_ERROR;
    }
 
    int mode = (value ? AudioSystem::MODE_IN_COMMUNICATION : AudioSystem::MODE_NORMAL);
    if (setMode(mode) == BAD_VALUE) {
        ALOGV("setMode fails");
        return UNKNOWN_ERROR;
    }

    if (setMicMute(!value) != NO_ERROR) {
        ALOGV("MicMute fails");
        return UNKNOWN_ERROR;
    }

    ALOGD("Device setup sucess for VOIP call");

    return NO_ERROR;
}
#endif /*QCOM_VOIP_ENABLED*/
// ----------------------------------------------------------------------------


//  VOIP stream class
//.----------------------------------------------------------------------------
#ifdef QCOM_VOIP_ENABLED
AudioHardware::AudioStreamInVoip::AudioStreamInVoip() :
    mHardware(0), mFd(-1), mState(AUDIO_INPUT_CLOSED), mRetryCount(0),
    mFormat(AUDIO_HW_IN_FORMAT), mChannels(AUDIO_HW_IN_CHANNELS),
    mSampleRate(AUDIO_HW_VOIP_SAMPLERATE_8K), mBufferSize(AUDIO_HW_VOIP_BUFFERSIZE_8K),
    mAcoustics((AudioSystem::audio_in_acoustics)0), mDevices(0), mSetupDevice(false)
{
}

status_t AudioHardware::AudioStreamInVoip::set(
        AudioHardware* hw, uint32_t devices, int *pFormat, uint32_t *pChannels, uint32_t *pRate,
        AudioSystem::audio_in_acoustics acoustic_flags)
{
    ALOGD("AudioStreamInVoip::set devices = %u format = %x pChannels = %u Rate = %u \n",
         devices, *pFormat, *pChannels, *pRate);

    mHardware = hw;

    if ((pFormat == 0) || BAD_INDEX == hw->getMvsMode(*pFormat, *pRate)) {
        ALOGE("Audio Format (%x) not supported \n",*pFormat);
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

    if(*pRate == AUDIO_HW_VOIP_SAMPLERATE_8K)
       mBufferSize = 320;
    else if(*pRate == AUDIO_HW_VOIP_SAMPLERATE_16K)
       mBufferSize = 640;
    else
    {
       ALOGE(" unsupported sample rate");
       return -1;
    }

    ALOGD("AudioStreamInVoip::set(%d, %d, %u)", *pFormat, *pChannels, *pRate);

    status_t status = NO_INIT;
    // open driver
    ALOGV("Check if driver is open");
    if(mHardware->mVoipFd >= 0) {
        mFd = mHardware->mVoipFd;
    } else {
        ALOGV("Going to enable RX/TX device for voice stream");

        Mutex::Autolock lock(mDeviceSwitchLock);
        // Routing Voip
        if ( (cur_rx != INVALID_DEVICE) && (cur_tx != INVALID_DEVICE))
        {
            ALOGV("Starting voip on Rx %d and Tx %d device", DEV_ID(cur_rx), DEV_ID(cur_tx));
            msm_route_voice(DEV_ID(cur_rx),DEV_ID(cur_tx), 1);
        }
        else
        {
            return -1;
        }

        if(cur_rx != INVALID_DEVICE && (enableDevice(cur_rx,1) == -1))
        {
            ALOGE(" Enable device for cur_rx failed \n");
            return -1;
        }

        if(cur_tx != INVALID_DEVICE&&(enableDevice(cur_tx,1) == -1))
        {
            ALOGE(" Enable device for cur_tx failed \n");
            return -1;
        }
#ifdef QCOM_ACDB_ENABLED
        // voice calibration
        acdb_loader_send_voice_cal(ACDB_ID(cur_rx),ACDB_ID(cur_tx));
#endif
        // start Voice call
#ifdef LEGACY_QCOM_VOICE
        msm_start_voice();
        msm_set_voice_tx_mute(0);
#else
        if(voip_session_id <= 0) {
             voip_session_id = msm_get_voc_session(VOIP_SESSION_NAME);
        }
        ALOGD("Starting voip call and UnMuting the call");
        msm_start_voice_ext(voip_session_id);
        msm_set_voice_tx_mute_ext(voip_session_mute,voip_session_id);
#endif
        addToTable(0,cur_rx,cur_tx,VOIP_CALL,true);

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

        // configuration
        ALOGV("get mvs config");
        struct msm_audio_mvs_config mvs_config;
        status = ioctl(mFd, AUDIO_GET_MVS_CONFIG, &mvs_config);
        if (status < 0) {
           ALOGE("Cannot read mvs config");
           goto Error;
        }

        mvs_config.mvs_mode = mHardware->getMvsMode(*pFormat, *pRate);
        status = mHardware->getMvsRateType(mvs_config.mvs_mode ,&mvs_config.rate_type);
        ALOGD("set mvs config mode %u rate_type %u", mvs_config.mvs_mode, mvs_config.rate_type);
        if (status < 0) {
            ALOGE("Incorrect mvs type");
            goto Error;
        }
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
       mBufferSize = AUDIO_HW_VOIP_BUFFERSIZE_8K;
    else if(mSampleRate == AUDIO_HW_VOIP_SAMPLERATE_16K)
       mBufferSize = AUDIO_HW_VOIP_BUFFERSIZE_16K;
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

    mHardware->mVoipInActive = true;

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
    mHardware->mVoipInActive = false;
    standby();
}



ssize_t AudioHardware::AudioStreamInVoip::read( void* buffer, ssize_t bytes)
{
//    ALOGV("AudioStreamInVoip::read(%p, %ld)", buffer, bytes);
    if (!mHardware) return -1;

    size_t count = bytes;
    size_t totalBytesRead = 0;

    if (mState < AUDIO_INPUT_OPENED || (mHardware->mVoipFd == -1)) {
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

    if(count < mBufferSize) {
      ALOGE("read:: read size requested is less than min input buffer size");
      return 0;
    }

    if (!mSetupDevice) {
        mSetupDevice = true;
        mHardware->setupDeviceforVoipCall(true);
    }
    struct msm_audio_mvs_frame audio_mvs_frame;
    memset(&audio_mvs_frame, 0, sizeof(audio_mvs_frame));
    if(mFormat == AudioSystem::PCM_16_BIT) {
    audio_mvs_frame.frame_type = 0;
       while (count >= mBufferSize) {
           audio_mvs_frame.len = mBufferSize;
           ALOGV("Calling read count = %u mBufferSize = %u \n", count, mBufferSize);
           int bytesRead = ::read(mFd, &audio_mvs_frame, sizeof(audio_mvs_frame));
           ALOGV("PCM read_bytes = %d mvs\n", bytesRead);
           if (bytesRead > 0) {
                   memcpy(buffer+totalBytesRead, &audio_mvs_frame.voc_pkt, mBufferSize);
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
    }else{
        struct msm_audio_mvs_frame *mvsFramePtr = (msm_audio_mvs_frame *)buffer;
        int bytesRead = ::read(mFd, &audio_mvs_frame, sizeof(audio_mvs_frame));
        ALOGV("Non PCM read_bytes = %d frame type %d len %d\n", bytesRead, audio_mvs_frame.frame_type, audio_mvs_frame.len);
        mvsFramePtr->frame_type = audio_mvs_frame.frame_type;
        mvsFramePtr->len = audio_mvs_frame.len;
        memcpy(&mvsFramePtr->voc_pkt, &audio_mvs_frame.voc_pkt, audio_mvs_frame.len);
        totalBytesRead = bytes;
    }
  return bytes;
}

status_t AudioHardware::AudioStreamInVoip::standby()
{
    Routing_table* temp = NULL;
    ALOGD("AudioStreamInVoip::standby");
    Mutex::Autolock lock(mHardware->mVoipLock);

    if (!mHardware) return -1;
    ALOGE("VoipOut %d driver fd %d", mHardware->mVoipOutActive, mHardware->mVoipFd);
    mHardware->mVoipInActive = false;
    if (mState > AUDIO_INPUT_CLOSED && !mHardware->mVoipOutActive) {
         int ret = 0;
#ifdef LEGACY_QCOM_VOICE
        msm_end_voice();
#else
         ret = msm_end_voice_ext(voip_session_id);
         if (ret < 0)
                 ALOGE("Error %d ending voice\n", ret);
#endif
        temp = getNodeByStreamType(VOIP_CALL);
        if(temp == NULL)
        {
            ALOGE("VOIPin: Going to disable RX/TX return 0");
            return 0;
        }

        if((temp->dev_id != INVALID_DEVICE && temp->dev_id_tx != INVALID_DEVICE)) {
           if(!getNodeByStreamType(VOICE_CALL)
#ifdef QCOM_TUNNEL_LPA_ENABLED
              && !getNodeByStreamType(LPA_DECODE)
#endif /*QCOM_TUNNEL_LPA_ENABLED*/
              && !getNodeByStreamType(PCM_PLAY)
#ifdef QCOM_FM_ENABLED
              && !getNodeByStreamType(FM_RADIO)
#endif /*QCOM_FM_ENABLED*/
            ) {
#ifdef QCOM_ANC_HEADSET_ENABLED
               if (anc_running == false) {
#endif
                   enableDevice(temp->dev_id, 0);
                   ALOGV("Voipin: disable voip rx");
#ifdef QCOM_ANC_HEADSET_ENABLED
               }
#endif
            }
            if(!getNodeByStreamType(VOICE_CALL) && !getNodeByStreamType(PCM_REC)) {
                 enableDevice(temp->dev_id_tx,0);
                 ALOGD("VOIPin: disable voip tx");
            }
        }
        deleteFromTable(VOIP_CALL);

         if (mHardware->mVoipFd >= 0) {
            ret = ioctl(mHardware->mVoipFd, AUDIO_STOP, NULL);
            ALOGD("MVS stop returned %d %d %d\n", ret, __LINE__, mHardware->mVoipFd);
            ::close(mFd);
            mFd = mHardware->mVoipFd = -1;
            mSetupDevice = false;
            mHardware->setupDeviceforVoipCall(false);
            ALOGD("MVS driver closed %d mFd %d", __LINE__, mHardware->mVoipFd);
        }
        mState = AUDIO_INPUT_CLOSED;
    } else
        ALOGE("Not closing MVS driver");
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
            status = mHardware->doRouting(this, device);
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

    key = String8("voip_flag");
    if (param.get(key, value) == NO_ERROR) {
        param.addInt(key, true);
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
#endif /*QCOM_VOIP_ENABLED*/
// ---------------------------------------------------------------------------
//  VOIP stream class end


AudioHardware::AudioStreamOutMSM8x60::AudioStreamOutMSM8x60() :
    mHardware(0), mFd(-1), mStartCount(0), mRetryCount(0), mStandby(true), mDevices(0)
{
}

status_t AudioHardware::AudioStreamOutMSM8x60::set(
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
        ALOGE("%s: Setting up correct values", __func__);
        return NO_ERROR;
    }

    if (pFormat) *pFormat = lFormat;
    if (pChannels) *pChannels = lChannels;
    if (pRate) *pRate = lRate;

    mDevices = devices;

    return NO_ERROR;
}

AudioHardware::AudioStreamOutMSM8x60::~AudioStreamOutMSM8x60()
{
    if (mFd >= 0) close(mFd);
}

ssize_t AudioHardware::AudioStreamOutMSM8x60::write(const void* buffer, size_t bytes)
{
    //ALOGE("AudioStreamOutMSM8x60::write(%p, %u)", buffer, bytes);
    status_t status = NO_INIT;
    size_t count = bytes;
    const uint8_t* p = static_cast<const uint8_t*>(buffer);
    unsigned short dec_id = INVALID_DEVICE;

    if (mStandby) {

        // open driver
        ALOGV("open driver");
        status = ::open("/dev/msm_pcm_out", O_WRONLY/*O_RDWR*/);
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
#ifdef HTC_ACOUSTIC_AUDIO
        if (support_tpa2051)
            do_tpa2051_control(0);
#endif
    }

    while (count) {
        ssize_t written = ::write(mFd, p, count);
        if (written > 0) {
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
            if(ioctl(mFd, AUDIO_GET_SESSION_ID, &dec_id)) {
                ALOGE("AUDIO_GET_SESSION_ID failed*********");
                return 0;
            }
            ALOGE("write(): dec_id = %d cur_rx = %d\n",dec_id,cur_rx);
            if(cur_rx == INVALID_DEVICE) {
                //return 0; //temporary fix until team upmerges code to froyo tip
                cur_rx = 0;
                cur_tx = 1;
            }

            Mutex::Autolock lock(mDeviceSwitchLock);

#ifdef HTC_ACOUSTIC_AUDIO
            int snd_dev = mHardware->get_snd_dev();
            if (support_aic3254)
                mHardware->do_aic3254_control(snd_dev);
#endif
            ALOGE("cur_rx for pcm playback = %d",cur_rx);
            if(enableDevice(cur_rx, 1)) {
                ALOGE("enableDevice failed for device cur_rx %d", cur_rx);
                return 0;
            }
#ifdef QCOM_ACDB_ENABLED
            acdb_loader_send_audio_cal(ACDB_ID(cur_rx), CAPABILITY(cur_rx));
#endif
            if(msm_route_stream(PCM_PLAY, dec_id, DEV_ID(cur_rx), 1)) {
                ALOGE("msm_route_stream failed");
                return 0;
            }
            Mutex::Autolock lock_1(mComboDeviceLock);
#ifdef QCOM_FM_ENABLED
            if(CurrentComboDeviceData.DeviceId == SND_DEVICE_FM_TX_AND_SPEAKER){
#ifdef QCOM_TUNNEL_LPA_ENABLED
                Routing_table *LpaNode = getNodeByStreamType(LPA_DECODE);
                /* This de-routes the LPA being routed on to speaker, which is done in
                  * enablecombo()
                  */
                if(LpaNode != NULL) {
                    ALOGD("combo:de-route:msm_route_stream(%d,%d,0)",LpaNode ->dec_id,
                        DEV_ID(DEVICE_SPEAKER_RX));
                    if(msm_route_stream(PCM_PLAY, LpaNode ->dec_id,
                        DEV_ID(DEVICE_SPEAKER_RX), 0)) {
                            ALOGE("msm_route_stream failed");
                            return -1;
                    }
                }
#endif
                ALOGD("Routing PCM stream to speaker for combo device");
                ALOGD("combo:msm_route_stream(PCM_PLAY,session id:%d,dev id:%d,1)",dec_id,
                    DEV_ID(DEVICE_SPEAKER_RX));
                if(msm_route_stream(PCM_PLAY, dec_id, DEV_ID(DEVICE_SPEAKER_RX),
                    1)) {
                    ALOGE("msm_route_stream failed");
                    return -1;
                }
                CurrentComboDeviceData.StreamType = PCM_PLAY;
            }
#endif
            addToTable(dec_id,cur_rx,INVALID_DEVICE,PCM_PLAY,true);
            ioctl(mFd, AUDIO_START, 0);
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

status_t AudioHardware::AudioStreamOutMSM8x60::standby()
{
    Routing_table* temp = NULL;
    //ALOGV("AudioStreamOutMSM8x60::standby()");
    status_t status = NO_ERROR;

    temp = getNodeByStreamType(PCM_PLAY);

    if(temp == NULL)
        return NO_ERROR;

    ALOGV("Deroute pcm stream");
    if(msm_route_stream(PCM_PLAY, temp->dec_id,DEV_ID(temp->dev_id), 0)) {
        ALOGE("could not set stream routing\n");
        deleteFromTable(PCM_PLAY);
        return -1;
    }
    deleteFromTable(PCM_PLAY);
    updateDeviceInfo(cur_rx, cur_tx);
    if(!getNodeByStreamType(VOICE_CALL)
#ifdef QCOM_TUNNEL_LPA_ENABLED
       && !getNodeByStreamType(LPA_DECODE)
#endif
#ifdef QCOM_FM_ENABLED
       && !getNodeByStreamType(FM_RADIO)
#endif
#ifdef QCOM_VOIP_ENABLED
       && !getNodeByStreamType(VOIP_CALL)
#endif
     ) {
#ifdef QCOM_ANC_HEADSET_ENABLED
    //in case if ANC don't disable cur device.
      if (anc_running == false){
#endif
#if 0
        if(enableDevice(cur_rx, 0)) {
            ALOGE("Disabling device failed for cur_rx %d", cur_rx);
            return 0;
        }
#endif
#ifdef QCOM_ANC_HEADSET_ENABLED
      }
#endif
    }

    if (!mStandby && mFd >= 0) {
        ::close(mFd);
        mFd = -1;
    }

    mStandby = true;
#ifdef HTC_ACOUSTIC_AUDIO
    if (support_aic3254)
        mHardware->do_aic3254_control(mHardware->get_snd_dev());
#endif
    return status;
}

status_t AudioHardware::AudioStreamOutMSM8x60::dump(int fd, const Vector<String16>& args)
{
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;
    result.append("AudioStreamOutMSM8x60::dump\n");
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

bool AudioHardware::AudioStreamOutMSM8x60::checkStandby()
{
    return mStandby;
}


status_t AudioHardware::AudioStreamOutMSM8x60::setParameters(const String8& keyValuePairs)
{
    AudioParameter param = AudioParameter(keyValuePairs);
    String8 key = String8(AudioParameter::keyRouting);
    status_t status = NO_ERROR;
    int device;
    ALOGV("AudioStreamOutMSM8x60::setParameters() %s", keyValuePairs.string());

    if (param.getInt(key, device) == NO_ERROR) {
        mDevices = device;
        ALOGV("set output routing %x", mDevices);
        status = mHardware->doRouting(NULL, device);
        param.remove(key);
    }

    if (param.size()) {
        status = BAD_VALUE;
    }
    return status;
}

String8 AudioHardware::AudioStreamOutMSM8x60::getParameters(const String8& keys)
{
    AudioParameter param = AudioParameter(keys);
    String8 value;
    String8 key = String8(AudioParameter::keyRouting);

    if (param.get(key, value) == NO_ERROR) {
        ALOGV("get routing %x", mDevices);
        param.addInt(key, (int)mDevices);
    }

    ALOGV("AudioStreamOutMSM8x60::getParameters() %s", param.toString().string());
    return param.toString();
}

status_t AudioHardware::AudioStreamOutMSM8x60::getRenderPosition(uint32_t *dspFrames)
{
    //TODO: enable when supported by driver
    return INVALID_OPERATION;
}

#ifdef QCOM_VOIP_ENABLED
// ----------------------------------------------------------------------------
// Audio Stream from DirectOutput thread
// ----------------------------------------------------------------------------

AudioHardware::AudioStreamOutDirect::AudioStreamOutDirect() :
    mHardware(0), mFd(-1), mStartCount(0), mRetryCount(0), mStandby(true), mDevices(0),mChannels(AudioSystem::CHANNEL_OUT_MONO),
    mSampleRate(AUDIO_HW_VOIP_SAMPLERATE_8K), mBufferSize(AUDIO_HW_VOIP_BUFFERSIZE_8K), mFormat(AudioSystem::PCM_16_BIT)
{
}

status_t AudioHardware::AudioStreamOutDirect::set(
        AudioHardware* hw, uint32_t devices, int *pFormat, uint32_t *pChannels, uint32_t *pRate)
{
    int lFormat = pFormat ? *pFormat : 0;
    uint32_t lChannels = pChannels ? *pChannels : 0;
    uint32_t lRate = pRate ? *pRate : 0;

    ALOGD("AudioStreamOutDirect::set  lFormat = %x lChannels= %u lRate = %u\n",
        lFormat, lChannels, lRate );

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

    mDevices = devices;
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

#ifndef LEGACY_QCOM_VOICE
    if((voip_session_id <= 0)) {
        voip_session_id = msm_get_voc_session(VOIP_SESSION_NAME);
    }
#endif

    mDevices = devices;
    mHardware->mVoipOutActive = true;
    
    return NO_ERROR;
}   

AudioHardware::AudioStreamOutDirect::~AudioStreamOutDirect()
{
    ALOGV("AudioStreamOutDirect destructor");
    mHardware->mVoipOutActive = false;
    standby();
}   

ssize_t AudioHardware::AudioStreamOutDirect::write(const void* buffer, size_t bytes)
{
//    ALOGE("AudioStreamOutDirect::write(%p, %u)", buffer, bytes);
    status_t status = NO_INIT;
    size_t count = bytes;
    const uint8_t* p = static_cast<const uint8_t*>(buffer);
    
    if (mStandby) {
        if(mHardware->mVoipFd >= 0) {
            mFd = mHardware->mVoipFd;
            
            mHardware->mVoipOutActive = true;
            if (mHardware->mVoipInActive)
                mHardware->setupDeviceforVoipCall(true);
                
            mStandby = false;
        } else {
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
            
            mvs_config.mvs_mode = mHardware->getMvsMode(mFormat, mSampleRate);
            status = mHardware->getMvsRateType(mvs_config.mvs_mode ,&mvs_config.rate_type);
            ALOGD("set mvs config mode %d rate_type %d", mvs_config.mvs_mode, mvs_config.rate_type);
            if (status < 0) {
                ALOGE("Incorrect mvs type");
                goto Error;
            }   
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
            mHardware->mVoipOutActive = true;
            if (mHardware->mVoipInActive)
                mHardware->setupDeviceforVoipCall(true);

            mStandby = false;

            Mutex::Autolock lock(mDeviceSwitchLock);
            //Routing Voip
            if ((cur_rx != INVALID_DEVICE) && (cur_tx != INVALID_DEVICE))
            {
                ALOGV("Starting voip call on Rx %d and Tx %d device", DEV_ID(cur_rx), DEV_ID(cur_tx));
                msm_route_voice(DEV_ID(cur_rx),DEV_ID(cur_tx), 1);
            }
            else {
               return -1;
            }

            //Enable RX device
            if(cur_rx != INVALID_DEVICE && (enableDevice(cur_rx,1) == -1))
            {
               return -1;
            }

            //Enable TX device
            if(cur_tx != INVALID_DEVICE&&(enableDevice(cur_tx,1) == -1))
            {
               return -1;
            }
#ifdef QCOM_ACDB_ENABLED
            // voip calibration
            acdb_loader_send_voice_cal(ACDB_ID(cur_rx),ACDB_ID(cur_tx));
#endif
            // start Voip call
            ALOGD("Starting voip call and UnMuting the call");
#ifdef LEGACY_QCOM_VOICE
            msm_start_voice();
            msm_set_voice_tx_mute(0);
#else
            msm_start_voice_ext(voip_session_id);
            msm_set_voice_tx_mute_ext(voip_session_mute,voip_session_id);
#endif
            addToTable(0,cur_rx,cur_tx,VOIP_CALL,true);
        }
    }
    struct msm_audio_mvs_frame audio_mvs_frame;
    memset(&audio_mvs_frame, 0, sizeof(audio_mvs_frame));
    if (mFormat == AudioSystem::PCM_16_BIT) {
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
    }   
    else {
        struct msm_audio_mvs_frame *mvsFramePtr = (msm_audio_mvs_frame *)buffer;
        audio_mvs_frame.frame_type = mvsFramePtr->frame_type;
        audio_mvs_frame.len = mvsFramePtr->len;
        ALOGV("Write Frametype %d, Frame len %d", audio_mvs_frame.frame_type, audio_mvs_frame.len);
        if(audio_mvs_frame.len < 0)
            goto Error;
        memcpy(&audio_mvs_frame.voc_pkt, &mvsFramePtr->voc_pkt, audio_mvs_frame.len);
        size_t written =::write(mFd, &audio_mvs_frame, sizeof(audio_mvs_frame));
        ALOGV(" mvs bytes written : %d bytes %d \n", written,bytes);
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
    Routing_table* temp = NULL;
    //ALOGV("AudioStreamOutDirect::standby()");
    Mutex::Autolock lock(mHardware->mVoipLock);
    status_t status = NO_ERROR;
    int ret = 0;

    ALOGD("Voipin %d driver fd %d", mHardware->mVoipInActive, mHardware->mVoipFd);
    mHardware->mVoipOutActive = false;
    if (mHardware->mVoipFd >= 0 && !mHardware->mVoipInActive) {
#ifdef LEGACY_QCOM_VOICE
        msm_end_voice();
#else
        ret = msm_end_voice_ext(voip_session_id);
        if (ret < 0)
                ALOGE("Error %d ending voip\n", ret);
#endif
        temp = getNodeByStreamType(VOIP_CALL);
        if(temp == NULL)
        {
            ALOGE("VOIP: Going to disable RX/TX return 0");
            return 0;
        }

        if((temp->dev_id != INVALID_DEVICE && temp->dev_id_tx != INVALID_DEVICE)&& (!isStreamOn(VOICE_CALL))) {
            ALOGE(" Streamout: disable VOIP rx tx ");
           enableDevice(temp->dev_id,0);
           enableDevice(temp->dev_id_tx,0);
        }
        deleteFromTable(VOIP_CALL);

       ret = ioctl(mHardware->mVoipFd, AUDIO_STOP, NULL);
       ALOGD("MVS stop returned %d %d %d \n", ret, __LINE__, mHardware->mVoipFd);
       ::close(mFd);
       mFd = mHardware->mVoipFd = -1;
       mHardware->setupDeviceforVoipCall(false);
       ALOGD("MVS driver closed %d mFd %d", __LINE__, mHardware->mVoipFd);
   } else
        ALOGE("Not closing MVS driver");
        

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
        status = mHardware->doRouting(NULL, device);
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
    
    key = String8("voip_flag");
    if (param.get(key, value) == NO_ERROR) {
        param.addInt(key, true);
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
//.----------------------------------------------------------------------------

#ifdef QCOM_TUNNEL_LPA_ENABLED
// ----------------------------------------------------------------------------
// Audio Stream from LPA output
// Start AudioSessionOutLPA
// ----------------------------------------------------------------------------

AudioHardware::AudioSessionOutLPA::AudioSessionOutLPA( AudioHardware *hw,
                                         uint32_t   devices,
                                         int        format,
                                         uint32_t   channels,
                                         uint32_t   samplingRate,
                                         int        type,
                                         status_t   *status)
{
    Mutex::Autolock autoLock(mLock);
    // Default initilization
	mHardware = hw;
    ALOGE("AudioSessionOutLPA constructor");
    mFormat             = format;
    mSampleRate         = samplingRate;
    mChannels           = popcount(channels);
    mBufferSize         = LPA_BUFFER_SIZE; //TODO to check what value is correct
    *status             = BAD_VALUE;

    mPaused             = false;
    mIsDriverStarted    = false;
    mSeeking            = false;
    mReachedEOS         = false;
    mSkipWrite          = false;
    timeStarted = 0;
    timePlayed = 0;

    mInputBufferSize    = LPA_BUFFER_SIZE;
    mInputBufferCount   = BUFFER_COUNT;
    efd = -1;
    mEosEventReceived   =false;

    mEventThread        = NULL;
    mEventThreadAlive   = false;
    mKillEventThread    = false;
    mObserver           = NULL;
    if((format == AUDIO_FORMAT_PCM_16_BIT) && (mChannels == 0 || mChannels > 2)) {
        ALOGE("Invalid number of channels %d", channels);
        return;
    }

    mDevices = devices;

    *status = openAudioSessionDevice();

    //Creates the event thread to poll events from LPA Driver
    if (*status == NO_ERROR)
        createEventThread();
}

AudioHardware::AudioSessionOutLPA::~AudioSessionOutLPA()
{
    ALOGV("AudioSessionOutLPA destructor");
	mSkipWrite = true;
    mWriteCv.signal();

    //TODO: This might need to be Locked using Parent lock
    reset();
    //standby();//TODO Do we really need standby?

}

ssize_t AudioHardware::AudioSessionOutLPA::write(const void* buffer, size_t bytes)
{
    Mutex::Autolock autoLock(mLock);
    int err;
    ALOGV("write Empty Queue size() = %d, Filled Queue size() = %d ",
         mEmptyQueue.size(),mFilledQueue.size());

    if (mSkipWrite) {
        mSkipWrite = false;
        if (bytes < LPA_BUFFER_SIZE)
            bytes = 0;
        else
            return UNKNOWN_ERROR;
    }

    //2.) Dequeue the buffer from empty buffer queue. Copy the data to be
    //    written into the buffer. Then Enqueue the buffer to the filled
    //    buffer queue
    mEmptyQueueMutex.lock();
    List<BuffersAllocated>::iterator it = mEmptyQueue.begin();
    BuffersAllocated buf = *it;
    mEmptyQueue.erase(it);
    mEmptyQueueMutex.unlock();

    memset(buf.memBuf, 0, bytes);
    memcpy(buf.memBuf, buffer, bytes);
    buf.bytesToWrite = bytes;

    struct msm_audio_aio_buf aio_buf_local;
    if ( buf.bytesToWrite > 0) {
        memset(&aio_buf_local, 0, sizeof(msm_audio_aio_buf));
        aio_buf_local.buf_addr = buf.memBuf;
        aio_buf_local.buf_len = buf.bytesToWrite;
        aio_buf_local.data_len = buf.bytesToWrite;
        aio_buf_local.private_data = (void*) buf.memFd;

        if ( (buf.bytesToWrite % 2) != 0 ) {
            ALOGV("Increment for even bytes");
            aio_buf_local.data_len += 1;
        }
        if (timeStarted == 0)
            timeStarted = nanoseconds_to_microseconds(systemTime(SYSTEM_TIME_MONOTONIC));
    } else {
            /* Put the buffer back into requestQ */
            ALOGV("mEmptyQueueMutex locking: %d", __LINE__);
            mEmptyQueueMutex.lock();
            ALOGV("mEmptyQueueMutex locked: %d", __LINE__);
            mEmptyQueue.push_back(buf);
            ALOGV("mEmptyQueueMutex unlocking: %d", __LINE__);
            mEmptyQueueMutex.unlock();
            ALOGV("mEmptyQueueMutex unlocked: %d", __LINE__);
            //Post EOS in case the filled queue is empty and EOS is reached.
        mReachedEOS = true;
        mFilledQueueMutex.lock();
        if (mFilledQueue.empty() && !mEosEventReceived) {
            ALOGV("mEosEventReceived made true");
            mEosEventReceived = true;
            if (mObserver != NULL) {
                ALOGV("mObserver: posting EOS");
                mObserver->postEOS(0);
            }
        }
        mFilledQueueMutex.unlock();
        return NO_ERROR;
    }
    mFilledQueueMutex.lock();
    mFilledQueue.push_back(buf);
    mFilledQueueMutex.unlock();

    ALOGV("PCM write start");
    //3.) Write the buffer to the Driver
    if(mIsDriverStarted) {
    if ( ioctl(afd, AUDIO_ASYNC_WRITE, &aio_buf_local ) < 0 ) {
        ALOGE("error on async write\n");
       }
    }
    ALOGV("PCM write complete");

    if (bytes < LPA_BUFFER_SIZE) {
        ALOGV("Last buffer case");
        mReachedEOS = true;
    }

    return NO_ERROR; //TODO Do we need to send error
}


status_t AudioHardware::AudioSessionOutLPA::standby()
{
    //ALOGV("AudioSessionOutLPA::standby()");
    status_t status = NO_ERROR;
    //TODO  Do we really need standby()
    return status;
}


status_t AudioHardware::AudioSessionOutLPA::dump(int fd, const Vector<String16>& args)
{
    return NO_ERROR;
}

status_t AudioHardware::AudioSessionOutLPA::setParameters(const String8& keyValuePairs)
{
    AudioParameter param = AudioParameter(keyValuePairs);
    String8 key = String8(AudioParameter::keyRouting);
    status_t status = NO_ERROR;
    int device;
    ALOGE("AudioSessionOutLPA::setParameters() %s", keyValuePairs.string());

    if (param.getInt(key, device) == NO_ERROR) {
        mDevices = device;
        ALOGE("set output routing %x", mDevices);
        status = mHardware->doRouting(NULL, device);
        param.remove(key);
    }

    if (param.size()) {
        status = BAD_VALUE;
    }
    return status;
}

String8 AudioHardware::AudioSessionOutLPA::getParameters(const String8& keys)
{
    AudioParameter param = AudioParameter(keys);
    String8 value;
    String8 key = String8(AudioParameter::keyRouting);

    if (param.get(key, value) == NO_ERROR) {
        ALOGV("get routing %x", mDevices);
        param.addInt(key, (int)mDevices);
    }

    ALOGV("AudioSessionOutLPA::getParameters() %s", param.toString().string());
    return param.toString();
}

status_t AudioHardware::AudioSessionOutLPA::setVolume(float left, float right)
{
    float v = (left + right) / 2;
    int sessionId = 0;
    unsigned short decId;
    if (v < 0.0) {
        ALOGW("AudioSessionOutLPA::setVolume(%f) under 0.0, assuming 0.0\n", v);
        v = 0.0;
    } else if (v > 1.0) {
        ALOGW("AudioSessionOutLPA::setVolume(%f) over 1.0, assuming 1.0\n", v);
        v = 1.0;
    }
    if ( ioctl(afd, AUDIO_GET_SESSION_ID, &decId) == -1 ) {
        ALOGE("AUDIO_GET_SESSION_ID FAILED\n");
        return BAD_VALUE;
    } else {
        sessionId = (int)decId;
        ALOGE("AUDIO_GET_SESSION_ID success : decId = %d", decId);
    }
    // Ensure to convert the log volume back to linear for LPA
    float vol = v * 100;
    ALOGV("AudioSessionOutLPA::setVolume(%f)\n", v);
    ALOGV("Setting session volume to %f (available range is 0 to 100)\n", vol);

    if(msm_set_volume(sessionId, vol)) {
        ALOGE("msm_set_volume(%d %f) failed errno = %d",sessionId, vol,errno);
        return -1;
    }
    ALOGV("LPA volume set (%f) succeeded",vol);
    return NO_ERROR;
}

status_t AudioHardware::AudioSessionOutLPA::openAudioSessionDevice( )
{
    status_t status = NO_ERROR;

    //It opens LPA driver
    ALOGE("Opening LPA pcm_dec driver");
    afd = open("/dev/msm_pcm_lp_dec", O_WRONLY | O_NONBLOCK);
    if ( afd < 0 ) {
        ALOGE("pcm_lp_dec: cannot open pcm_dec device and the error is %d", errno);
        //initCheck = false;
        return UNKNOWN_ERROR;
    } else {
        //initCheck = true;
        ALOGV("pcm_lp_dec: pcm_lp_dec Driver opened");
    }

    start();
    ALOGE("Calling bufferAlloc ");
        bufferAlloc();

    return status;
}

void AudioHardware::AudioSessionOutLPA::bufferAlloc( )
{
    // Allocate ION buffers
    void *ion_buf; int32_t ion_fd;
    struct msm_audio_ion_info ion_info;
    ALOGE("Allocate ION buffers");
    //1. Open the ion_audio
    ionfd = open("/dev/ion", O_RDONLY);
    if (ionfd < 0) {
        ALOGE("/dev/ion open failed \n");
        return;
    }
    for (int i = 0; i < mInputBufferCount; i++) {
        ion_buf = memBufferAlloc(mInputBufferSize, &ion_fd);
        memset(&ion_info, 0, sizeof(msm_audio_ion_info));
        ALOGE("Registering ION with fd %d and address as %p", ion_fd, ion_buf);
        ion_info.fd = ion_fd;
        ion_info.vaddr = ion_buf;
        if ( ioctl(afd, AUDIO_REGISTER_ION, &ion_info) < 0 ) {
            ALOGE("Registration of ION with the Driver failed with fd %d and memory %x",
                 ion_info.fd, (unsigned int)ion_info.vaddr);
        }
    }
    ALOGE("Allocating ION buffers complete");
}


void* AudioHardware::AudioSessionOutLPA::memBufferAlloc(int nSize, int32_t *ion_fd)
{
    void  *ion_buf = NULL;
    void  *local_buf = NULL;
    struct ion_fd_data fd_data;
    struct ion_allocation_data alloc_data;

    alloc_data.len =   nSize;
    alloc_data.align = 0x1000;
    alloc_data.heap_mask = ION_HEAP(ION_AUDIO_HEAP_ID);
    alloc_data.flags = 0;
    int rc = ioctl(ionfd, ION_IOC_ALLOC, &alloc_data);
    if (rc) {
        ALOGE("ION_IOC_ALLOC ioctl failed\n");
        return ion_buf;
    }
    fd_data.handle = alloc_data.handle;

    rc = ioctl(ionfd, ION_IOC_SHARE, &fd_data);
    if (rc) {
        ALOGE("ION_IOC_SHARE ioctl failed\n");
        rc = ioctl(ionfd, ION_IOC_FREE, &(alloc_data.handle));
        if (rc) {
            ALOGE("ION_IOC_FREE ioctl failed\n");
        }
        return ion_buf;
    }

    // 2. MMAP to get the virtual address
    ion_buf = mmap(NULL, nSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd_data.fd, 0);
    if(MAP_FAILED == ion_buf) {
        ALOGE("mmap() failed \n");
        close(fd_data.fd);
        rc = ioctl(ionfd, ION_IOC_FREE, &(alloc_data.handle));
        if (rc) {
            ALOGE("ION_IOC_FREE ioctl failed\n");
        }
        return ion_buf;
    }

    local_buf = malloc(nSize);
    if (NULL == local_buf) {
        // unmap the corresponding ION buffer and close the fd
        munmap(ion_buf, mInputBufferSize);
        close(fd_data.fd);
        rc = ioctl(ionfd, ION_IOC_FREE, &(alloc_data.handle));
        if (rc) {
            ALOGE("ION_IOC_FREE ioctl failed\n");
        }
        return NULL;
    }

    // 3. Store this information for internal mapping / maintanence
    BuffersAllocated buf(local_buf, ion_buf, nSize, fd_data.fd, alloc_data.handle);
    mEmptyQueue.push_back(buf);
    mBufPool.push_back(buf);

    // 4. Send the mem fd information
    *ion_fd = fd_data.fd;
    ALOGV("IONBufferAlloc calling with required size %d", nSize);
    ALOGV("ION allocated is %d, fd_data.fd %d and buffer is %x", *ion_fd, fd_data.fd, (unsigned int)ion_buf);

    // 5. Return the virtual address
    return ion_buf;
}

void AudioHardware::AudioSessionOutLPA::bufferDeAlloc()
{
    // De-Allocate ION buffers
    int rc = 0;
    //Remove all the buffers from empty queue
    mEmptyQueueMutex.lock();
    while (!mEmptyQueue.empty())  {
        List<BuffersAllocated>::iterator it = mEmptyQueue.begin();
        BuffersAllocated &ionBuffer = *it;
        struct msm_audio_ion_info ion_info;
        ion_info.vaddr = (*it).memBuf;
        ion_info.fd = (*it).memFd;
        if (ioctl(afd, AUDIO_DEREGISTER_ION, &ion_info) < 0) {
            ALOGE("ION deregister failed");
        }
        ALOGV("Ion Unmapping the address %p, size %d, fd %d from empty",ionBuffer.memBuf,ionBuffer.bytesToWrite,ionBuffer.memFd);
        munmap(ionBuffer.memBuf, mInputBufferSize);
        ALOGV("closing the ion shared fd");
        close(ionBuffer.memFd);
        rc = ioctl(ionfd, ION_IOC_FREE, &ionBuffer.ion_handle);
        if (rc) {
            ALOGE("ION_IOC_FREE ioctl failed\n");
        }
        // free the local buffer corresponding to ion buffer
        free(ionBuffer.localBuf);
        ALOGE("Removing from empty Q");
        mEmptyQueue.erase(it);
    }
    mEmptyQueueMutex.unlock();

    //Remove all the buffers from Filled queue
    mFilledQueueMutex.lock();
    while(!mFilledQueue.empty()){
        List<BuffersAllocated>::iterator it = mFilledQueue.begin();
        BuffersAllocated &ionBuffer = *it;
        struct msm_audio_ion_info ion_info;
        ion_info.vaddr = (*it).memBuf;
        ion_info.fd = (*it).memFd;
        if (ioctl(afd, AUDIO_DEREGISTER_ION, &ion_info) < 0) {
            ALOGE("ION deregister failed");
        }
        ALOGV("Ion Unmapping the address %p, size %d, fd %d from Request",ionBuffer.memBuf,ionBuffer.bytesToWrite,ionBuffer.memFd);
        munmap(ionBuffer.memBuf, mInputBufferSize);
        ALOGV("closing the ion shared fd");
        close(ionBuffer.memFd);
        rc = ioctl(ionfd, ION_IOC_FREE, &ionBuffer.ion_handle);
        if (rc) {
            ALOGE("ION_IOC_FREE ioctl failed\n");
        }
        // free the local buffer corresponding to ion buffer
        free(ionBuffer.localBuf);
        ALOGV("Removing from Filled Q");
        mFilledQueue.erase(it);
    }
    while (!mBufPool.empty()) {
        List<BuffersAllocated>::iterator it = mBufPool.begin();
        ALOGE("Removing input buffer from Buffer Pool ");
        mBufPool.erase(it);
    }
    mFilledQueueMutex.unlock();
    if (ionfd >= 0) {
        close(ionfd);
        ionfd = -1;
    }
}

uint32_t AudioHardware::AudioSessionOutLPA::latency() const
{
    return 54; // latency equal to regular hpcm session
}

void AudioHardware::AudioSessionOutLPA::requestAndWaitForEventThreadExit()
{
    if (!mEventThreadAlive)
        return;
    mKillEventThread = true;
    if (ioctl(afd, AUDIO_ABORT_GET_EVENT, 0) < 0) {
        ALOGE("Audio Abort event failed");
    }
    pthread_join(mEventThread,NULL);
}

void * AudioHardware::AudioSessionOutLPA::eventThreadWrapper(void *me)
{
    static_cast<AudioSessionOutLPA *>(me)->eventThreadEntry();
    return NULL;
}

void  AudioHardware::AudioSessionOutLPA::eventThreadEntry()
{
    struct msm_audio_event cur_pcmdec_event;
    mEventThreadAlive = true;
    int rc = 0;
    //2.) Set the priority for the event thread
    pid_t tid  = gettid();
    androidSetThreadPriority(tid, ANDROID_PRIORITY_AUDIO);
    prctl(PR_SET_NAME, (unsigned long)"HAL Audio EventThread", 0, 0, 0);
    ALOGV("event thread created ");
    if (mKillEventThread) {
        mEventThreadAlive = false;
        ALOGV("Event Thread is dying.");
        return;
    }
    while (1) {
        //Wait for an event to occur
        rc = ioctl(afd, AUDIO_GET_EVENT, &cur_pcmdec_event);
        ALOGE("pcm dec Event Thread rc = %d and errno is %d",rc, errno);

        if ( (rc < 0) && ((errno == ENODEV) || (errno == EBADF)) ) {
            ALOGV("AUDIO__GET_EVENT called. Exit the thread");
            break;
        }

        switch ( cur_pcmdec_event.event_type ) {
        case AUDIO_EVENT_WRITE_DONE:
            {
                Mutex::Autolock autoLock(mLock);
                ALOGE("WRITE_DONE: addr %p len %d and fd is %d\n",
                     cur_pcmdec_event.event_payload.aio_buf.buf_addr,
                     cur_pcmdec_event.event_payload.aio_buf.data_len,
                     (int32_t) cur_pcmdec_event.event_payload.aio_buf.private_data);
                mFilledQueueMutex.lock();
                BuffersAllocated buf = *(mFilledQueue.begin());
                for (List<BuffersAllocated>::iterator it = mFilledQueue.begin();
                    it != mFilledQueue.end(); ++it) {
                    if (it->memBuf == cur_pcmdec_event.event_payload.aio_buf.buf_addr) {
                        buf = *it;
                        mFilledQueue.erase(it);
                        // Post buffer to Empty Q
                        ALOGV("mEmptyQueueMutex locking: %d", __LINE__);
                        mEmptyQueueMutex.lock();
                        ALOGV("mEmptyQueueMutex locked: %d", __LINE__);
                        mEmptyQueue.push_back(buf);
                        ALOGV("mEmptyQueueMutex unlocking: %d", __LINE__);
                        mEmptyQueueMutex.unlock();
                        ALOGV("mEmptyQueueMutex unlocked: %d", __LINE__);
                        if (mFilledQueue.empty() && mReachedEOS) {
                            ALOGV("Posting the EOS to the observer player %p", mObserver);
                            mEosEventReceived = true;
                            if (mObserver != NULL) {
                                mLock.unlock();
                                if (fsync(afd) != 0) {
                                    ALOGE("fsync failed.");
                                }
                                mLock.lock();
                                ALOGV("mObserver: posting EOS");
                                mObserver->postEOS(0);
                            }
                        }
                        break;
                    }
                }
                mFilledQueueMutex.unlock();
                 mWriteCv.signal();
            }
            break;
        case AUDIO_EVENT_SUSPEND:
            {
                struct msm_audio_stats stats;
                int nBytesConsumed = 0;

                ALOGV("AUDIO_EVENT_SUSPEND received\n");
                if (!mPaused) {
                    ALOGV("Not in paused, no need to honor SUSPEND event");
                    break;
                }
                // 1. Get the Byte count that is consumed
                if ( ioctl(afd, AUDIO_GET_STATS, &stats)  < 0 ) {
                    ALOGE("AUDIO_GET_STATUS failed");
                } else {
                    ALOGV("Number of bytes consumed by DSP is %u", stats.byte_count);
                    nBytesConsumed = stats.byte_count;
                    }
                    // Reset eosflag to resume playback where we actually paused
                    mReachedEOS = false;
                    // 3. Call AUDIO_STOP on the Driver.
                    ALOGV("Received AUDIO_EVENT_SUSPEND and calling AUDIO_STOP");
                    if ( ioctl(afd, AUDIO_STOP, 0) < 0 ) {
                         ALOGE("AUDIO_STOP failed");
                    }
                    mIsDriverStarted = false;
                    break;
            }
            break;
        case AUDIO_EVENT_RESUME:
            {
                ALOGV("AUDIO_EVENT_RESUME received\n");
            }
            break;
        default:
            ALOGE("Received Invalid Event from driver\n");
            break;
        }
    }
    mEventThreadAlive = false;
    ALOGV("Event Thread is dying.");
}


void AudioHardware::AudioSessionOutLPA::createEventThread()
{
    ALOGV("Creating Event Thread");
    mKillEventThread = false;
    mEventThreadAlive = true;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    pthread_create(&mEventThread, &attr, eventThreadWrapper, this);
    ALOGV("Event Thread created");
}

status_t AudioHardware::AudioSessionOutLPA::start( )
{

    ALOGE("LPA playback start");
    if (mPaused && mIsDriverStarted) {
        mPaused = false;
        if (ioctl(afd, AUDIO_PAUSE, 0) < 0) {
            ALOGE("Resume:: LPA driver resume failed");
            return UNKNOWN_ERROR;
        }

    } else {
	 //get config, set config and AUDIO_START LPA driver
	 int sessionId = 0;
        mPaused = false;
        if ( afd >= 0 ) {
           struct msm_audio_config config;
           if ( ioctl(afd, AUDIO_GET_CONFIG, &config) < 0 ) {
               ALOGE("could not get config");
               close(afd);
               afd = -1;
               return BAD_VALUE;
        }

        config.sample_rate = mSampleRate;
        config.channel_count = mChannels;
        ALOGE("sample_rate=%d and channel count=%d \n", mSampleRate, mChannels);
        if ( ioctl(afd, AUDIO_SET_CONFIG, &config) < 0 ) {
            ALOGE("could not set config");
            close(afd);
            afd = -1;
            return BAD_VALUE;
        }
    }
    unsigned short decId;
    // Get the session id from the LPA Driver
    // Register the session id with HAL for routing
    if ( ioctl(afd, AUDIO_GET_SESSION_ID, &decId) == -1 ) {
        ALOGE("AUDIO_GET_SESSION_ID FAILED\n");
        return BAD_VALUE;
    } else {
        sessionId = (int)decId;
        ALOGV("AUDIO_GET_SESSION_ID success : decId = %d", decId);
    }

    Mutex::Autolock lock(mDeviceSwitchLock);
#ifdef QCOM_TUNNEL_LPA_ENABLED
    if(getNodeByStreamType(LPA_DECODE) != NULL) {
        ALOGE("Not allowed, There is alread an LPA Node existing");
        return -1;
    }
    ALOGE("AudioSessionOutMSM8x60::set() Adding LPA_DECODE Node to Table");
    addToTable(sessionId,cur_rx,INVALID_DEVICE,LPA_DECODE,true);
#endif
    ALOGE("enableDevice(cur_rx = %d, dev_id = %d)",cur_rx,DEV_ID(cur_rx));
    if (enableDevice(cur_rx, 1)) {
        ALOGE("enableDevice failed for device cur_rx %d", cur_rx);
        return -1;
    }

    ALOGE("msm_route_stream(PCM_PLAY,%d,%d,0)",sessionId,DEV_ID(cur_rx));
#ifdef QCOM_ACDB_ENABLED
    acdb_loader_send_audio_cal(ACDB_ID(cur_rx), CAPABILITY(cur_rx));
#endif
    if(msm_route_stream(PCM_PLAY,sessionId,DEV_ID(cur_rx),1)) {
        ALOGE("msm_route_stream(PCM_PLAY,%d,%d,1) failed",sessionId,DEV_ID(cur_rx));
        return -1;
    }

    Mutex::Autolock lock_1(mComboDeviceLock);
#ifdef QCOM_FM_ENABLED
    if(CurrentComboDeviceData.DeviceId == SND_DEVICE_FM_TX_AND_SPEAKER){
        ALOGD("Routing LPA steam to speaker for combo device");
        ALOGD("combo:msm_route_stream(LPA_DECODE,session id:%d,dev id:%d,1)",sessionId,
            DEV_ID(DEVICE_SPEAKER_RX));
            /* music session is already routed to speaker in
             * enableComboDevice(), but at this point it can
             * be said that it was done with incorrect session id,
             * so re-routing with correct session id here.
             */
        if(msm_route_stream(PCM_PLAY, sessionId, DEV_ID(DEVICE_SPEAKER_RX),
           1)) {
            ALOGE("msm_route_stream failed");
            return -1;
        }
        CurrentComboDeviceData.StreamType = LPA_DECODE;
    }
#endif
	//Start the Driver
    if (ioctl(afd, AUDIO_START,0) < 0) {
        ALOGE("Driver start failed!");
        return BAD_VALUE;
    }
    mIsDriverStarted = true;
    ALOGE("LPA Playback started");
    if (timeStarted == 0)
        timeStarted = nanoseconds_to_microseconds(systemTime(SYSTEM_TIME_MONOTONIC));// Needed
	}
	return NO_ERROR;
}

status_t AudioHardware::AudioSessionOutLPA::pause()
{
    ALOGV("LPA playback pause");
    if (ioctl(afd, AUDIO_PAUSE, 1) < 0) {
    ALOGE("Audio Pause failed");
    }
    mPaused = true;
	timePlayed += (nanoseconds_to_microseconds(systemTime(SYSTEM_TIME_MONOTONIC)) - timeStarted);//needed
    return NO_ERROR;
}

status_t AudioHardware::AudioSessionOutLPA::drain()
{
    ALOGV("LPA playback EOS");
    return NO_ERROR;
}

status_t AudioHardware::AudioSessionOutLPA::flush()
{
    Mutex::Autolock autoLock(mLock);
    ALOGV("LPA playback flush ");
    int err;

    mFilledQueueMutex.lock();
    mEmptyQueueMutex.lock();
    // 1.) Clear the Empty and Filled buffer queue
    mEmptyQueue.clear();
    mFilledQueue.clear();
    // 2.) Add all the available buffers to Empty Queue (Maintain order)
    List<BuffersAllocated>::iterator it = mBufPool.begin();
    for (; it!=mBufPool.end(); ++it) {
       memset(it->memBuf, 0x0, (*it).memBufsize);
       mEmptyQueue.push_back(*it);
    }
    mEmptyQueueMutex.unlock();
    mFilledQueueMutex.unlock();
    ALOGV("Transferred all the buffers from Filled queue to "
          "Empty queue to handle seek");
    mReachedEOS = false;
    if (!mPaused) {
        if (!mEosEventReceived) {
            if (ioctl(afd, AUDIO_PAUSE, 1) < 0) {
                ALOGE("Audio Pause failed");
                return UNKNOWN_ERROR;
            }
            if (ioctl(afd, AUDIO_FLUSH, 0) < 0) {
                ALOGE("Audio Flush failed");
                return UNKNOWN_ERROR;
            }
        }
    } else {
        timeStarted = 0;
        if (ioctl(afd, AUDIO_FLUSH, 0) < 0) {
            ALOGE("Audio Flush failed");
            return UNKNOWN_ERROR;
        }
        if (ioctl(afd, AUDIO_PAUSE, 1) < 0) {
            ALOGE("Audio Pause failed");
            return UNKNOWN_ERROR;
        }
    }
    mEosEventReceived = false;
    //4.) Skip the current write from the decoder and signal to the Write get
    //   the next set of data from the decoder
    mSkipWrite = true;
    mWriteCv.signal();
    return NO_ERROR;
}
status_t AudioHardware::AudioSessionOutLPA::stop()
{
    Mutex::Autolock autoLock(mLock);
    ALOGV("AudioSessionOutLPA- stop");
    // close all the existing PCM devices
    mSkipWrite = true;
    mWriteCv.signal();
    return NO_ERROR;
}

status_t AudioHardware::AudioSessionOutLPA::setObserver(void *observer)
{
    ALOGV("Registering the callback \n");
    mObserver = reinterpret_cast<AudioEventObserver *>(observer);
    return NO_ERROR;
}

status_t  AudioHardware::AudioSessionOutLPA::getNextWriteTimestamp(int64_t *timestamp)
{

    *timestamp = nanoseconds_to_microseconds(systemTime(SYSTEM_TIME_MONOTONIC)) - timeStarted + timePlayed;//needed
    ALOGV("Timestamp returned = %lld\n", *timestamp);
    return NO_ERROR;
}

void AudioHardware::AudioSessionOutLPA::reset()
{
	Routing_table* temp = NULL;
    ALOGD("AudioSessionOutLPA::reset()");
    ioctl(afd,AUDIO_STOP,0);
    mIsDriverStarted = false;
	requestAndWaitForEventThreadExit();
    status_t status = NO_ERROR;
    bufferDeAlloc();
    ::close(afd);
    temp = getNodeByStreamType(LPA_DECODE);

    if (temp == NULL) {
        ALOGE("LPA node does not exist");
        return ;
    }
    ALOGV("Deroute lpa playback stream");
    if(msm_route_stream(PCM_PLAY, temp->dec_id,DEV_ID(temp->dev_id), 0)) {
        ALOGE("could not set stream routing\n");
        deleteFromTable(LPA_DECODE);
    }
    deleteFromTable(LPA_DECODE);
    if (!getNodeByStreamType(VOICE_CALL) && !getNodeByStreamType(PCM_PLAY)
#ifdef QCOM_FM_ENABLED
        && !getNodeByStreamType(FM_RADIO)
#endif
#ifdef QCOM_VOIP_ENABLED
        && !getNodeByStreamType(VOIP_CALL)
#endif
       ) {
#ifdef QCOM_ANC_HEADSET_ENABLED
        if (anc_running == false) {
#endif
            if (enableDevice(cur_rx, 0)) {
                ALOGE("Disabling device failed for cur_rx %d", cur_rx);
            }
#ifdef QCOM_ANC_HEADSET_ENABLED
        }
#endif
    }
    ALOGE("AudioSessionOutLPA::reset() complete");
}

status_t AudioHardware::AudioSessionOutLPA::getRenderPosition(uint32_t *dspFrames)
{
    //TODO: enable when supported by driver
    return INVALID_OPERATION;
}

status_t AudioHardware::AudioSessionOutLPA::getBufferInfo(buf_info **buf) {

    buf_info *tempbuf = (buf_info *)malloc(sizeof(buf_info) + mInputBufferCount*sizeof(int *));
    ALOGV("Get buffer info");
    tempbuf->bufsize = LPA_BUFFER_SIZE;
    tempbuf->nBufs = mInputBufferCount;
    tempbuf->buffers = (int **)((char*)tempbuf + sizeof(buf_info));
    List<BuffersAllocated>::iterator it = mBufPool.begin();
    for (int i = 0; i < mInputBufferCount; i++) {
        tempbuf->buffers[i] = (int *)it->memBuf;
        it++;
    }
    *buf = tempbuf;
    return NO_ERROR;
}

status_t AudioHardware::AudioSessionOutLPA::isBufferAvailable(int *isAvail) {

    Mutex::Autolock autoLock(mLock);
    ALOGV("isBufferAvailable Empty Queue size() = %d, Filled Queue size() = %d ",
          mEmptyQueue.size(),mFilledQueue.size());
    *isAvail = false;
    // 1.) Wait till a empty buffer is available in the Empty buffer queue
    mEmptyQueueMutex.lock();
    if (mEmptyQueue.empty()) {
        ALOGV("Write: waiting on mWriteCv");
        mLock.unlock();
        mWriteCv.wait(mEmptyQueueMutex);
        mEmptyQueueMutex.unlock();
        mLock.lock();
        if (mSkipWrite) {
            ALOGV("Write: Flushing the previous write buffer");
            mSkipWrite = false;
            return NO_ERROR;
        }
        ALOGV("Write: received a signal to wake up");
    } else {
        ALOGV("Buffer available in empty queue");
        mEmptyQueueMutex.unlock();
    }

    *isAvail = true;
    return NO_ERROR;
}

#ifdef TUNNEL_PLAYBACK
AudioHardware::AudioSessionOutTunnel::AudioSessionOutTunnel( AudioHardware *hw,
                                         uint32_t   devices,
                                         int        format,
                                         uint32_t   channels,
                                         uint32_t   samplingRate,
                                         int        type,
                                         status_t   *status)
{
    Mutex::Autolock autoLock(mLock);
    // Default initilization
    mHardware = hw;
    ALOGE("AudioSessionOutTunnel constructor");
    mFormat             = format;
    mSampleRate         = samplingRate;
    mChannels           = popcount(channels);
    *status            = BAD_VALUE;
    mInputBufferSize    = TUNNEL_BUFFER_SIZE;
    mInputBufferCount   = TUNNEL_BUFFER_COUNT;

    mPaused             = false;
    mSkipWrite          = false;

    efd = -1;
    mEosEventReceived   =false;

    mEventThread        = NULL;
    mEventThreadAlive   = false;
    mKillEventThread    = false;
    mObserver           = NULL;
    if((format == AUDIO_FORMAT_PCM_16_BIT) && (mChannels == 0 || mChannels > 2)) {
        ALOGE("Invalid number of channels %d", channels);
        return;
    }

    *status = initSession();
    if ( *status != NO_ERROR)
        return;

    //Creates the event thread to poll events from Tunnel Driver
    createEventThread();
    *status = NO_ERROR;
}

AudioHardware::AudioSessionOutTunnel::~AudioSessionOutTunnel()
{
    ALOGV("AudioSessionOutTunnel destructor");
    mSkipWrite = true;
    mWriteCv.signal();
    reset();

}

ssize_t AudioHardware::AudioSessionOutTunnel::write(const void* buffer, size_t bytes)
{
    Mutex::Autolock autoLock(mLock);
    int err;
    ALOGV("AudioSessionOutTunnel::write for bytes %d QueueStats empty q %d filled q %d",
         bytes, mEmptyQueue.size(), mFilledQueue.size());

    mEmptyQueueMutex.lock();
    if (mEmptyQueue.empty()) {
        ALOGV("AudioSessionOutTunnel::write: waiting for want of buffers");
        mLock.unlock();
        mWriteCv.wait(mEmptyQueueMutex);
        mLock.lock();
        if (mSkipWrite) {
            ALOGV("AudioSessionOutTunnel::write: Skipping the previous write buffer");
            mSkipWrite = false;
            mEmptyQueueMutex.unlock();
            return 0;
        }
        ALOGV("AudioSessionOutTunnel::write: received a signal to wake up");
    }

    List<BuffersAllocated>::iterator it = mEmptyQueue.begin();
    BuffersAllocated buf = *it;
    mEmptyQueue.erase(it);
    mEmptyQueueMutex.unlock();

    struct msm_audio_aio_buf aio_buf_local;
    if (bytes > 0) {
        memcpy(buf.memBuf, buffer, bytes);
        buf.bytesToWrite = bytes;

        memset(&aio_buf_local, 0, sizeof(msm_audio_aio_buf));
        aio_buf_local.buf_addr = buf.memBuf;
        aio_buf_local.buf_len = buf.bytesToWrite;
        aio_buf_local.data_len = buf.bytesToWrite;
        aio_buf_local.private_data = (void*) buf.memFd;

        if ( (buf.bytesToWrite % 2) != 0 ) {
            ALOGV("AudioSessionOutTunnel::writeIncrement for even bytes");
            aio_buf_local.data_len += 1;
        }
    } else {
        /* Put the buffer back into emptyQ */
        ALOGV("AudioSessionOutTunnel::writemEmptyQueueMutex locking: %d", __LINE__);
        mEmptyQueueMutex.lock();
        ALOGV("AudioSessionOutTunnel::writemEmptyQueueMutex locked: %d", __LINE__);
        mEmptyQueue.push_back(buf);
        ALOGV("AudioSessionOutTunnel::writemEmptyQueueMutex unlocking: %d", __LINE__);
        mEmptyQueueMutex.unlock();
        ALOGV("AudioSessionOutTunnel::writemEmptyQueueMutex unlocked: %d", __LINE__);

        ALOGD("AudioSessionOutTunnel: Invalid bytes(%d), nothing to write", buf.bytesToWrite);
        return 0;
    }
    mFilledQueueMutex.lock();
    mFilledQueue.push_back(buf);
    mFilledQueueMutex.unlock();

    ALOGV("AudioSessionOutTunnel::write Buffer: QueueStats empty q %d filled q %d %d"
        , mEmptyQueue.size(), mFilledQueue.size(), __LINE__);

    mFlushLock.lock(); /*Dont push buffers when flushing*/
    if ( ioctl(afd, AUDIO_ASYNC_WRITE, &aio_buf_local ) < 0 ) {
        ALOGE("AudioSessionOutTunnel:error on async write\n");
    }
    ALOGV("AudioSessionOutTunnel::Write Buffer done.");
    mFlushLock.unlock();

    if (bytes < TUNNEL_BUFFER_SIZE) {
        ALOGD("AudioSessionOutTunnel::Write Calling Fsync");
        if (!fsync(afd)){
            ALOGD("AudioSessionOutTunnel::EOS");
            mObserver->postEOS(0);
        }
        else
            ALOGE("AudioSessionOutTunnel::Write Thrown out of Fsync probably because of Flush");

        ALOGD("AudioSessionOutTunnel::Write Outof Fsync");
    }

    return NO_ERROR; //TODO Do we need to send error
}


status_t AudioHardware::AudioSessionOutTunnel::standby()
{
    //ALOGV("AudioSessionOutTunnel::standby()");
    status_t status = NO_ERROR;
    //TODO  Do we really need standby()
    return status;
}


status_t AudioHardware::AudioSessionOutTunnel::dump(int fd, const Vector<String16>& args)
{
    return NO_ERROR;
}

status_t AudioHardware::AudioSessionOutTunnel::setParameters(const String8& keyValuePairs)
{
    AudioParameter param = AudioParameter(keyValuePairs);
    String8 key = String8(AudioParameter::keyRouting);
    status_t status = NO_ERROR;
    int device;
    ALOGV("AudioSessionOutTunnel::setParameters() %s", keyValuePairs.string());

    if (param.getInt(key, device) == NO_ERROR) {
        mDevices = device;
        ALOGE("set output routing %x", mDevices);
        status = mHardware->doRouting(NULL, device);
        param.remove(key);
    }

    if (param.size()) {
        status = BAD_VALUE;
    }
    return status;
}

String8 AudioHardware::AudioSessionOutTunnel::getParameters(const String8& keys)
{
    AudioParameter param = AudioParameter(keys);
    String8 value;
    String8 key = String8(AudioParameter::keyRouting);

    if (param.get(key, value) == NO_ERROR) {
        ALOGV("get routing %x", mDevices);
        param.addInt(key, (int)mDevices);
    }

    ALOGV("AudioSessionOutTunnel::getParameters() %s", param.toString().string());
    return param.toString();
}

status_t AudioHardware::AudioSessionOutTunnel::setVolume(float left, float right)
{
    float v = (left + right) / 2;
    int sessionId = 0;
    unsigned short decId;
    if (v < 0.0) {
        ALOGW("AudioSessionOutTunnel::setVolume(%f) under 0.0, assuming 0.0\n", v);
        v = 0.0;
    } else if (v > 1.0) {
        ALOGW("AudioSessionOutTunnel::setVolume(%f) over 1.0, assuming 1.0\n", v);
        v = 1.0;
    }
    if ( ioctl(afd, AUDIO_GET_SESSION_ID, &decId) == -1 ) {
        ALOGE("AUDIO_GET_SESSION_ID FAILED\n");
        return BAD_VALUE;
    } else {
        sessionId = (int)decId;
        ALOGD("AUDIO_GET_SESSION_ID success : decId = %d", decId);
    }
    // Ensure to convert the log volume back to linear for Tunnel
    float vol = v * 100;
    ALOGV("AudioSessionOutTunnel::setVolume(%f)\n", v);
    ALOGV("Setting session volume to %f (available range is 0 to 100)\n", vol);

    if(msm_set_volume(sessionId, vol)) {
        ALOGE("msm_set_volume(%d %f) failed errno = %d",sessionId, vol,errno);
        return -1;
    }
    ALOGV("AudioSessionOutTunnel:msm_set_volume(%f) succeeded",vol);
    return NO_ERROR;
}

status_t AudioHardware::AudioSessionOutTunnel::initSession()
{
    status_t status = NO_ERROR;
    char *drvr=NULL;

    if(mFormat == AUDIO_FORMAT_MP3) {
        drvr = "/dev/msm_mp3";
    } else if(mFormat == AUDIO_FORMAT_AAC) {
        drvr = "/dev/msm_aac";
    } else {
        ALOGE("AudioSessionOutTunnel::initSession:Tunnel session supported only for MP3 and AAC");
        return -1;
    }

    ALOGD("AudioSessionOutTunnel::initSession:Opening Tunnel Mode driver %s", drvr);
    afd = open(drvr, O_WRONLY|O_NONBLOCK);
    if ( afd < 0 ) {
        ALOGE("AudioSessionOutTunnel::initSession:Error Opening Tunnel Mode driver %s err %d", drvr, errno);
        return UNKNOWN_ERROR;
    } else {
        ALOGV("AudioSessionOutTunnel::initSession:Tunnel mode Drvr Opened");
    }

    start();

    return status;
}

void AudioHardware::AudioSessionOutTunnel::allocAndRegisterbuffs( )
{
    // Allocate ION buffers
    void *ion_buf; int32_t ion_fd;
    struct msm_audio_ion_info ion_info;
    //1. Open the ion_audio
    ionfd = open("/dev/ion", O_RDONLY | O_SYNC);
    if (ionfd < 0) {
        ALOGE("AudioSessionOutTunnel:/dev/ion open failed \n");
        return;
    }
    for (int i = 0; i < mInputBufferCount; i++) {
        ion_buf = memBufferAlloc(mInputBufferSize, &ion_fd);
        memset(&ion_info, 0, sizeof(msm_audio_ion_info));
        ALOGD("AudioSessionOutTunnel:Registering ION with fd %d and address as %p", ion_fd, ion_buf);
        ion_info.fd = ion_fd;
        ion_info.vaddr = ion_buf;
        if ( ioctl(afd, AUDIO_REGISTER_ION, &ion_info) < 0 ) {
            ALOGE("AudioSessionOutTunnel:Registration of ION with the Driver failed with fd %d and memory %x",
                 ion_info.fd, (unsigned int)ion_info.vaddr);
        }
    }
}


void* AudioHardware::AudioSessionOutTunnel::memBufferAlloc(int nSize, int32_t *ion_fd)
{
    void  *ion_buf = NULL;
    void  *local_buf = NULL;
    struct ion_fd_data fd_data;
    struct ion_allocation_data alloc_data;

    alloc_data.len =   nSize;
    alloc_data.align = 0x1000;
    alloc_data.flags = ION_HEAP(ION_AUDIO_HEAP_ID);
    int rc = ioctl(ionfd, ION_IOC_ALLOC, &alloc_data);
    if (rc) {
        ALOGE("ION_IOC_ALLOC ioctl failed\n");
        return ion_buf;
    }
    fd_data.handle = alloc_data.handle;

    rc = ioctl(ionfd, ION_IOC_SHARE, &fd_data);
    if (rc) {
        ALOGE("ION_IOC_SHARE ioctl failed\n");
        rc = ioctl(ionfd, ION_IOC_FREE, &(alloc_data.handle));
        if (rc) {
            ALOGE("ION_IOC_FREE ioctl failed\n");
        }
        return ion_buf;
    }

    // 2. MMAP to get the virtual address
    ion_buf = mmap(NULL, nSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd_data.fd, 0);
    if(MAP_FAILED == ion_buf) {
        ALOGE("mmap() failed \n");
        close(fd_data.fd);
        rc = ioctl(ionfd, ION_IOC_FREE, &(alloc_data.handle));
        if (rc) {
            ALOGE("ION_IOC_FREE ioctl failed\n");
        }
        return ion_buf;
    }

    local_buf = malloc(nSize);
    if (NULL == local_buf) {
        // unmap the corresponding ION buffer and close the fd
        munmap(ion_buf, mInputBufferSize);
        close(fd_data.fd);
        rc = ioctl(ionfd, ION_IOC_FREE, &(alloc_data.handle));
        if (rc) {
            ALOGE("ION_IOC_FREE ioctl failed\n");
        }
        return NULL;
    }

    // 3. Store this information for internal mapping / maintanence
    BuffersAllocated buf(local_buf, ion_buf, nSize, fd_data.fd, alloc_data.handle);
    mEmptyQueue.push_back(buf);

    // 4. Send the mem fd information
    *ion_fd = fd_data.fd;
    ALOGV("IONBufferAlloc calling with required size %d", nSize);
    ALOGV("ION allocated is %d, fd_data.fd %d and buffer is %x", *ion_fd, fd_data.fd, (unsigned int)ion_buf);

    // 5. Return the virtual address
    return ion_buf;
}

void AudioHardware::AudioSessionOutTunnel::deallocAndDeregisterbuffs()
{
    // De-Allocate ION buffers
    int rc = 0;
    //Remove all the buffers from empty queue
    while (!mEmptyQueue.empty())  {
        List<BuffersAllocated>::iterator it = mEmptyQueue.begin();
        BuffersAllocated &ionBuffer = *it;
        struct msm_audio_ion_info ion_info;
        ion_info.vaddr = (*it).memBuf;
        ion_info.fd = (*it).memFd;
        if (ioctl(afd, AUDIO_DEREGISTER_ION, &ion_info) < 0) {
            ALOGE("ION deregister failed");
        }
        ALOGV("Ion Unmapping the address %p, size %d, fd %d from empty",ionBuffer.memBuf,ionBuffer.bytesToWrite,ionBuffer.memFd);
        munmap(ionBuffer.memBuf, mInputBufferSize);
        ALOGV("closing the ion shared fd");
        close(ionBuffer.memFd);
        rc = ioctl(ionfd, ION_IOC_FREE, &ionBuffer.ion_handle);
        if (rc) {
            ALOGE("ION_IOC_FREE ioctl failed\n");
        }
        // free the local buffer corresponding to ion buffer
        free(ionBuffer.localBuf);
        ALOGD("Removing from empty Q");
        mEmptyQueue.erase(it);
    }

    //Remove all the buffers from Filled queue
    while(!mFilledQueue.empty()){
        List<BuffersAllocated>::iterator it = mFilledQueue.begin();
        BuffersAllocated &ionBuffer = *it;
        struct msm_audio_ion_info ion_info;
        ion_info.vaddr = (*it).memBuf;
        ion_info.fd = (*it).memFd;
        if (ioctl(afd, AUDIO_DEREGISTER_ION, &ion_info) < 0) {
            ALOGE("ION deregister failed");
        }
        ALOGV("Ion Unmapping the address %p, size %d, fd %d from Request",ionBuffer.memBuf,ionBuffer.bytesToWrite,ionBuffer.memFd);
        munmap(ionBuffer.memBuf, mInputBufferSize);
        ALOGV("closing the ion shared fd");
        close(ionBuffer.memFd);
        rc = ioctl(ionfd, ION_IOC_FREE, &ionBuffer.ion_handle);
        if (rc) {
            ALOGE("ION_IOC_FREE ioctl failed\n");
        }
        // free the local buffer corresponding to ion buffer
        free(ionBuffer.localBuf);
        ALOGV("Removing from Filled Q");
        mFilledQueue.erase(it);
    }
    if (ionfd >= 0) {
        close(ionfd);
        ionfd = -1;
    }
}

uint32_t AudioHardware::AudioSessionOutTunnel::latency() const
{
    // Android wants latency in milliseconds.
    return 1000;//TODO to correct the value
}

void AudioHardware::AudioSessionOutTunnel::requestAndWaitForEventThreadExit()
{
    if (!mEventThreadAlive)
        return;
    mKillEventThread = true;
    if (ioctl(afd, AUDIO_ABORT_GET_EVENT, 0) < 0) {
        ALOGE("AudioSessionOutTunnel:Audio Abort event failed");
    }
    pthread_join(mEventThread,NULL);
}

void * AudioHardware::AudioSessionOutTunnel::eventThreadWrapper(void *me)
{
    static_cast<AudioSessionOutTunnel *>(me)->eventThreadEntry();
    return NULL;
}

void  AudioHardware::AudioSessionOutTunnel::eventThreadEntry()
{
    struct msm_audio_event cur_pcmdec_event;
    mEventThreadAlive = true;
    int rc = 0;
    pid_t tid  = gettid();
    androidSetThreadPriority(tid, ANDROID_PRIORITY_AUDIO);
    prctl(PR_SET_NAME, (unsigned long)"HAL Audio EventThread", 0, 0, 0);
    ALOGV("AudioSessionOutTunnel::event thread: created ");
    if (mKillEventThread) {
        mEventThreadAlive = false;
        ALOGV("AudioSessionOutTunnel::event thread: is dying.");
        return;
    }
    while (1) {
        ALOGD("AudioSessionOutTunnel::event thread:: wait for an event");
        rc = ioctl(afd, AUDIO_GET_EVENT, &cur_pcmdec_event);
        ALOGD("AudioSessionOutTunnel::event thread:wakes up with retval %d and errno is %d",rc, errno);

        if ( (rc < 0) && ((errno == ENODEV) || (errno == EBADF) ) ) {
            ALOGV("AudioSessionOutTunnel::event thread: Exit the thread");
            break;
        }

        switch ( cur_pcmdec_event.event_type ) {
        case AUDIO_EVENT_WRITE_DONE:
            {
                ALOGD("AudioSessionOutTunnel::event thread:WRITE_DONE: addr %p len %d and fd is %d\n",
                     cur_pcmdec_event.event_payload.aio_buf.buf_addr,
                     cur_pcmdec_event.event_payload.aio_buf.data_len,
                     (int32_t) cur_pcmdec_event.event_payload.aio_buf.private_data);
                mFilledQueueMutex.lock();
                BuffersAllocated buf = *(mFilledQueue.begin());
                ALOGV("AudioSessionOutTunnel::event thread:Write Buffer Done: QueueStats empty q %d filled q %d"
                    , mEmptyQueue.size(), mFilledQueue.size(), __LINE__);
                for (List<BuffersAllocated>::iterator it = mFilledQueue.begin();
                    it != mFilledQueue.end(); ++it) {
                    if (it->memBuf == cur_pcmdec_event.event_payload.aio_buf.buf_addr) {
                        buf = *it;
                        mFilledQueue.erase(it);
                        // Post buffer to Empty Q
                        ALOGV("mEmptyQueueMutex locking: %d", __LINE__);
                        mEmptyQueueMutex.lock();
                        ALOGV("mEmptyQueueMutex locked: %d", __LINE__);
                        mEmptyQueue.push_back(buf);
                        ALOGV("mEmptyQueueMutex unlocking: %d", __LINE__);
                        mEmptyQueueMutex.unlock();
                        ALOGV("mEmptyQueueMutex unlocked: %d", __LINE__);
                        ALOGV("Write Buffer Done: QueueStats empty q %d filled q %d %d"
                            , mEmptyQueue.size(), mFilledQueue.size(), __LINE__);
                        mWriteCv.signal();
                        if (mPaused)
                            continue;
                        break;
                    }
                }
                mFilledQueueMutex.unlock();
            }
            break;
        case AUDIO_EVENT_SUSPEND:
            {
                struct msm_audio_stats stats;
                int nBytesConsumed = 0;

                ALOGV("AudioSessionOutTunnel::event thread:AUDIO_EVENT_SUSPEND received\n");
                if (!mPaused) {
                    ALOGV("AudioSessionOutTunnel::event thread:Not in paused, no need to honor SUSPEND event");
                    break;
                }
                // 1. Get the Byte count that is consumed
                if ( ioctl(afd, AUDIO_GET_STATS, &stats)  < 0 ) {
                    ALOGE("AudioSessionOutTunnel::event thread:AUDIO_GET_STATUS failed");
                } else {
                    ALOGV("AudioSessionOutTunnel::event thread:Number of bytes consumed by DSP is %u", stats.byte_count);
                    nBytesConsumed = stats.byte_count;
                    }
                    // 3. Call AUDIO_STOP on the Driver.
                    ALOGV("AudioSessionOutTunnel::event thread:Received AUDIO_EVENT_SUSPEND and calling AUDIO_STOP");
                    if ( ioctl(afd, AUDIO_STOP, 0) < 0 ) {
                         ALOGE("AUDIO_STOP failed");
                    }
                    break;
            }
            break;
        case AUDIO_EVENT_RESUME:
            {
                ALOGV("AudioSessionOutTunnel::event thread:AUDIO_EVENT_RESUME received\n");
            }
            break;
        default:
            ALOGE("AudioSessionOutTunnel::event thread:Received Invalid Event from driver\n");
            break;
        }
    }
    mEventThreadAlive = false;
    ALOGV("Event Thread is dying.");
}


void AudioHardware::AudioSessionOutTunnel::createEventThread()
{
    ALOGV("Creating Event Thread");
    mKillEventThread = false;
    mEventThreadAlive = true;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    pthread_create(&mEventThread, &attr, eventThreadWrapper, this);
    ALOGV("Event Thread created");
}

status_t AudioHardware::AudioSessionOutTunnel::start( )
{

    ALOGV("AudioSessionOutTunnel::start Tunnel playback start");
    if (mPaused) {
        if (ioctl(afd, AUDIO_PAUSE, 0) < 0) {
            ALOGE("AudioSessionOutTunnel::start: Resume:: Tunnel driver resume failed");
            return UNKNOWN_ERROR;
        }
        mPaused = false;
    } else {
        int sessionId = 0;
        struct msm_audio_aac_config aac_config;
        unsigned short decId;

        if ( ioctl(afd, AUDIO_GET_SESSION_ID, &decId) == -1 ) {
            ALOGE("AudioSessionOutTunnel::start:AUDIO_GET_SESSION_ID FAILED\n");
            return BAD_VALUE;
        } else {
            sessionId = (int)decId;
            ALOGV("AudioSessionOutTunnel::start:AUDIO_GET_SESSION_ID success : decId = %d", decId);
        }

        if(mFormat == AUDIO_FORMAT_AAC) {
            memset(&aac_config,0,sizeof(aac_config));
            aac_config.format = AUDIO_AAC_FORMAT_RAW;
            //aac_config.sample_rate = mSampleRate;
            aac_config.channel_configuration = mChannels;
            ALOGD("AudioSessionOutTunnel::start:sample_rate=%d and channel count=%d \n", mSampleRate, mChannels);

            if (ioctl(afd, AUDIO_SET_AAC_CONFIG, &aac_config)) {
                ALOGE("AudioSessionOutTunnel::start:could not set AUDIO_SET_AAC_CONFIG_V2 %d", errno);
                afd = -1;
                return BAD_VALUE;
            }
        }


        Mutex::Autolock lock(mDeviceSwitchLock);
#ifdef QCOM_TUNNEL_LPA_ENABLED
        if(getNodeByStreamType(LPA_DECODE) != NULL) {
            ALOGE("AudioSessionOutTunnel::start:Not allowed, There is alread an LPA Node existing");
            return -1;
        }
        ALOGV("AudioSessionOutTunnel::start:AudioSessionOutTunnel::set() Adding LPA_DECODE Node to Table");
        addToTable(sessionId,cur_rx,INVALID_DEVICE,LPA_DECODE,true);
#endif
        ALOGV("AudioSessionOutTunnel::start:enableDevice(cur_rx = %d, dev_id = %d)",cur_rx,DEV_ID(cur_rx));
        if (enableDevice(cur_rx, 1)) {
            ALOGE("AudioSessionOutTunnel::start:enableDevice failed for device cur_rx %d", cur_rx);
            return -1;
        }

        ALOGV("AudioSessionOutTunnel::start:msm_route_stream(PCM_PLAY,%d,%d,0)",sessionId,DEV_ID(cur_rx));
#ifdef QCOM_ACDB_ENABLED
        acdb_loader_send_audio_cal(ACDB_ID(cur_rx), CAPABILITY(cur_rx));
#endif
        if(msm_route_stream(PCM_PLAY,sessionId,DEV_ID(cur_rx),1)) {
            ALOGE("AudioSessionOutTunnel::start:msm_route_stream(PCM_PLAY,%d,%d,1) failed",sessionId,DEV_ID(cur_rx));
            return -1;
        }

        Mutex::Autolock lock_1(mComboDeviceLock);
#ifdef QCOM_FM_ENABLED
        if(CurrentComboDeviceData.DeviceId == SND_DEVICE_FM_TX_AND_SPEAKER){
            ALOGD("AudioSessionOutTunnel::start:Routing LPA steam to speaker for combo device");
            ALOGD("AudioSessionOutTunnel::start:combo:msm_route_stream(LPA_DECODE,session id:%d,dev id:%d,1)",sessionId,
                DEV_ID(DEVICE_SPEAKER_RX));
                /* music session is already routed to speaker in
                 * enableComboDevice(), but at this point it can
                 * be said that it was done with incorrect session id,
                 * so re-routing with correct session id here.
                 */
            if(msm_route_stream(PCM_PLAY, sessionId, DEV_ID(DEVICE_SPEAKER_RX),
               1)) {
                ALOGE("AudioSessionOutTunnel::start:msm_route_stream failed");
                return -1;
            }
            CurrentComboDeviceData.StreamType = LPA_DECODE;
        }
#endif
        if (ioctl(afd, AUDIO_START,0) < 0) {
            ALOGE("AudioSessionOutTunnel::start:Driver start failed!");
            return BAD_VALUE;
        }
        allocAndRegisterbuffs();
    }
    return NO_ERROR;
}

status_t AudioHardware::AudioSessionOutTunnel::pause()
{
    ALOGV("AudioSessionOutTunnel::pause:Tunnel playback pause");
    if (ioctl(afd, AUDIO_PAUSE, 1) < 0) {
    ALOGE("AudioSessionOutTunnel::pause:Audio Pause failed");
    }
    mPaused = true;
    return NO_ERROR;
}

status_t AudioHardware::AudioSessionOutTunnel::drain()
{
    ALOGV("Tunnel playback EOS");
    return NO_ERROR;
}

status_t AudioHardware::AudioSessionOutTunnel::flush()
{
    ALOGE("AudioSessionOutTunnel::flush Tunnel playback flush ");
    Mutex::Autolock autoLock(mFlushLock);
    int err;

    // 2.) Add all the available buffers to Empty Queue (Maintain order)
    mFilledQueueMutex.lock();
    mEmptyQueueMutex.lock();
    List<BuffersAllocated>::iterator it = mBufPool.begin();
    ALOGV("AudioSessionOutTunnel::flush : QueueStats empty q %d filled q %d %d"
        , mEmptyQueue.size(), mFilledQueue.size(), __LINE__);

    while (!mFilledQueue.empty()) {
        List<BuffersAllocated>::iterator it = mFilledQueue.begin();
        BuffersAllocated buf = *it;
        buf.bytesToWrite = 0;
        mEmptyQueue.push_back(buf);
        mFilledQueue.erase(it);
    }
    mEmptyQueueMutex.unlock();
    mFilledQueueMutex.unlock();

    ALOGV("AudioSessionOutTunnel::flush : QueueStats empty q %d filled q %d %d"
        , mEmptyQueue.size(), mFilledQueue.size(), __LINE__);

    /* If Write is blocked on empty buffers, then it's better skip
     * that buffer.
     */
    mSkipWrite = true;
    mWriteCv.signal();
    if (ioctl(afd, AUDIO_FLUSH, 0) < 0) {
        ALOGE("Audio Flush failed");
        return UNKNOWN_ERROR;
    }
    ALOGD("AudioSessionOutTunnel::flush exit");
    return NO_ERROR;
}
status_t AudioHardware::AudioSessionOutTunnel::stop()
{
    Mutex::Autolock autoLock(mLock);
    ALOGV("AudioSessionOutTunnel- stop");
    // close all the existing PCM devices
    mSkipWrite = true;
    mWriteCv.signal();

    return NO_ERROR;
}

status_t AudioHardware::AudioSessionOutTunnel::setObserver(void *observer)
{
    ALOGV("Registering the callback \n");
    mObserver = reinterpret_cast<AudioEventObserver *>(observer);
    return NO_ERROR;
}

status_t  AudioHardware::AudioSessionOutTunnel::getNextWriteTimestamp(int64_t *timestamp)
{
    msm_audio_stats stats;
    ioctl(afd, AUDIO_GET_STATS, &stats);
    memcpy(timestamp ,&stats.unused[0],sizeof(int64_t));
    ALOGV("Timestamp returned = %lld\n", *timestamp);
    return NO_ERROR;
}

void AudioHardware::AudioSessionOutTunnel::reset()
{
    Routing_table* temp = NULL;
    ALOGD("AudioSessionOutTunnel::reset()");
    requestAndWaitForEventThreadExit();
    status_t status = NO_ERROR;
    temp = getNodeByStreamType(LPA_DECODE);

    if (temp == NULL) {
        ALOGE("AudioSessionOutTunnel::resetTunnel node does not exist");
        return ;
    }
    ALOGD("AudioSessionOutTunnel::resetDeroute Tunnel playback stream");
    if(msm_route_stream(PCM_PLAY, temp->dec_id,DEV_ID(temp->dev_id), 0)) {
        ALOGE("AudioSessionOutTunnel::resetcould not set stream routing\n");
        deleteFromTable(LPA_DECODE);
    }
    deleteFromTable(LPA_DECODE);
    if (!getNodeByStreamType(VOICE_CALL) && !getNodeByStreamType(PCM_PLAY)
#ifdef QCOM_FM_ENABLED
        && !getNodeByStreamType(FM_RADIO)
#endif
#ifdef QCOM_VOIP_ENABLED
        && !getNodeByStreamType(VOIP_CALL)
#endif
       ) {
#ifdef QCOM_ANC_HEADSET_ENABLED
        if (anc_running == false) {
#endif
            if (enableDevice(cur_rx, 0)) {
                ALOGE("AudioSessionOutTunnel::resetDisabling device failed for cur_rx %d", cur_rx);
            }
#ifdef QCOM_ANC_HEADSET_ENABLED
        }
#endif
    }

    ALOGD("AudioSessionOutTunnel::reset:dealloc buffers");
    deallocAndDeregisterbuffs();

    ALOGD("AudioSessionOutTunnel::resetStop and close Tunnel Driver");
    status = ioctl(afd, AUDIO_STOP, 0);
    ::close(afd);
    ALOGD("AudioSessionOutTunnel::reset ends %d", status);
}

status_t AudioHardware::AudioSessionOutTunnel::getRenderPosition(uint32_t *dspFrames)
{
    //TODO: enable when supported by driver
    return INVALID_OPERATION;
}

#endif /*TUNNEL_PLAYBACK*/

// End AudioSessionOutLPA
//.----------------------------------------------------------------------------
#endif

int mFdin = -1;
AudioHardware::AudioStreamInMSM8x60::AudioStreamInMSM8x60() :
    mHardware(0), mState(AUDIO_INPUT_CLOSED), mRetryCount(0),
    mFormat(AUDIO_HW_IN_FORMAT), mChannels(AUDIO_HW_IN_CHANNELS),
    mSampleRate(AUDIO_HW_IN_SAMPLERATE), mBufferSize(AUDIO_HW_IN_BUFFERSIZE),
    mAcoustics((AudioSystem::audio_in_acoustics)0), mDevices(0), mForVR(0)
{
}

status_t AudioHardware::AudioStreamInMSM8x60::set(
        AudioHardware* hw, uint32_t devices, int *pFormat, uint32_t *pChannels, uint32_t *pRate,
        AudioSystem::audio_in_acoustics acoustic_flags)
{
    if ((pFormat == 0) || (*pFormat != AUDIO_HW_IN_FORMAT))
    {
        *pFormat = AUDIO_HW_IN_FORMAT;
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

    if (pChannels == 0 || (*pChannels & (AudioSystem::CHANNEL_IN_MONO | AudioSystem::CHANNEL_IN_STEREO)) == 0) {
        *pChannels = AUDIO_HW_IN_CHANNELS;
        ALOGE(" Channel count does not match\n");
        return BAD_VALUE;
    }

    mHardware = hw;

    ALOGV("AudioStreamInMSM8x60::set(%d, %d, %u)", *pFormat, *pChannels, *pRate);
    if (mFdin >= 0) {
        ALOGE("Audio record already open");
        return -EPERM;
    }
    status_t status =0;
    struct msm_voicerec_mode voc_rec_cfg;
#ifdef QCOM_FM_ENABLED
    if(devices == AUDIO_DEVICE_IN_FM_RX_A2DP) {
        status = ::open("/dev/msm_pcm_in", O_RDONLY);
        if (status < 0) {
            ALOGE("Cannot open /dev/msm_pcm_in errno: %d", errno);
            goto Error;
        }
        mFdin = status;
        // configuration
        ALOGV("get config");
        struct msm_audio_config config;
        status = ioctl(mFdin, AUDIO_GET_CONFIG, &config);
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
        status = ioctl(mFdin, AUDIO_SET_CONFIG, &config);
        if (status < 0) {
            ALOGE("Cannot set config");
            if (ioctl(mFdin, AUDIO_GET_CONFIG, &config) == 0) {
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
        status = ioctl(mFdin, AUDIO_GET_CONFIG, &config);
        if (status < 0) {
            ALOGE("Cannot read config");
            goto Error;
        }
        ALOGV("buffer_size: %u", config.buffer_size);
        ALOGV("buffer_count: %u", config.buffer_count);
        ALOGV("channel_count: %u", config.channel_count);
        ALOGV("sample_rate: %u", config.sample_rate);

        mDevices = devices;
        mFormat = AUDIO_HW_IN_FORMAT;
        mChannels = *pChannels;
        mSampleRate = config.sample_rate;
        mBufferSize = config.buffer_size;
    } else
#endif
      if (*pFormat == AUDIO_HW_IN_FORMAT) {
        if (mHardware->mNumPcmRec > 0) {
            /* Only one PCM recording is allowed at a time */
            ALOGE("Multiple PCM recordings is not allowed");
            status = -1;
            goto Error;
        }
        // open audio input device
        status = ::open("/dev/msm_pcm_in", O_RDWR);
        if (status < 0) {
            ALOGE("Cannot open /dev/msm_pcm_in errno: %d", errno);
            goto Error;
        }
        mHardware->mNumPcmRec ++;
        mFdin = status;

        // configuration
        ALOGV("get config");
        struct msm_audio_config config;
        status = ioctl(mFdin, AUDIO_GET_CONFIG, &config);
        if (status < 0) {
            ALOGE("Cannot read config");
            goto Error;
        }

        ALOGV("set config");
        config.channel_count = AudioSystem::popCount((*pChannels) &
                              (AudioSystem::CHANNEL_IN_STEREO|
                               AudioSystem::CHANNEL_IN_MONO));

        config.sample_rate = *pRate;

        mBufferSize = mHardware->getInputBufferSize(config.sample_rate,
                                                    AUDIO_HW_IN_FORMAT,
                                                    config.channel_count);
        config.buffer_size = bufferSize();
        // Make buffers to be allocated in driver equal to the number of buffers
        // that AudioFlinger allocates (Shared memory)
        config.buffer_count = 4;
        config.type = CODEC_TYPE_PCM;
        status = ioctl(mFdin, AUDIO_SET_CONFIG, &config);
        if (status < 0) {
            ALOGE("Cannot set config");
            if (ioctl(mFdin, AUDIO_GET_CONFIG, &config) == 0) {
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
        status = ioctl(mFdin, AUDIO_GET_CONFIG, &config);
        if (status < 0) {
            ALOGE("Cannot read config");
            goto Error;
        }
        ALOGV("buffer_size: %u", config.buffer_size);
        ALOGV("buffer_count: %u", config.buffer_count);
        ALOGV("channel_count: %u", config.channel_count);
        ALOGV("sample_rate: %u", config.sample_rate);

        mDevices = devices;
        mFormat = AUDIO_HW_IN_FORMAT;
        mChannels = *pChannels;
        mSampleRate = config.sample_rate;
        mBufferSize = config.buffer_size;

        if (mDevices == AUDIO_DEVICE_IN_VOICE_CALL) {
            if ((mChannels & AudioSystem::CHANNEL_IN_VOICE_DNLINK) &&
                (mChannels & AudioSystem::CHANNEL_IN_VOICE_UPLINK)) {
                ALOGV("Recording Source: Voice Call Both Uplink and Downlink");
                voc_rec_cfg.rec_mode = VOC_REC_BOTH;
            } else if (mChannels & AudioSystem::CHANNEL_IN_VOICE_DNLINK) {
                ALOGV("Recording Source: Voice Call DownLink");
                voc_rec_cfg.rec_mode = VOC_REC_DOWNLINK;
            } else if (mChannels & AudioSystem::CHANNEL_IN_VOICE_UPLINK) {
                ALOGV("Recording Source: Voice Call UpLink");
                voc_rec_cfg.rec_mode = VOC_REC_UPLINK;
            }
            if (ioctl(mFdin, AUDIO_SET_INCALL, &voc_rec_cfg)) {
                ALOGE("Error: AUDIO_SET_INCALL failed\n");
                goto  Error;
            }
        }
#ifdef QCOM_ACDB_ENABLED
    if(vr_enable && dualmic_enabled) {
        int audpre_mask = 0;
        audpre_mask = FLUENCE_ENABLE;

            ALOGV("enable fluence");
            if (ioctl(mFdin, AUDIO_ENABLE_AUDPRE, &audpre_mask)) {
                ALOGV("cannot write audio config");
                goto Error;
            }
        }
#endif
    }
    //mHardware->setMicMute_nosync(false);
    mState = AUDIO_INPUT_OPENED;
#ifdef HTC_ACOUSTIC_AUDIO
    mHardware->set_mRecordState(true);
#endif

    if (!acoustic)
    {
        return NO_ERROR;
    }
#ifdef QCOM_ACDB_ENABLED
    int (*msm8x60_set_audpre_params)(int, int);
    msm8x60_set_audpre_params = (int (*)(int, int))::dlsym(acoustic, "msm8x60_set_audpre_params");
    if ((*msm8x60_set_audpre_params) == 0) {
        ALOGI("msm8x60_set_audpre_params not present");
        return NO_ERROR;
    }

    int (*msm8x60_enable_audpre)(int, int, int);
    msm8x60_enable_audpre = (int (*)(int, int, int))::dlsym(acoustic, "msm8x60_enable_audpre");
    if ((*msm8x60_enable_audpre) == 0) {
        ALOGI("msm8x60_enable_audpre not present");
        return NO_ERROR;
    }

    audpre_index = calculate_audpre_table_index(mSampleRate);
    tx_iir_index = (audpre_index * 2) + (hw->checkOutputStandby() ? 0 : 1);
    ALOGD("audpre_index = %d, tx_iir_index = %d\n", audpre_index, tx_iir_index);

    /**
     * If audio-preprocessing failed, we should not block record.
     */
    status = msm8x60_set_audpre_params(audpre_index, tx_iir_index);
    if (status < 0)
        ALOGE("Cannot set audpre parameters");

    mAcoustics = acoustic_flags;
    status = msm8x60_enable_audpre((int)acoustic_flags, audpre_index, tx_iir_index);
    if (status < 0)
        ALOGE("Cannot enable audpre");
#endif
    return NO_ERROR;

Error:
    if (mFdin >= 0) {
        ::close(mFdin);
        mFdin = -1;
    }
    return status;
}

AudioHardware::AudioStreamInMSM8x60::~AudioStreamInMSM8x60()
{
    ALOGV("AudioStreamInMSM8x60 destructor");
    standby();
}

ssize_t AudioHardware::AudioStreamInMSM8x60::read( void* buffer, ssize_t bytes)
{
    unsigned short dec_id = INVALID_DEVICE;
    ALOGV("AudioStreamInMSM8x60::read(%p, %ld)", buffer, bytes);
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
        if (status != NO_ERROR) {
            hw->mLock.unlock();
            return -1;
        }
#ifdef QCOM_FM_ENABLED
        if((mDevices == AUDIO_DEVICE_IN_FM_RX) || (mDevices == AUDIO_DEVICE_IN_FM_RX_A2DP) ){
            if(ioctl(mFdin, AUDIO_GET_SESSION_ID, &dec_id)) {
                ALOGE("AUDIO_GET_SESSION_ID failed*********");
                hw->mLock.unlock();
                return -1;
            }

            if(enableDevice(DEVICE_FMRADIO_STEREO_TX, 1)) {
                ALOGE("enableDevice DEVICE_FMRADIO_STEREO_TX failed");
                hw->mLock.unlock();
                return -1;
             }

            acdb_loader_send_audio_cal(ACDB_ID(DEVICE_FMRADIO_STEREO_TX),
            CAPABILITY(DEVICE_FMRADIO_STEREO_TX));

            ALOGV("route FM");
            if(msm_route_stream(PCM_REC, dec_id, DEV_ID(DEVICE_FMRADIO_STEREO_TX), 1)) {
                ALOGE("msm_route_stream failed");
                hw->mLock.unlock();
                return -1;
            }

            //addToTable(dec_id,cur_tx,INVALID_DEVICE,PCM_REC,true);
            mFirstread = false;
            if (mDevices == AUDIO_DEVICE_IN_FM_RX_A2DP) {
                addToTable(dec_id,cur_tx,INVALID_DEVICE,FM_A2DP,true);
                mFmRec = FM_A2DP_REC;
            } else {
                addToTable(dec_id,cur_tx,INVALID_DEVICE,FM_REC,true);
                mFmRec = FM_FILE_REC;
            }
            hw->mLock.unlock();
        } else
#endif
        {
            hw->mLock.unlock();
            if(ioctl(mFdin, AUDIO_GET_SESSION_ID, &dec_id)) {
                ALOGE("AUDIO_GET_SESSION_ID failed*********");
                return -1;
            }
            Mutex::Autolock lock(mDeviceSwitchLock);
            if (!(mChannels & AudioSystem::CHANNEL_IN_VOICE_DNLINK ||
                  mChannels & AudioSystem::CHANNEL_IN_VOICE_UPLINK)) {
                 ALOGV("dec_id = %d,cur_tx= %d",dec_id,cur_tx);
                 if(cur_tx == INVALID_DEVICE)
                     cur_tx = DEVICE_HANDSET_TX;
                 if(enableDevice(cur_tx, 1)) {
                     ALOGE("enableDevice failed for device cur_rx %d",cur_rx);
                     return -1;
                 }
#ifdef QCOM_ACDB_ENABLED
                 acdb_loader_send_audio_cal(ACDB_ID(cur_tx), CAPABILITY(cur_tx));
#endif
                 if(msm_route_stream(PCM_REC, dec_id, DEV_ID(cur_tx), 1)) {
                    ALOGE("msm_route_stream failed");
                    return -1;
                 }
                 addToTable(dec_id,cur_tx,INVALID_DEVICE,PCM_REC,true);
            }
            mFirstread = false;
        }
    }


    if (mState < AUDIO_INPUT_STARTED) {
        if (!(mChannels & AudioSystem::CHANNEL_IN_VOICE_DNLINK ||
            mChannels & AudioSystem::CHANNEL_IN_VOICE_UPLINK)) {
#ifdef QCOM_FM_ENABLED
            // force routing to input device
            // for FM recording, no need to reconfigure afe loopback path
            if (mFmRec != FM_FILE_REC) {
#endif
                mHardware->clearCurDevice();
                mHardware->doRouting(this, 0);
#ifdef HTC_ACOUSTIC_AUDIO
                if (support_aic3254) {
                    int snd_dev = mHardware->get_snd_dev();
                    mHardware->aic3254_config(snd_dev);
                    mHardware->do_aic3254_control(snd_dev);
                }
#endif
#ifdef QCOM_FM_ENABLED
            }
#endif
        }
        if (ioctl(mFdin, AUDIO_START, 0)) {
            ALOGE("Error starting record");
            standby();
            return -1;
        }
        mState = AUDIO_INPUT_STARTED;
    }

    bytes = 0;
    if(mFormat == AUDIO_HW_IN_FORMAT)
    {
        if(count < mBufferSize) {
            ALOGE("read:: read size requested is less than min input buffer size");
            return 0;
        }
        while (count >= mBufferSize) {
            ssize_t bytesRead = ::read(mFdin, buffer, count);
            usleep(1);
            if (bytesRead >= 0) {
                count -= bytesRead;
                p += bytesRead;
                bytes += bytesRead;
                if(!mFirstread)
                {
                   mFirstread = true;
                   ALOGE(" FirstRead Done bytesRead = %d count = %d",bytesRead,count);
                   break;
                }
            } else {
                if (errno != EAGAIN) return bytesRead;
                mRetryCount++;
                ALOGW("EAGAIN - retrying");
            }
        }
    }

    return bytes;
}

status_t AudioHardware::AudioStreamInMSM8x60::standby()
{
    bool isDriverClosed = false;
    ALOGD("AudioStreamInMSM8x60::standby()");
    Routing_table* temp = NULL;
    if (!mHardware) return -1;
#ifdef HTC_ACOUSTIC_AUDIO
    mHardware->set_mRecordState(false);
    if (support_aic3254) {
        int snd_dev = mHardware->get_snd_dev();
        mHardware->aic3254_config(snd_dev);
        mHardware->do_aic3254_control(snd_dev);
    }
#endif
    if (mState > AUDIO_INPUT_CLOSED) {
        if (mFdin >= 0) {
            ::close(mFdin);
            mFdin = -1;
            ALOGV("driver closed");
            isDriverClosed = true;
            if(mHardware->mNumPcmRec && mFormat == AUDIO_HW_IN_FORMAT) {
                mHardware->mNumPcmRec --;
            }
        }
        mState = AUDIO_INPUT_CLOSED;
    }
#ifdef QCOM_FM_ENABLED
       if (mFmRec == FM_A2DP_REC) {
        //A2DP Recording
        temp = getNodeByStreamType(FM_A2DP);
        if(temp == NULL)
            return NO_ERROR;
        if(msm_route_stream(PCM_PLAY, temp->dec_id, DEV_ID(DEVICE_FMRADIO_STEREO_TX), 0)) {
           ALOGE("msm_route_stream failed");
           return 0;
        }
        deleteFromTable(FM_A2DP);
        if(enableDevice(DEVICE_FMRADIO_STEREO_TX, 0)) {
            ALOGE("enableDevice failed for device cur_rx %d", cur_rx);
        }
    }
    if (mFmRec == FM_FILE_REC) {
        //FM Recording
        temp = getNodeByStreamType(FM_REC);
        if(temp == NULL)
            return NO_ERROR;
        if(msm_route_stream(PCM_PLAY, temp->dec_id, DEV_ID(DEVICE_FMRADIO_STEREO_TX), 0)) {
           ALOGE("msm_route_stream failed");
           return 0;
        }
        deleteFromTable(FM_REC);
    }
#endif
    temp = getNodeByStreamType(PCM_REC);
    if(temp == NULL)
        return NO_ERROR;

    if(isDriverClosed){
        ALOGV("Deroute pcm stream");
        if (!(mChannels & AudioSystem::CHANNEL_IN_VOICE_DNLINK ||
            mChannels & AudioSystem::CHANNEL_IN_VOICE_UPLINK)) {
            if(msm_route_stream(PCM_REC, temp->dec_id,DEV_ID(temp->dev_id), 0)) {
               ALOGE("could not set stream routing\n");
               deleteFromTable(PCM_REC);
               return -1;
            }
        }
        ALOGV("Disable device");
        deleteFromTable(PCM_REC);
        if(!getNodeByStreamType(VOICE_CALL)
#ifdef QCOM_VOIP_ENABLED
         && !getNodeByStreamType(VOIP_CALL)
#endif
         ) {
            if(enableDevice(cur_tx, 0)) {
                ALOGE("Disabling device failed for cur_tx %d", cur_tx);
                return 0;
            }
        }
    } //isDriverClosed condition
    // restore output routing if necessary
    mHardware->clearCurDevice();
    mHardware->doRouting(this, 0);
    return NO_ERROR;
}

status_t AudioHardware::AudioStreamInMSM8x60::dump(int fd, const Vector<String16>& args)
{
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;
    result.append("AudioStreamInMSM8x60::dump\n");
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
    snprintf(buffer, SIZE, "\tmFd count: %d\n", mFdin);
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmState: %d\n", mState);
    result.append(buffer);
    snprintf(buffer, SIZE, "\tmRetryCount: %d\n", mRetryCount);
    result.append(buffer);
    ::write(fd, result.string(), result.size());
    return NO_ERROR;
}

status_t AudioHardware::AudioStreamInMSM8x60::setParameters(const String8& keyValuePairs)
{
    AudioParameter param = AudioParameter(keyValuePairs);
    String8 key = String8(AudioParameter::keyRouting);
    status_t status = NO_ERROR;
    int device;
    ALOGV("AudioStreamInMSM8x60::setParameters() %s", keyValuePairs.string());

    if (param.getInt(String8("vr_mode"), mForVR) == NO_ERROR)
        ALOGV("voice_recognition=%d", mForVR);

    if (param.getInt(key, device) == NO_ERROR) {
        ALOGV("set input routing %x", device);
        if (device & (device - 1)) {
            status = BAD_VALUE;
        } else if (device) {
            mDevices = device;
            status = mHardware->doRouting(this, 0);
        }
        param.remove(key);
    }

    if (param.size()) {
        status = BAD_VALUE;
    }
    return status;
}

String8 AudioHardware::AudioStreamInMSM8x60::getParameters(const String8& keys)
{
    AudioParameter param = AudioParameter(keys);
    String8 value;
    String8 key = String8(AudioParameter::keyRouting);

    if (param.get(key, value) == NO_ERROR) {
        ALOGV("get routing %x", mDevices);
        param.addInt(key, (int)mDevices);
    }

    ALOGV("AudioStreamInMSM8x60::getParameters() %s", param.toString().string());
    return param.toString();
}

// getActiveInput_l() must be called with mLock held
AudioHardware::AudioStreamInMSM8x60 *AudioHardware::getActiveInput_l()
{
    for (size_t i = 0; i < mInputs.size(); i++) {
        // return first input found not being in standby mode
        // as only one input can be in this state
        if (mInputs[i]->state() > AudioStreamInMSM8x60::AUDIO_INPUT_CLOSED) {
            return mInputs[i];
        }
    }

    return NULL;
}
extern "C" AudioHardwareInterface* createAudioHardware(void) {
    return new AudioHardware();
}

}; // namespace android_audio_legacy
