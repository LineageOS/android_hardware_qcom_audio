/* audio_stream_out.c
 **
 ** Copyright 2008-2009 Wind River Systems
 ** Copyright (c) 2011-2013, The Linux Foundation. All rights reserved
 ** Not a Contribution, Apache license notifications and license are retained
 ** for attribution purposes only.
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

#define LOG_TAG "audio_stream_out"
/*#define LOG_NDEBUG 0*/
/*#define VERY_VERY_VERBOSE_LOGGING*/
#ifdef VERY_VERY_VERBOSE_LOGGING
#define ALOGVV ALOGV
#else
#define ALOGVV(a...) do { } while(0)
#endif

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/time.h>
#include <stdlib.h>
#include <math.h>
#include <dlfcn.h>
#include <sys/resource.h>
#include <sys/prctl.h>

#include <cutils/log.h>
#include <cutils/str_parms.h>
#include <cutils/properties.h>
#include <cutils/atomic.h>
#include <cutils/sched_policy.h>

#include <system/thread_defs.h>
#include "audio_hw.h"
#include "platform_api.h"
#include <platform.h>

#include "sound/compress_params.h"
#include "audio_bitstream_sm.h"

//TODO: enable sw_decode if required
#define USE_SWDECODE 0

#if USE_SWDECODE
#include "SoftMS11.h"
#endif

#define COMPRESS_OFFLOAD_FRAGMENT_SIZE (32 * 1024)
#define COMPRESS_OFFLOAD_NUM_FRAGMENTS 4
/* ToDo: Check and update a proper value in msec */
#define COMPRESS_OFFLOAD_PLAYBACK_LATENCY 96
#define COMPRESS_PLAYBACK_VOLUME_MAX 0x2000
#define STRING_LENGTH_OF_INTEGER 12

static int send_offload_cmd_l(struct stream_out* out, int command);
static int get_snd_codec_id(audio_format_t format);

struct pcm_config pcm_config_deep_buffer = {
    .channels = 2,
    .rate = DEFAULT_OUTPUT_SAMPLING_RATE,
    .period_size = DEEP_BUFFER_OUTPUT_PERIOD_SIZE,
    .period_count = DEEP_BUFFER_OUTPUT_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = DEEP_BUFFER_OUTPUT_PERIOD_SIZE / 4,
    .stop_threshold = INT_MAX,
    .avail_min = DEEP_BUFFER_OUTPUT_PERIOD_SIZE / 4,
};

struct pcm_config pcm_config_low_latency = {
    .channels = 2,
    .rate = DEFAULT_OUTPUT_SAMPLING_RATE,
    .period_size = LOW_LATENCY_OUTPUT_PERIOD_SIZE,
    .period_count = LOW_LATENCY_OUTPUT_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = LOW_LATENCY_OUTPUT_PERIOD_SIZE / 4,
    .stop_threshold = INT_MAX,
    .avail_min = LOW_LATENCY_OUTPUT_PERIOD_SIZE / 4,
};

struct pcm_config pcm_config_hdmi_multi = {
    .channels = HDMI_MULTI_DEFAULT_CHANNEL_COUNT, /* changed when the stream is opened */
    .rate = DEFAULT_OUTPUT_SAMPLING_RATE, /* changed when the stream is opened */
    .period_size = HDMI_MULTI_PERIOD_SIZE,
    .period_count = HDMI_MULTI_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = 0,
    .stop_threshold = INT_MAX,
    .avail_min = 0,
};

inline int nextMultiple(int n, int m) {
    return ((n/m) + 1) * m;
}

/*******************************************************************************
Description: check for MS11 supported formats
*******************************************************************************/
//TODO: enable sw_decode if required
#if USE_SWDECODE
int is_ms11_supported_fromats(int format)
{
    ALOGVV("is_ms11_supported_fromats");
    int main_format = format & AUDIO_FORMAT_MAIN_MASK;
    if(((main_format == AUDIO_FORMAT_AAC) ||
        (main_format == AUDIO_FORMAT_HE_AAC_V1) ||
        (main_format == AUDIO_FORMAT_HE_AAC_V2) ||
        (main_format == AUDIO_FORMAT_AC3) ||
        (main_format == AUDIO_FORMAT_AC3_PLUS) ||
        (main_format == AUDIO_FORMAT_EAC3))) {
        return 1;
    } else {
        return 0;
    }
}
#endif

/*******************************************************************************
Description: check if ac3 can played as pass through without MS11 decoder
*******************************************************************************/
//TODO: enable sw_decode if required
#if USE_SWDECODE
int can_ac3_passthrough_without_ms11(struct stream_out *out, int format)
{
    ALOGVV("can_ac3_passthrough_without_ms11");
    int main_format = format & AUDIO_FORMAT_MAIN_MASK;
    if(main_format == AUDIO_FORMAT_AC3) {
        if(((out->hdmi_format == COMPRESSED) ||
            (out->hdmi_format == AUTO_DEVICE_FORMAT) ||
            (out->hdmi_format == COMPRESSED_CONVERT_EAC3_AC3) ||
            (out->hdmi_format == COMPRESSED_CONVERT_ANY_AC3)) &&
            ((out->spdif_format == COMPRESSED) ||
             (out->spdif_format == AUTO_DEVICE_FORMAT) ||
             (out->spdif_format == COMPRESSED_CONVERT_EAC3_AC3) ||
             (out->spdif_format == COMPRESSED_CONVERT_ANY_AC3))) {
                return 1;
        }
    }
    return 0;
}
#endif

/*******************************************************************************
Description: get levels of buffering, interms of number of buffers
*******************************************************************************/
int get_buffering_factor(struct stream_out *out)
{
    ALOGVV("get_buffering_factor");
    if((out->format == AUDIO_FORMAT_PCM_16_BIT) ||
       (out->format == AUDIO_FORMAT_PCM_24_BIT))
        return 1;
    else
        return NUM_OF_PERIODS;
}

/*******************************************************************************
Description: get the buffer size based on format and device format type
*******************************************************************************/
void get_fragment_size_and_format(struct stream_out *out, int routeFormat, int *fragment_size,
                                       int *fragment_count, int *format)
{
    ALOGV("get_fragment_size_and_format");

    int frame_size = 0;
    *format = out->format;
    *fragment_count = NUM_OF_PERIODS;
    switch(out->format) {
    case AUDIO_FORMAT_PCM_16_BIT:
        frame_size = PCM_16_BITS_PER_SAMPLE * out->channels;
        /*TODO: do we need below calculation */
        *fragment_size = nextMultiple(((frame_size * out->sample_rate * TIME_PER_BUFFER)/1000) + MIN_SIZE_FOR_METADATA , frame_size * 32);
        break;
    case AUDIO_FORMAT_PCM_24_BIT:
        frame_size = PCM_24_BITS_PER_SAMPLE * out->channels;
        *fragment_size = nextMultiple(((frame_size * out->sample_rate * TIME_PER_BUFFER)/1000) + MIN_SIZE_FOR_METADATA, frame_size * 32);
        break;
    case AUDIO_FORMAT_AAC:
    case AUDIO_FORMAT_HE_AAC_V1:
    case AUDIO_FORMAT_HE_AAC_V2:
    case AUDIO_FORMAT_AAC_ADIF:
    case AUDIO_FORMAT_AC3:
    case AUDIO_FORMAT_AC3_DM:
    case AUDIO_FORMAT_EAC3:
    case AUDIO_FORMAT_EAC3_DM:
        if(routeFormat == ROUTE_UNCOMPRESSED_MCH) {
            frame_size = PCM_16_BITS_PER_SAMPLE * out->channels;
            *fragment_size = nextMultiple(AC3_PERIOD_SIZE * out->channels + MIN_SIZE_FOR_METADATA, frame_size * 32);
            *format = AUDIO_FORMAT_PCM_16_BIT;
        } else if(routeFormat == ROUTE_UNCOMPRESSED) {
            frame_size = PCM_16_BITS_PER_SAMPLE * 2;
            *fragment_size = nextMultiple(AC3_PERIOD_SIZE * 2 + MIN_SIZE_FOR_METADATA, frame_size * 32);
            *format = AUDIO_FORMAT_PCM_16_BIT;
        } else {
            *fragment_size = PERIOD_SIZE_COMPR;
        }
        break;
    case AUDIO_FORMAT_DTS:
    case AUDIO_FORMAT_DTS_LBR:
    case AUDIO_FORMAT_MP3:
    case AUDIO_FORMAT_WMA:
    case AUDIO_FORMAT_WMA_PRO:
    case AUDIO_FORMAT_MP2:
        *fragment_size = PERIOD_SIZE_COMPR;
        break;
    default:
        *fragment_size = PERIOD_SIZE_COMPR;
        *format = out->format;
    }

    /*TODO: remove this if fragement count needs to be decided based on the format*/
    *fragment_count = COMPRESS_OFFLOAD_NUM_FRAGMENTS;
    fragment_size = COMPRESS_OFFLOAD_FRAGMENT_SIZE;

    ALOGV("fragment_size: %d, fragment_count: %d", *fragment_size, *fragment_count);
    return;
}

/*******************************************************************************
Description: buffer length updated to player
*******************************************************************************/
int get_buffer_length(struct stream_out *out)
{
    /* TODO: Do we need below */
    ALOGV("get_buffer_length");
    int buffer_size = COMPRESS_OFFLOAD_FRAGMENT_SIZE;
    switch(out->format) {
    case AUDIO_FORMAT_PCM_16_BIT:
        buffer_size = ((PCM_16_BITS_PER_SAMPLE * out->channels * out->sample_rate * TIME_PER_BUFFER)/1000);
        break;
    case AUDIO_FORMAT_PCM_24_BIT:
        buffer_size = ((PCM_24_BITS_PER_SAMPLE * out->channels * out->sample_rate * TIME_PER_BUFFER)/1000);
        break;
    case AUDIO_FORMAT_AAC:
    case AUDIO_FORMAT_HE_AAC_V1:
    case AUDIO_FORMAT_HE_AAC_V2:
    case AUDIO_FORMAT_AAC_ADIF:
        buffer_size = AAC_BLOCK_PER_CHANNEL_MS11 * out->channels;
        break;
    case AUDIO_FORMAT_AC3:
    case AUDIO_FORMAT_AC3_DM:
    case AUDIO_FORMAT_EAC3:
    case AUDIO_FORMAT_EAC3_DM:
        buffer_size = AC3_BUFFER_SIZE;
        break;
    case AUDIO_FORMAT_DTS:
    case AUDIO_FORMAT_DTS_LBR:
    case AUDIO_FORMAT_MP3:
    case AUDIO_FORMAT_WMA:
    case AUDIO_FORMAT_WMA_PRO:
    case AUDIO_FORMAT_MP2:
        buffer_size = COMPR_INPUT_BUFFER_SIZE;
        break;
    default:
        buffer_size = COMPRESS_OFFLOAD_FRAGMENT_SIZE;
    }

    /*TODO: remove this if fragement count needs to be decided based on the format*/
    buffer_size = COMPRESS_OFFLOAD_FRAGMENT_SIZE;
    return buffer_size;
}

/* TODO: Uncomment this when enabling A2DP
 TODO: add support for the 24 bit playback*/
#if 0
/*******************************************************************************
Description: fix up devices for supporting A2DP playback
*******************************************************************************/
void fixUpDevicesForA2DPPlayback(struct stream_out *out)
{
    ALOGVV("fixUpDevicesForA2DPPlayback");
    if(out->devices & AUDIO_DEVICE_OUT_ALL_A2DP) {
        out->route_audio_to_a2dp = 1;
        out->devices  &= ~AUDIO_DEVICE_OUT_ALL_A2DP;
        //TODO: add spdif and proxy
        //out->devices  &= ~AUDIO_DEVICE_OUT_SPDIF;
        //out->devices |=  AudioSystem::DEVICE_OUT_PROXY;
    }
}
#endif

/*******************************************************************************
Description: open temp buffer so that meta data mode can be updated properly
*******************************************************************************/
int  open_temp_buf_for_metadata(struct stream_out *out)
{
    ALOGV("%s", __func__);
    if (out->write_temp_buf == NULL) {
        /*Max Period size which is exposed by the compr driver
        The value needs to be modified when the period size is modified*/
        out->write_temp_buf = (char *) malloc(PLAYBACK_MAX_PERIOD_SIZE);
        if (out->write_temp_buf == NULL) {
            ALOGE("Memory allocation of temp buffer to write pcm to driver failed");
            return -EINVAL;
        }
    }
    return 0;
}

/*******************************************************************************
Description: get index of handle based on device handle device
*******************************************************************************/
struct alsa_handle * get_handle_based_on_devices(struct stream_out *out, int handleDevices)
{
    ALOGVV("get_handle_based_on_devices");
    struct listnode *node;
    struct alsa_handle *handle = NULL;

    list_for_each(node, &out->session_list) {
            handle = node_to_item(node, struct alsa_handle, list);
            if(handle->devices & handleDevices)
                break;
    }
    return handle;
}

void reset_out_parameters(struct stream_out *out) {

    out->hdmi_format = UNCOMPRESSED;
    out->spdif_format = UNCOMPRESSED;
    out->decoder_type = UNCOMPRESSED ;
    out->dec_conf_set = false;
    out->min_bytes_req_to_dec = 0;
    out->is_m11_file_mode = false;
    out->dec_conf_bufLength = 0;
    out->first_bitstrm_buf = false;
    out->open_dec_route = false;
    out->dec_format_devices = AUDIO_DEVICE_NONE;
    out->open_dec_mch_route = false;
    out->dec_mch_format_devices =AUDIO_DEVICE_NONE;
    out->open_passt_route = false;
    out->passt_format_devices = AUDIO_DEVICE_NONE;
    out->sw_open_trans_route = false;
    out->sw_trans_format_devices = AUDIO_DEVICE_NONE;
    out->hw_open_trans_route =false ;
    out->hw_trans_format_devices = AUDIO_DEVICE_NONE;
    out->channel_status_set = false;
    out->route_audio_to_a2dp = false;
    out->is_ms11_file_playback_mode = false;
    out->write_temp_buf = NULL;
    return;
}

