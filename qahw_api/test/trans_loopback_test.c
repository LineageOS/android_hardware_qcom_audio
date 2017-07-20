/*
* Copyright (c) 2017, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*    * Redistributions of source code must retain the above copyright
*      notice, this list of conditions and the following disclaimer.
*    * Redistributions in binary form must reproduce the above
*      copyright notice, this list of conditions and the following
*      disclaimer in the documentation and/or other materials provided
*      with the distribution.
*    * Neither the name of The Linux Foundation nor the names of its
*      contributors may be used to endorse or promote products derived
*      from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
* ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
* OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/* Test app to capture event updates from kernel */
/*#define LOG_NDEBUG 0*/
#include <fcntl.h>
#include <linux/netlink.h>
#include <pthread.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <utils/Log.h>

#include <cutils/list.h>
#include <hardware/audio.h>
#include <system/audio.h>
#include "qahw_api.h"
#include "qahw_defs.h"


typedef struct tlb_hdmi_config {
    int hdmi_conn_state;
    int hdmi_audio_state;
    int hdmi_sample_rate;
    int hdmi_num_channels;
} tlb_hdmi_config_t;

const char tlb_hdmi_in_audio_sys_path[] =
"/sys/devices/virtual/switch/hpd_state/state";
const char tlb_hdmi_in_audio_dev_path[] = "/devices/virtual/switch/hpd_state";
const char tlb_hdmi_in_audio_state_sys_path[] =
"/sys/devices/virtual/switch/audio_state/state";
const char tlb_hdmi_in_audio_state_dev_path[] =
"/devices/virtual/switch/audio_state";
const char tlb_hdmi_in_audio_sample_rate_sys_path[] =
"/sys/devices/virtual/switch/sample_rate/state";
const char tlb_hdmi_in_audio_sample_rate_dev_path[] =
"/devices/virtual/switch/sample_rate";
const char tlb_hdmi_in_audio_channel_sys_path[] =
"/sys/devices/virtual/switch/channels/state";
const char tlb_hdmi_in_audio_channel_dev_path[] =
"/devices/virtual/switch/channels";

qahw_module_handle_t *primary_hal_handle = NULL;

FILE * log_file = NULL;
volatile bool stop_playback = false;
const char *log_filename = NULL;

#define TRANSCODE_LOOPBACK_SOURCE_PORT_ID 0x4C00
#define TRANSCODE_LOOPBACK_SINK_PORT_ID 0x4D00

#define MAX_MODULE_NAME_LENGTH  100

typedef enum source_port_type {
    SOURCE_PORT_NONE,
    SOURCE_PORT_HDMI,
    SOURCE_PORT_SPDIF,
    SOURCE_PORT_MIC
} source_port_type_t;

typedef struct trnscode_loopback_config {
    qahw_module_handle_t *hal_handle;
    audio_devices_t devices;
    struct audio_port_config source_config;
    struct audio_port_config sink_config;
    audio_patch_handle_t patch_handle;
} transcode_loopback_config_t;

transcode_loopback_config_t g_trnscode_loopback_config;


void init_transcode_loopback_config(transcode_loopback_config_t **p_transcode_loopback_config)
{
    fprintf(log_file,"\nInitializing global transcode loopback config\n");
    g_trnscode_loopback_config.hal_handle = NULL;

    audio_devices_t out_device = AUDIO_DEVICE_OUT_SPEAKER; // Get output device mask from connected device
    audio_devices_t in_device = AUDIO_DEVICE_IN_HDMI;

    g_trnscode_loopback_config.devices = (out_device | in_device);

    /* Patch source port config init */
    g_trnscode_loopback_config.source_config.id = TRANSCODE_LOOPBACK_SOURCE_PORT_ID;
    g_trnscode_loopback_config.source_config.role = AUDIO_PORT_ROLE_SOURCE;
    g_trnscode_loopback_config.source_config.type = AUDIO_PORT_TYPE_DEVICE;
    g_trnscode_loopback_config.source_config.config_mask =
                        (AUDIO_PORT_CONFIG_ALL ^ AUDIO_PORT_CONFIG_GAIN);
    g_trnscode_loopback_config.source_config.sample_rate = 48000;
    g_trnscode_loopback_config.source_config.channel_mask =
                        AUDIO_CHANNEL_OUT_STEREO; // Using OUT as this is digital data and not mic capture
    g_trnscode_loopback_config.source_config.format = AUDIO_FORMAT_PCM_16_BIT;
    /*TODO: add gain */
    g_trnscode_loopback_config.source_config.ext.device.hw_module =
                        AUDIO_MODULE_HANDLE_NONE;
    g_trnscode_loopback_config.source_config.ext.device.type = in_device;

    /* Patch sink port config init */
    g_trnscode_loopback_config.sink_config.id = TRANSCODE_LOOPBACK_SINK_PORT_ID;
    g_trnscode_loopback_config.sink_config.role = AUDIO_PORT_ROLE_SINK;
    g_trnscode_loopback_config.sink_config.type = AUDIO_PORT_TYPE_DEVICE;
    g_trnscode_loopback_config.sink_config.config_mask =
                            (AUDIO_PORT_CONFIG_ALL ^ AUDIO_PORT_CONFIG_GAIN);
    g_trnscode_loopback_config.sink_config.sample_rate = 48000;
    g_trnscode_loopback_config.sink_config.channel_mask =
                             AUDIO_CHANNEL_OUT_STEREO;
    g_trnscode_loopback_config.sink_config.format = AUDIO_FORMAT_PCM_16_BIT;

    g_trnscode_loopback_config.sink_config.ext.device.hw_module =
                            AUDIO_MODULE_HANDLE_NONE;
    g_trnscode_loopback_config.sink_config.ext.device.type = out_device;

    /* Init patch handle */
    g_trnscode_loopback_config.patch_handle = AUDIO_PATCH_HANDLE_NONE;

    *p_transcode_loopback_config = &g_trnscode_loopback_config;

    fprintf(log_file,"\nDone Initializing global transcode loopback config\n");
}

