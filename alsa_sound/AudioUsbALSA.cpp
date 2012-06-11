/* AudioUsbALSA.cpp
Copyright (c) 2012, Code Aurora Forum. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.
    * Neither the name of Code Aurora Forum, Inc. nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.*/

#define LOG_TAG "AudioUsbALSA"
//#define LOG_NDEBUG 0
//#define LOG_NDDEBUG 0
#include <utils/Log.h>
#include <utils/String8.h>

#include <cutils/properties.h>
#include <media/AudioRecord.h>
#include <hardware_legacy/power.h>
#include <sys/poll.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <jni.h>
#include <stdio.h>
#include <sys/eventfd.h>


#include "AudioUsbALSA.h"
struct pollfd pfdProxyPlayback[2];
struct pollfd pfdUsbPlayback[2];
struct pollfd pfdProxyRecording[1];
struct pollfd pfdUsbRecording[1];

namespace android_audio_legacy
{
AudioUsbALSA::AudioUsbALSA()
{
    mproxypfdPlayback = -1;
    musbpfdPlayback = -1;
    mkillPlayBackThread = false;
    mkillRecordingThread = false;
}

AudioUsbALSA::~AudioUsbALSA()
{
    mkillPlayBackThread = true;
    mkillRecordingThread = true;
}


int AudioUsbALSA::getnumOfRates(char *ratesStr){
    int i, size = 0;
    char *nextSRString, *temp_ptr;
    nextSRString = strtok_r(ratesStr, " ,", &temp_ptr);
    if (nextSRString == NULL) {
        LOGE("ERROR: getnumOfRates: could not find rates string");
        return NULL;
    }
    for (i = 1; nextSRString != NULL; i++) {
        size ++;
        nextSRString = strtok_r(NULL, " ,.-", &temp_ptr);
    }
    return size;
}

status_t AudioUsbALSA::getPlaybackCap()
{
    LOGD("getPlaybackCap");
    long unsigned fileSize;
    FILE *fp;
    char *buffer;
    int err = 1;
    int size = 0;
    int fd, i, lchannelsPlayback;
    char *read_buf, *playbackstr_start, *channel_start, *ratesStr, *ratesStrForVal,
    *ratesStrStart, *chString, *nextSRStr, *test, *nextSRString, *temp_ptr;
    struct stat st;
    memset(&st, 0x0, sizeof(struct stat));
    msampleRatePlayback = 0;
    fd = open(PATH, O_RDONLY);
    if (fd <0) {
        LOGE("ERROR: failed to open config file %s error: %d\n", PATH, errno);
        close(fd);
        return UNKNOWN_ERROR;
    }

    if (fstat(fd, &st) < 0) {
        LOGE("ERROR: failed to stat %s error %d\n", PATH, errno);
        close(fd);
        return UNKNOWN_ERROR;
    }

    fileSize = st.st_size;

    read_buf = (char *)malloc(BUFFSIZE);
    memset(read_buf, 0x0, BUFFSIZE);
    err = read(fd, read_buf, BUFFSIZE);
    playbackstr_start = strstr(read_buf, "Playback:");
    if (playbackstr_start == NULL) {
        LOGE("ERROR:Playback section not found in usb config file");
        close(fd);
        free(read_buf);
        return UNKNOWN_ERROR;
    }

    channel_start = strstr(playbackstr_start, "Channels:");
    if (channel_start == NULL) {
        LOGE("ERROR: Could not find Channels information");
        close(fd);
        free(read_buf);
        return UNKNOWN_ERROR;
    }
    channel_start = strstr(channel_start, " ");
    if (channel_start == NULL) {
        LOGE("ERROR: Channel section not found in usb config file");
        close(fd);
        free(read_buf);
        return UNKNOWN_ERROR;
    }

    lchannelsPlayback = atoi(channel_start);
    if (lchannelsPlayback == 1) {
        mchannelsPlayback = 1;
    } else {
        mchannelsPlayback = 2;
    }
    LOGD("channels supported by device: %d", lchannelsPlayback);
    ratesStrStart = strstr(playbackstr_start, "Rates:");
    if (ratesStrStart == NULL) {
        LOGE("ERROR: Cant find rates information");
        close(fd);
        free(read_buf);
        return UNKNOWN_ERROR;
    }

    ratesStrStart = strstr(ratesStrStart, " ");
    if (ratesStrStart == NULL) {
        LOGE("ERROR: Channel section not found in usb config file");
        close(fd);
        free(read_buf);
        return UNKNOWN_ERROR;
    }

    //copy to ratesStr, current line.
    char *target = strchr(ratesStrStart, '\n');
    if (target == NULL) {
        LOGE("ERROR: end of line not found");
        close(fd);
        free(read_buf);
        return UNKNOWN_ERROR;
    }
    size = target - ratesStrStart;
    ratesStr = (char *)malloc(size + 1) ;
    ratesStrForVal = (char *)malloc(size + 1) ;
    memcpy(ratesStr, ratesStrStart, size);
    memcpy(ratesStrForVal, ratesStrStart, size);
    ratesStr[size] = '\0';
    ratesStrForVal[size] = '\0';

    size = getnumOfRates(ratesStr);
    if (!size) {
        LOGE("ERROR: Could not get rate size, returning");
        close(fd);
        free(ratesStrForVal);
        free(ratesStr);
        free(read_buf);
        return UNKNOWN_ERROR;
    }

    //populate playback rates array
    int ratesSupported[size];
    nextSRString = strtok_r(ratesStrForVal, " ,", &temp_ptr);
    if (nextSRString == NULL) {
        LOGE("ERROR: Could not get first rate val");
        close(fd);
        free(ratesStrForVal);
        free(ratesStr);
        free(read_buf);
        return UNKNOWN_ERROR;
    }

    ratesSupported[0] = atoi(nextSRString);
    for (i = 1; i<size; i++) {
        nextSRString = strtok_r(NULL, " ,.-", &temp_ptr);
        ratesSupported[i] = atoi(nextSRString);
        LOGV("ratesSupported[%d] for playback: %d",i, ratesSupported[i]);
    }

    for (i = 0; i<=size; i++) {
        if (ratesSupported[i] <= 48000) {
            msampleRatePlayback = ratesSupported[i];
            break;
        }
    }
    LOGD("msampleRatePlayback: %d", msampleRatePlayback);

    close(fd);
    free(ratesStrForVal);
    free(ratesStr);
    free(read_buf);
    ratesStrForVal = NULL;
    ratesStr = NULL;
    read_buf = NULL;
    return NO_ERROR;
}


/******************************** Capture ******************************/
status_t AudioUsbALSA::getCaptureCap(){
    char *read_buf, *target, *capturestr_start, *channel_startCapture, *ratesStr, *ratesStrForVal,
    *ratesStrStartCapture, *chString, *nextSRStr, *test, *nextSRString, *temp_ptr;

    int fd, i, lchannelsCapture;

    int err =1, size=0;
    struct stat st;
    memset(&st, 0x0, sizeof(struct stat));

    msampleRateCapture = 0;
    fd = open(PATH, O_RDONLY);
    if (fd <0) {
        LOGE("ERROR: failed to open config file %s error: %d\n", PATH, errno);
        return UNKNOWN_ERROR;
    }

    if (fstat(fd, &st) < 0) {
        LOGE("ERROR: failed to stat %s error %d\n", PATH, errno);
        close(fd);
        return UNKNOWN_ERROR;
    }

    read_buf = (char *)malloc(BUFFSIZE);
    memset(read_buf, 0x0, BUFFSIZE);
    err = read(fd, read_buf, BUFFSIZE);
    capturestr_start = strstr(read_buf, "Capture:");
    if (capturestr_start == NULL) {
        LOGE("ERROR: Could not find capture section for recording");
        free(read_buf);
        close(fd);
        return NULL;
    }

    channel_startCapture = strstr(capturestr_start, "Channels: ");
    if (channel_startCapture == NULL) {
        LOGE("ERROR: Could not find Channels info for recording");
        close(fd);
        free(read_buf);
        return UNKNOWN_ERROR;
    }
    channel_startCapture = strstr(channel_startCapture, " ");
    if (channel_startCapture == NULL) {
        LOGE("ERROR: Could not find channels information for recording");
        close(fd);
        free(read_buf);
        return UNKNOWN_ERROR;
    } else {
        lchannelsCapture = atoi(channel_startCapture);
        if (lchannelsCapture == 1) {
            mchannelsCapture = 1;
        } else {
            LOGD("lchannelsCapture: %d", lchannelsCapture);
            mchannelsCapture = 2;
        }
    }

    ratesStrStartCapture = strstr(capturestr_start, "Rates:");
    if (ratesStrStartCapture == NULL) {
        LOGE("ERROR; Could not find rates section in config file for recording");
        close(fd);
        free(read_buf);
        return UNKNOWN_ERROR;
    }
    ratesStrStartCapture = strstr(ratesStrStartCapture, " ");
    if (ratesStrStartCapture == NULL) {
        LOGE("ERROR: Could not find rates section in config file for recording");
        close(fd);
        free(read_buf);
        return UNKNOWN_ERROR;
    }

    //copy to ratesStr, current line.
    target = strchr(ratesStrStartCapture, '\n');
    if (target == NULL) {
        LOGE("ERROR: end of line not found for rates");
        close(fd);
        free(read_buf);
        return UNKNOWN_ERROR;
    }

    size = target - ratesStrStartCapture;
    ratesStr = (char *)malloc(size + 1) ;
    ratesStrForVal = (char *)malloc(size + 1) ;
    memcpy(ratesStr, ratesStrStartCapture, size);
    memcpy(ratesStrForVal, ratesStrStartCapture, size);
    ratesStr[size] = '\0';
    ratesStrForVal[size] = '\0';

    size = getnumOfRates(ratesStr);
    if (!size) {
        LOGE("ERROR: Could not get rate size for capture, returning");
        close(fd);
        free(read_buf);
        free(ratesStr);
        free(ratesStrForVal);
        return UNKNOWN_ERROR;
    }

    //populate playback rates array
    int ratesSupportedCapture[size];
    nextSRString = strtok_r(ratesStrForVal, " ,", &temp_ptr);
    if (nextSRString == NULL) {
        LOGE("ERROR: Could not find ratesStr for recording");
        close(fd);
        free(read_buf);
        free(ratesStr);
        free(ratesStrForVal);
        return UNKNOWN_ERROR;
    }

    ratesSupportedCapture[0] = atoi(nextSRString);
    for (i = 1; i<size; i++) {
        nextSRString = strtok_r(NULL, " ,.-", &temp_ptr);
        ratesSupportedCapture[i] = atoi(nextSRString);
    }
    for (i = 0;i<=size; i++) {
        if (ratesSupportedCapture[i] <= 48000) {
            msampleRateCapture = ratesSupportedCapture[i];
            break;
        }
    }
    LOGD("msampleRateCapture: %d", msampleRateCapture);

    close(fd);
    free(read_buf);
    free(ratesStr);
    free(ratesStrForVal);
    read_buf = NULL;
    ratesStr = NULL;
    ratesStrForVal = NULL;
    return NO_ERROR;
}

void AudioUsbALSA::exitPlaybackThread(uint64_t writeVal)
{
    LOGD("exitPlaybackThread, mproxypfdPlayback: %d", mproxypfdPlayback);
    if (writeVal == SIGNAL_EVENT_KILLTHREAD) {
        closePlaybackDevices();
    }
    if ((mproxypfdPlayback != -1) && (musbpfdPlayback != -1)) {
        write(mproxypfdPlayback, &writeVal, sizeof(uint64_t));
        write(musbpfdPlayback, &writeVal, sizeof(uint64_t));
        mkillPlayBackThread = true;
        pthread_join(mPlaybackUsb,NULL);
    }
}

void AudioUsbALSA::exitRecordingThread(uint64_t writeVal)
{
    LOGD("exitRecordingThread");
    if (writeVal == SIGNAL_EVENT_KILLTHREAD) {
        closeRecordingDevices();
    }
    mkillRecordingThread = true;
}

void AudioUsbALSA::closeRecordingDevices(){
    int err;

    err = closeDevice(mproxyRecordingHandle);
    if (err) {
        LOGE("Info: Could not close proxy for recording %p", mproxyRecordingHandle);
    }
    err = closeDevice(musbRecordingHandle);
    if (err) {
        LOGE("Info: Could not close USB recording device %p", musbRecordingHandle);
    }
}

void AudioUsbALSA::closePlaybackDevices(){
    int err;

    err = closeDevice(mproxyPlaybackHandle);
    if (err) {
        LOGE("Info: Could not close proxy %p", mproxyPlaybackHandle);
    }
    err = closeDevice(musbPlaybackHandle);
    if (err) {
        LOGE("Info: Could not close USB device %p", musbPlaybackHandle);
    }
}

void AudioUsbALSA::setkillUsbRecordingThread(bool val){
    LOGD("setkillUsbRecordingThread");
    mkillRecordingThread = val;
}

status_t AudioUsbALSA::setHardwareParams(pcm *txHandle, uint32_t sampleRate, uint32_t channels)
{
    LOGD("setHardwareParams");
    struct snd_pcm_hw_params *params;
    unsigned long bufferSize, reqBuffSize;
    unsigned int periodTime, bufferTime;
    unsigned int requestedRate = sampleRate;
    int status = 0;

    params = (snd_pcm_hw_params*) calloc(1, sizeof(struct snd_pcm_hw_params));
    if (!params) {
        return NO_INIT;
    }

    param_init(params);
    param_set_mask(params, SNDRV_PCM_HW_PARAM_ACCESS,
                   SNDRV_PCM_ACCESS_MMAP_INTERLEAVED);
    param_set_mask(params, SNDRV_PCM_HW_PARAM_FORMAT,
                   SNDRV_PCM_FORMAT_S16_LE);
    param_set_mask(params, SNDRV_PCM_HW_PARAM_SUBFORMAT,
                   SNDRV_PCM_SUBFORMAT_STD);
    LOGV("Setting period size: 768 samplerate:%d, channels: %d",sampleRate, channels);
    param_set_min(params, SNDRV_PCM_HW_PARAM_PERIOD_BYTES, 768);
    param_set_int(params, SNDRV_PCM_HW_PARAM_SAMPLE_BITS, 16);
    param_set_int(params, SNDRV_PCM_HW_PARAM_FRAME_BITS,
                  channels - 1 ? 32 : 16);
    param_set_int(params, SNDRV_PCM_HW_PARAM_CHANNELS,
                  channels);
    param_set_int(params, SNDRV_PCM_HW_PARAM_RATE, sampleRate);
    param_set_hw_refine(txHandle, params);

    if (param_set_hw_params(txHandle, params)) {
        LOGE("ERROR: cannot set hw params");
        return NO_INIT;
    }

    param_dump(params);

    txHandle->period_size = pcm_period_size(params);
    txHandle->buffer_size = pcm_buffer_size(params);
    txHandle->period_cnt = txHandle->buffer_size/txHandle->period_size;

    LOGD("setHardwareParams: buffer_size %d, period_size %d, period_cnt %d",
         txHandle->buffer_size, txHandle->period_size,
         txHandle->period_cnt);

    return NO_ERROR;
}

status_t AudioUsbALSA::setSoftwareParams(pcm *pcm)
{
    LOGD("setSoftwareParams");
    struct snd_pcm_sw_params* params;

    unsigned long periodSize = 1024;

    params = (snd_pcm_sw_params*) calloc(1, sizeof(struct snd_pcm_sw_params));
    if (!params) {
        LOG_ALWAYS_FATAL("Failed to allocate ALSA software parameters!");
        return NO_INIT;
    }

    params->tstamp_mode = SNDRV_PCM_TSTAMP_NONE;
    params->period_step = 1;

    params->avail_min = (pcm->flags & PCM_MONO) ? pcm->period_size/2 : pcm->period_size/4;

    params->start_threshold = (pcm->flags & PCM_MONO) ? pcm->period_size/2 : pcm->period_size/4;
    params->stop_threshold = pcm->buffer_size;
    params->xfer_align = (pcm->flags & PCM_MONO) ? pcm->period_size/2 : pcm->period_size/4;
    params->silence_size = 0;
    params->silence_threshold = 0;

    if (param_set_sw_params(pcm, params)) {
        LOGE("ERROR: cannot set sw params");
        return NO_INIT;
    }

    return NO_ERROR;
}

status_t AudioUsbALSA::closeDevice(pcm *handle)
{
    LOGD("closeDevice handle %p", handle);
    status_t err = NO_ERROR;
    if (handle) {
        err = pcm_close(handle);
        if (err != NO_ERROR) {
            LOGE("INFO: closeDevice: pcm_close failed with err %d", err);
        }
    }
    handle = NULL;
    return err;
}

void AudioUsbALSA::RecordingThreadEntry() {
    LOGD("Inside RecordingThreadEntry");
    int nfds = 1;
    mtimeOutRecording = TIMEOUT_INFINITE;
    int fd;
    long frames;
    static int start = 0;
    struct snd_xferi x;
    int filed;
    unsigned avail, bufsize;
    int bytes_written;
    uint32_t sampleRate;
    uint32_t channels;
    u_int8_t *srcUsb_addr = NULL;
    u_int8_t *dstProxy_addr = NULL;
    int err;
    const char *fn = "/data/RecordPcm.pcm";
    filed = open(fn, O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0664);

    err = configureUsbDeviceForRecording();
    if (err) {
        LOGE("ERROR: Could not configure USB device for recording");
        closeDevice(musbRecordingHandle);
        return;
    } else {
        LOGD("USB device Configured for recording");
    }

    pfdUsbRecording[0].fd = musbRecordingHandle->fd;                           //DEBUG
    pfdUsbRecording[0].events = POLLIN;

    err = configureProxyDeviceForRecording();
    if (err) {
        LOGE("ERROR: Could not configure Proxy for recording");
        closeDevice(mproxyRecordingHandle);
        closeDevice(musbRecordingHandle);
        return;
    } else {
        LOGD("Proxy Configured for recording");
    }

    bufsize = musbRecordingHandle->period_size;
    pfdProxyRecording[0].fd = mproxyRecordingHandle->fd;
    pfdProxyRecording[0].events = POLLOUT;
    frames = (musbRecordingHandle->flags & PCM_MONO) ? (bufsize / 2) : (bufsize / 4);
    x.frames = (musbRecordingHandle->flags & PCM_MONO) ? (bufsize / 2) : (bufsize / 4);

    /***********************keep reading from usb and writing to proxy******************************************/
    while (mkillRecordingThread != true) {
        if (!musbRecordingHandle->running) {
            if (pcm_prepare(musbRecordingHandle)) {
                LOGE("ERROR: pcm_prepare failed for usb device for recording");
                mkillRecordingThread = true;
                break;;
            }
        }
        if (!mproxyRecordingHandle->running) {
            if (pcm_prepare(mproxyRecordingHandle)) {
                LOGE("ERROR: pcm_prepare failed for proxy device for recording");
                mkillRecordingThread = true;
                break;;
            }
        }

        /********** USB syncing before write **************/
        if (!musbRecordingHandle->start && !mkillRecordingThread) {
            err = startDevice(musbRecordingHandle, &mkillRecordingThread);
            if (err == EPIPE) {
                continue;
            } else if (err != NO_ERROR) {
                mkillRecordingThread = true;
                break;
            }
        }
        for (;;) {
            if (!musbRecordingHandle->running) {
                if (pcm_prepare(musbRecordingHandle)) {
                    LOGE("ERROR: pcm_prepare failed for proxy device for recording");
                    mkillRecordingThread = true;
                    break;
                }
            }
            /* Sync the current Application pointer from the kernel */
            musbRecordingHandle->sync_ptr->flags = SNDRV_PCM_SYNC_PTR_APPL |
                                                   SNDRV_PCM_SYNC_PTR_AVAIL_MIN;

            err = syncPtr(musbRecordingHandle, &mkillRecordingThread);
            if (err == EPIPE) {
                continue;
            } else if (err != NO_ERROR) {
                break;
            }

            avail = pcm_avail(musbRecordingHandle);
            if (avail < musbRecordingHandle->sw_p->avail_min) {
                poll(pfdUsbRecording, nfds, TIMEOUT_INFINITE);
                continue;
            } else {
                break;
            }
        }
        if (mkillRecordingThread) {
            break;
        }
        if (x.frames > avail)
            frames = avail;

        srcUsb_addr = dst_address(musbRecordingHandle);
        /**********End USB syncing before write**************/

        /*************Proxy syncing before write ******************/

        for (;;) {
            if (!mproxyRecordingHandle->running) {
                if (pcm_prepare(mproxyRecordingHandle)) {
                    LOGE("ERROR: pcm_prepare failed for proxy device for recording");
                    mkillRecordingThread = true;
                    break;
                }
            }
            mproxyRecordingHandle->sync_ptr->flags = SNDRV_PCM_SYNC_PTR_APPL |
                                                     SNDRV_PCM_SYNC_PTR_AVAIL_MIN;

            err = syncPtr(mproxyRecordingHandle, &mkillRecordingThread);
            if (err == EPIPE) {
                continue;
            } else if (err != NO_ERROR) {
                break;
            }
            avail = pcm_avail(mproxyRecordingHandle);
            if (avail < mproxyRecordingHandle->sw_p->avail_min) {
                poll(pfdProxyRecording, nfds, TIMEOUT_INFINITE);
                continue;
            } else {
                break;
            }
        }
        if (mkillRecordingThread) {
            break;
        }

        dstProxy_addr = dst_address(mproxyRecordingHandle);
        memset(dstProxy_addr, 0x0, bufsize);

        /**************End Proxy syncing before write *************/

        memcpy(dstProxy_addr, srcUsb_addr, bufsize );

        /************* sync up after write -- USB  *********************/
        musbRecordingHandle->sync_ptr->c.control.appl_ptr += frames;
        musbRecordingHandle->sync_ptr->flags = 0;
        err = syncPtr(musbRecordingHandle, &mkillRecordingThread);
        if (err == EPIPE) {
            continue;
        } else if (err != NO_ERROR) {
            break;
        }

        /************* end sync up after write -- USB *********************/

        /**************** sync up after write -- Proxy  ************************/
        mproxyRecordingHandle->sync_ptr->c.control.appl_ptr += frames;
        mproxyRecordingHandle->sync_ptr->flags = 0;

        err = syncPtr(mproxyRecordingHandle, &mkillRecordingThread);
        if (err == EPIPE) {
            continue;
        } else if (err != NO_ERROR) {
            break;
        }

        bytes_written = mproxyRecordingHandle->sync_ptr->c.control.appl_ptr - mproxyRecordingHandle->sync_ptr->s.status.hw_ptr;
        if ((bytes_written >= mproxyRecordingHandle->sw_p->start_threshold) && (!mproxyRecordingHandle->start)) {
            if (!mkillPlayBackThread) {
                err = startDevice(mproxyRecordingHandle, &mkillRecordingThread);
                if (err == EPIPE) {
                    continue;
                } else if (err != NO_ERROR) {
                    mkillRecordingThread = true;
                    break;
                }
            }
        }
    }
    /***************  End sync up after write -- Proxy *********************/
    if (mkillRecordingThread) {
        closeDevice(mproxyRecordingHandle);
        closeDevice(musbRecordingHandle);
    }
    LOGD("Exiting USB Recording thread");
}

void *AudioUsbALSA::PlaybackThreadWrapper(void *me) {
    static_cast<AudioUsbALSA *>(me)->PlaybackThreadEntry();
    return NULL;
}

void *AudioUsbALSA::RecordingThreadWrapper(void *me) {
    static_cast<AudioUsbALSA *>(me)->RecordingThreadEntry();
    return NULL;
}

status_t AudioUsbALSA::configureUsbDevice(){
    unsigned flags = 0;
    int err = NO_ERROR;

    flags = PCM_OUT|PCM_STEREO|PCM_MMAP;

    musbPlaybackHandle = pcm_open(flags, (char *)"hw:1,0");
    if (!musbPlaybackHandle) {
        LOGE("ERROR: pcm_open failed for usb playback case");
        return UNKNOWN_ERROR;
    }

    if (!pcm_ready(musbPlaybackHandle)) {
        LOGE("ERROR: pcm_ready failed for usb playback case");
        return err;
    }

    err = getPlaybackCap();
    if (err) {
        LOGE("ERROR: Could not get playback capabilities from usb device");
        return UNKNOWN_ERROR;
    }

    LOGD("Setting hardware params: sampleRate:%d, channels: %d",msampleRatePlayback, mchannelsPlayback);
    err = setHardwareParams(musbPlaybackHandle, msampleRatePlayback, mchannelsPlayback);
    if (err != NO_ERROR) {
        LOGE("ERROR: setHardwareParams failed for usb playback case");
        return err;
    }

    err = setSoftwareParams(musbPlaybackHandle);
    if (err != NO_ERROR) {
        LOGE("ERROR: setSoftwareParams failed for usb playback case");
        return err;
    }

    err = mmap_buffer(musbPlaybackHandle);
    if (err) {
        LOGE("ERROR: mmap_buffer failed for usb playback case");
        return err;
    }

    err = pcm_prepare(musbPlaybackHandle);
    if (err) {
        LOGE("ERROR: pcm_prepare failed for usb playback case");
        return err;
    }

    return err;
}

status_t AudioUsbALSA::configureUsbDeviceForRecording(){
    unsigned flags = 0;
    int err = NO_ERROR;

    flags = PCM_IN|PCM_MONO|PCM_MMAP;

    musbRecordingHandle = pcm_open(flags, (char *)"hw:1,0");
    if (!musbRecordingHandle) {
        LOGE("ERROR: pcm_open failed for usb recording case");
        return UNKNOWN_ERROR;
    }

    if (!pcm_ready(musbRecordingHandle)) {
        LOGE("ERROR: pcm_ready failed for usb recording case");
        return err;
    }

    err = getCaptureCap();
    if (err) {
        LOGE("ERROR: Could not get capture capabilities from usb device");
        return UNKNOWN_ERROR;
    }

    LOGD("Setting hardwareParams for Usb recording msampleRateCapture %d, mchannelsCapture %d", msampleRateCapture, mchannelsCapture);
    err = setHardwareParams(musbRecordingHandle, msampleRateCapture, mchannelsCapture);
    if (err != NO_ERROR) {
        LOGE("ERROR: setHardwareParams failed for usb recording case");
        return err;
    }

    err = setSoftwareParams(musbRecordingHandle);
    if (err != NO_ERROR) {
        LOGE("ERROR: setSoftwareParams failed for usb recording case");
        return err;
    }

    err = mmap_buffer(musbRecordingHandle);
    if (err) {
        LOGE("ERROR: mmap_buffer failed for usb recording case");
        return err;
    }

    err = pcm_prepare(musbRecordingHandle);
    if (err) {
        LOGE("ERROR: pcm_prepare failed for usb recording case");
        return err;
    }

    return err;
}

status_t AudioUsbALSA::configureProxyDeviceForRecording(){
    unsigned flags = 0;
    int err = 0;
    flags = PCM_OUT|PCM_MONO|PCM_MMAP;
    mproxyRecordingHandle = pcm_open(flags, (char *)"hw:0,7");
    if (!mproxyRecordingHandle) {
        LOGE("ERROR: pcm_open failed for proxy recording case");
        return UNKNOWN_ERROR;
    }

    if (!pcm_ready(mproxyRecordingHandle)) {
        LOGE("ERROR: pcm_ready failed for proxy recording case");
        return UNKNOWN_ERROR;
    }

    err = setHardwareParams(mproxyRecordingHandle, msampleRateCapture, mchannelsCapture);
    if (err != NO_ERROR) {
        LOGE("ERROR: setHardwareParams failed for proxy recording case");
        return err;
    }

    err = setSoftwareParams(mproxyRecordingHandle);
    if (err != NO_ERROR) {
        LOGE("ERROR: setSoftwareParams failed for proxy recording case");
        return err;
    }

    err = mmap_buffer(mproxyRecordingHandle);
    if (err != NO_ERROR) {
        LOGE("ERROR: mmap_buffer failed for proxy recording case");
        return err;
    }

    err = pcm_prepare(mproxyRecordingHandle);
    if (err != NO_ERROR) {
        LOGE("ERROR: pcm_prepare failed for proxy recording case");
        return err;
    }

    return err;
}

status_t AudioUsbALSA::configureProxyDevice(){
    unsigned flags = 0;
    int err = 0;
    flags = PCM_IN|PCM_STEREO|PCM_MMAP;
    mproxyPlaybackHandle = pcm_open(flags, (char *)"hw:0,8");
    if (!mproxyPlaybackHandle) {
        LOGE("ERROR: pcm_open failed for proxy playback case");
        return UNKNOWN_ERROR;
    }

    if (!pcm_ready(mproxyPlaybackHandle)) {
        LOGE("ERROR: pcm_ready failed for proxy playback case");
        return err;
    }

    err = setHardwareParams(mproxyPlaybackHandle, msampleRatePlayback, mchannelsPlayback);
    if (err != NO_ERROR) {
        LOGE("ERROR: setHardwareParams failed for proxy playback case");
        return err;;
    }

    err = setSoftwareParams(mproxyPlaybackHandle);
    if (err != NO_ERROR) {
        LOGE("ERROR: setSoftwareParams failed for proxy playback case");
        return err;
    }

    err = mmap_buffer(mproxyPlaybackHandle);
    if (err != NO_ERROR) {
        LOGE("ERROR: mmap_buffer failed for proxy playback case");
        return err;
    }

    err = pcm_prepare(mproxyPlaybackHandle);
    if (err != NO_ERROR) {
        LOGE("ERROR: pcm_prepare failed for proxy playback case");
        return err;
    }

    return err;
}

status_t AudioUsbALSA::startDevice(pcm *handle, bool *killThread) {
    int err = NO_ERROR;;
    if (ioctl(handle->fd, SNDRV_PCM_IOCTL_START)) {
        err = -errno;
        if (errno == EPIPE) {
            LOGE("ERROR: SNDRV_PCM_IOCTL_START returned EPIPE for usb recording case");
            handle->underruns++;
            handle->running = 0;
            handle->start = 0;
            return errno;
        } else {
            LOGE("ERROR: SNDRV_PCM_IOCTL_START failed for usb recording case errno:%d", errno);
            *killThread = true;
            return errno;
        }
    }
    handle->start = 1;
    if (handle == musbRecordingHandle) {
        LOGD("Usb Driver started for recording");
    } else if (handle == mproxyRecordingHandle) {
        LOGD("Proxy Driver started for recording");
    } else if (handle == musbPlaybackHandle) {
        LOGD("Usb Driver started for playback");
    } else if (handle == mproxyPlaybackHandle) {
        LOGD("proxy Driver started for playback");
    }
    return NO_ERROR;
}

status_t AudioUsbALSA::syncPtr(struct pcm *handle, bool *killThread) {
    int err;
    err = sync_ptr(handle);
    if (err == EPIPE) {
        LOGE("ERROR: Failed in sync_ptr \n");
        handle->running = 0;
        handle->underruns++;
        handle->start = 0;
    } else if (err == ENODEV) {
        LOGE("Info: Device not available");
    } else if (err != NO_ERROR) {
        LOGE("ERROR: Sync ptr returned %d", err);
        *killThread = true;
    }
    return err;
}

void AudioUsbALSA::pollForProxyData(){
    int err_poll = poll(pfdProxyPlayback, mnfdsPlayback, mtimeOut);
    if (err_poll == 0 ) {
        LOGD("POLL timedout");
        mkillPlayBackThread = true;
        pfdProxyPlayback[0].revents = 0;
        pfdProxyPlayback[1].revents = 0;
        return;
    }

    if (pfdProxyPlayback[1].revents & POLLIN) {
        LOGD("Signalled from HAL about timeout");
        uint64_t u;
        read(mproxypfdPlayback, &u, sizeof(uint64_t));
        pfdProxyPlayback[1].revents = 0;
        if (u == SIGNAL_EVENT_KILLTHREAD) {
            LOGD("kill thread event");
            mkillPlayBackThread = true;
            pfdProxyPlayback[0].revents = 0;
            pfdProxyPlayback[1].revents = 0;
            return;
        } else if (u == SIGNAL_EVENT_TIMEOUT) {
            LOGD("Setting timeout for 3 sec");
            mtimeOut = POLL_TIMEOUT;
        }
    } else if (pfdProxyPlayback[1].revents & POLLERR || pfdProxyPlayback[1].revents & POLLHUP ||
               pfdProxyPlayback[1].revents & POLLNVAL) {
        LOGE("Info: proxy throwing error from location 1");
        mkillPlayBackThread = true;
        pfdProxyPlayback[0].revents = 0;
        pfdProxyPlayback[1].revents = 0;
        return;
    }

    if (pfdProxyPlayback[0].revents & POLLERR || pfdProxyPlayback[0].revents & POLLHUP ||
        pfdProxyPlayback[0].revents & POLLNVAL) {
        LOGE("Info: proxy throwing error");
        mkillPlayBackThread = true;
        pfdProxyPlayback[0].revents = 0;
        pfdProxyPlayback[1].revents = 0;
    }
}

void AudioUsbALSA::pollForUsbData(){
    int err_poll = poll(pfdUsbPlayback, mnfdsPlayback, mtimeOut);
    if (err_poll == 0 ) {
        LOGD("POLL timedout");
        mkillPlayBackThread = true;
        pfdUsbPlayback[0].revents = 0;
        pfdUsbPlayback[1].revents = 0;
        return;
    }

    if (pfdUsbPlayback[1].revents & POLLIN) {
        LOGD("Info: Signalled from HAL about an event");
        uint64_t u;
        read(musbpfdPlayback, &u, sizeof(uint64_t));
        pfdUsbPlayback[0].revents = 0;
        pfdUsbPlayback[1].revents = 0;
        if (u == SIGNAL_EVENT_KILLTHREAD) {
            LOGD("kill thread");
            mkillPlayBackThread = true;
            return;
        } else if (u == SIGNAL_EVENT_TIMEOUT) {
            LOGD("Setting timeout for 3 sec");
            mtimeOut = POLL_TIMEOUT;
        }
    } else if (pfdUsbPlayback[1].revents & POLLERR || pfdUsbPlayback[1].revents & POLLHUP ||
               pfdUsbPlayback[1].revents & POLLNVAL) {
        LOGE("Info: usb throwing error from location 1");
        mkillPlayBackThread = true;
        pfdUsbPlayback[0].revents = 0;
        pfdUsbPlayback[1].revents = 0;
        return;
    }

    if (pfdUsbPlayback[0].revents & POLLERR || pfdProxyPlayback[0].revents & POLLHUP ||
        pfdUsbPlayback[0].revents & POLLNVAL) {
        LOGE("Info: usb throwing error");
        mkillPlayBackThread = true;
        pfdUsbPlayback[0].revents = 0;
        return;
    }
}

void AudioUsbALSA::PlaybackThreadEntry() {
    LOGD("PlaybackThreadEntry");
    mnfdsPlayback = 2;
    mtimeOut = TIMEOUT_INFINITE;
    long frames;
    static int fd;
    struct snd_xferi x;
    int bytes_written;
    unsigned avail, xfer, bufsize;
    uint32_t sampleRate;
    uint32_t channels;
    unsigned int tmp;
    int numOfBytesWritten;
    int err;
    int filed;
    const char *fn = "/data/test.pcm";
    mdstUsb_addr = NULL;
    msrcProxy_addr = NULL;

    filed = open(fn, O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0664);
    err = configureUsbDevice();
    if (err) {
        LOGE("ERROR: configureUsbDevice failed, returning");
        closeDevice(musbPlaybackHandle);
        return;
    } else {
        LOGD("USB Configured for playback");
    }

    if (!mkillPlayBackThread) {
        pfdUsbPlayback[0].fd = musbPlaybackHandle->fd;
        pfdUsbPlayback[0].events = POLLOUT;
        musbpfdPlayback = eventfd(0,0);
        pfdUsbPlayback[1].fd = musbpfdPlayback;
        pfdUsbPlayback[1].events = (POLLIN | POLLOUT | POLLERR | POLLNVAL | POLLHUP);
    }

    err = configureProxyDevice();
    if (err) {
        LOGE("ERROR: Could not configure Proxy, returning");
        closeDevice(musbPlaybackHandle);
        closeDevice(mproxyPlaybackHandle);
        return;
    } else {
        LOGD("Proxy Configured for playback");
    }

    bufsize = mproxyPlaybackHandle->period_size;

    if (!mkillPlayBackThread) {
        pfdProxyPlayback[0].fd = mproxyPlaybackHandle->fd;
        pfdProxyPlayback[0].events = (POLLIN);                                 // | POLLERR | POLLNVAL);
        mproxypfdPlayback = eventfd(0,0);
        pfdProxyPlayback[1].fd = mproxypfdPlayback;
        pfdProxyPlayback[1].events = (POLLIN | POLLOUT| POLLERR | POLLNVAL);
    }

    frames = (mproxyPlaybackHandle->flags & PCM_MONO) ? (bufsize / 2) : (bufsize / 4);
    x.frames = (mproxyPlaybackHandle->flags & PCM_MONO) ? (bufsize / 2) : (bufsize / 4);

    /***********************keep reading from proxy and writing to USB******************************************/
    while (mkillPlayBackThread != true) {
        if (!mproxyPlaybackHandle->running) {
            if (pcm_prepare(mproxyPlaybackHandle)) {
                LOGE("ERROR: pcm_prepare failed for proxy");
                mkillPlayBackThread = true;
                break;
            }
        }
        if (!musbPlaybackHandle->running) {
            if (pcm_prepare(musbPlaybackHandle)) {
                LOGE("ERROR: pcm_prepare failed for usb");
                mkillPlayBackThread = true;
                break;
            }
        }

        /********** Proxy syncing before write **************/
        if (!mkillPlayBackThread && (!mproxyPlaybackHandle->start)) {
            err = startDevice(mproxyPlaybackHandle, &mkillPlayBackThread);
            if (err == EPIPE) {
                continue;
            } else if (err != NO_ERROR) {
                mkillPlayBackThread = true;
                break;
            }
        }

        for (;;) {
            if (!mproxyPlaybackHandle->running) {
                if (pcm_prepare(mproxyPlaybackHandle)) {
                    LOGE("ERROR: pcm_prepare failed for proxy");
                    mkillPlayBackThread = true;
                    break;
                }
            }
            /* Sync the current Application pointer from the kernel */
            mproxyPlaybackHandle->sync_ptr->flags = SNDRV_PCM_SYNC_PTR_APPL |
                                                    SNDRV_PCM_SYNC_PTR_AVAIL_MIN;

            if (mtimeOut == TIMEOUT_INFINITE && !mkillPlayBackThread) {
                err = syncPtr(mproxyPlaybackHandle, &mkillPlayBackThread);
                if (err == EPIPE) {
                    continue;
                } else if (err != NO_ERROR) {
                    break;
                }
                avail = pcm_avail(mproxyPlaybackHandle);
            }
            if (avail < mproxyPlaybackHandle->sw_p->avail_min && !mkillPlayBackThread) {
                pollForProxyData();
                //if polling returned some error
                if (!mkillPlayBackThread) {
                    continue;
                } else {
                    break;
                }
            } else {                                                           //Got some data or mkillPlayBackThread is true
                break;
            }
        }
        if (mkillPlayBackThread) {
            break;
        }

        if (x.frames > avail)
            frames = avail;

        if (!mkillPlayBackThread) {
            msrcProxy_addr = dst_address(mproxyPlaybackHandle);
            /**********End Proxy syncing before write**************/
        }

        for (;;) {
            if (!musbPlaybackHandle->running) {
                if (pcm_prepare(musbPlaybackHandle)) {
                    LOGE("ERROR: pcm_prepare failed for usb");
                    mkillPlayBackThread = true;
                    break;
                }
            }
            /*************USB syncing before write ******************/
            musbPlaybackHandle->sync_ptr->flags = SNDRV_PCM_SYNC_PTR_APPL |
                                                  SNDRV_PCM_SYNC_PTR_AVAIL_MIN;
            if (mtimeOut == TIMEOUT_INFINITE && !mkillPlayBackThread) {
                err = syncPtr(musbPlaybackHandle, &mkillPlayBackThread);
                if (err == EPIPE) {
                    continue;
                } else if (err != NO_ERROR) {
                    break;
                }
                avail = pcm_avail(musbPlaybackHandle);
                //LOGV("Avail USB is: %d", avail);
            }

            if (avail < musbPlaybackHandle->sw_p->avail_min && !mkillPlayBackThread) {
                pollForUsbData();
                if (!mkillPlayBackThread) {
                    continue;
                } else {
                    break;
                }
            } else {
                break;
            }
        }
        if (mkillPlayBackThread) {
            break;
        }

        if (!mkillPlayBackThread) {
            mdstUsb_addr = dst_address(musbPlaybackHandle);
            memset(mdstUsb_addr, 0x0, bufsize);

            /**************End USB syncing before write *************/

            memcpy(mdstUsb_addr, msrcProxy_addr, bufsize );

            /************* sync up after write -- Proxy  *********************/
            x.frames -= frames;
            mproxyPlaybackHandle->sync_ptr->c.control.appl_ptr += frames;
            mproxyPlaybackHandle->sync_ptr->flags = 0;
        }

        if (!mkillPlayBackThread) {
            err = syncPtr(mproxyPlaybackHandle, &mkillPlayBackThread);
            if (err == EPIPE) {
                continue;
            } else if (err != NO_ERROR) {
                break;
            }
        }
        /************* end sync up after write -- Proxy *********************/


        /**************** sync up after write -- USB  ************************/
        musbPlaybackHandle->sync_ptr->c.control.appl_ptr += frames;
        musbPlaybackHandle->sync_ptr->flags = 0;
        if (!mkillPlayBackThread) {
            err = syncPtr(musbPlaybackHandle, &mkillPlayBackThread);
            if (err == EPIPE) {
                continue;
            } else if (err != NO_ERROR) {
                break;
            }
        }

        bytes_written = musbPlaybackHandle->sync_ptr->c.control.appl_ptr - musbPlaybackHandle->sync_ptr->s.status.hw_ptr;
        if ((bytes_written >= musbPlaybackHandle->sw_p->start_threshold) && (!musbPlaybackHandle->start)) {
            if (!mkillPlayBackThread) {
                err = startDevice(musbPlaybackHandle, &mkillPlayBackThread);
                if (err == EPIPE) {
                    continue;
                } else if (err != NO_ERROR) {
                    mkillPlayBackThread = true;
                    break;
                }
            }
        }
        /***************  End sync up after write -- USB *********************/
    }
    if (mkillPlayBackThread) {
        mproxypfdPlayback = -1;
        musbpfdPlayback = -1;
        closeDevice(mproxyPlaybackHandle);
        closeDevice(musbPlaybackHandle);
    }
    LOGD("Exiting USB Playback Thread");
}

void AudioUsbALSA::startPlayback()
{
    mkillPlayBackThread = false;
    LOGD("Creating USB Playback Thread");
    pthread_create(&mPlaybackUsb, NULL, PlaybackThreadWrapper, this);
}

void AudioUsbALSA::startRecording()
{
    //create Thread
    mkillRecordingThread = false;
    LOGV("Creating USB recording Thread");
    pthread_create(&mRecordingUsb, NULL, RecordingThreadWrapper, this);
}
}