struct alsa_handle *get_alsa_handle() {

    struct alsa_handle *handle;
        handle = (struct alsa_handle *)calloc(1, sizeof(struct alsa_handle));
        if(handle == NULL) {
            ALOGE("%s calloc failed for handle", __func__);
        } else {
            ALOGE("%s handle is 0x%x", __func__,(uint32_t)handle);
        }

        return handle;
}

void free_alsa_handle(struct alsa_handle *handle) {

    if(handle == NULL) {
        ALOGE("%s Invalid handle", __func__);
    }
    free(handle);

    return;
}


struct alsa_handle *get_handle_by_route_format(struct stream_out *out,
                            int route_format)
{
    struct listnode *node;
    struct alsa_handle *handle = NULL;
    ALOGV("%s",__func__);
    list_for_each(node, &out->session_list)  {
        handle = node_to_item(node, struct alsa_handle, list);
        if(handle->route_format & route_format) {
            ALOGV("%s found handle %x",__func__,(uint32_t)handle);
            break;
        }
    }

    return handle;
}

/*******************************************************************************
Description: get the format index
*******************************************************************************/
int get_format_index(int format)
{
    ALOGVV("get_format_index");
    int idx = 0,i;
    for(i=0; i<NUM_SUPPORTED_CODECS; i++) {
        if(format == format_index[i][0]) {
            idx = format_index[i][1];
            break;
        }
    }
   return idx;
}

int get_compress_available_space(struct alsa_handle *handle)
{
   uint32_t ret;
   size_t avail = 0;
   struct timespec tstamp;
   ret = compress_get_hpointer(handle->compr,&avail, &tstamp);
   if(ret!=0) {
       ALOGE("cannot get available space\n");
    } else
        ret = (int)avail;
    return ret;
}


/*******************************************************************************
Description: validate if the decoder requires configuration to be set as first
             buffer
*******************************************************************************/
int is_decoder_config_required(struct stream_out *out)
{
    ALOGVV("is_decoder_config_required");
    int main_format = out->format & AUDIO_FORMAT_MAIN_MASK;
    uint32_t i;
    if(!out->is_ms11_file_playback_mode)
        return 0;
    for(i=0; i<sizeof(decodersRequireConfig)/sizeof(int); i++)
        if(main_format == decodersRequireConfig[i])
            return 1;
    return 0;
}

/*******************************************************************************
Description: query if input buffering mode require
*******************************************************************************/
int is_input_buffering_mode_reqd(struct stream_out *out)
{
    ALOGVV("is_input_buffering_mode_reqd");
    if((out->decoder_type == SW_PASSTHROUGH) ||
       (out->decoder_type == DSP_PASSTHROUGH))
        return 1;
    else
        return 0;
}



/*******************************************************************************
Description: update use case and routing flags
*******************************************************************************/
void update_decode_type_and_routing_states(struct stream_out *out)
{
    ALOGV("%s", __func__);

    int format_index = get_format_index(out->format);
    int decodeType, idx;

    out->open_dec_route = false;
    out->open_dec_mch_route = false;
    out->open_passt_route = false;
    out->sw_open_trans_route = false;
    out->hw_open_trans_route = false;
    out->dec_format_devices = out->devices;
    out->dec_mch_format_devices = AUDIO_DEVICE_NONE;
    out->passt_format_devices = AUDIO_DEVICE_NONE;
    out->sw_trans_format_devices = AUDIO_DEVICE_NONE;
    out->hw_trans_format_devices = AUDIO_DEVICE_NONE;
    out->decoder_type = 0;

//TODO: enable sw_decode if required
#if USE_SWDECODE
    if(is_ms11_supported_fromats(out->format))
        out->use_ms11_decoder = true;
#endif

    ALOGV("format_index: %d devices %x", format_index,out->devices);
    if(out->devices & AUDIO_DEVICE_OUT_SPDIF) {
        decodeType = usecase_docode_hdmi_spdif[NUM_STATES_FOR_EACH_DEVICE_FMT*format_index]
            [out->spdif_format];
        ALOGV("SPDIF: decoderType: %d", decodeType);
        out->decoder_type = decodeType;
        for(idx=0; idx<NUM_DECODE_PATH; idx++) {
            if(route_to_driver[idx][DECODER_TYPE_IDX] == decodeType) {
                switch(route_to_driver[idx][ROUTE_FORMAT_IDX]) {
                    case ROUTE_UNCOMPRESSED:
                        ALOGVV("ROUTE_UNCOMPRESSED");
                        ALOGVV("SPDIF opened with stereo decode");
                        out->open_dec_route = true;
                        break;
                    case ROUTE_UNCOMPRESSED_MCH:
                        ALOGVV("ROUTE_UNCOMPRESSED_MCH");
                        ALOGVV("SPDIF opened with multichannel decode");
                        out->open_dec_mch_route = true;
                        out->dec_format_devices &= ~AUDIO_DEVICE_OUT_SPDIF;
                        out->dec_mch_format_devices |= AUDIO_DEVICE_OUT_SPDIF;
                        break;
                    case ROUTE_COMPRESSED:
                        ALOGVV("ROUTE_COMPRESSED");
                        out->open_passt_route = true;
                        out->dec_format_devices &= ~AUDIO_DEVICE_OUT_SPDIF;
                        out->passt_format_devices = AUDIO_DEVICE_OUT_SPDIF;
                        break;
                    case ROUTE_DSP_TRANSCODED_COMPRESSED:
                        ALOGVV("ROUTE_DSP_TRANSCODED_COMPRESSED");
                        out->hw_open_trans_route = true;
                        out->hw_trans_format_devices = AUDIO_DEVICE_OUT_SPDIF;
                        break;
                    case ROUTE_SW_TRANSCODED_COMPRESSED:
                        ALOGVV("ROUTE_SW_TRANSCODED_COMPRESSED");
                        out->sw_open_trans_route = true;
                        out->dec_format_devices &= ~AUDIO_DEVICE_OUT_SPDIF;
                        out->sw_trans_format_devices = AUDIO_DEVICE_OUT_SPDIF;
                        break;
                    default:
                        ALOGW("INVALID ROUTE for SPDIF, decoderType %d, routeFormat %d",
                                decodeType, route_to_driver[idx][ROUTE_FORMAT_IDX]);
                        break;
                }
            }
        }
    }
    if(out->devices & AUDIO_DEVICE_OUT_AUX_DIGITAL) {
        decodeType = usecase_docode_hdmi_spdif[NUM_STATES_FOR_EACH_DEVICE_FMT*format_index]
            [out->hdmi_format];
        ALOGV("HDMI: decoderType: %d", decodeType);
        out->decoder_type |= decodeType;
        for(idx=0; idx<NUM_DECODE_PATH; idx++) {
            if(route_to_driver[idx][DECODER_TYPE_IDX] == decodeType) {
                switch(route_to_driver[idx][ROUTE_FORMAT_IDX]) {
                    case ROUTE_UNCOMPRESSED:
                        ALOGVV("ROUTE_UNCOMPRESSED");
                        ALOGVV("HDMI opened with stereo decode");
                        out->open_dec_route = true;
                        break;
                    case ROUTE_UNCOMPRESSED_MCH:
                        ALOGVV("ROUTE_UNCOMPRESSED_MCH");
                        ALOGVV("HDMI opened with multichannel decode");
                        out->open_dec_mch_route = true;
                        out->dec_format_devices &= ~AUDIO_DEVICE_OUT_AUX_DIGITAL;
                        out->dec_mch_format_devices |= AUDIO_DEVICE_OUT_AUX_DIGITAL;
                        break;
                    case ROUTE_COMPRESSED:
                        ALOGVV("ROUTE_COMPRESSED");
                        out->open_passt_route = true;
                        out->dec_format_devices &= ~AUDIO_DEVICE_OUT_AUX_DIGITAL;
                        out->passt_format_devices |= AUDIO_DEVICE_OUT_AUX_DIGITAL;
                        break;
                    case ROUTE_DSP_TRANSCODED_COMPRESSED:
                        ALOGVV("ROUTE_DSP_TRANSCODED_COMPRESSED");
                        out->hw_open_trans_route = true;
                        out->hw_trans_format_devices |= AUDIO_DEVICE_OUT_AUX_DIGITAL;
                        break;
                    case ROUTE_SW_TRANSCODED_COMPRESSED:
                        ALOGVV("ROUTE_SW_TRANSCODED_COMPRESSED");
                        out->sw_open_trans_route = true;
                        out->dec_format_devices &= ~AUDIO_DEVICE_OUT_AUX_DIGITAL;
                        out->sw_trans_format_devices |= AUDIO_DEVICE_OUT_AUX_DIGITAL;
                        break;
                    default:
                        ALOGW("INVALID ROUTE for HDMI, decoderType %d, routeFormat %d",
                                decodeType, route_to_driver[idx][ROUTE_FORMAT_IDX]);
                        break;
                }
            }
        }
    }
    if(out->devices & ~(AUDIO_DEVICE_OUT_AUX_DIGITAL |
                AUDIO_DEVICE_OUT_SPDIF)) {
        decodeType = usecase_decode_format[NUM_STATES_FOR_EACH_DEVICE_FMT*format_index];
        ALOGV("Other Devices: decoderType: %d", decodeType);
        out->decoder_type |= decodeType;
        for(idx=0; idx<NUM_DECODE_PATH; idx++) {
            if(route_to_driver[idx][DECODER_TYPE_IDX] == decodeType) {
                switch(route_to_driver[idx][ROUTE_FORMAT_IDX]) {
                    case ROUTE_UNCOMPRESSED:
                        ALOGVV("ROUTE_UNCOMPRESSED");
                        ALOGVV("Other Devices opened with stereo decode");
                        out->open_dec_route = true;
                        break;
                    case ROUTE_UNCOMPRESSED_MCH:
                        ALOGVV("ROUTE_UNCOMPRESSED_MCH");
                        ALOGVV("Other Devices opened with multichannel decode");
                        out->open_dec_mch_route = true;
                        out->dec_format_devices &= ~(out->devices &
                                ~(AUDIO_DEVICE_OUT_SPDIF |
                                    AUDIO_DEVICE_OUT_AUX_DIGITAL));
                        out->dec_mch_format_devices |= (out->devices &
                                ~(AUDIO_DEVICE_OUT_SPDIF |
                                    AUDIO_DEVICE_OUT_AUX_DIGITAL));
                        break;
                    default:
                        ALOGW("INVALID ROUTE for Other Devices, decoderType %d, routeFormat %d",
                                decodeType, route_to_driver[idx][ROUTE_FORMAT_IDX]);
                        break;
                }
            }
        }
    }
}

/*******************************************************************************
Description: update handle states
*******************************************************************************/
int update_alsa_handle_state(struct stream_out *out)
{
    ALOGV("%s", __func__);

    struct alsa_handle *handle = NULL;
    struct listnode *node;

    if(out->open_dec_route) {
        if((handle = get_alsa_handle())== NULL)
            goto error;
        list_add_tail(&out->session_list, &handle->list);
        handle->route_format = ROUTE_UNCOMPRESSED;
        handle->devices = out->dec_format_devices;
        handle->usecase = platform_get_usecase(USECASE_AUDIO_PLAYBACK_OFFLOAD);
        handle->out = out;
        handle->cmd_pending = false;
        ALOGD("open_dec_route: routeformat: %d, devices: 0x%x: "
                ,handle->route_format, handle->devices);
    }
    if(out->open_dec_mch_route) {
        if((handle = get_alsa_handle())== NULL)
            goto error;
        list_add_tail(&out->session_list, &handle->list);
        handle->route_format = ROUTE_UNCOMPRESSED_MCH;
        handle->devices = out->dec_mch_format_devices;
        handle->usecase = platform_get_usecase(USECASE_AUDIO_PLAYBACK_OFFLOAD);
        handle->out = out;
        handle->cmd_pending = false;
        ALOGD("OpenMCHDecodeRoute: routeformat: %d, devices: 0x%x: "
               ,handle->route_format, handle->devices);
    }
    if(out->open_passt_route) {
        if((handle = get_alsa_handle())== NULL)
            goto error;
        list_add_tail(&out->session_list, &handle->list);
        handle->route_format = ROUTE_COMPRESSED;
        handle->devices = out->passt_format_devices;
        handle->usecase = platform_get_usecase(USECASE_AUDIO_PLAYBACK_OFFLOAD);
        handle->out = out;
        handle->cmd_pending = false;
        ALOGD("open_passt_route: routeformat: %d, devices: 0x%x: "
               ,handle->route_format, handle->devices);
    }
    if(out->sw_open_trans_route) {
        if((handle = get_alsa_handle())== NULL)
            goto error;
        handle->route_format = ROUTE_SW_TRANSCODED_COMPRESSED;
        handle->devices = out->sw_trans_format_devices;
        handle->usecase = platform_get_usecase(USECASE_AUDIO_PLAYBACK_OFFLOAD);
        handle->out = out;
        handle->cmd_pending = false;
        ALOGD("OpenTranscodeRoute: routeformat: %d, devices: 0x%x: "
               ,handle->route_format, handle->devices);
    }
    if(out->hw_open_trans_route) {
        if((handle = get_alsa_handle())== NULL)
            goto error;
        handle->route_format = ROUTE_DSP_TRANSCODED_COMPRESSED;
        handle->devices = out->hw_trans_format_devices;
        handle->usecase = platform_get_usecase(USECASE_AUDIO_PLAYBACK_OFFLOAD);
        handle->out = out;
        handle->cmd_pending = false;
        ALOGD("OpenTranscodeRoute: routeformat: %d, devices: 0x%x: "
               ,handle->route_format, handle->devices);
    }

return 0;

error:
    list_for_each(node, &out->session_list) {
        handle = node_to_item(node, struct alsa_handle, list);
        free_alsa_handle(handle);
    }

    return -ENOMEM;
}