void deinit_transcode_loopback_config()
{
    g_trnscode_loopback_config.hal_handle = NULL;

    g_trnscode_loopback_config.devices = AUDIO_DEVICE_NONE;
}

void read_data_from_fd(const char* path, int *value)
{
    int fd = -1;
    char buf[16];
    int ret;

    fd = open(path, O_RDONLY, 0);
    if (fd < 0) {
        ALOGE("Unable open fd for file %s", path);
        return;
    }

    ret = read(fd, buf, 15);
    if (ret < 0) {
        ALOGE("File %s Data is empty", path);
        close(fd);
        return;
    }

    buf[ret] = '\0';
    *value = atoi(buf);
    close(fd);
}

int actual_channels_from_audio_infoframe(int infoframe_channels)
{
    if (infoframe_channels > 0 && infoframe_channels < 8) {
      /* refer CEA-861-D Table 17 Audio InfoFrame Data Byte 1 */
        return (infoframe_channels+1);
    }
    fprintf(log_file,"\nInfoframe channels 0, need to get from stream, returning default 2\n");
    return 2;
}

int read_and_set_source_config(source_port_type_t source_port_type,
                               struct audio_port_config *dest_port_config)
{
    int rc=0, channels = 2;
    tlb_hdmi_config_t hdmi_config = {0};
    switch(source_port_type)
    {
    case SOURCE_PORT_HDMI :
    read_data_from_fd(tlb_hdmi_in_audio_sys_path, &hdmi_config.hdmi_conn_state);
    read_data_from_fd(tlb_hdmi_in_audio_state_sys_path,
                      &hdmi_config.hdmi_audio_state);
    read_data_from_fd(tlb_hdmi_in_audio_sample_rate_sys_path,
                      &hdmi_config.hdmi_sample_rate);
    read_data_from_fd(tlb_hdmi_in_audio_channel_sys_path,
                      &hdmi_config.hdmi_num_channels);

    channels = actual_channels_from_audio_infoframe(hdmi_config.hdmi_num_channels);
    fprintf(log_file,"\nHDMI In state: %d, audio_state: %d, samplerate: %d, channels: %d\n",
            hdmi_config.hdmi_conn_state, hdmi_config.hdmi_audio_state,
            hdmi_config.hdmi_sample_rate, channels);

    ALOGD("HDMI In state: %d, audio_state: %d, samplerate: %d, channels: %d",
           hdmi_config.hdmi_conn_state, hdmi_config.hdmi_audio_state,
           hdmi_config.hdmi_sample_rate, channels);

        dest_port_config->sample_rate = hdmi_config.hdmi_sample_rate;
        switch(channels) {
        case 2 :
            dest_port_config->channel_mask = AUDIO_CHANNEL_OUT_STEREO;
            break;
        case 3 :
            dest_port_config->channel_mask = AUDIO_CHANNEL_OUT_2POINT1;
            break;
        case 4 :
            dest_port_config->channel_mask = AUDIO_CHANNEL_OUT_QUAD;
            break;
        case 5 :
            dest_port_config->channel_mask = AUDIO_CHANNEL_OUT_PENTA;
            break;
        case 6 :
            dest_port_config->channel_mask = AUDIO_CHANNEL_OUT_5POINT1;
            break;
        case 7 :
            dest_port_config->channel_mask = AUDIO_CHANNEL_OUT_6POINT1;
            break;
        case 8 :
            dest_port_config->channel_mask = AUDIO_CHANNEL_OUT_7POINT1;
            break;
        default :
            fprintf(log_file,"\nUnsupported number of channels in source port %d\n",
                    channels);
            rc = -1;
            break;
        }
        break;
    default :
        fprintf(log_file,"\nUnsupported port type, cannot set configuration\n");
        rc = -1;
        break;
    }
    return rc;
}

