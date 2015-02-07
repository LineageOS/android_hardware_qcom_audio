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

int bootup_complete = 0;

namespace android {

    AudioDaemon::AudioDaemon() : Thread(false) {
    }

    AudioDaemon::~AudioDaemon() {
        putStateFDs(mSndCardFd);
    }

    void AudioDaemon::onFirstRef() {
        ALOGV("Start audiod daemon");
        run("AudioDaemon", PRIORITY_URGENT_AUDIO);
    }

    void AudioDaemon::binderDied(const wp<IBinder>& who __unused)
    {
        requestExit();
    }

    bool AudioDaemon::getStateFDs(std::vector<std::pair<int,int> > &sndcardFdPair)
    {
        FILE *fp;
        int fd;
        char *ptr, *saveptr;
        char buffer[128];
        int line = 0;
        String8 path;
        int sndcard;
        const char* cards = "/proc/asound/cards";

        if ((fp = fopen(cards, "r")) == NULL) {
            ALOGE("Cannot open %s file to get list of sound cars", cards);
            return false;
        }

        sndcardFdPair.clear();
        memset(buffer, 0x0, sizeof(buffer));
        while ((fgets(buffer, sizeof(buffer), fp) != NULL)) {
            if (line % 2)
                continue;
            ptr = strtok_r(buffer, " [", &saveptr);
            if (ptr) {
                path = "/proc/asound/card";
                path += ptr;
                path += "/state";
                ALOGD("Opening sound card state : %s", path.string());
                fd = open(path.string(), O_RDONLY);
                if (fd == -1) {
                    ALOGE("Open %s failed : %s", path.string(), strerror(errno));
                } else {
                    /* returns vector of pair<sndcard, fd> */
                    sndcard = atoi(ptr);
                    sndcardFdPair.push_back(std::make_pair(sndcard, fd));
                }
            }
            line++;
        }

        ALOGV("%s: %d sound cards detected", __func__, sndcardFdPair.size());
        fclose(fp);

        return sndcardFdPair.size() > 0 ? true : false;
    }

    void AudioDaemon::putStateFDs(std::vector<std::pair<int,int> > &sndcardFdPair)
    {
        unsigned int i;
        for (i = 0; i < sndcardFdPair.size(); i++)
            close(sndcardFdPair[i].second);
        sndcardFdPair.clear();
    }

    status_t AudioDaemon::readyToRun() {

        ALOGV("readyToRun: open snd card state node files");
        return NO_ERROR;
    }

#define MAX_SLEEP_RETRY 100
#define AUDIO_INIT_SLEEP_WAIT 100 /* 100 ms */

    bool AudioDaemon::threadLoop()
    {
        int max = -1;
        unsigned int i;
        bool ret = true;
        snd_card_status cur_state = snd_card_offline;
        struct pollfd *pfd = NULL;
        char rd_buf[9];
        unsigned int sleepRetry = 0;
        bool audioInitDone = false;

        ALOGV("Start threadLoop()");
        while (audioInitDone == false && sleepRetry < MAX_SLEEP_RETRY) {
            if (mSndCardFd.empty() && !getStateFDs(mSndCardFd)) {
                ALOGE("Sleeping for 100 ms");
                usleep(AUDIO_INIT_SLEEP_WAIT*1000);
                sleepRetry++;
            } else {
                audioInitDone = true;
            }
        }

        if (audioInitDone == false)
            ALOGE("Sound Card is empty!!!");

        pfd = new pollfd[mSndCardFd.size()];
        bzero(pfd, sizeof(*pfd) * mSndCardFd.size());
        for (i = 0; i < mSndCardFd.size(); i++) {
            pfd[i].fd = mSndCardFd[i].second;
            pfd[i].events = POLLPRI;
        }

       ALOGD("read for sound card state change before while");
       for (i = 0; i < mSndCardFd.size(); i++) {
           if (!read(pfd[i].fd, (void *)rd_buf, 8)) {
               ALOGE("Error receiving sound card state event (%s)", strerror(errno));
               ret = false;
           } else {
               rd_buf[8] = '\0';
               ALOGD("sound card state file content: %s before while",rd_buf);
               lseek(pfd[i].fd, 0, SEEK_SET);

               if (strstr(rd_buf, "OFFLINE")) {
                   ALOGE("put cur_state to offline");
                   cur_state = snd_card_offline;
               } else if (strstr(rd_buf, "ONLINE")){
                   ALOGE("put cur_state to online");
                   cur_state = snd_card_online;
               } else {
                   ALOGE("ERROR rd_buf %s", rd_buf);
               }

               ALOGD("cur_state=%d, bootup_complete=%d", cur_state, cur_state );
               if (cur_state == snd_card_online && !bootup_complete) {
                   bootup_complete = 1;
                   ALOGE("sound card up is deteced before while");
                   ALOGE("bootup_complete set to 1");
               }
           }
       }

        while (1) {
           ALOGD("poll() for sound card state change ");
           if (poll(pfd, mSndCardFd.size(), -1) < 0) {
              ALOGE("poll() failed (%s)", strerror(errno));
              ret = false;
              break;
           }

           ALOGD("out of poll() for sound card state change, SNDCARD size=%d", mSndCardFd.size());
           for (i = 0; i < mSndCardFd.size(); i++) {
               if (pfd[i].revents & POLLPRI) {
                   if (!read(pfd[i].fd, (void *)rd_buf, 8)) {
                       ALOGE("Error receiving sound card state event (%s)", strerror(errno));
                       ret = false;
                   } else {
                       rd_buf[8] = '\0';
                       ALOGV("sound card state file content: %s, bootup_complete=%d",rd_buf, bootup_complete);
                       lseek(pfd[i].fd, 0, SEEK_SET);

                       if (strstr(rd_buf, "OFFLINE")) {
                           cur_state = snd_card_offline;
                       } else if (strstr(rd_buf, "ONLINE")){
                           cur_state = snd_card_online;
                       }

                       if (bootup_complete) {
                           ALOGV("bootup_complete, so NofityAudioSystem");
                           notifyAudioSystem(mSndCardFd[i].first, cur_state);
                       }

                       if (cur_state == snd_card_online && !bootup_complete) {
                           bootup_complete = 1;
                       }
                   }
               }
           }
       }

       putStateFDs(mSndCardFd);
       delete [] pfd;

       ALOGV("Exiting Poll ThreadLoop");
       return ret;
    }

    void AudioDaemon::notifyAudioSystem(int snd_card, snd_card_status status) {

        String8 str;
        char buf[4] = {0,};

        str = "SND_CARD_STATUS=";
        snprintf(buf, sizeof(buf), "%d", snd_card);
        str += buf;
        if (status == snd_card_online)
            str += ",ONLINE";
        else
            str += ",OFFLINE";
        ALOGV("%s: notifyAudioSystem : %s", __func__, str.string());
        AudioSystem::setParameters(0, str);
    }
}