/*******************************************************************************
Description: setup input path
*******************************************************************************/
int allocate_internal_buffers(struct stream_out *out)
{
    ALOGV("%s",__func__);
    int ret = 0;
    int main_format = out->format & AUDIO_FORMAT_MAIN_MASK;

    /*
    setup the bitstream state machine
    */
    out->bitstrm = ( struct audio_bitstream_sm *)calloc(1,
            sizeof(struct audio_bitstream_sm));
    if(!audio_bitstream_init(out->bitstrm, get_buffering_factor(out))) {
        ALOGE("%s Unable to allocate bitstream buffering for MS11",__func__);
        free(out->bitstrm);
        out->bitstrm  = NULL;
        return -EINVAL;
    }

    if(is_input_buffering_mode_reqd(out))
        audio_bitstream_start_input_buffering_mode(out->bitstrm);

    /*
       setup the buffering data required for decode to start
       AAC_ADIF would require worst case frame size before decode starts
       other decoder formats handles the partial data, hence threshold is zero.
     */

    if(main_format == AUDIO_FORMAT_AAC_ADIF)
        out->min_bytes_req_to_dec = AAC_BLOCK_PER_CHANNEL_MS11*out->channels-1;
    else
        out->min_bytes_req_to_dec = 0;

    ret = open_temp_buf_for_metadata(out);
    if(ret < 0) {
        free(out->bitstrm);
        out->bitstrm  = NULL;
    }
    out->buffer_size = get_buffer_length(out);

    return ret;
}

/*******************************************************************************
Description: setup input path
*******************************************************************************/
int free_internal_buffers(struct stream_out *out)
{
    if(out->bitstrm) {
        free(out->bitstrm);
        out->bitstrm  = NULL;
    }

    if(out->write_temp_buf) {
        free(out->write_temp_buf);
        out->write_temp_buf = NULL;
    }

    if(out->dec_conf_buf) {
        free(out->dec_conf_buf);
        out->dec_conf_buf = NULL;
    }
    return 0;
}

/*******************************************************************************
Description: open MS11 instance
*******************************************************************************/
//TODO: enable sw_decode if required
#if USE_SWDECODE
static int open_ms11_instance(struct stream_out *out)
{
    ALOGV("openMS11Instance");
    int32_t formatMS11;
    int main_format = out->format & AUDIO_FORMAT_MAIN_MASK;
    out->ms11_decoder = get_soft_ms11();
    if(!out->ms11_decoder) {
        ALOGE("Could not resolve all symbols Required for MS11");
        return -EINVAL;
    }
    /*
    MS11 created
    */
    if(initialize_ms11_function_pointers(out->ms11_decoder) == false) {
        ALOGE("Could not resolve all symbols Required for MS11");
        free_soft_ms11(out->ms11_decoder);
        return -EINVAL;
    }
    /*
    update format
    */
    if((main_format == AUDIO_FORMAT_AC3) ||
       (main_format == AUDIO_FORMAT_EAC3)) {
        /*TODO: who wil setCOMPRESSED_CONVERT_AC3_ASSOC */
        if (out->spdif_format == COMPRESSED_CONVERT_AC3_ASSOC)
            formatMS11 = FORMAT_DOLBY_DIGITAL_PLUS_MAIN_ASSOC;
        else
            formatMS11 = FORMAT_DOLBY_DIGITAL_PLUS_MAIN;
    } else
        formatMS11 = FORMAT_DOLBY_PULSE_MAIN;
    /*
    set the use case to the MS11 decoder and open the stream for decoding
    */
    if(ms11_set_usecase_and_open_stream_with_mode(out->ms11_decoder,
                                    formatMS11, out->channels, out->sample_rate,
                                    out->is_m11_file_mode)) {
        ALOGE("SetUseCaseAndOpen MS11 failed");
        free_soft_ms11(out->ms11_decoder);
        return EINVAL;
    }
    if(is_decoder_config_required(out) && out->dec_conf_buf && out->dec_conf_bufLength) {
        if(ms11_set_aac_config(out->ms11_decoder, (unsigned char *)out->dec_conf_buf,
                                out->dec_conf_bufLength) == true) {
            out->dec_conf_set = true;
        }
    }

    return 0;
}
#endif
/*******************************************************************************
Description: copy input to internal buffer
*******************************************************************************/
void copy_bitstream_internal_buffer(struct audio_bitstream_sm *bitstrm,
                    char *buffer, size_t bytes)
{
    // copy bitstream to internal buffer
    audio_bitstream_copy_to_internal_buffer(bitstrm, (char *)buffer, bytes);
#ifdef DEBUG
    dumpInputOutput(INPUT, buffer, bytes, 0);
#endif
}

/*******************************************************************************
Description: set decoder config
*******************************************************************************/
//TODO: enable sw_decode if required
#if USE_SWDECODE
int setDecodeConfig(struct stream_out *out, char *buffer, size_t bytes)
{
    ALOGV("%s ", __func__);

    int main_format = out->format & AUDIO_FORMAT_MAIN_MASK;
    if(!out->dec_conf_set) {
        if(main_format == AUDIO_FORMAT_AAC ||
                  main_format == AUDIO_FORMAT_HE_AAC_V1 ||
                  main_format == AUDIO_FORMAT_AAC_ADIF ||
                  main_format == AUDIO_FORMAT_HE_AAC_V2) {
            if(out->ms11_decoder != NULL) {
                if(ms11_set_aac_config(out->ms11_decoder,(unsigned char *)buffer,
                            bytes) == false) {
                    ALOGE("AAC decoder config fail");
                    return 0;
                }
            }
        }

        out->dec_conf_bufLength = bytes;
        if(out->dec_conf_buf)
            free(out->dec_conf_buf);

        out->dec_conf_buf = malloc(out->dec_conf_bufLength);
        memcpy(out->dec_conf_buf,
                buffer,
                out->dec_conf_bufLength);
        out->dec_conf_set = true;
    }
    out->dec_conf_set = true;
    return bytes;
}
#endif

//TODO: enable sw_decode if required
#if USE_SWDECODE
int validate_sw_free_space(struct stream_out* out, int bytes_consumed_in_decode, int *pcm_2ch_len,
        int *pcm_mch_len, int *passthru_len, int *transcode_len, bool *wait_for_write_done) {

    struct alsa_handle *handle = NULL;
    char    *bufPtr;
    int copy_output_buffer_size;

    *pcm_2ch_len = *pcm_mch_len = *passthru_len = *transcode_len = *wait_for_write_done = 0;

    if(out->decoder_type & SW_DECODE) {
        bufPtr = audio_bitstream_get_output_buffer_write_ptr(out->bitstrm,
                                                                PCM_2CH_OUT);
        /*TODO: there is chance of illegale access if ms11 output exceeds bitstream
            output buffer boudary */
        copy_output_buffer_size = ms11_copy_output_from_ms11buf(out->ms11_decoder,
                                                            PCM_2CH_OUT,
                                                            bufPtr);
        handle = get_handle_by_route_format(out, ROUTE_UNCOMPRESSED);
        if(handle == NULL) {
           ALOGE("%s Invalid handle", __func__);
           return -EINVAL;
        }
        if(get_compress_available_space(handle) < copy_output_buffer_size) {
            handle->cmd_pending = true;
            *wait_for_write_done = true;
        }
        *pcm_2ch_len = copy_output_buffer_size;

    }
    if(out->decoder_type & SW_DECODE_MCH) {
        bufPtr=audio_bitstream_get_output_buffer_write_ptr(out->bitstrm,
                                                PCM_MCH_OUT);
        copy_output_buffer_size = ms11_copy_output_from_ms11buf(out->ms11_decoder,
                                                PCM_MCH_OUT,
                                                bufPtr);
        handle = get_handle_by_route_format(out, ROUTE_UNCOMPRESSED_MCH);
        if(handle == NULL) {
           ALOGE("%s Invalid handle", __func__);
           return -EINVAL;
        }

        if(get_compress_available_space(handle) < copy_output_buffer_size) {
            handle->cmd_pending = true;
            *wait_for_write_done = true;
        }
        *pcm_mch_len = copy_output_buffer_size;
    }
    if(out->decoder_type & SW_PASSTHROUGH) {
        bufPtr = audio_bitstream_get_output_buffer_write_ptr(out->bitstrm, COMPRESSED_OUT);
        copy_output_buffer_size = bytes_consumed_in_decode;
        memcpy(bufPtr, audio_bitstream_get_input_buffer_ptr(out->bitstrm), copy_output_buffer_size);

        handle = get_handle_by_route_format(out, ROUTE_COMPRESSED);
        if(handle == NULL) {
           ALOGE("%s Invalid handle", __func__);
           return -EINVAL;
        }

        if(get_compress_available_space(handle) < copy_output_buffer_size) {
            handle->cmd_pending = true;
            *wait_for_write_done = true;
        }
        *passthru_len = copy_output_buffer_size;
    }
    if(out->decoder_type & SW_TRANSCODE) {
        bufPtr = audio_bitstream_get_output_buffer_write_ptr(out->bitstrm,
                                                         TRANSCODE_OUT);
        copy_output_buffer_size = ms11_copy_output_from_ms11buf(out->bitstrm,
                                                         COMPRESSED_OUT,
                                                         bufPtr);
        handle = get_handle_by_route_format(out, ROUTE_SW_TRANSCODED_COMPRESSED);
        if(handle == NULL) {
           ALOGE("%s Invalid handle", __func__);
           return -EINVAL;
        }
        if(get_compress_available_space(handle) < copy_output_buffer_size) {
            handle->cmd_pending = true;
            *wait_for_write_done = true;
        }
        *transcode_len = copy_output_buffer_size;
    }
    return 0;
}
#endif

int validate_hw_free_space(struct stream_out *out, int bytes_consumed_in_decode, int *pcm_2ch_len,
        int *pcm_mch_len, int *passthru_len, int *transcode_len, bool *wait_for_write_done) {

    struct alsa_handle *handle = NULL;
    char    *bufPtr;
    int copy_output_buffer_size;
    *pcm_2ch_len = *pcm_mch_len = *passthru_len = *transcode_len = *wait_for_write_done = 0;
    if(out->decoder_type & DSP_DECODE) {
        ALOGVV("DSP_DECODE");
        bufPtr = audio_bitstream_get_output_buffer_write_ptr(out->bitstrm,
                            PCM_MCH_OUT);
        copy_output_buffer_size = bytes_consumed_in_decode;
        memcpy(bufPtr, audio_bitstream_get_input_buffer_ptr(out->bitstrm),
                    copy_output_buffer_size);
        ALOGVV("%s  bytes_consumed %d out bufPtr %x, pcm_mch_out_buf_size%d",
                __func__,bytes_consumed_in_decode,bufPtr,
                out->bitstrm->pcm_mch_out_buf_size);
        handle = get_handle_by_route_format(out, ROUTE_UNCOMPRESSED);/*TODO: revisit */
        if(handle == NULL) {
            ALOGE("%s Invalid handle", __func__);
            return -EINVAL;
        }
        if(get_compress_available_space(handle) < copy_output_buffer_size) {
            handle->cmd_pending = true;
            *wait_for_write_done = true;
            /*reset input buffer pointer as flinger will resend the data back */
            audio_bitstream_set_input_buffer_write_ptr(out->bitstrm,
                        -copy_output_buffer_size);
            *pcm_mch_len = copy_output_buffer_size;
        }
        else
            *pcm_mch_len = copy_output_buffer_size;
    }
    if(out->decoder_type & DSP_PASSTHROUGH) {
        ALOGVV("DSP_PASSTHROUGH");
        bufPtr = audio_bitstream_get_output_buffer_write_ptr(out->bitstrm, COMPRESSED_OUT);
        copy_output_buffer_size = bytes_consumed_in_decode;
        memcpy(bufPtr, audio_bitstream_get_input_buffer_ptr(out->bitstrm), copy_output_buffer_size);
        handle = get_handle_by_route_format(out, ROUTE_COMPRESSED);
        if(handle == NULL) {
            ALOGE("%s Invalid handle", __func__);
            return -EINVAL;
        }
        if(get_compress_available_space(handle) < copy_output_buffer_size) {
            handle->cmd_pending = true;
             *wait_for_write_done = true;
            *passthru_len = copy_output_buffer_size;
            /*reset input buffer pointer as flinger will resend the data back */
            audio_bitstream_set_input_buffer_ptr(out->bitstrm, -copy_output_buffer_size);
        }
        else
            *passthru_len = copy_output_buffer_size;
    }
    /*TODO: handle DSP Transcode usecase */
    return 0;
}

int update_bitstrm_pointers(struct stream_out *out, int pcm_2ch_len,
                                int pcm_mch_len, int passthru_len, int transcode_len) {

    if(out->decoder_type & SW_DECODE) {
            audio_bitstream_set_output_buffer_write_ptr(out->bitstrm, PCM_2CH_OUT,
                        pcm_2ch_len);

    }
    if(out->decoder_type & SW_DECODE_MCH || out->decoder_type & DSP_DECODE) {
            audio_bitstream_set_output_buffer_write_ptr(out->bitstrm, PCM_MCH_OUT, pcm_mch_len);
    }
    if(out->decoder_type & SW_PASSTHROUGH || out->decoder_type & DSP_PASSTHROUGH) {
            audio_bitstream_set_output_buffer_write_ptr(out->bitstrm, COMPRESSED_OUT, passthru_len);
    }
    if(out->decoder_type & SW_TRANSCODE) {
            audio_bitstream_set_output_buffer_write_ptr(out->bitstrm,
                                TRANSCODE_OUT,
                                 transcode_len);
    }
    return 0;
}

