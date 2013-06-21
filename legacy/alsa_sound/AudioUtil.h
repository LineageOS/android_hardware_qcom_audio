/* AudioUtil.h
 *
 * Copyright (C) 2012 The Android Open Source Project
 * Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.
 * Not a Contribution.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ALSA_SOUND_AUDIO_UTIL_H
#define ALSA_SOUND_AUDIO_UTIL_H

#define BIT(nr)     (1UL << (nr))
#define MAX_EDID_BLOCKS 10
#define MAX_SHORT_AUDIO_DESC_CNT        30
#define MIN_AUDIO_DESC_LENGTH           3
#define MIN_SPKR_ALLOCATION_DATA_LENGTH 3
#define MAX_CHANNELS_SUPPORTED          8

/* Front left channel. */
#define PCM_CHANNEL_FL    1
/* Front right channel. */
#define PCM_CHANNEL_FR    2
/* Front center channel. */
#define PCM_CHANNEL_FC    3
/* Left surround channel.*/
#define PCM_CHANNEL_LS   4
/* Right surround channel.*/
#define PCM_CHANNEL_RS   5
/* Low frequency effect channel. */
#define PCM_CHANNEL_LFE  6
/* Center surround channel; Rear center channel. */
#define PCM_CHANNEL_CS   7
/* Left back channel; Rear left channel. */
#define PCM_CHANNEL_LB   8
/* Right back channel; Rear right channel. */
#define PCM_CHANNEL_RB   9
/* Top surround channel. */
#define PCM_CHANNEL_TS   10
/* Center vertical height channel.*/
#define PCM_CHANNEL_CVH  11
/* Mono surround channel.*/
#define PCM_CHANNEL_MS   12
/* Front left of center. */
#define PCM_CHANNEL_FLC  13
/* Front right of center. */
#define PCM_CHANNEL_FRC  14
/* Rear left of center. */
#define PCM_CHANNEL_RLC  15
/* Rear right of center. */
#define PCM_CHANNEL_RRC  16

typedef enum EDID_AUDIO_FORMAT_ID {
    LPCM = 1,
    AC3,
    MPEG1,
    MP3,
    MPEG2_MULTI_CHANNEL,
    AAC,
    DTS,
    ATRAC,
    SACD,
    DOLBY_DIGITAL_PLUS,
    DTS_HD,
    MAT,
    DST,
    WMA_PRO
} EDID_AUDIO_FORMAT_ID;

typedef struct EDID_AUDIO_BLOCK_INFO {
    EDID_AUDIO_FORMAT_ID nFormatId;
    int nSamplingFreq;
    int nBitsPerSample;
    int nChannels;
} EDID_AUDIO_BLOCK_INFO;

typedef struct EDID_AUDIO_INFO {
    int nAudioBlocks;
    unsigned char nSpeakerAllocation[MIN_SPKR_ALLOCATION_DATA_LENGTH];
    EDID_AUDIO_BLOCK_INFO AudioBlocksArray[MAX_EDID_BLOCKS];
    char channelMap[MAX_CHANNELS_SUPPORTED];
    int  channelAllocation;
} EDID_AUDIO_INFO;

class AudioUtil {
public:

    //Parses EDID audio block when if HDMI is connected to determine audio sink capabilities.
    static bool getHDMIAudioSinkCaps(EDID_AUDIO_INFO*);
    static bool getHDMIAudioSinkCaps(EDID_AUDIO_INFO*, char *hdmiEDIDData);

private:
    static int printFormatFromEDID(unsigned char format);
    static int getSamplingFrequencyFromEDID(unsigned char byte);
    static int getBitsPerSampleFromEDID(unsigned char byte,
                                        unsigned char format);
    static bool getSpeakerAllocation(EDID_AUDIO_INFO* pInfo);
    static void updateChannelMap(EDID_AUDIO_INFO* pInfo);
    static void updateChannelMapLPASS(EDID_AUDIO_INFO* pInfo);
    static void updateChannelAllocation(EDID_AUDIO_INFO* pInfo);
    static void printSpeakerAllocation(EDID_AUDIO_INFO* pInfo);
};

#endif /* ALSA_SOUND_AUDIO_UTIL_H */