void stop_transcode_loopback(
            transcode_loopback_config_t *transcode_loopback_config)
{
    qahw_release_audio_patch(transcode_loopback_config->hal_handle,
                             transcode_loopback_config->patch_handle);
}

int create_run_transcode_loopback(
            transcode_loopback_config_t *transcode_loopback_config)
{
    int rc=0;
    qahw_module_handle_t *module_handle = transcode_loopback_config->hal_handle;


    fprintf(log_file,"\nCreating audio patch\n");
    rc = qahw_create_audio_patch(module_handle,
                        1,
                        &transcode_loopback_config->source_config,
                        1,
                        &transcode_loopback_config->sink_config,
                        &transcode_loopback_config->patch_handle);
    fprintf(log_file,"\nCreate patch returned %d\n",rc);
    return rc;
}

static audio_hw_device_t *load_hal(audio_devices_t dev)
{
    if (primary_hal_handle == NULL) {
        primary_hal_handle = qahw_load_module(QAHW_MODULE_ID_PRIMARY);
        if (primary_hal_handle == NULL) {
            fprintf(stderr,"failure in Loading primary HAL\n");
            goto exit;
		}
    }

exit:
    return primary_hal_handle;
}

/*
* this function unloads all the loaded hal modules so this should be called
* after all the stream playback are concluded.
*/
static int unload_hals(void) {
    if (primary_hal_handle) {
        qahw_unload_module(primary_hal_handle);
        primary_hal_handle = NULL;
    }
    return 1;
}

int main(int argc, char *argv[]) {

    int status = 0,play_duration_in_seconds = 30;
    source_port_type_t source_port_type = SOURCE_PORT_NONE;
    log_file = stdout;

    fprintf(log_file,"\nTranscode loopback test begin\n");
    if (argc == 2) {
        play_duration_in_seconds = atoi(argv[1]);
        if (play_duration_in_seconds < 0 | play_duration_in_seconds > 3600) {
            fprintf(log_file,
                    "\nPlayback duration %s invalid or unsupported(range : 1 to 3600 )\n",
                    argv[1]);
            goto usage;
        }
    } else {
        goto usage;
    }

    transcode_loopback_config_t    *transcode_loopback_config = NULL;
    transcode_loopback_config_t *temp = NULL;

    /* Initialize global transcode loopback struct */
    init_transcode_loopback_config(&temp);
    transcode_loopback_config = &g_trnscode_loopback_config;

    /* Load HAL */
    fprintf(log_file,"\nLoading HAL for loopback usecase begin\n");
    primary_hal_handle = load_hal(transcode_loopback_config->devices);
    if (primary_hal_handle == NULL) {
        fprintf(log_file,"\n Failure in Loading HAL, exiting\n");
        goto exit_transcode_loopback_test;
    }
    transcode_loopback_config->hal_handle = primary_hal_handle;
    fprintf(log_file,"\nLoading HAL for loopback usecase done\n");

    /* Configuration assuming source port is HDMI */
    {
        source_port_type = SOURCE_PORT_HDMI;
        fprintf(log_file,"\nSet port config being\n");
        status = read_and_set_source_config(source_port_type,&transcode_loopback_config->source_config);
        fprintf(log_file,"\nSet port config end\n");

        if (status != 0) {
            fprintf(log_file,"\nFailed to set port config, exiting\n");
            goto exit_transcode_loopback_test;
        }
    }

    /* Open transcode loopback session */
    fprintf(log_file,"\nCreate and start transcode loopback session begin\n");
    status = create_run_transcode_loopback(transcode_loopback_config);
    fprintf(log_file,"\nCreate and start transcode loopback session end\n");

    /* If session opened successfully, run for a duration and close session */
    if (status == 0) {
        fprintf(log_file,"\nSleeping for %d seconds for loopback session to run\n",
                play_duration_in_seconds);
        usleep(play_duration_in_seconds*1000*1000);

        fprintf(log_file,"\nStop transcode loopback session begin\n");
        stop_transcode_loopback(transcode_loopback_config);
        fprintf(log_file,"\nStop transcode loopback session end\n");
    } else {
        fprintf(log_file,"\nEncountered error %d in creating transcode loopback session\n",
              status);
    }

exit_transcode_loopback_test:
    fprintf(log_file,"\nUnLoading HAL for loopback usecase begin\n");
    unload_hals();
    fprintf(log_file,"\nUnLoading HAL for loopback usecase end\n");

    deinit_transcode_loopback_config();
    transcode_loopback_config = NULL;

    fprintf(log_file,"\nTranscode loopback test end\n");
    return 0;
usage:
    fprintf(log_file,"\nInvald arguments\n");
    fprintf(log_file,"\nUsage : trans_loopback_test <duration_in_seconds>\n");
    fprintf(log_file,"\nExample to play for 1 minute : trans_loopback_test 60\n");
    return 0;
}