/*TODO correct it */
static int configure_compr(struct stream_out *out,
        struct alsa_handle *handle) {
    handle->compr_config.codec = (struct snd_codec *)
        calloc(1, sizeof(struct snd_codec));
    handle->compr_config.codec->id =
        get_snd_codec_id(out->format); /*TODO: correct this based on format*/
    handle->compr_config.fragment_size = COMPRESS_OFFLOAD_FRAGMENT_SIZE;
    handle->compr_config.fragments = COMPRESS_OFFLOAD_NUM_FRAGMENTS;
    handle->compr_config.codec->sample_rate =
        compress_get_alsa_rate(out->sample_rate);
    handle->compr_config.codec->bit_rate = out->compr_config.codec->bit_rate;
    handle->compr_config.codec->ch_in =
        popcount(out->channel_mask);
    handle->compr_config.codec->ch_out = handle->compr_config.codec->ch_in;
    handle->compr_config.codec->format = out->compr_config.codec->format;
    memcpy(&handle->compr_config.codec->options,
                    &out->compr_config.codec->options,
                     sizeof(union snd_codec_options));
    return 0;
}

/*TODO: do we need to apply volume at the session open*/
static int set_compress_volume(struct alsa_handle *handle, int left, int right)
{

    struct audio_device *adev = handle->out->dev;
    struct mixer_ctl *ctl;
    int volume[2];

    char mixer_ctl_name[44]; // max length of name is 44 as defined
    char device_id[STRING_LENGTH_OF_INTEGER+1];

    memset(mixer_ctl_name, 0, sizeof(mixer_ctl_name));
    strlcpy(mixer_ctl_name, "Compress Playback Volume", sizeof(mixer_ctl_name));

    memset(device_id, 0, sizeof(device_id));
    snprintf(device_id, "%d", handle->device_id, sizeof(device_id));

    strlcat(mixer_ctl_name, device_id, sizeof(mixer_ctl_name));

    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
                __func__, mixer_ctl_name);
        return -EINVAL;
    }
    volume[0] = (int)(left * COMPRESS_PLAYBACK_VOLUME_MAX);
    volume[1] = (int)(right * COMPRESS_PLAYBACK_VOLUME_MAX);
    mixer_ctl_set_array(ctl, volume, sizeof(volume)/sizeof(volume[0]));

    return 0;

}

/*******************************************************************************
Description: software decode handling
*******************************************************************************/
//TODO: enable sw_decode if required
#if USE_SWDECODE
static int sw_decode(struct stream_out *out,
                                char *buffer,
                                size_t bytes,
                                size_t *bytes_consumed,
                                bool *continueDecode)
{
    /* bytes pending to be decoded in current buffer*/
    bool wait_for_write_done = false;
    int bytes_pending_for_decode = 0;
    /* bytes consumed in current write  buffer */
    int total_bytes_consumed = 0;
    size_t  copyBytesMS11 = 0;
    size_t  bytes_consumed_in_decode = 0;
    size_t  copy_output_buffer_size = 0;
    uint32_t outSampleRate = out->sample_rate;
    uint32_t outChannels = out->channels;
    char * bufPtr;
    int pcm_2ch_len, pcm_mch_len, passthru_len, transcode_len;
    struct alsa_handle *handle = NULL;

    ALOGVV("sw Decode");
    // eos handling
    if(bytes == 0) {
        if(out->format == AUDIO_FORMAT_AAC_ADIF)
            audio_bitstream_append_silence_internal_buffer(out->bitstrm,
                                                  out->min_bytes_req_to_dec,0x0);
        else
            return false;
    }
    /*
    check for sync word, if present then configure MS11 for fileplayback mode
    OFF. This is specifically done to handle Widevine usecase, in which the
    ADTS HEADER is not stripped off by the Widevine parser
    */
    if(out->first_bitstrm_buf == true) {
        uint16_t uData = (*((char *)buffer) << 8) + *((char *)buffer + 1) ;
        if(ADTS_HEADER_SYNC_RESULT == (uData & ADTS_HEADER_SYNC_MASK)) {
            ALOGD("Sync word found hence configure MS11 in file_playback Mode OFF");
            free_soft_ms11(out->ms11_decoder);
            out->is_m11_file_mode = false;
            open_ms11_instance(out);
        }
        out->first_bitstrm_buf = false;
    }
    //decode
    if(out->decoder_type == SW_PASSTHROUGH) {
        /*TODO: check if correct */
        bytes_consumed_in_decode = audio_bitstream_get_size(out->bitstrm);
    } else {
        if(audio_bitstream_sufficient_buffer_to_decode(out->bitstrm,
                                out->min_bytes_req_to_dec) == true) {
            bufPtr = audio_bitstream_get_input_buffer_ptr(out->bitstrm);
            copyBytesMS11 = audio_bitstream_get_size(out->bitstrm);
            ms11_copy_bitstream_to_ms11_inpbuf(out->ms11_decoder, bufPtr,copyBytesMS11);
            bytes_consumed_in_decode = ms11_stream_decode(out->ms11_decoder,
                                            &outSampleRate, &outChannels);
        }
    }

    if((out->sample_rate != outSampleRate) || (out->channels != outChannels)) {
        ALOGD("Change in sample rate. New sample rate: %d", outSampleRate);
        out->sample_rate = outSampleRate;
        out->channels = outChannels;
        handle = get_handle_by_route_format(out, ROUTE_UNCOMPRESSED);
        if(handle !=NULL) {
            configure_compr(out, handle);
            handle->compr = compress_open(SOUND_CARD, handle->device_id,
                    COMPRESS_IN, &handle->compr_config);
            if (handle->compr && !is_compress_ready(handle->compr)) {
                ALOGE("%s: %s", __func__, compress_get_error(handle->compr));
                compress_close(handle->compr);
                handle->compr = NULL;
            }
            if (out->offload_callback)
                compress_nonblock(handle->compr, out->non_blocking);

            set_compress_volume(handle, out->left_volume, out->right_volume);
        }

        handle = get_handle_by_route_format(out, ROUTE_UNCOMPRESSED_MCH);
        if(handle !=NULL) {
            configure_compr(out, handle);
            handle->compr = compress_open(SOUND_CARD, handle->device_id,
                    COMPRESS_IN, &handle->compr_config);
            if (handle->compr && !is_compress_ready(handle->compr)) {
                ALOGE("%s: %s", __func__, compress_get_error(handle->compr));
                compress_close(handle->compr);
                handle->compr = NULL;
            }
            if (out->offload_callback)
                compress_nonblock(handle->compr, out->non_blocking);
            set_compress_volume(handle, out->left_volume, out->right_volume);
            out->channel_status_set = false;
        }
    }


    validate_sw_free_space(out, bytes_consumed_in_decode, &pcm_2ch_len, &pcm_mch_len,
            &passthru_len, &transcode_len, &wait_for_write_done);

    if(wait_for_write_done && out->non_blocking) {
        send_offload_cmd_l(out, OFFLOAD_CMD_WAIT_FOR_BUFFER);
        *continueDecode = false;
        *bytes_consumed = 0;
        return 0;
    } else {
        update_bitstrm_pointers(out, pcm_2ch_len, pcm_mch_len,
                passthru_len, transcode_len);
        audio_bitstream_copy_residue_to_start(out->bitstrm, bytes_consumed_in_decode);
        *bytes_consumed = bytes_consumed_in_decode;
    }

    copy_output_buffer_size = pcm_2ch_len + pcm_mch_len + passthru_len + transcode_len;
    if(copy_output_buffer_size &&
       audio_bitstream_sufficient_buffer_to_decode(out->bitstrm, out->min_bytes_req_to_dec) == true) {
        *continueDecode = true;
        return 0;
    }
    return 0;
}
#endif

/*******************************************************************************
Description: dsp decode handling
*******************************************************************************/
static bool dsp_decode(struct stream_out *out, char *buffer, size_t bytes,
                      size_t *bytes_consumed, bool *continueDecode)
{
    char    *bufPtr;
    size_t  bytes_consumed_in_decode = 0;

    bool wait_for_write_done = false;
    int pcm_2ch_len, pcm_mch_len, passthru_len, transcode_len;

    ALOGVV("dsp_decode");
    // decode
    {
        bytes_consumed_in_decode = audio_bitstream_get_size(out->bitstrm);
    }
    // handle change in sample rate
    {
    }
    //TODO: check if the copy of the buffers can be avoided
    /* can be removed as its not required for dsp decode usecase */
    *continueDecode = false;
    validate_hw_free_space(out, bytes_consumed_in_decode, &pcm_2ch_len, &pcm_mch_len,
            &passthru_len, &transcode_len, &wait_for_write_done);

    if(wait_for_write_done && out->non_blocking) {
        send_offload_cmd_l(out, OFFLOAD_CMD_WAIT_FOR_BUFFER);
        *bytes_consumed = 0;
        return 0;
    } else {
        update_bitstrm_pointers(out, pcm_2ch_len, pcm_mch_len,
                passthru_len, transcode_len);
        audio_bitstream_copy_residue_to_start(out->bitstrm, bytes_consumed_in_decode);
        *bytes_consumed = bytes_consumed_in_decode;
        ALOGV("%s bytes_consumed_in_decode =%d",__func__,bytes_consumed_in_decode);
    }

    return 0;
}

static bool decode(struct stream_out *out, char * buffer, size_t bytes,
                    size_t *bytes_consumed, bool *continuedecode)
{
    ALOGV("decode");
    bool    continueDecode = false;
    int ret  = 0;

    // TODO: enable software decode if required
    /*if (out->use_ms11_decoder) {
        ret = sw_decode(out, buffer, bytes,
                                bytes_consumed, continuedecode);

        // set channel status
        // Set the channel status after first frame decode/transcode
        //TODO: set the SPDIF channel status bits
      if(out->channel_status_set == false)
            setSpdifchannel_status(
                    audio_bitstream_get_output_buffer_ptr(out->bitstrm, COMPRESSED_OUT),
                    bytes, AUDIO_PARSER_CODEC_AC3);

    } else */{
        ret = dsp_decode(out, buffer, bytes,
                                bytes_consumed, continuedecode);
        // set channel status
        // Set the channel status after first frame decode/transcode
        //TODO: set the SPDIF channel status bits
/*        if(out->channel_status_set == false)
            setSpdifchannel_status(
                    audio_bitstream_get_output_buffer_ptr(out->bitstrm, COMPRESSED_OUT),
                    bytes, AUDIO_PARSER_CODEC_DTS);
*/
    }
    return ret;
}

/*******************************************************************************
Description: fixup sample rate and channel info based on format
*******************************************************************************/
void fixupSampleRateChannelModeMS11Formats(struct stream_out *out)
{
    ALOGV("fixupSampleRateChannelModeMS11Formats");
    int main_format = out->format & AUDIO_FORMAT_MAIN_MASK;
    int subFormat = out->format & AUDIO_FORMAT_SUB_MASK;
/*
NOTE: For AAC, the output of MS11 is 48000 for the sample rates greater than
      24000. The samples rates <= 24000 will be at their native sample rate
      For AC3, the PCM output is at its native sample rate if the decoding is
      single decode usecase for MS11.
*/
    if(main_format == AUDIO_FORMAT_AAC ||
       main_format == AUDIO_FORMAT_HE_AAC_V1 ||
       main_format == AUDIO_FORMAT_HE_AAC_V2 ||
       main_format == AUDIO_FORMAT_AAC_ADIF) {
        out->sample_rate = out->sample_rate > 24000 ? 48000 : out->sample_rate;
        out->channels       = 6;
    } else if (main_format == AUDIO_FORMAT_AC3 ||
               main_format == AUDIO_FORMAT_EAC3) {
        /* transcode AC3/EAC3 44.1K to 48K AC3 for non dual-mono clips */
        if (out->sample_rate == 44100 &&
            (subFormat != AUDIO_FORMAT_DOLBY_SUB_DM) &&
            (out->spdif_format == COMPRESSED ||
             out->spdif_format == AUTO_DEVICE_FORMAT ||
             out->spdif_format == COMPRESSED_CONVERT_EAC3_AC3) &&
             (out->hdmi_format == UNCOMPRESSED ||
              out->hdmi_format == UNCOMPRESSED_MCH)) {
            out->sample_rate = 48000;
            out->spdif_format = COMPRESSED_CONVERT_AC3_ASSOC;
        } else if (out->sample_rate == 44100) {
            out->spdif_format = UNCOMPRESSED;
        }
        out->channels   = 6;
    }
    ALOGD("ms11 format fixup: out->spdif_format %d, out->hdmi_format %d",
                                     out->spdif_format, out->hdmi_format);
}

static bool is_supported_format(audio_format_t format)
{
    switch (format) {
    case AUDIO_FORMAT_PCM_16_BIT:
    case AUDIO_FORMAT_MP3:
    case AUDIO_FORMAT_AAC:
    case AUDIO_FORMAT_WMA:
    case AUDIO_FORMAT_WMA_PRO:
    case AUDIO_FORMAT_MP2:
        return true;
    default:
        ALOGE("%s: Unsupported audio format: %x", __func__, format);
        break;
    }

    return false;
}

