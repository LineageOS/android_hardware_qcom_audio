/* AudioDaemon.cpp
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

#define LOG_TAG "AudioDaemon"
#define LOG_NDEBUG 0
#define LOG_NDDEBUG 0

#include <media/AudioSystem.h>
#include <sys/poll.h>

#include "AudioDaemon.h"

int pil_fd;
#define NUM_FD 1
int bootup_complete = 0;
char adsp_pil_state_file[] = "/sys/kernel/audio_voice_service/status";

namespace android {

    AudioDaemon::AudioDaemon() : Thread(false) {
    }

    AudioDaemon::~AudioDaemon() {
    }

    void AudioDaemon::onFirstRef() {
        ALOGV("Start audiod daemon");
        run("AudioDaemon", PRIORITY_AUDIO);
    }

    void AudioDaemon::binderDied(const wp<IBinder>& who)
    {
        requestExit();
    }


    status_t AudioDaemon::readyToRun() {

        ALOGV("readyToRun: open adsp sysfs node file: %s", adsp_pil_state_file);

        pil_fd = open(adsp_pil_state_file, O_RDONLY);

        if (pil_fd < 0) {
            ALOGE("File %s open failed (%s)", adsp_pil_state_file, strerror(errno));
            return errno;
        }
        return NO_ERROR;
    }

    bool AudioDaemon::threadLoop()
    {
        int max = -1;
        int ret;
        adsp_status cur_state = adsp_offline;
        struct pollfd pfd[NUM_FD];
        char rd_buf[9];

        ALOGV("Start threadLoop()");

        if (pil_fd < 0)
            pil_fd = open(adsp_pil_state_file, O_RDONLY);

        if (pil_fd < 0) {
            ALOGE("File %s open failed (%s)", adsp_pil_state_file, strerror(errno));
            return false;
        }

        pfd[0].fd = pil_fd;
        pfd[0].events = POLLPRI;

        while (1) {
           ALOGD("poll() for adsp state change ");
           if (poll(pfd, 1, -1) < 0) {
              ALOGE("poll() failed (%s)", strerror(errno));
              return false;
           }

           ALOGD("out of poll() for adsp state change ");
           if (pfd[0].revents & POLLPRI) {
               if (!read(pil_fd, (void *)rd_buf, 8)) {
                   ALOGE("Error receiving adsp_state event (%s)", strerror(errno));
               } else {
                   rd_buf[8] = '\0';
                   ALOGV("adsp state file content: %s",rd_buf);
                   lseek(pil_fd, 0, SEEK_SET);

                   if (!strncmp(rd_buf, "OFFLINE", strlen("OFFLINE"))) {
                       cur_state = adsp_offline;
                   } else if (!strncmp(rd_buf, "ONLINE", strlen("ONLINE"))) {
                       cur_state = adsp_online;
                   }

                   if (bootup_complete) {
                       notifyAudioSystem(cur_state);
                   }

                   if (cur_state == adsp_online && !bootup_complete) {
                       bootup_complete = 1;
                       ALOGV("adsp is up, device bootup time");
                   }
               }
           }
       }

       ALOGV("Exiting Poll ThreadLoop");
       return true;
    }

    void AudioDaemon::notifyAudioSystem(adsp_status status) {

        ALOGV("notifyAudioSystem : %d", status);
        switch (status) {
        case adsp_online:
            ALOGV("notifyAudioSystem : ADSP_STATUS=ONLINE");
            AudioSystem::setParameters(0, String8("ADSP_STATUS=ONLINE"));
            break;
        case adsp_offline:
            ALOGV("notifyAudioSystem : ADSP_STATUS=OFFLINE");
            AudioSystem::setParameters(0, String8("ADSP_STATUS=OFFLINE"));
            break;
        }
    }
}
