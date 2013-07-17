/* AudioUsbALSA.cpp

Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.
    * Neither the name of The Linux Foundation nor the names of its
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
#define LOG_NDEBUG 0
#define LOG_NDDEBUG 0
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
struct pollfd pfdProxyRecording[2];
struct pollfd pfdUsbRecording[2];

#define USB_PERIOD_SIZE 2048
#define PROXY_PERIOD_SIZE 3072
#define PROXY_SUPPORTED_RATE_8000 8000
#define PROXY_SUPPORTED_RATE_16000 16000
#define PROXY_SUPPORTED_RATE_48000 48000
#define AFE_PROXY_PERIOD_COUNT 32
//#define OUTPUT_PROXY_BUFFER_LOG
//#define OUTPUT_RECORD_PROXY_BUFFER_LOG
#ifdef OUTPUT_PROXY_BUFFER_LOG
    FILE *outputBufferFile1;
    char outputfilename [256] = "/data/usb_output_proxy";
    char outputfilename1 [256] = "";
    static int number = 0;
#endif

#ifdef OUTPUT_RECORD_PROXY_BUFFER_LOG
    FILE *outputRecordBufferFile1;
    char outputRecordfilename [256] = "/data/usb_record";
    char outputRecordfilename1 [256] = "";

    static int rNumber = 0;
#endif
#define USB_CARDCONTROL_PATH "/dev/snd/controlC1"

namespace android_audio_legacy
{
AudioUsbALSA::AudioUsbALSA()
{
    mproxypfdPlayback = -1;
    musbpfdPlayback = -1;
    mkillPlayBackThread = false;
    mkillRecordingThread = false;
    musbRecordingHandle = NULL;
    mproxyRecordingHandle = NULL;
    musbPlaybackHandle  = NULL;
    mproxyPlaybackHandle = NULL;
    mProxySoundCard = 0;
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
        ALOGE("ERROR: getnumOfRates: could not find rates string");
        return NULL;
    }
    for (i = 1; nextSRString != NULL; i++) {
        size ++;
        nextSRString = strtok_r(NULL, " ,.-", &temp_ptr);
    }
    return size;
}


status_t AudioUsbALSA::getCap(char * type, int &channels, int &sampleRate)
{
    ALOGD("getCap for %s",type);
    long unsigned fileSize;
    FILE *fp;
    char *buffer;
    int err = 1;
    int size = 0;
    int fd, i, lchannelsPlayback;
    char *read_buf, *str_start, *channel_start, *ratesStr, *ratesStrForVal,
    *ratesStrStart, *chString, *nextSRStr, *test, *nextSRString, *temp_ptr;
    struct stat st;
    memset(&st, 0x0, sizeof(struct stat));
    sampleRate = 0;
    fd = open(PATH, O_RDONLY);
    if (fd <0) {
        ALOGE("ERROR: failed to open config file %s error: %d\n", PATH, errno);
        close(fd);
        return UNKNOWN_ERROR;
    }

    if (fstat(fd, &st) < 0) {
        ALOGE("ERROR: failed to stat %s error %d\n", PATH, errno);
        close(fd);
        return UNKNOWN_ERROR;
    }

    fileSize = st.st_size;

    if ((read_buf = (char *)malloc(BUFFSIZE)) == NULL) {
        ALOGE("ERROR: Unable to allocate memory to hold stream caps");
        close(fd);
        return NO_MEMORY;
    }
    memset(read_buf, 0x0, BUFFSIZE);
    err = read(fd, read_buf, BUFFSIZE);
    str_start = strstr(read_buf, type);
    if (str_start == NULL) {
        ALOGE("ERROR:%s section not found in usb config file", type);
        close(fd);
        free(read_buf);
        return UNKNOWN_ERROR;
    }

    channel_start = strstr(str_start, "Channels:");
    if (channel_start == NULL) {
        ALOGE("ERROR: Could not find Channels information");
        close(fd);
        free(read_buf);
        return UNKNOWN_ERROR;
    }
    channel_start = strstr(channel_start, " ");
    if (channel_start == NULL) {
        ALOGE("ERROR: Channel section not found in usb config file");
        close(fd);
        free(read_buf);
        return UNKNOWN_ERROR;
    }

    lchannelsPlayback = atoi(channel_start);
    if (lchannelsPlayback == 1) {
        channels = 1;
    } else {
        channels = 2;
    }
    ALOGD("channels supported by device: %d", lchannelsPlayback);
    ratesStrStart = strstr(str_start, "Rates:");
    if (ratesStrStart == NULL) {
        ALOGE("ERROR: Cant find rates information");
        close(fd);
        free(read_buf);
        return UNKNOWN_ERROR;
    }

    ratesStrStart = strstr(ratesStrStart, " ");
    if (ratesStrStart == NULL) {
        ALOGE("ERROR: Channel section not found in usb config file");
        close(fd);
        free(read_buf);
        return UNKNOWN_ERROR;
    }

    //copy to ratesStr, current line.
    char *target = strchr(ratesStrStart, '\n');
    if (target == NULL) {
        ALOGE("ERROR: end of line not found");
        close(fd);
        free(read_buf);
        return UNKNOWN_ERROR;
    }
    size = target - ratesStrStart;
    if ((ratesStr = (char *)malloc(size + 1)) == NULL) {
        ALOGE("ERROR: Unable to allocate memory to hold sample rate strings");
        close(fd);
        free(read_buf);
        return NO_MEMORY;
    }
    if ((ratesStrForVal = (char *)malloc(size + 1)) == NULL) {
        ALOGE("ERROR: Unable to allocate memory to hold sample rate string");
        close(fd);
        free(ratesStr);
        free(read_buf);
        return NO_MEMORY;
    }
    memcpy(ratesStr, ratesStrStart, size);
    memcpy(ratesStrForVal, ratesStrStart, size);
    ratesStr[size] = '\0';
    ratesStrForVal[size] = '\0';

    size = getnumOfRates(ratesStr);
    if (!size) {
        ALOGE("ERROR: Could not get rate size, returning");
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
        ALOGE("ERROR: Could not get first rate val");
        close(fd);
        free(ratesStrForVal);
        free(ratesStr);
        free(read_buf);
        return UNKNOWN_ERROR;
    }

    ratesSupported[0] = atoi(nextSRString);
    ALOGV("ratesSupported[0] for playback: %d", ratesSupported[0]);
    for (i = 1; i<size; i++) {
        nextSRString = strtok_r(NULL, " ,.-", &temp_ptr);
        ratesSupported[i] = atoi(nextSRString);
        ALOGD("ratesSupported[%d] for playback: %d",i, ratesSupported[i]);
    }

    for (i = 0; i<size; i++) {
        if ((ratesSupported[i] > sampleRate) && (ratesSupported[i] <= 48000)) {
            // Sample Rate should be one of the proxy supported rates only
            // This is because proxy port is used to read from/write to DSP .
            if ((ratesSupported[i] == PROXY_SUPPORTED_RATE_8000) ||
                (ratesSupported[i] == PROXY_SUPPORTED_RATE_16000) ||
                (ratesSupported[i] == PROXY_SUPPORTED_RATE_48000)) {
                sampleRate = ratesSupported[i];
            }
        }
    }
    ALOGD("sampleRate: %d", sampleRate);

    close(fd);
    free(ratesStrForVal);
    free(ratesStr);
    free(read_buf);
    ratesStrForVal = NULL;
    ratesStr = NULL;
    read_buf = NULL;
    return NO_ERROR;
}

void AudioUsbALSA::exitPlaybackThread(uint64_t writeVal)
{
#ifdef OUTPUT_PROXY_BUFFER_LOG
    ALOGV("close file output");
    if(outputBufferFile1)
        fclose (outputBufferFile1);
#endif
    {
        Mutex::Autolock autoLock(mLock);
        ALOGD("exitPlaybackThread, mproxypfdPlayback: %d", mproxypfdPlayback);
        mkillPlayBackThread = true;
        if ((mproxypfdPlayback != -1) && (musbpfdPlayback != -1)) {
            write(mproxypfdPlayback, &writeVal, sizeof(uint64_t));
            write(musbpfdPlayback, &writeVal, sizeof(uint64_t));
        }
    }
    if(mPlaybackUsb) {
        status_t ret = pthread_join(mPlaybackUsb,NULL);
        ALOGE("return for pthreadjoin = %d", ret);
        mPlaybackUsb = NULL;
    }
    if (writeVal == SIGNAL_EVENT_KILLTHREAD) {
        int err;
        {
            Mutex::Autolock autoLock(mLock);
            err = closeDevice(mproxyPlaybackHandle);
            if (err) {
                ALOGE("Info: Could not close proxy %p", mproxyPlaybackHandle);
            }
            err = closeDevice(musbPlaybackHandle);
            if (err) {
                ALOGE("Info: Could not close USB device %p", musbPlaybackHandle);
            }
        }
    }
}

void AudioUsbALSA::exitRecordingThread(uint64_t writeVal)
{
    //TODO: Need to use userspace fd to kill the thread.
    // Not a valid assumption to blindly close the thread.
    ALOGD("exitRecordingThread");
#ifdef OUTPUT_RECORD_PROXY_BUFFER_LOG
    ALOGV("close file output");
    if(outputRecordBufferFile1)
        fclose (outputRecordBufferFile1);
#endif

    mkillRecordingThread = true;
    {
        Mutex::Autolock autoRecordLock(mRecordLock);
        if ((pfdProxyRecording[1].fd != -1) && (pfdUsbRecording[1].fd != -1)) {
            ALOGD("write to fd");
            write(pfdUsbRecording[1].fd, &writeVal, sizeof(uint64_t));
            write(pfdProxyRecording[1].fd, &writeVal, sizeof(uint64_t));
        }
    }

    if(mRecordingUsb) {
        int err = pthread_join(mRecordingUsb,NULL);
        ALOGD("pthread join err = %d",err);
        mRecordingUsb = NULL;
    }
    if (writeVal == SIGNAL_EVENT_KILLTHREAD ) {
        int err;
        {
            Mutex::Autolock autoRecordLock(mRecordLock);
            err = closeDevice(mproxyRecordingHandle);
            if (err) {
                ALOGE("Info: Could not close proxy for recording %p", mproxyRecordingHandle);
            } else {
                mproxyRecordingHandle = NULL;
            }
            err = closeDevice(musbRecordingHandle);
            if (err) {
                ALOGE("Info: Could not close USB recording device %p", musbRecordingHandle);
            } else {
                musbRecordingHandle = NULL;
            }
        }
    }
}

void AudioUsbALSA::setkillUsbRecordingThread(bool val){
    ALOGD("setkillUsbRecordingThread");
    mkillRecordingThread = val;
}

status_t AudioUsbALSA::setHardwareParams(pcm *txHandle, uint32_t sampleRate,
        uint32_t channels, int periodBytes, UsbAudioPCMModes usbAudioPCMModes)
{
    ALOGD("setHardwareParams");
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
    ALOGV("Setting period size:%d samplerate:%d, channels: %d",periodBytes,sampleRate, channels);
    param_set_min(params, SNDRV_PCM_HW_PARAM_PERIOD_BYTES, periodBytes);
    param_set_int(params, SNDRV_PCM_HW_PARAM_SAMPLE_BITS, 16);
    param_set_int(params, SNDRV_PCM_HW_PARAM_FRAME_BITS,
                  channels - 1 ? 32 : 16);
    param_set_int(params, SNDRV_PCM_HW_PARAM_CHANNELS,
                  channels);
    param_set_int(params, SNDRV_PCM_HW_PARAM_RATE, sampleRate);
    param_set_hw_refine(txHandle, params);

    if (param_set_hw_params(txHandle, params)) {
        ALOGE("ERROR: cannot set hw params");
        free(params);
        return NO_INIT;
    }

    param_dump(params);

    txHandle->period_size = pcm_period_size(params);
    txHandle->buffer_size = pcm_buffer_size(params);
    txHandle->period_cnt = txHandle->buffer_size/txHandle->period_size;

    ALOGD("setHardwareParams: buffer_size %d, period_size %d, period_cnt %d",
         txHandle->buffer_size, txHandle->period_size,
         txHandle->period_cnt);
 
    //Do not free params here, params is used after setting hw/sw params
    //params will be free-ed in pcm_close
    return NO_ERROR;
}

status_t AudioUsbALSA::setSoftwareParams(pcm *pcm, UsbAudioPCMModes usbAudioPCMModes)
{
    ALOGD("setSoftwareParams");
    struct snd_pcm_sw_params* params;

    params = (snd_pcm_sw_params*) calloc(1, sizeof(struct snd_pcm_sw_params));
    if (!params) {
        LOG_ALWAYS_FATAL("Failed to allocate ALSA software parameters!");
        return NO_INIT;
    }

    params->tstamp_mode = SNDRV_PCM_TSTAMP_NONE;
    params->period_step = 1;

    params->avail_min = (pcm->flags & PCM_MONO) ? pcm->period_size/2 : pcm->period_size/4;

    if (usbAudioPCMModes == USB_PLAYBACK) {
        params->start_threshold = (pcm->flags & PCM_MONO) ? pcm->period_size*8 : pcm->period_size*4;
        params->xfer_align = (pcm->flags & PCM_MONO) ? pcm->period_size*8 : pcm->period_size*4;
    } else if(usbAudioPCMModes == PROXY_PLAYBACK) {
        params->start_threshold = (pcm->flags & PCM_MONO) ? pcm->period_size*2 : pcm->period_size;
        params->xfer_align = (pcm->flags & PCM_MONO) ? pcm->period_size*2 : pcm->period_size;
    } else {
        params->start_threshold = (pcm->flags & PCM_MONO) ? pcm->period_size/2 : pcm->period_size/4;
        params->xfer_align = (pcm->flags & PCM_MONO) ? pcm->period_size/2 : pcm->period_size/4;
    }
    //Setting stop threshold to a huge value to avoid trigger stop being called internally
    params->stop_threshold = 0x0FFFFFFF;

    params->silence_size = 0;
    params->silence_threshold = 0;

    if (param_set_sw_params(pcm, params)) {
        ALOGE("ERROR: cannot set sw params");
        free(params);
        return NO_INIT;
    }

    //Do not free params here, params is used after setting hw/sw params
    //params will be free-ed in pcm_close
    return NO_ERROR;
}

status_t AudioUsbALSA::closeDevice(pcm *handle)
{
    ALOGD("closeDevice handle %p", handle);
    status_t err = NO_ERROR;
    if (handle) {
        err = pcm_close(handle);
        if (err != NO_ERROR) {
            ALOGE("INFO: closeDevice: pcm_close failed with err %d", err);
        }
    }
    handle = NULL;
    return err;
}

void AudioUsbALSA::RecordingThreadEntry() {
    ALOGD("Inside RecordingThreadEntry");
    int nfds = 2;
    mtimeOutRecording = TIMEOUT_INFINITE;
    int fd;
    long frames;
    static int start = 0;
    struct snd_xferi x;
    unsigned avail, bufsize;
    int bytes_written;
    uint32_t sampleRate;
    uint32_t channels;
    u_int8_t *srcUsb_addr = NULL;
    u_int8_t *dstProxy_addr = NULL;
    int err;
    pfdProxyRecording[0].fd = -1;
    pfdProxyRecording[1].fd = -1;
    pfdUsbRecording[0].fd = -1;
    pfdUsbRecording[1].fd = -1;
#ifdef OUTPUT_RECORD_PROXY_BUFFER_LOG
    sprintf(outputRecordfilename1, "%s%d%s", outputRecordfilename, rNumber,".pcm");
    outputRecordBufferFile1 = fopen (outputRecordfilename, "ab");
    rNumber++;
#endif

    {
        Mutex::Autolock autoRecordLock(mRecordLock);
        err = getCap((char *)"Capture:", mchannelsCapture, msampleRateCapture);
        if (err) {
            ALOGE("ERROR: Could not get capture capabilities from usb device");
            return;
        }
        int channelFlag = PCM_MONO;
        if (mchannelsCapture >= 2) {
            channelFlag = PCM_STEREO;
        }

        musbRecordingHandle = configureDevice(PCM_IN|channelFlag|PCM_MMAP, (char *)"hw:1,0",
                                         msampleRateCapture, mchannelsCapture,2048,USB_RECORDING);
        if (!musbRecordingHandle) {
            ALOGE("ERROR: Could not configure USB device for recording");
            return;
        } else {
            ALOGD("USB device Configured for recording");
        }

        if (!mkillRecordingThread) {
            pfdUsbRecording[0].fd = musbRecordingHandle->fd;                           //DEBUG
            pfdUsbRecording[0].events = POLLIN;
            musbpfdRecording = eventfd(0,0);
            pfdUsbRecording[1].fd = musbpfdRecording;
            pfdUsbRecording[1].events = (POLLIN | POLLERR | POLLNVAL | POLLHUP);
            pfdUsbRecording[1].revents = 0;
        }

        mproxyRecordingHandle = configureDevice(PCM_OUT|channelFlag|PCM_MMAP, (char *)"hw:0,7",
                                            msampleRateCapture, mchannelsCapture,2048,PROXY_PLAYBACK);
        if (!mproxyRecordingHandle) {
            ALOGE("ERROR: Could not configure Proxy for recording");
            err = closeDevice(musbRecordingHandle);
            if(err == OK) {
                musbRecordingHandle = NULL;
            }
            return;
        } else {
            ALOGD("Proxy Configured for recording");
        }

        bufsize = musbRecordingHandle->period_size;

        if(!mkillRecordingThread) {
            pfdProxyRecording[0].fd = mproxyRecordingHandle->fd;
            pfdProxyRecording[0].events = POLLOUT;
            mProxypfdRecording = eventfd(0,0);
            pfdUsbRecording[1].fd = mProxypfdRecording;
            pfdUsbRecording[1].events = (POLLIN | POLLERR | POLLNVAL | POLLHUP);
            pfdUsbRecording[1].revents = 0;
        }

        frames = (musbRecordingHandle->flags & PCM_MONO) ? (bufsize / 2) : (bufsize / 4);
        x.frames = (musbRecordingHandle->flags & PCM_MONO) ? (bufsize / 2) : (bufsize / 4);

    }
    /***********************keep reading from usb and writing to proxy******************************************/
    while (mkillRecordingThread != true) {
        if (!musbRecordingHandle->running) {
            if (pcm_prepare(musbRecordingHandle)) {
                ALOGE("ERROR: pcm_prepare failed for usb device for recording");
                mkillRecordingThread = true;
                break;;
            }
        }
        if (!mproxyRecordingHandle->running) {
            if (pcm_prepare(mproxyRecordingHandle)) {
                ALOGE("ERROR: pcm_prepare failed for proxy device for recording");
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
                    ALOGE("ERROR: pcm_prepare failed for proxy device for recording");
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
                int err_poll = poll(pfdUsbRecording, nfds, TIMEOUT_INFINITE);
                //ALOGD("pfdUsbRecording[0].revents = %d, pfdUsbRecording[1].revents =%d", pfdUsbRecording[0].revents,pfdUsbRecording[1].revents);
                if (err_poll == 0 ) {
                     ALOGD("POLL timedout");
                     mkillRecordingThread = true;
                     pfdUsbRecording[0].revents = 0;
                     pfdUsbRecording[1].revents = 0;
                }

                if (pfdUsbRecording[1].revents & POLLIN && !mkillRecordingThread) {
                    ALOGD("Info: Signalled from HAL about an event");
                    uint64_t u;
                    read(musbpfdRecording, &u, sizeof(uint64_t));
                    pfdUsbRecording[0].revents = 0;
                    pfdUsbRecording[1].revents = 0;
                    if (u == SIGNAL_EVENT_KILLTHREAD) {
                        ALOGD("kill thread");
                        mkillRecordingThread = true;
                    }
                }
                if(!mkillRecordingThread)
                    continue;
                break;
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
                    ALOGE("ERROR: pcm_prepare failed for proxy device for recording");
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
                int err_poll = poll(pfdProxyRecording, nfds, TIMEOUT_INFINITE);
                if (err_poll == 0 ) {
                     ALOGD("POLL timedout");
                     mkillRecordingThread = true;
                     pfdProxyRecording[0].revents = 0;
                     pfdProxyRecording[1].revents = 0;
                }

                if (pfdProxyRecording[1].revents & POLLIN && !mkillRecordingThread) {
                    ALOGD("Info:Proxy Signalled from HAL about an event");
                    uint64_t u;
                    read(mProxypfdRecording, &u, sizeof(uint64_t));
                    pfdProxyRecording[0].revents = 0;
                    pfdProxyRecording[1].revents = 0;
                    if (u == SIGNAL_EVENT_KILLTHREAD) {
                        ALOGD("kill thread");
                        mkillRecordingThread = true;
                    }
                }
                if(!mkillRecordingThread)
                    continue;
                break;
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
#ifdef OUTPUT_RECORD_PROXY_BUFFER_LOG
    if (outputRecordBufferFile1)
    {
        fwrite (srcUsb_addr,1,bufsize,outputRecordBufferFile1);
    }
#endif

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
            if (!mkillRecordingThread) {
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
    ALOGD("Thread dying = %d", mkillRecordingThread);
#ifdef OUTPUT_RECORD_PROXY_BUFFER_LOG
    ALOGV("close file output");
    if(outputRecordBufferFile1)
        fclose (outputRecordBufferFile1);
#endif

    {
        Mutex::Autolock autoRecordLock(mRecordLock);
        if(musbpfdRecording != -1) {
            close(musbpfdRecording);
            musbpfdRecording = -1;
        }
        if(mProxypfdRecording != -1) {
            close(mProxypfdRecording);
            mProxypfdRecording = -1;
        }

        if (mkillRecordingThread) {
            err = closeDevice(mproxyRecordingHandle);
            if(err == OK) {
                mproxyRecordingHandle = NULL;
            }
            err = closeDevice(musbRecordingHandle);
            if(err == OK) {
                musbRecordingHandle = NULL;
            }
        }
    }
    mRecordingUsb = NULL;
    ALOGD("Exiting USB Recording thread");
}

void *AudioUsbALSA::PlaybackThreadWrapper(void *me) {
    static_cast<AudioUsbALSA *>(me)->PlaybackThreadEntry();
    return NULL;
}

void *AudioUsbALSA::RecordingThreadWrapper(void *me) {
    static_cast<AudioUsbALSA *>(me)->RecordingThreadEntry();
    return NULL;
}

struct pcm * AudioUsbALSA::configureDevice(unsigned flags, char* hw,
            int sampleRate, int channelCount,
            int periodSize, UsbAudioPCMModes usbAudioPCMModes){
    int err = NO_ERROR;
    struct pcm * handle = NULL;
    handle = pcm_open(flags, hw);
    if (!handle || handle->fd < 0) {
        ALOGE("ERROR: pcm_open failed");
        return NULL;
    }

    if (!pcm_ready(handle)) {
        ALOGE("ERROR: pcm_ready failed");
        closeDevice(handle);
        return NULL;
    }

    ALOGD("Setting hardware params: sampleRate:%d, channels: %d",sampleRate, channelCount);
    err = setHardwareParams(handle, sampleRate, channelCount,periodSize, usbAudioPCMModes);
    if (err != NO_ERROR) {
        ALOGE("ERROR: setHardwareParams failed");
        {
             closeDevice(handle);
             return NULL;
        }
    }

    err = setSoftwareParams(handle, usbAudioPCMModes);
    if (err != NO_ERROR) {
        ALOGE("ERROR: setSoftwareParams failed");
        {
            closeDevice(handle);
            return NULL;
        }
    }

    err = mmap_buffer(handle);
    if (err) {
        ALOGE("ERROR: mmap_buffer failed");
        {
            closeDevice(handle);
            return NULL;
        }
    }

    err = pcm_prepare(handle);
    if (err) {
        ALOGE("ERROR: pcm_prepare failed");
        {
            closeDevice(handle);
            return NULL;
        }
    }

    return handle;
}

status_t AudioUsbALSA::startDevice(pcm *handle, bool *killThread) {
    int err = NO_ERROR;;
    if (ioctl(handle->fd, SNDRV_PCM_IOCTL_START)) {
        err = -errno;
        if (errno == EPIPE) {
            ALOGE("ERROR: SNDRV_PCM_IOCTL_START returned EPIPE for usb recording case");
            handle->underruns++;
            handle->running = 0;
            handle->start = 0;
            return errno;
        } else {
            ALOGE("ERROR: SNDRV_PCM_IOCTL_START failed for usb recording case errno:%d", errno);
            *killThread = true;
            return errno;
        }
    }
    handle->start = 1;
    if (handle == musbRecordingHandle) {
        ALOGD("Usb Driver started for recording");
    } else if (handle == mproxyRecordingHandle) {
        ALOGD("Proxy Driver started for recording");
    } else if (handle == musbPlaybackHandle) {
        ALOGD("Usb Driver started for playback");
    } else if (handle == mproxyPlaybackHandle) {
        ALOGD("proxy Driver started for playback");
    }
    return NO_ERROR;
}

status_t AudioUsbALSA::syncPtr(struct pcm *handle, bool *killThread) {
    int err;
    err = sync_ptr(handle);
    if (err == EPIPE) {
        ALOGE("ERROR: Failed in sync_ptr \n");
        handle->running = 0;
        handle->underruns++;
        handle->start = 0;
    } else if (err == ENODEV) {
        ALOGE("Info: Device not available");
    } else if (err != NO_ERROR) {
        ALOGE("ERROR: Sync ptr returned %d", err);
        *killThread = true;
    }
    return err;
}

void AudioUsbALSA::pollForProxyData(){
    int err_poll = poll(pfdProxyPlayback, mnfdsPlayback, mtimeOut);
    if (err_poll == 0 ) {
        ALOGD("POLL timedout");
        mkillPlayBackThread = true;
        pfdProxyPlayback[0].revents = 0;
        pfdProxyPlayback[1].revents = 0;
        return;
    }

    if (pfdProxyPlayback[1].revents & POLLIN) {
        ALOGD("Signalled from HAL about timeout");
        uint64_t u;
        read(mproxypfdPlayback, &u, sizeof(uint64_t));
        pfdProxyPlayback[1].revents = 0;
        if (u == SIGNAL_EVENT_KILLTHREAD) {
            ALOGD("kill thread event");
            mkillPlayBackThread = true;
            pfdProxyPlayback[0].revents = 0;
            pfdProxyPlayback[1].revents = 0;
            return;
        } else if (u == SIGNAL_EVENT_TIMEOUT) {
            ALOGD("Setting timeout for 3 sec");
            mtimeOut = POLL_TIMEOUT;
        }
    } else if (pfdProxyPlayback[1].revents & POLLERR || pfdProxyPlayback[1].revents & POLLHUP ||
               pfdProxyPlayback[1].revents & POLLNVAL) {
        ALOGE("Info: proxy throwing error from location 1");
        mkillPlayBackThread = true;
        pfdProxyPlayback[0].revents = 0;
        pfdProxyPlayback[1].revents = 0;
        return;
    }

    if (pfdProxyPlayback[0].revents & POLLERR || pfdProxyPlayback[0].revents & POLLHUP ||
        pfdProxyPlayback[0].revents & POLLNVAL) {
        ALOGE("Info: proxy throwing error");
        mkillPlayBackThread = true;
        pfdProxyPlayback[0].revents = 0;
        pfdProxyPlayback[1].revents = 0;
    }
}

void AudioUsbALSA::pollForUsbData(){
    int err_poll = poll(pfdUsbPlayback, mnfdsPlayback, mtimeOut);
    if (err_poll == 0 ) {
        ALOGD("POLL timedout");
        mkillPlayBackThread = true;
        pfdUsbPlayback[0].revents = 0;
        pfdUsbPlayback[1].revents = 0;
        return;
    }

    if (pfdUsbPlayback[1].revents & POLLIN) {
        ALOGD("Info: Signalled from HAL about an event");
        uint64_t u;
        read(musbpfdPlayback, &u, sizeof(uint64_t));
        pfdUsbPlayback[0].revents = 0;
        pfdUsbPlayback[1].revents = 0;
        if (u == SIGNAL_EVENT_KILLTHREAD) {
            ALOGD("kill thread");
            mkillPlayBackThread = true;
            return;
        } else if (u == SIGNAL_EVENT_TIMEOUT) {
            ALOGD("Setting timeout for 3 sec");
            mtimeOut = POLL_TIMEOUT;
        }
    } else if (pfdUsbPlayback[1].revents & POLLERR || pfdUsbPlayback[1].revents & POLLHUP ||
               pfdUsbPlayback[1].revents & POLLNVAL) {
        ALOGE("Info: usb throwing error from location 1");
        mkillPlayBackThread = true;
        pfdUsbPlayback[0].revents = 0;
        pfdUsbPlayback[1].revents = 0;
        return;
    }

    if (pfdUsbPlayback[0].revents & POLLERR || pfdUsbPlayback[0].revents & POLLHUP ||
        pfdUsbPlayback[0].revents & POLLNVAL) {
        ALOGE("Info: usb throwing error");
        mkillPlayBackThread = true;
        pfdUsbPlayback[0].revents = 0;
        return;
    }
}

// Some USB audio accessories have a really low default volume set. Look for a suitable
// volume control and set the volume to default volume level.
void AudioUsbALSA::initPlaybackVolume() {
    ALOGD("initPlaybackVolume");
    struct mixer *usbMixer = mixer_open(USB_CARDCONTROL_PATH);

    if (usbMixer) {
         struct mixer_ctl *ctl = NULL;
         unsigned int usbPlaybackVolume;
         int i;

         // Look for the first control named ".*Playback Volume" that isn't for a microphone
         for (i = 0; i < usbMixer->count; i++) {
             if (strstr((const char *)usbMixer->info[i].id.name, "Playback Volume") &&
                 !strstr((const char *)usbMixer->info[i].id.name, "Mic")) {
                   ctl = usbMixer->ctl + i;
                   break;
             }
         }
         if (ctl != NULL) {
            ALOGD("Found a volume control for USB: %s", usbMixer->info[i].id.name);
            mixer_ctl_get(ctl, &usbPlaybackVolume);
            ALOGD("Value got from mixer_ctl_get is:%u", usbPlaybackVolume);
            if (mixer_ctl_set(ctl,usbPlaybackVolume) < 0) {
               ALOGE("Failed to set volume; default volume might be used");
            }
         } else {
            ALOGE("No playback volume control found; default volume will be used");
         }
         mixer_close(usbMixer);
    } else {
         ALOGE("Failed to open mixer for card 1");
    }
}

void AudioUsbALSA::PlaybackThreadEntry() {
    ALOGD("PlaybackThreadEntry");
    mnfdsPlayback = 2;
    mtimeOut = TIMEOUT_INFINITE;
    long frames;
    static int fd;
    struct snd_xferi x;
    int bytes_written;
    unsigned avail, xfer, bufsize;
    unsigned proxyPeriod, usbPeriod;
    uint32_t sampleRate;
    uint32_t channels;
    unsigned int tmp;
    int numOfBytesWritten;
    int err;
    char proxyDeviceName[10];

    mdstUsb_addr = NULL;
    msrcProxy_addr = NULL;

    int proxySizeRemaining = 0;
    int usbSizeFilled = 0;
    int usbframes = 0;
    u_int8_t *proxybuf = NULL;
    u_int8_t *usbbuf = NULL;

    pid_t tid  = gettid();
    androidSetThreadPriority(tid, ANDROID_PRIORITY_URGENT_AUDIO);
#ifdef OUTPUT_PROXY_BUFFER_LOG
    sprintf(outputfilename1, "%s%d%s", outputfilename, number,".pcm");
    outputBufferFile1 = fopen (outputfilename, "ab");
    number++;
#endif

    {
        Mutex::Autolock autoLock(mLock);

        err = getCap((char *)"Playback:", mchannelsPlayback, msampleRatePlayback);
        if (err) {
            ALOGE("ERROR: Could not get playback capabilities from usb device");
            return;
        }
        musbPlaybackHandle = configureDevice(PCM_OUT|PCM_STEREO|PCM_MMAP, (char *)"hw:1,0",
                                         msampleRatePlayback, mchannelsPlayback,
                                         USB_PERIOD_SIZE, USB_PLAYBACK);
        if (!musbPlaybackHandle || mkillPlayBackThread) {
            ALOGE("ERROR: configureUsbDevice failed, returning");
            return;
        } else {
           ALOGD("USB Configured for playback");
        }

        if (!mkillPlayBackThread) {
            pfdUsbPlayback[0].fd = musbPlaybackHandle->timer_fd;
            pfdUsbPlayback[0].events = POLLIN;
            musbpfdPlayback = eventfd(0,0);
            pfdUsbPlayback[1].fd = musbpfdPlayback;
            pfdUsbPlayback[1].events = (POLLIN | POLLOUT | POLLERR | POLLNVAL | POLLHUP);
        }

        snprintf(proxyDeviceName, sizeof(proxyDeviceName), "hw:%u,8", mProxySoundCard);
        ALOGD("Configuring Proxy capture device %s", proxyDeviceName);
        int ProxyOpenRetryCount=PROXY_OPEN_RETRY_COUNT;
        while(ProxyOpenRetryCount){
                mproxyPlaybackHandle = configureDevice(PCM_IN|PCM_STEREO|PCM_MMAP, proxyDeviceName,
                               msampleRatePlayback, mchannelsPlayback, PROXY_PERIOD_SIZE, PROXY_RECORDING);
                if(!mproxyPlaybackHandle){
                     ProxyOpenRetryCount --;
                     usleep(PROXY_OPEN_WAIT_TIME * 1000);
                     ALOGD("openProxyDevice failed retrying = %d", ProxyOpenRetryCount);
                }
                else{
                  break;
                }
        }
        if (!mproxyPlaybackHandle || mkillPlayBackThread) {
           ALOGE("ERROR: Could not configure Proxy, returning");
           err = closeDevice(musbPlaybackHandle);
           if(err == OK) {
              musbPlaybackHandle = NULL;
           }
           return;
        } else {
            ALOGD("Proxy Configured for playback");
        }
        ALOGV("Init USB volume");
        initPlaybackVolume();

        proxyPeriod = mproxyPlaybackHandle->period_size;
        usbPeriod = musbPlaybackHandle->period_size;

        if (!mkillPlayBackThread) {
            pfdProxyPlayback[0].fd = mproxyPlaybackHandle->fd;
            pfdProxyPlayback[0].events = (POLLIN);                                 // | POLLERR | POLLNVAL);
            mproxypfdPlayback = eventfd(0,0);
            pfdProxyPlayback[1].fd = mproxypfdPlayback;
            pfdProxyPlayback[1].events = (POLLIN | POLLERR | POLLNVAL);
        }

        frames = (mproxyPlaybackHandle->flags & PCM_MONO) ? (proxyPeriod / 2) : (proxyPeriod / 4);
        x.frames = (mproxyPlaybackHandle->flags & PCM_MONO) ? (proxyPeriod / 2) : (proxyPeriod / 4);
        usbframes = (musbPlaybackHandle->flags & PCM_MONO) ? (usbPeriod / 2) : (usbPeriod / 4);

        proxybuf = ( u_int8_t *) malloc(PROXY_PERIOD_SIZE);
        usbbuf = ( u_int8_t *) malloc(USB_PERIOD_SIZE);

        if (proxybuf == NULL || usbbuf == NULL) {
            ALOGE("ERROR: Unable to allocate USB audio buffer(s): proxybuf=%p, usbbuf=%p",
                  proxybuf, usbbuf);
            /* Don't run the playback loop if we failed to allocate either of these buffers.
            If either pointer is non-NULL they'll be freed after the end of the loop. */
           mkillPlayBackThread = true;
        } else {
            memset(proxybuf, 0x0, PROXY_PERIOD_SIZE);
            memset(usbbuf, 0x0, USB_PERIOD_SIZE);
        }
    }
    /***********************keep reading from proxy and writing to USB******************************************/
    while (mkillPlayBackThread != true) {
        if (!mproxyPlaybackHandle->running) {
            if (pcm_prepare(mproxyPlaybackHandle)) {
                ALOGE("ERROR: pcm_prepare failed for proxy");
                mkillPlayBackThread = true;
                break;
            }
        }
        if (!musbPlaybackHandle->running) {
            if (pcm_prepare(musbPlaybackHandle)) {
                ALOGE("ERROR: pcm_prepare failed for usb");
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
        if (proxySizeRemaining == 0) {
            for (;;) {
                if (!mproxyPlaybackHandle->running) {
                    if (pcm_prepare(mproxyPlaybackHandle)) {
                        ALOGE("ERROR: pcm_prepare failed for proxy");
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
                memcpy(proxybuf, msrcProxy_addr, proxyPeriod );

#ifdef OUTPUT_PROXY_BUFFER_LOG
    if (outputBufferFile1)
    {
        fwrite (proxybuf,1,proxyPeriod,outputBufferFile1);
    }
#endif
                x.frames -= frames;
                mproxyPlaybackHandle->sync_ptr->c.control.appl_ptr += frames;
                mproxyPlaybackHandle->sync_ptr->flags = 0;
                proxySizeRemaining = proxyPeriod;
            }

            if (!mkillPlayBackThread) {
                err = syncPtr(mproxyPlaybackHandle, &mkillPlayBackThread);
                if (err == EPIPE) {
                    continue;
                } else if (err != NO_ERROR && err != ENODEV) {
                    break;
                }
            }
        }
        //ALOGV("usbSizeFilled %d, proxySizeRemaining %d ",usbSizeFilled,proxySizeRemaining);
        if (usbPeriod - usbSizeFilled <= proxySizeRemaining) {
            memcpy(usbbuf + usbSizeFilled, proxybuf + proxyPeriod - proxySizeRemaining, usbPeriod - usbSizeFilled);
            proxySizeRemaining -= (usbPeriod - usbSizeFilled);
            usbSizeFilled = usbPeriod;
        }
        else {
            memcpy(usbbuf + usbSizeFilled, proxybuf + proxyPeriod - proxySizeRemaining,proxySizeRemaining);
            usbSizeFilled += proxySizeRemaining;
            proxySizeRemaining = 0;
        }

        if (usbSizeFilled == usbPeriod) {
            for (;;) {
                if (!musbPlaybackHandle->running) {
                    if (pcm_prepare(musbPlaybackHandle)) {
                        ALOGE("ERROR: pcm_prepare failed for usb");
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
                    //ALOGV("Avail USB is: %d", avail);
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

                /**************End USB syncing before write *************/

                memcpy(mdstUsb_addr, usbbuf, usbPeriod );
                usbSizeFilled = 0;
                memset(usbbuf, 0x0, usbPeriod);
            }

            /**************** sync up after write -- USB  ************************/
            musbPlaybackHandle->sync_ptr->c.control.appl_ptr += usbframes;
            musbPlaybackHandle->sync_ptr->flags = 0;
            if (!mkillPlayBackThread) {
                err = syncPtr(musbPlaybackHandle, &mkillPlayBackThread);
                if (err == EPIPE) {
                    continue;
                } else if (err != NO_ERROR && err != ENODEV ) {
                    break;
                }
            }

            bytes_written = musbPlaybackHandle->sync_ptr->c.control.appl_ptr - musbPlaybackHandle->sync_ptr->s.status.hw_ptr;
            ALOGV("Appl ptr %lu , hw_ptr %lu, difference %d",musbPlaybackHandle->sync_ptr->c.control.appl_ptr, musbPlaybackHandle->sync_ptr->s.status.hw_ptr, bytes_written);

            /*
                Following is the check to prevent USB from going to bad state.
                This happens in case of an underrun where there is not enough
                data from the proxy
            */
            if (bytes_written <= usbPeriod && musbPlaybackHandle->start) {
                ioctl(musbPlaybackHandle->fd, SNDRV_PCM_IOCTL_PAUSE,1);
                pcm_prepare(musbPlaybackHandle);
                musbPlaybackHandle->start = false;
                continue;
            }
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
    }
#ifdef OUTPUT_PROXY_BUFFER_LOG
    ALOGV("close file output");
    if(outputBufferFile1)
        fclose (outputBufferFile1);
#endif
    if (proxybuf)
        free(proxybuf);
    if (usbbuf)
        free(usbbuf);
    if(mproxypfdPlayback != -1) {
        close(mproxypfdPlayback);
        mproxypfdPlayback = -1;
    }
    if(musbpfdPlayback != -1) {
        close(musbpfdPlayback);
        musbpfdPlayback = -1;
    }
    if(mkillPlayBackThread) {
        {
            Mutex::Autolock autoLock(mLock);
            err = closeDevice(mproxyPlaybackHandle);
            if(err == OK) {
                mproxyPlaybackHandle = NULL;
            } else {
                ALOGE("mproxyPlaybackHandle - err = %d", err);
            }
            err = closeDevice(musbPlaybackHandle);
            if(err == OK) {
                musbPlaybackHandle = NULL;
            } else {
                ALOGE("musbPlaybackHandle - err = %d", err);
            }
        }
    }
    mPlaybackUsb = NULL;
    ALOGD("Exiting USB Playback Thread");
}

void AudioUsbALSA::startPlayback()
{
    mkillPlayBackThread = false;
    ALOGD("Creating USB Playback Thread");
    pthread_create(&mPlaybackUsb, NULL, PlaybackThreadWrapper, this);
}

void AudioUsbALSA::startRecording()
{
    //create Thread
    mkillRecordingThread = false;
    ALOGV("Creating USB recording Thread");
    pthread_create(&mRecordingUsb, NULL, RecordingThreadWrapper, this);
}
}