static int get_snd_codec_id(audio_format_t format)
{
    int id = 0;

    switch (format) {
    case AUDIO_FORMAT_PCM_16_BIT:
        id = SND_AUDIOCODEC_PCM;
        break;
    case AUDIO_FORMAT_MP3:
        id = SND_AUDIOCODEC_MP3;
        break;
    case AUDIO_FORMAT_AAC:
        id = SND_AUDIOCODEC_AAC;
        break;
    case AUDIO_FORMAT_WMA:
        id = SND_AUDIOCODEC_WMA;
        break;
    case AUDIO_FORMAT_WMA_PRO:
        id = SND_AUDIOCODEC_WMA_PRO;
        break;
    case AUDIO_FORMAT_MP2:
        id = SND_AUDIOCODEC_MP2;
        break;
    default:
        ALOGE("%s: Unsupported audio format %x", __func__, format);
    }

    return id;
}

/* must be called with hw device mutex locked */
static int read_hdmi_channel_masks(struct stream_out *out)
{
    int ret = 0;
    int channels = platform_edid_get_max_channels(out->dev->platform);

    switch (channels) {
        /*
         * Do not handle stereo output in Multi-channel cases
         * Stereo case is handled in normal playback path
         */
    case 6:
        ALOGV("%s: HDMI supports 5.1", __func__);
        out->supported_channel_masks[0] = AUDIO_CHANNEL_OUT_5POINT1;
        break;
    case 8:
        ALOGV("%s: HDMI supports 5.1 and 7.1 channels", __func__);
        out->supported_channel_masks[0] = AUDIO_CHANNEL_OUT_5POINT1;
        out->supported_channel_masks[1] = AUDIO_CHANNEL_OUT_7POINT1;
        break;
    default:
        ALOGE("HDMI does not support multi channel playback");
        ret = -ENOSYS;
        break;
    }
    return ret;
}

/* must be called with out->lock locked */
static int send_offload_cmd_l(struct stream_out* out, int command)
{
    struct offload_cmd *cmd = (struct offload_cmd *)calloc(1, sizeof(struct offload_cmd));

    ALOGVV("%s %d", __func__, command);

    cmd->cmd = command;
    list_add_tail(&out->offload_cmd_list, &cmd->node);
    pthread_cond_signal(&out->offload_cond);
    return 0;
}

/* must be called iwth out->lock locked */
static void stop_compressed_output_l(struct stream_out *out)
{
    struct listnode *node;
    struct alsa_handle *handle;
    bool is_compr_out = false;

    ALOGV("%s", __func__);
    out->offload_state = OFFLOAD_STATE_IDLE;
    out->playback_started = 0;
    out->send_new_metadata = 1;
    list_for_each(node, &out->session_list) {
        handle = node_to_item(node, struct alsa_handle, list);
        if (handle->compr != NULL) {
            compress_stop(handle->compr);
            is_compr_out = true;
        }
    }
    if (is_compr_out) {
        while (out->offload_thread_blocked)
            pthread_cond_wait(&out->cond, &out->lock);
    }
}

static void *offload_thread_loop(void *context)
{
    struct stream_out *out = (struct stream_out *) context;
    struct listnode *item;
    struct listnode *node;
    struct alsa_handle *handle;

    out->offload_state = OFFLOAD_STATE_IDLE;
    out->playback_started = 0;

    setpriority(PRIO_PROCESS, 0, ANDROID_PRIORITY_AUDIO);
    set_sched_policy(0, SP_FOREGROUND);
    prctl(PR_SET_NAME, (unsigned long)"Offload Callback", 0, 0, 0);

    ALOGV("%s", __func__);
    pthread_mutex_lock(&out->lock);
    for (;;) {
        struct offload_cmd *cmd = NULL;
        stream_callback_event_t event;
        bool send_callback = false;

        ALOGVV("%s offload_cmd_list %d out->offload_state %d",
              __func__, list_empty(&out->offload_cmd_list),
              out->offload_state);
        if (list_empty(&out->offload_cmd_list)) {
            ALOGV("%s SLEEPING", __func__);
            pthread_cond_wait(&out->offload_cond, &out->lock);
            ALOGV("%s RUNNING", __func__);
            continue;
        }

        item = list_head(&out->offload_cmd_list);
        cmd = node_to_item(item, struct offload_cmd, node);
        list_remove(item);

        ALOGVV("%s STATE %d CMD %d",
               __func__, out->offload_state, cmd->cmd);

        if (cmd->cmd == OFFLOAD_CMD_EXIT) {
            free(cmd);
            break;
        }

        if (list_empty(&out->session_list)) {
            ALOGE("%s: Compress handle is NULL", __func__);
            pthread_cond_signal(&out->cond);
            continue;
        }
        out->offload_thread_blocked = true;
        pthread_mutex_unlock(&out->lock);
        send_callback = false;
        switch(cmd->cmd) {
        case OFFLOAD_CMD_WAIT_FOR_BUFFER:
            list_for_each(node, &out->session_list) {
                handle = node_to_item(node, struct alsa_handle, list);
                if (handle->compr && handle->cmd_pending) {
                    compress_wait(handle->compr, -1);
                    handle->cmd_pending = false;
                }
            }
            send_callback = true;
            event = STREAM_CBK_EVENT_WRITE_READY;
            break;
        case OFFLOAD_CMD_PARTIAL_DRAIN:
            list_for_each(node, &out->session_list) {
                handle = node_to_item(node, struct alsa_handle, list);
                if (handle->compr) {
                    compress_next_track(handle->compr);
                    compress_partial_drain(handle->compr);
                }
            }
            send_callback = true;
            event = STREAM_CBK_EVENT_DRAIN_READY;
            break;
        case OFFLOAD_CMD_DRAIN:
            list_for_each(node, &out->session_list) {
                handle = node_to_item(node, struct alsa_handle, list);
                if (handle->compr) {
                    compress_drain(handle->compr);
                }
            }
            send_callback = true;
            event = STREAM_CBK_EVENT_DRAIN_READY;
            break;
        default:
            ALOGE("%s unknown command received: %d", __func__, cmd->cmd);
            break;
        }
        pthread_mutex_lock(&out->lock);
        out->offload_thread_blocked = false;
        pthread_cond_signal(&out->cond);
        if (send_callback) {
            out->offload_callback(event, NULL, out->offload_cookie);
        }
        free(cmd);
    }

    pthread_cond_signal(&out->cond);
    while (!list_empty(&out->offload_cmd_list)) {
        item = list_head(&out->offload_cmd_list);
        list_remove(item);
        free(node_to_item(item, struct offload_cmd, node));
    }
    pthread_mutex_unlock(&out->lock);

    return NULL;
}

static int create_offload_callback_thread(struct stream_out *out)
{
    pthread_cond_init(&out->offload_cond, (const pthread_condattr_t *) NULL);
    list_init(&out->offload_cmd_list);
    pthread_create(&out->offload_thread, (const pthread_attr_t *) NULL,
                    offload_thread_loop, out);
    return 0;
}

static int destroy_offload_callback_thread(struct stream_out *out)
{
    pthread_mutex_lock(&out->lock);
    stop_compressed_output_l(out);
    send_offload_cmd_l(out, OFFLOAD_CMD_EXIT);

    pthread_mutex_unlock(&out->lock);
    pthread_join(out->offload_thread, (void **) NULL);
    pthread_cond_destroy(&out->offload_cond);

    return 0;
}

static bool allow_hdmi_channel_config(struct audio_device *adev)
{
    struct listnode *node;
    struct audio_usecase *usecase;
    bool ret = true;

    list_for_each(node, &adev->usecase_list) {
        usecase = node_to_item(node, struct audio_usecase, list);
        if (usecase->devices & AUDIO_DEVICE_OUT_AUX_DIGITAL) {
            /*
             * If voice call is already existing, do not proceed further to avoid
             * disabling/enabling both RX and TX devices, CSD calls, etc.
             * Once the voice call done, the HDMI channels can be configured to
             * max channels of remaining use cases.
             */
            if (usecase->id == USECASE_VOICE_CALL) {
                ALOGD("%s: voice call is active, no change in HDMI channels",
                      __func__);
                ret = false;
                break;
            } else if (usecase->id == USECASE_AUDIO_PLAYBACK_MULTI_CH) {
                ALOGD("%s: multi channel playback is active, "
                      "no change in HDMI channels", __func__);
                ret = false;
                break;
            }
        }
    }
    return ret;
}

static int check_and_set_hdmi_channels(struct audio_device *adev,
                                       unsigned int channels)
{
    struct listnode *node;
    struct audio_usecase *usecase;

    /* Check if change in HDMI channel config is allowed */
    if (!allow_hdmi_channel_config(adev))
        return 0;

    if (channels == adev->cur_hdmi_channels) {
        ALOGD("%s: Requested channels are same as current", __func__);
        return 0;
    }

    platform_set_hdmi_channels(adev->platform, channels);
    adev->cur_hdmi_channels = channels;

    /*
     * Deroute all the playback streams routed to HDMI so that
     * the back end is deactivated. Note that backend will not
     * be deactivated if any one stream is connected to it.
     */
    list_for_each(node, &adev->usecase_list) {
        usecase = node_to_item(node, struct audio_usecase, list);
        if (usecase->type == PCM_PLAYBACK &&
                usecase->devices & AUDIO_DEVICE_OUT_AUX_DIGITAL) {
            disable_audio_route(adev, usecase, true);
        }
    }

    /*
     * Enable all the streams disabled above. Now the HDMI backend
     * will be activated with new channel configuration
     */
    list_for_each(node, &adev->usecase_list) {
        usecase = node_to_item(node, struct audio_usecase, list);
        if (usecase->type == PCM_PLAYBACK &&
                usecase->devices & AUDIO_DEVICE_OUT_AUX_DIGITAL) {
            enable_audio_route(adev, usecase, true);
        }
    }

    return 0;
}

static int stop_output_stream(struct stream_out *out, struct alsa_handle *handle)
{
    int i, ret = 0;
    struct audio_usecase *uc_info;
    struct audio_device *adev = out->dev;

    ALOGV("%s: enter: usecase(%d: %s)", __func__,
          handle->usecase, use_case_table[handle->usecase]);
    uc_info = get_usecase_from_list(adev, handle->usecase);
    if (uc_info == NULL) {
        ALOGE("%s: Could not find the usecase (%d) in the list",
              __func__, handle->usecase);
        return -EINVAL;
    }

    if (out->usecase == USECASE_AUDIO_PLAYBACK_OFFLOAD &&
            adev->visualizer_stop_output != NULL)
        adev->visualizer_stop_output(out->handle);

    /* 1. Get and set stream specific mixer controls */
    disable_audio_route(adev, uc_info, true);

    /* 2. Disable the rx device */
    disable_snd_device(adev, uc_info->out_snd_device, true);

    list_remove(&uc_info->list);
    free(uc_info);

    /* Must be called after removing the usecase from list */
    if (out->devices & AUDIO_DEVICE_OUT_AUX_DIGITAL)
        check_and_set_hdmi_channels(adev, DEFAULT_HDMI_OUT_CHANNELS);

    ALOGV("%s: exit: status(%d)", __func__, ret);
    return ret;
}

int start_output_stream(struct stream_out *out, struct alsa_handle *handle)
{
    int ret = 0;
    struct audio_usecase *uc_info;
    struct audio_device *adev = out->dev;

    ALOGV("%s: enter: usecase(%d: %s) devices(%#x)",
          __func__, handle->usecase, use_case_table[handle->usecase], handle->devices);
    handle->device_id = platform_get_pcm_device_id(handle->usecase, PCM_PLAYBACK);
    if (handle->device_id < 0) {
        ALOGE("%s: Invalid PCM device id(%d) for the usecase(%d)",
              __func__, handle->device_id, handle->usecase);
        ret = -EINVAL;
        goto error_config;
    }

    uc_info = (struct audio_usecase *)calloc(1, sizeof(struct audio_usecase));
    uc_info->id = handle->usecase;
    uc_info->handle = handle;
    uc_info->type = PCM_PLAYBACK;
    uc_info->stream.out = out;
    uc_info->devices = handle->devices;
    uc_info->in_snd_device = SND_DEVICE_NONE;
    uc_info->out_snd_device = SND_DEVICE_NONE;

    /* This must be called before adding this usecase to the list */
    //if (out->devices & AUDIO_DEVICE_OUT_AUX_DIGITAL)
    //    check_and_set_hdmi_channels(adev, out->config.channels);

    list_add_tail(&adev->usecase_list, &uc_info->list);

    select_devices(adev, handle->usecase);

    ALOGV("%s: Opening PCM device card_id(%d) device_id(%d)",
          __func__, 0, handle->device_id);
    if (out->uc_strm_type != OFFLOAD_PLAYBACK_STREAM) {
        handle->compr = NULL;
        handle->pcm = pcm_open(SOUND_CARD, handle->device_id,
                               PCM_OUT | PCM_MONOTONIC, &handle->config);
        if (handle->pcm && !pcm_is_ready(handle->pcm)) {
            ALOGE("%s: %s", __func__, pcm_get_error(handle->pcm));
            pcm_close(handle->pcm);
            handle->pcm = NULL;
            ret = -EIO;
            goto error_open;
        }
    } else {
        handle->pcm = NULL;
        configure_compr(out, handle);
        handle->compr = compress_open(SOUND_CARD, handle->device_id,
                                   COMPRESS_IN, &handle->compr_config);
        if (handle->compr && !is_compress_ready(handle->compr)) {
            ALOGE("%s: %s", __func__, compress_get_error(handle->compr));
            compress_close(handle->compr);
            handle->compr = NULL;
            ret = -EIO;
            goto error_open;
        }
        if (out->offload_callback)
            compress_nonblock(handle->compr, out->non_blocking);

        if (adev->visualizer_start_output != NULL)
            adev->visualizer_start_output(out->handle);
    }
    ALOGV("%s: exit", __func__);
    return 0;
error_open:
    stop_output_stream(out, handle);
error_config:
    return ret;
}

static uint32_t out_get_sample_rate(const struct audio_stream *stream)
{
    struct stream_out *out = (struct stream_out *)stream;

    return out->sample_rate;
}

static int out_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    return -ENOSYS;
}

static size_t out_get_buffer_size(const struct audio_stream *stream)
{
    struct stream_out *out = (struct stream_out *)stream;

    /*if (out->usecase == USECASE_AUDIO_PLAYBACK_OFFLOAD)
        return out->compr_config.fragment_size;
    */
    return (size_t)out->buffer_size;

    //return out->config.period_size * audio_stream_frame_size(stream);
}

static uint32_t out_get_channels(const struct audio_stream *stream)
{
    struct stream_out *out = (struct stream_out *)stream;

    return out->channel_mask;
}

static audio_format_t out_get_format(const struct audio_stream *stream)
{
    struct stream_out *out = (struct stream_out *)stream;

    return out->format;
}

static int out_set_format(struct audio_stream *stream, audio_format_t format)
{
    return -ENOSYS;
}

static int out_standby(struct audio_stream *stream)
{
    struct stream_out *out = (struct stream_out *)stream;
    struct audio_device *adev = out->dev;
    struct listnode *node;
    struct alsa_handle *handle;

    ALOGV("%s: enter: usecase(%d: %s)", __func__,
          out->usecase, use_case_table[out->usecase]);
    if (out->usecase == USECASE_COMPRESS_VOIP_CALL) {
        /* Ignore standby in case of voip call because the voip output
         * stream is closed in adev_close_output_stream()
         */
        ALOGV("%s: Ignore Standby in VOIP call", __func__);
        return 0;
    }

    pthread_mutex_lock(&out->lock);
    pthread_mutex_lock(&adev->lock);
    if (!out->standby) {
        out->standby = true;
        stop_compressed_output_l(out);
        out->gapless_mdata.encoder_delay = 0;
        out->gapless_mdata.encoder_padding = 0;

        list_for_each(node, &out->session_list) {
            handle = node_to_item(node, struct alsa_handle, list);
            if (handle->compr != NULL) {
                compress_close(handle->compr);
                handle->compr = NULL;
            } else if (handle->pcm) {
                pcm_close(handle->pcm);
                handle->pcm = NULL;
            }
            stop_output_stream(out, handle);
        }
    }
    pthread_mutex_unlock(&adev->lock);
    pthread_mutex_unlock(&out->lock);
    ALOGV("%s: exit", __func__);
    return 0;
}

static int out_dump(const struct audio_stream *stream, int fd)
{
    return 0;
}

static int parse_compress_metadata(struct stream_out *out, struct str_parms *parms)
{
    int ret = 0;
    char value[32];
    struct compr_gapless_mdata tmp_mdata;
    bool gapless_meta_set = true;

    if (!out || !parms) {
        return -EINVAL;
    }

    ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_DELAY_SAMPLES, value, sizeof(value));
    if (ret >= 0) {
        tmp_mdata.encoder_delay = atoi(value); //whats a good limit check?
    } else {
        gapless_meta_set = false;
    }

    ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_PADDING_SAMPLES, value, sizeof(value));
    if (ret >= 0) {
        tmp_mdata.encoder_padding = atoi(value);
    } else {
        gapless_meta_set = false;
    }

    if (gapless_meta_set) {
        out->gapless_mdata = tmp_mdata;
        out->send_new_metadata = 1;
        ALOGV("%s new encoder delay %u and padding %u", __func__,
            out->gapless_mdata.encoder_delay, out->gapless_mdata.encoder_padding);
    }

    if(out->format == AUDIO_FORMAT_WMA || out->format == AUDIO_FORMAT_WMA_PRO) {
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_WMA_FORMAT_TAG, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->format = atoi(value);
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_WMA_BLOCK_ALIGN, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.wma.super_block_align = atoi(value);
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_WMA_BIT_PER_SAMPLE, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.wma.bits_per_sample = atoi(value);
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_WMA_CHANNEL_MASK, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.wma.channelmask = atoi(value);
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_WMA_ENCODE_OPTION, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.wma.encodeopt = atoi(value);
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_WMA_ENCODE_OPTION1, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.wma.encodeopt1 = atoi(value);
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_WMA_ENCODE_OPTION2, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.wma.encodeopt2 = atoi(value);
        }
        ALOGV("WMA params: fmt %x, balgn %x, sr %d, chmsk %x, encop %x, op1 %x, op2 %x",
                                out->compr_config.codec->format,
                                out->compr_config.codec->options.wma.super_block_align,
                                out->compr_config.codec->options.wma.bits_per_sample,
                                out->compr_config.codec->options.wma.channelmask,
                                out->compr_config.codec->options.wma.encodeopt,
                                out->compr_config.codec->options.wma.encodeopt1,
                                out->compr_config.codec->options.wma.encodeopt2);
    }
    return 0;
}

static int out_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    struct stream_out *out = (struct stream_out *)stream;
    struct audio_device *adev = out->dev;
    struct audio_usecase *usecase;
    struct listnode *node;
    struct str_parms *parms;
    char value[32];
    int ret, val = 0;
    bool select_new_device = false;

    ALOGD("%s: enter: kvpairs: %s", __func__, kvpairs);
    parms = str_parms_create_str(kvpairs);
    ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING, value, sizeof(value));
    if (ret >= 0) {
        val = atoi(value);
        pthread_mutex_lock(&out->lock);
        pthread_mutex_lock(&adev->lock);

        /*
         * When HDMI cable is unplugged the music playback is paused and
         * the policy manager sends routing=0. But the audioflinger
         * continues to write data until standby time (3sec).
         * As the HDMI core is turned off, the write gets blocked.
         * Avoid this by routing audio to speaker until standby.
         */
        if (out->devices == AUDIO_DEVICE_OUT_AUX_DIGITAL &&
                val == AUDIO_DEVICE_NONE) {
            val = AUDIO_DEVICE_OUT_SPEAKER;
        }

        /*
         * select_devices() call below switches all the usecases on the same
         * backend to the new device. Refer to check_usecases_codec_backend() in
         * the select_devices(). But how do we undo this?
         *
         * For example, music playback is active on headset (deep-buffer usecase)
         * and if we go to ringtones and select a ringtone, low-latency usecase
         * will be started on headset+speaker. As we can't enable headset+speaker
         * and headset devices at the same time, select_devices() switches the music
         * playback to headset+speaker while starting low-lateny usecase for ringtone.
         * So when the ringtone playback is completed, how do we undo the same?
         *
         * We are relying on the out_set_parameters() call on deep-buffer output,
         * once the ringtone playback is ended.
         * NOTE: We should not check if the current devices are same as new devices.
         *       Because select_devices() must be called to switch back the music
         *       playback to headset.
         */
        if (val != 0) {
            out->devices = val;

            if (!out->standby)
                select_devices(adev, out->usecase);
        }
//TODO:
//Get the device and device format mapping from the RoutingManager.
//Decide which streams need to be derouted and which need to opened/closed
//Update the respective device in each of the handles
#if 0
        if (out->uc_strm_type == OFFLOAD_PLAYBACK_STREAM) {

            /* TODO get format form routing manager */
            update_decode_type_and_routing_states(out);

            if(is_input_buffering_mode_reqd(out))
                audio_bitstream_start_input_buffering_mode(out->bitstrm);
            else
                audio_bitstream_stop_input_buffering_mode(out->bitstrm);
            /*
               For the runtime format change, close the device first to avoid any
               concurrent PCM + Compressed sessions on the same device.
             */
             close_handles_for_device_switch(out);
            if(!out->mopen_dec_route)
                handleCloseForDeviceSwitch(ROUTE_UNCOMPRESSED);

            if(!out->mopen_dec_mch_route)
                handleCloseForDeviceSwitch(ROUTE_UNCOMPRESSED_MCH);

            if(!out->mopen_passt_route)
                handleCloseForDeviceSwitch(ROUTE_COMPRESSED);

            if(!msw_open_trans_route)
                handleCloseForDeviceSwitch(ROUTE_SW_TRANSCODED_COMPRESSED);

            if(!mhw_open_trans_route)
                handleCloseForDeviceSwitch(ROUTE_DSP_TRANSCODED_COMPRESSED);

            if(out->mopen_dec_route)
                handleSwitchAndOpenForDeviceSwitch(mdec_format_devices,
                        ROUTE_UNCOMPRESSED);
            if(out->mopen_dec_mch_route)
                handleSwitchAndOpenForDeviceSwitch(mdec_mch_format_devices,
                        ROUTE_UNCOMPRESSED_MCH);
            if(out->mopen_passt_route)
                handleSwitchAndOpenForDeviceSwitch(mpasst_format_devices,
                        ROUTE_COMPRESSED);
            if(out->msw_open_trans_route)
                handleSwitchAndOpenForDeviceSwitch(msw_trans_format_devices,
                        ROUTE_SW_TRANSCODED_COMPRESSED);
            if(out->mhw_open_trans_route)
                handleSwitchAndOpenForDeviceSwitch(mhw_trans_format_devices,
                        ROUTE_DSP_TRANSCODED_COMPRESSED);
        }
#endif

        pthread_mutex_unlock(&adev->lock);
        pthread_mutex_unlock(&out->lock);
    }

    if (out->uc_strm_type == OFFLOAD_PLAYBACK_STREAM) {
        ret = parse_compress_metadata(out, parms);
    }

    str_parms_destroy(parms);
    ALOGV("%s: exit: code(%d)", __func__, ret);
    return ret;
}

static char* out_get_parameters(const struct audio_stream *stream, const char *keys)
{
    struct stream_out *out = (struct stream_out *)stream;
    struct str_parms *query = str_parms_create_str(keys);
    char *str;
    char value[256];
    struct str_parms *reply = str_parms_create();
    size_t i, j;
    int ret;
    bool first = true;
    ALOGV("%s: enter: keys - %s", __func__, keys);
    ret = str_parms_get_str(query, AUDIO_PARAMETER_STREAM_SUP_CHANNELS, value, sizeof(value));
    if (ret >= 0) {
        value[0] = '\0';
        i = 0;
        while (out->supported_channel_masks[i] != 0) {
            for (j = 0; j < ARRAY_SIZE(out_channels_name_to_enum_table); j++) {
                if (out_channels_name_to_enum_table[j].value == out->supported_channel_masks[i]) {
                    if (!first) {
                        strlcat(value, "|", sizeof(value));
                    }
                    strlcat(value, out_channels_name_to_enum_table[j].name, sizeof(value));
                    first = false;
                    break;
                }
            }
            i++;
        }
        str_parms_add_str(reply, AUDIO_PARAMETER_STREAM_SUP_CHANNELS, value);
        str = str_parms_to_str(reply);
    }
    str_parms_destroy(query);
    str_parms_destroy(reply);
    ALOGV("%s: exit: returns - %s", __func__, str);
    return str;
}

static uint32_t out_get_latency(const struct audio_stream_out *stream)
{
    struct stream_out *out = (struct stream_out *)stream;
    struct listnode *item;
    struct alsa_handle *handle;

    //TODO: decide based on the clip properties
    if (out->uc_strm_type == OFFLOAD_PLAYBACK_STREAM)
        return COMPRESS_OFFLOAD_PLAYBACK_LATENCY;

    item = list_head(&out->session_list);
    handle = node_to_item(item, struct alsa_handle, list);
    if(!handle) {
        ALOGE("%s: error pcm handle NULL", __func__);
        return -EINVAL;
    }

    return (handle->config.period_count * handle->config.period_size * 1000) /
        (handle->config.rate);
}

static int out_set_volume(struct audio_stream_out *stream, float left,
                          float right)
{
    struct stream_out *out = (struct stream_out *)stream;
    struct listnode *node;
    struct alsa_handle *handle;
    struct audio_device *adev = out->dev;
    int ret = -ENOSYS;
    ALOGV("%s", __func__);
    pthread_mutex_lock(&out->lock);
    list_for_each(node, &out->session_list) {
        handle = node_to_item(node, struct alsa_handle, list);
        if (handle->pcm && (out->usecase == USECASE_AUDIO_PLAYBACK_MULTI_CH)){
            /* only take left channel into account: the API is for stereo anyway */
            out->muted = (left == 0.0f);
            ret = 0;
        } else if (handle->compr) {

            out->left_volume = left;
            out->right_volume = right;

            //ret = set_compress_volume(handle, left, right);
        }
    }
    pthread_mutex_unlock(&out->lock);

    return ret;
}

static int write_data(struct stream_out *out, struct alsa_handle *handle,
                   const void *buffer, int bytes) {

    int ret = 0;
    if (out->uc_strm_type == OFFLOAD_PLAYBACK_STREAM) {
        ALOGV("%s: writing buffer (%d bytes) to compress device", __func__, bytes);

        ret = compress_write(handle->compr, buffer, bytes);
        ALOGV("%s: writing buffer (%d bytes) to compress device returned %d",
                    __func__, bytes, ret);
        /* TODO:disnable this if ms12 */

        if (ret >= 0 && ret < (ssize_t)bytes) {
            send_offload_cmd_l(out, OFFLOAD_CMD_WAIT_FOR_BUFFER);
        }
        return ret;
    } else {
        if (handle->pcm) {
            if (out->muted)
                memset((void *)buffer, 0, bytes);
            ALOGV("%s: writing buffer (%d bytes) to pcm device", __func__, bytes);
            ret = pcm_write(handle->pcm, (void *)buffer, bytes);
        }
    }

    if (ret != 0) {
        if ((handle && handle->pcm))
            ALOGE("%s: error %d - %s", __func__, ret, pcm_get_error(handle->pcm));
        out_standby(&out->stream.common);
        usleep(bytes * 1000000 / audio_stream_frame_size(&out->stream.common) /
                out_get_sample_rate(&out->stream.common));
    }
    return bytes;
}

/*******************************************************************************
Description: render
*******************************************************************************/
size_t render_offload_data(struct stream_out *out, const void *buffer, size_t bytes)
{
    int ret =0;
    uint32_t renderedPcmBytes = 0;
    int      fragment_size;
    uint32_t availableSize;
    int bytes_to_write = bytes;
    int renderType;
    /*int metadataLength = sizeof(out->output_meta_data);*/
    struct listnode *node;
    struct alsa_handle *handle;

    ALOGV("%s", __func__);

    list_for_each(node, &out->session_list) {
        handle = node_to_item(node, struct alsa_handle, list);
        if (out->send_new_metadata) {
            ALOGVV("send new gapless metadata");
            compress_set_gapless_metadata(handle->compr, &out->gapless_mdata);
        }

        switch(handle->route_format) {
        case ROUTE_UNCOMPRESSED:
            ALOGVV("ROUTE_UNCOMPRESSED");
            renderType = PCM_2CH_OUT;
            break;
        case ROUTE_UNCOMPRESSED_MCH:
            ALOGVV("ROUTE_UNCOMPRESSED_MCH");
            renderType = PCM_MCH_OUT;
            break;
        case ROUTE_COMPRESSED:
            ALOGVV("ROUTE_COMPRESSED");
            renderType = COMPRESSED_OUT;
            break;
        case ROUTE_SW_TRANSCODED_COMPRESSED:
            ALOGVV("ROUTE_SW_TRANSCODED_COMPRESSED");
            renderType = TRANSCODE_OUT;
            break;
        case ROUTE_DSP_TRANSCODED_COMPRESSED:
            ALOGVV("ROUTE_DSP_TRANSCODED_COMPRESSED");
            continue;
        default:
            continue;
        };

        fragment_size = handle->compr_config.fragment_size;
        /*TODO handle timestamp case */
#if USE_SWDECODE
        while(audio_bitstream_sufficient_sample_to_render(out->bitstrm,
                                                renderType, 1) == true) {
            availableSize = audio_bitstream_get_output_buffer_write_ptr(out->bitstrm, renderType) -
                audio_bitstream_get_output_buffer_ptr(out->bitstrm, renderType);
            buffer = audio_bitstream_get_output_buffer_ptr(out->bitstrm, renderType);
            bytes_to_write   = availableSize;

            TODO: meta data is only neded for TS mode
            out->output_meta_data.metadataLength = metadataLength;
            out->output_meta_data.bufferLength = (availableSize >=
                                             (fragment_size - metadataLength)) ?
                                           fragment_size - metadataLength :
                                           availableSize;
            bytes_to_write = metadataLength +out->output_meta_data.bufferLength;
            out->output_meta_data.timestamp = 0;
            memcpy(out->write_temp_buf, &out->output_meta_data, metadataLength);
            memcpy(out->write_temp_buf+metadataLength,
                   audio_bitstream_get_output_buffer_ptr(out->bitstrm, renderType),
                   out->output_meta_data.bufferLength);
            ret = write_data(out, handle, out->write_temp_buf, bytes_to_write);
#endif

            ret = write_data(out, handle, buffer, bytes_to_write);
            ALOGD("write_data returned with %d", ret);
            if(ret < 0) {
                ALOGE("write_data returned ret < 0");
                return ret;
            } else {
                if (!out->playback_started) {
                    compress_start(handle->compr);
                }
                /*TODO:Do we need this
                if(renderType == ROUTE_UNCOMPRESSED ||
                   (renderType == ROUTE_UNCOMPRESSED_MCH && !out->open_dec_route)) {
                    mFrameCount++;
                renderedPcmBytes += out->output_meta_data.bufferLength;
                }*/
                renderedPcmBytes += ret;
#if USE_SWDECODE
                 /*iTODO: enable for MS11
                audio_bitstream_copy_residue_output_start(out->bitstrm, renderType,
                        bytes_to_write);
                TODO:what if ret<bytes_to_write*/
#endif
            }
#if USE_SWDECODE
        }
#endif
    }
    out->playback_started = 1;
    out->offload_state = OFFLOAD_STATE_PLAYING;
    out->send_new_metadata = 0;
    return renderedPcmBytes;
}

size_t render_pcm_data(struct stream_out *out, const void *buffer, size_t bytes)
{
    ALOGV("%s", __func__);
    size_t ret = 0;
    struct listnode *node;
    struct alsa_handle *handle;
    list_for_each(node, &out->session_list) {
        handle = node_to_item(node, struct alsa_handle, list);
        ALOGV("%s handle is 0x%x", __func__,(uint32_t)handle);
        ret = write_data(out, handle, buffer, bytes);
    }
    return ret;
}

static ssize_t out_write(struct audio_stream_out *stream, const void *buffer,
                         size_t bytes)
{
    struct stream_out *out = (struct stream_out *)stream;
    struct audio_device *adev = out->dev;
    ssize_t ret = 0;
    struct listnode *node;
    bool continueDecode;
    struct alsa_handle *handle;
    size_t bytes_consumed;
    size_t total_bytes_consumed = 0;

    ALOGV("%s bytes =%d", __func__, bytes);

    pthread_mutex_lock(&out->lock);

//TODO: handle a2dp
/*      if (mRouteAudioToA2dp &&
                        mA2dpUseCase == AudioHardwareALSA::USECASE_NONE) {
                a2dpRenderingControl(A2DP_RENDER_SETUP);
        }
*/
   /* TODO: meta data comes in set_parameter it will be passed in compre_open
      for all format exxce ms11 format
     and  for ms11 it will be set sdecode fucntion while opneing ms11 instance
     hence below piece of  code is no required*/
   /*
   if(!out->dec_conf_set && is_decoder_config_required(out)) {
        if (setDecodeConfig(out, (char *)buffer, bytes))
            ALOGD("decoder configuration set");
    }
    */

    if (out->standby) {
        out->standby = false;
        list_for_each(node, &out->session_list) {
            handle = node_to_item(node, struct alsa_handle, list);
            pthread_mutex_lock(&adev->lock);
            ret = start_output_stream(out, handle);
            pthread_mutex_unlock(&adev->lock);
            /* ToDo: If use case is compress offload should return 0 */
            if (ret != 0) {
                out->standby = true;
                goto exit;
            }
        }
    }

    if (out->uc_strm_type == OFFLOAD_PLAYBACK_STREAM) {
#if USE_SWDECODE
        //TODO: Enable for MS11
        copy_bitstream_internal_buffer(out->bitstrm, (char *)buffer, bytes);
        //DO check if timestamp mode handle partial buffer
        do {

            bytes_consumed = 0;
            ret = decode(out, (char *)buffer, bytes, &bytes_consumed, &continueDecode);
            if(ret < 0)
                goto exit;
            /*TODO: check for return size from write when ms11 is removed*/
            render_offload_data(out, continueDecode);
            total_bytes_consumed += bytes_consumed;

        } while(continueDecode == true);
#endif
#if 0
        ALOGVV("%s: writing buffer (%d bytes) to compress device", __func__, bytes);
        if (out->send_new_metadata) {
            ALOGVV("send new gapless metadata");
            compress_set_gapless_metadata(out->compr, &out->gapless_mdata);
            out->send_new_metadata = 0;
        }

        ret = compress_write(out->compr, buffer, bytes);
        ALOGVV("%s: writing buffer (%d bytes) to compress device returned %d", __func__, bytes, ret);
        if (ret >= 0 && ret < (ssize_t)bytes) {
            send_offload_cmd_l(out, OFFLOAD_CMD_WAIT_FOR_BUFFER);
        }
        if (!out->playback_started) {
            compress_start(out->compr);
            out->playback_started = 1;
            out->offload_state = OFFLOAD_STATE_PLAYING;
        }
        pthread_mutex_unlock(&out->lock);
        return ret;
    } else {
        if (out->pcm) {
            if (out->muted)
                memset((void *)buffer, 0, bytes);
            ALOGVV("%s: writing buffer (%d bytes) to pcm device", __func__, bytes);
            ret = pcm_write(out->pcm, (void *)buffer, bytes);
            if (ret == 0)
                out->written += bytes / (out->config.channels * sizeof(short));
        }
    }

exit:
    pthread_mutex_unlock(&out->lock);

    if (ret != 0) {
        if (out->pcm)
            ALOGE("%s: error %d - %s", __func__, ret, pcm_get_error(out->pcm));
        out_standby(&out->stream.common);
        usleep(bytes * 1000000 / audio_stream_frame_size(&out->stream.common) /
               out_get_sample_rate(&out->stream.common));
    }
    return bytes;
#endif
        ret = render_offload_data(out, buffer, bytes);
        total_bytes_consumed = ret;
    } else {
        ret = render_pcm_data(out, buffer, bytes);
        total_bytes_consumed = ret;
    }

exit:
    pthread_mutex_unlock(&out->lock);
    ALOGV("total_bytes_consumed %d",total_bytes_consumed);
    return total_bytes_consumed;
}

static int out_get_render_position(const struct audio_stream_out *stream,
                                   uint32_t *dsp_frames)
{
    struct listnode *node;
    struct alsa_handle *handle;
    struct stream_out *out = (struct stream_out *)stream;
    struct audio_device *adev = out->dev;
    *dsp_frames = 0;
    ALOGV("%s", __func__);
    pthread_mutex_lock(&out->lock);
    if ((out->uc_strm_type == OFFLOAD_PLAYBACK_STREAM) && (dsp_frames != NULL)) {
        list_for_each(node, &out->session_list) {
            handle = node_to_item(node, struct alsa_handle, list);
            if ((handle && handle->compr &&
                        handle->route_format != ROUTE_DSP_TRANSCODED_COMPRESSED)){
                compress_get_tstamp(handle->compr, (unsigned long *)dsp_frames,
                        &out->sample_rate);
                ALOGV("%s rendered frames %d sample_rate %d",
                        __func__, *dsp_frames, out->sample_rate);
            }
            pthread_mutex_unlock(&out->lock);
            return 0;
        }
    }
    else {
        pthread_mutex_unlock(&out->lock);
        return -EINVAL;
    }
    return 0;
#if 0
    struct stream_out *out = (struct stream_out *)stream;
    *dsp_frames = 0;
    if ((out->usecase == USECASE_AUDIO_PLAYBACK_OFFLOAD) && (dsp_frames != NULL)) {
        pthread_mutex_lock(&out->lock);
        if (out->compr != NULL) {
            compress_get_tstamp(out->compr, (unsigned long *)dsp_frames,
                    &out->sample_rate);
            ALOGVV("%s rendered frames %d sample_rate %d",
                   __func__, *dsp_frames, out->sample_rate);
        }
        pthread_mutex_unlock(&out->lock);
        return 0;
    } else
        return -EINVAL;
#endif
}

static int out_add_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    return 0;
}

static int out_remove_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    return 0;
}

static int out_get_next_write_timestamp(const struct audio_stream_out *stream,
                                        int64_t *timestamp)
{
    return -EINVAL;
}

static int out_get_presentation_position(const struct audio_stream_out *stream,
                                   uint64_t *frames, struct timespec *timestamp)
{
    struct listnode *node;
    struct alsa_handle *handle;
    struct stream_out *out = (struct stream_out *)stream;
    struct audio_device *adev = out->dev;
    *frames = 0;
    ALOGV("%s", __func__);
    pthread_mutex_lock(&out->lock);
    if ((frames != NULL)) {
        list_for_each(node, &out->session_list) {
            handle = node_to_item(node, struct alsa_handle, list);
            if ((handle && handle->compr &&
                        handle->route_format != ROUTE_DSP_TRANSCODED_COMPRESSED)){
                compress_get_tstamp(handle->compr, (unsigned long *)frames,
                        &out->sample_rate);
            clock_gettime(CLOCK_MONOTONIC, timestamp);
                ALOGV("%s rendered frames %d sample_rate %d",
                        __func__, *frames, out->sample_rate);
            }
        else if (handle->pcm) {
            size_t avail;
            if (pcm_get_htimestamp(handle->pcm, &avail, timestamp) == 0) {
                size_t kernel_buffer_size = handle->config.period_size * handle->config.period_count;
                int64_t signed_frames = out->written - kernel_buffer_size + avail;
                // This adjustment accounts for buffering after app processor.
                // It is based on estimated DSP latency per use case, rather than exact.
                signed_frames -=
                    (platform_render_latency(handle->usecase) * out->sample_rate / 1000000LL);

                // It would be unusual for this value to be negative, but check just in case ...
                if (signed_frames >= 0) {
                    *frames = signed_frames;
                }
            }
        }

        }
    }
        pthread_mutex_unlock(&out->lock);
        return -EINVAL;
#if 0
    struct stream_out *out = (struct stream_out *)stream;
    int ret = -1;
    unsigned long dsp_frames;

    pthread_mutex_lock(&out->lock);

    if (out->usecase == USECASE_AUDIO_PLAYBACK_OFFLOAD) {
        if (out->compr != NULL) {
            compress_get_tstamp(out->compr, &dsp_frames,
                    &out->sample_rate);
            ALOGVV("%s rendered frames %ld sample_rate %d",
                   __func__, dsp_frames, out->sample_rate);
            *frames = dsp_frames;
            ret = 0;
            /* this is the best we can do */
            clock_gettime(CLOCK_MONOTONIC, timestamp);
        }
    } else {
        if (out->pcm) {
            size_t avail;
            if (pcm_get_htimestamp(out->pcm, &avail, timestamp) == 0) {
                size_t kernel_buffer_size = out->config.period_size * out->config.period_count;
                int64_t signed_frames = out->written - kernel_buffer_size + avail;
                // This adjustment accounts for buffering after app processor.
                // It is based on estimated DSP latency per use case, rather than exact.
                signed_frames -=
                    (platform_render_latency(out->usecase) * out->sample_rate / 1000000LL);

                // It would be unusual for this value to be negative, but check just in case ...
                if (signed_frames >= 0) {
                    *frames = signed_frames;
                    ret = 0;
                }
            }
        }
    }

    pthread_mutex_unlock(&out->lock);

    return ret;
#endif
}

static int out_set_callback(struct audio_stream_out *stream,
            stream_callback_t callback, void *cookie)
{
    struct stream_out *out = (struct stream_out *)stream;

    ALOGV("%s", __func__);
    pthread_mutex_lock(&out->lock);
    out->offload_callback = callback;
    out->offload_cookie = cookie;
    pthread_mutex_unlock(&out->lock);
    return 0;
}

static int out_pause(struct audio_stream_out* stream)
{
    struct listnode *node;
    struct alsa_handle *handle;
    struct stream_out *out = (struct stream_out *)stream;
    int status = -ENOSYS;
    ALOGV("%s", __func__);
    pthread_mutex_lock(&out->lock);
    list_for_each(node, &out->session_list) {
        handle = node_to_item(node, struct alsa_handle, list);
        if (handle->compr != NULL && out->offload_state ==
                OFFLOAD_STATE_PLAYING) {
            status = compress_pause(handle->compr);
            out->offload_state = OFFLOAD_STATE_PAUSED;
        }
    }
    pthread_mutex_unlock(&out->lock);
    return status;
}

static int out_resume(struct audio_stream_out* stream)
{
    struct listnode *node;
    struct alsa_handle *handle;
    struct stream_out *out = (struct stream_out *)stream;
    int status = -ENOSYS;
    ALOGV("%s", __func__);
    pthread_mutex_lock(&out->lock);
    list_for_each(node, &out->session_list) {
        handle = node_to_item(node, struct alsa_handle, list);
            status = 0;
            if (handle->compr != NULL && out->offload_state ==
                    OFFLOAD_STATE_PAUSED) {
                status = compress_resume(handle->compr);
                out->offload_state = OFFLOAD_STATE_PLAYING;
            }
    }
    pthread_mutex_unlock(&out->lock);
    return status;
}

static int out_drain(struct audio_stream_out* stream, audio_drain_type_t type )
{
    struct listnode *node;
    struct alsa_handle *handle;
    struct stream_out *out = (struct stream_out *)stream;
    int status = -ENOSYS;
    ALOGV("%s", __func__);
    pthread_mutex_lock(&out->lock);
    list_for_each(node, &out->session_list) {
        handle = node_to_item(node, struct alsa_handle, list);
        status = 0;
        if (handle->compr != NULL) {
            if (type == AUDIO_DRAIN_EARLY_NOTIFY)
                status = send_offload_cmd_l(out, OFFLOAD_CMD_PARTIAL_DRAIN);
            else
                status = send_offload_cmd_l(out, OFFLOAD_CMD_DRAIN);
        }
    }
    pthread_mutex_unlock(&out->lock);
    return status;
}

static int out_flush(struct audio_stream_out* stream)
{
    struct listnode *node;
    struct alsa_handle *handle;
    struct stream_out *out = (struct stream_out *)stream;
    int status = -ENOSYS;
    ALOGV("%s", __func__);
    pthread_mutex_lock(&out->lock);
    list_for_each(node, &out->session_list) {
        handle = node_to_item(node, struct alsa_handle, list);
        status = 0;
        if (handle->compr != NULL) {
            stop_compressed_output_l(out);
        }
    }
    pthread_mutex_unlock(&out->lock);
    return status;
}

int adev_open_output_stream(struct audio_hw_device *dev,
                                   audio_io_handle_t handle,
                                   audio_devices_t devices,
                                   audio_output_flags_t flags,
                                   struct audio_config *config,
                                   struct audio_stream_out **stream_out)
{
    struct audio_device *adev = (struct audio_device *)dev;
    struct stream_out *out;
    struct alsa_handle *device_handle = NULL;
    int i, ret, channels;
    struct listnode *item;

    ALOGV("%s: enter: sample_rate(%d) channel_mask(%#x) devices(%#x) flags(%#x)",
          __func__, config->sample_rate, config->channel_mask, devices, flags);
    *stream_out = NULL;
    out = (struct stream_out *)calloc(1, sizeof(struct stream_out));

    if (devices == AUDIO_DEVICE_NONE)
        devices = AUDIO_DEVICE_OUT_SPEAKER;
    list_init(&out->session_list);

    reset_out_parameters(out);
    out->flags = flags;
    out->devices = devices;
    out->dev = adev;
    out->format = config->format;
    out->sample_rate = config->sample_rate;
    out->channel_mask = AUDIO_CHANNEL_OUT_STEREO;
    out->supported_channel_masks[0] = AUDIO_CHANNEL_OUT_STEREO;
    out->config = config;
    out->handle = handle;

//*TODO: get hdmi/spdif format/channels from routing manager and intialize out->spdif_format & out->hdmi_format*/
    /* Init use case and pcm_config */
    out->hdmi_format = UNCOMPRESSED;
    out->spdif_format = UNCOMPRESSED;
    out->stream.common.get_sample_rate = out_get_sample_rate;
    out->stream.common.set_sample_rate = out_set_sample_rate;
    out->stream.common.get_buffer_size = out_get_buffer_size;
    out->stream.common.get_channels = out_get_channels;
    out->stream.common.get_format = out_get_format;
    out->stream.common.set_format = out_set_format;
    out->stream.common.standby = out_standby;
    out->stream.common.dump = out_dump;
    out->stream.common.set_parameters = out_set_parameters;
    out->stream.common.get_parameters = out_get_parameters;
    out->stream.common.add_audio_effect = out_add_audio_effect;
    out->stream.common.remove_audio_effect = out_remove_audio_effect;
    out->stream.get_latency = out_get_latency;
    out->stream.set_volume = out_set_volume;
    out->stream.write = out_write;
    out->stream.get_render_position = out_get_render_position;
    out->stream.get_next_write_timestamp = out_get_next_write_timestamp;
    out->stream.get_presentation_position = out_get_presentation_position;

    out->standby = 1;
    /* out->muted = false; by calloc() */
    /* out->written = 0; by calloc() */

    pthread_mutex_init(&out->lock, (const pthread_mutexattr_t *) NULL);
    pthread_cond_init(&out->cond, (const pthread_condattr_t *) NULL);

    if (out->flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) {
        ALOGE("%s: Usecase is OFFLOAD", __func__);
        if (config->offload_info.version != AUDIO_INFO_INITIALIZER.version ||
            config->offload_info.size != AUDIO_INFO_INITIALIZER.size) {
            ALOGE("%s: Unsupported Offload information", __func__);
            ret = -EINVAL;
            goto error_open;
        }

        if (!is_supported_format(config->offload_info.format)) {
            ALOGE("%s: Unsupported audio format", __func__);
            ret = -EINVAL;
            goto error_open;
        }
        out->compr_config.codec = (struct snd_codec *)
                calloc(1, sizeof(struct snd_codec));
        //Session/clip config.
        out->format = config->offload_info.format;
        out->sample_rate = config->offload_info.sample_rate;
        out->compr_config.codec->id =
                get_snd_codec_id(config->offload_info.format);
        out->compr_config.fragment_size = COMPRESS_OFFLOAD_FRAGMENT_SIZE;
        out->compr_config.fragments = COMPRESS_OFFLOAD_NUM_FRAGMENTS;
        out->compr_config.codec->sample_rate =
                    compress_get_alsa_rate(config->offload_info.sample_rate);
        out->compr_config.codec->bit_rate =
                    config->offload_info.bit_rate;
        out->compr_config.codec->ch_in =
                    popcount(config->channel_mask);
        out->compr_config.codec->ch_out = out->compr_config.codec->ch_in;

        if (config->offload_info.channel_mask)
            out->channel_mask = config->offload_info.channel_mask;
        else if (config->channel_mask)
            out->channel_mask = config->channel_mask;
        out->uc_strm_type = OFFLOAD_PLAYBACK_STREAM;

        //Initialize the handles
        /* ------------------------------------------------------------------------
        Update use decoder type and routing flags and corresponding states
        decoderType will cache the decode types such as decode/passthrough/transcode
        and in s/w or dsp. Besides, the states to open decode/passthrough/transcode
        handles with the corresponding devices and device formats are updated
           -------------------------------------------------------------------------*/
        update_decode_type_and_routing_states(out);

        /* ------------------------------------------------------------------------
        Update rxHandle states
        Based on the states, we open the driver and store the handle at appropriate
        index
        -------------------------------------------------------------------------*/
        update_alsa_handle_state(out);

        /* ------------------------------------------------------------------------
        setup routing
        -------------------------------------------------------------------------*/
        ret = allocate_internal_buffers(out);
        if(ret < 0) {
            ALOGE("%s:Error %d",__func__, ret);
            goto error_handle;
        }

        //Callbacks
        out->stream.set_callback = out_set_callback;
        out->stream.pause = out_pause;
        out->stream.resume = out_resume;
        out->stream.drain = out_drain;
        out->stream.flush = out_flush;

        if (flags & AUDIO_OUTPUT_FLAG_NON_BLOCKING)
            out->non_blocking = 1;

        out->send_new_metadata = 1;
        create_offload_callback_thread(out);
        ALOGV("%s: offloaded output offload_info version %04x bit rate %d",
                __func__, config->offload_info.version,
                config->offload_info.bit_rate);
    } else { //if (out->flags & AUDIO_OUTPUT_FLAG_DEEP_BUFFER) {
        ALOGE("%s: Usecase is DEEP_BUFFER", __func__);
        if((device_handle = get_alsa_handle())== NULL)
            goto error_handle;
        list_add_tail(&out->session_list, &device_handle->list);
        device_handle->usecase = USECASE_AUDIO_PLAYBACK_DEEP_BUFFER;
        device_handle->config = pcm_config_deep_buffer;
        device_handle->out = out;
        device_handle->cmd_pending = false;
        out->sample_rate = device_handle->config.rate;
        out->uc_strm_type = DEEP_BUFFER_PLAYBACK_STREAM;
        out->buffer_size = device_handle->config.period_size *
                            audio_stream_frame_size(&out->stream.common);
    }/* else {
        if((device_handle = get_alsa_handle())== NULL)
            goto error_handle;
        list_add_tail(&out->session_list, &device_handle->list);
        device_handle->usecase = USECASE_AUDIO_PLAYBACK_LOW_LATENCY;
        device_handle->config = pcm_config_low_latency;
        device_handle->sample_rate = device_handle->config.rate;
        device_handle->out = out;
        device_handle->cmd_pending = false;
        out->uc_strm_type  = LOW_LATENCY_PLAYBACK_STREAM;
        out->buffer_size = device_handle->config.period_size *
                            audio_stream_frame_size(&out->stream.common);
    }*/

    if (flags & AUDIO_OUTPUT_FLAG_PRIMARY) {
        ALOGE("%s: Usecase is primary ", __func__);
        if(adev->primary_output == NULL)
            adev->primary_output = out;
        else {
            ALOGE("%s: Primary output is already opened", __func__);
            ret = -EEXIST;
            goto error_open;
        }
    }

    /* Check if this usecase is already existing */
    pthread_mutex_lock(&adev->lock);
    if (out->uc_strm_type != OFFLOAD_PLAYBACK_STREAM) {
        if (get_usecase_from_list(adev, device_handle->usecase) != NULL) {
            ALOGE("%s: Usecase (%d) is already present", __func__,
                        device_handle->usecase);
            pthread_mutex_unlock(&adev->lock);
            ret = -EEXIST;
            goto error_open;
        }
    }
    pthread_mutex_unlock(&adev->lock);


    /* out->muted = false; by calloc() */


    config->format = out->stream.common.get_format(&out->stream.common);
    config->channel_mask = out->stream.common.get_channels(&out->stream.common);
    config->sample_rate = out->stream.common.get_sample_rate(&out->stream.common);

    *stream_out = &out->stream;
    ALOGV("%s: exit", __func__);
    return 0;

error_handle:
    ret = -EINVAL;
    ALOGE("%s: exit: error handle %d", __func__, ret);
    while (!list_empty(&out->session_list)) {
        item = list_head(&out->session_list);
        list_remove(item);
        device_handle  = node_to_item(item, struct alsa_handle, list);
        platform_free_usecase(device_handle->usecase);
        free_alsa_handle(device_handle);
    }

error_open:
    free(out);
    *stream_out = NULL;
    ALOGD("%s: exit: ret %d", __func__, ret);
    return ret;
}

void adev_close_output_stream(struct audio_hw_device *dev,
                                     struct audio_stream_out *stream)
{
    struct stream_out *out = (struct stream_out *)stream;
    struct audio_device *adev = out->dev;
    struct listnode *item;
    struct alsa_handle *handle;

    ALOGV("%s", __func__);

    out_standby(&stream->common);
    if (out->uc_strm_type == OFFLOAD_PLAYBACK_STREAM) {
        destroy_offload_callback_thread(out);

        while (!list_empty(&out->session_list)) {
            item = list_head(&out->session_list);
            list_remove(item);
            handle  = node_to_item(item, struct alsa_handle, list);
                if(handle->compr_config.codec != NULL)
                    free(handle->compr_config.codec);
            platform_free_usecase(handle->usecase);
            free_alsa_handle(handle);
        }
        free(out->compr_config.codec);
    }

    free_internal_buffers(out);
    pthread_cond_destroy(&out->cond);
    pthread_mutex_destroy(&out->lock);
    free(stream);
    ALOGV("%s: exit", __func__);
}
