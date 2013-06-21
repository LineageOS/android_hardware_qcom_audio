/* AudioUtil.cpp
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

#define LOG_TAG "AudioUtil"
//#define LOG_NDEBUG 0
#include <utils/Log.h>

#include "AudioUtil.h"

int AudioUtil::printFormatFromEDID(unsigned char format) {
    switch (format) {
    case LPCM:
        ALOGV("Format:LPCM");
        break;
    case AC3:
        ALOGV("Format:AC-3");
        break;
    case MPEG1:
        ALOGV("Format:MPEG1 (Layers 1 & 2)");
        break;
    case MP3:
        ALOGV("Format:MP3 (MPEG1 Layer 3)");
        break;
    case MPEG2_MULTI_CHANNEL:
        ALOGV("Format:MPEG2 (multichannel)");
        break;
    case AAC:
        ALOGV("Format:AAC");
        break;
    case DTS:
        ALOGV("Format:DTS");
        break;
    case ATRAC:
        ALOGV("Format:ATRAC");
        break;
    case SACD:
        ALOGV("Format:One-bit audio aka SACD");
        break;
    case DOLBY_DIGITAL_PLUS:
        ALOGV("Format:Dolby Digital +");
        break;
    case DTS_HD:
        ALOGV("Format:DTS-HD");
        break;
    case MAT:
        ALOGV("Format:MAT (MLP)");
        break;
    case DST:
        ALOGV("Format:DST");
        break;
    case WMA_PRO:
        ALOGV("Format:WMA Pro");
        break;
    default:
        ALOGV("Invalid format ID....");
        break;
    }
    return format;
}

int AudioUtil::getSamplingFrequencyFromEDID(unsigned char byte) {
    int nFreq = 0;

    if (byte & BIT(6)) {
        ALOGV("192kHz");
        nFreq = 192000;
    } else if (byte & BIT(5)) {
        ALOGV("176kHz");
        nFreq = 176000;
    } else if (byte & BIT(4)) {
        ALOGV("96kHz");
        nFreq = 96000;
    } else if (byte & BIT(3)) {
        ALOGV("88.2kHz");
        nFreq = 88200;
    } else if (byte & BIT(2)) {
        ALOGV("48kHz");
        nFreq = 48000;
    } else if (byte & BIT(1)) {
        ALOGV("44.1kHz");
        nFreq = 44100;
    } else if (byte & BIT(0)) {
        ALOGV("32kHz");
        nFreq = 32000;
    }
    return nFreq;
}

int AudioUtil::getBitsPerSampleFromEDID(unsigned char byte,
    unsigned char format) {
    int nBitsPerSample = 0;
    if (format == 1) {
        if (byte & BIT(2)) {
            ALOGV("24bit");
            nBitsPerSample = 24;
        } else if (byte & BIT(1)) {
            ALOGV("20bit");
            nBitsPerSample = 20;
        } else if (byte & BIT(0)) {
            ALOGV("16bit");
            nBitsPerSample = 16;
        }
    } else {
        ALOGV("not lpcm format, return 0");
        return 0;
    }
    return nBitsPerSample;
}

bool AudioUtil::getHDMIAudioSinkCaps(EDID_AUDIO_INFO* pInfo) {
    unsigned char channels[16];
    unsigned char formats[16];
    unsigned char frequency[16];
    unsigned char bitrate[16];
    unsigned char* data = NULL;
    unsigned char* original_data_ptr = NULL;
    int count = 0;
    bool bRet = false;
    const char* file = "/sys/class/graphics/fb1/audio_data_block";
    FILE* fpaudiocaps = fopen(file, "rb");
    if (fpaudiocaps) {
        ALOGV("opened audio_caps successfully...");
        fseek(fpaudiocaps, 0, SEEK_END);
        long size = ftell(fpaudiocaps);
        ALOGV("audiocaps size is %ld\n",size);
        data = (unsigned char*) malloc(size);
        if (data) {
            fseek(fpaudiocaps, 0, SEEK_SET);
            original_data_ptr = data;
            fread(data, 1, size, fpaudiocaps);
        }
        fclose(fpaudiocaps);
    } else {
        ALOGE("failed to open audio_caps");
    }

    if (pInfo && data) {
        int length = 0;
        memcpy(&count,  data, sizeof(int));
        data+= sizeof(int);
        ALOGV("#Audio Block Count is %d",count);
        memcpy(&length, data, sizeof(int));
        data += sizeof(int);
        ALOGV("Total length is %d",length);
        unsigned int sad[MAX_SHORT_AUDIO_DESC_CNT];
        int nblockindex = 0;
        int nCountDesc = 0;
        while (length >= MIN_AUDIO_DESC_LENGTH && count < MAX_SHORT_AUDIO_DESC_CNT) {
            sad[nblockindex] = (unsigned int)data[0] + ((unsigned int)data[1] << 8)
                               + ((unsigned int)data[2] << 16);
            nblockindex+=1;
            nCountDesc++;
            length -= MIN_AUDIO_DESC_LENGTH;
            data += MIN_AUDIO_DESC_LENGTH;
        }
        memset(pInfo, 0, sizeof(EDID_AUDIO_INFO));
        pInfo->nAudioBlocks = nCountDesc;
        ALOGV("Total # of audio descriptors %d",nCountDesc);
        int nIndex = 0;
        while (nCountDesc--) {
              channels [nIndex]   = (sad[nIndex] & 0x7) + 1;
              formats  [nIndex]   = (sad[nIndex] & 0xFF) >> 3;
              frequency[nIndex]   = (sad[nIndex] >> 8) & 0xFF;
              bitrate  [nIndex]   = (sad[nIndex] >> 16) & 0xFF;
              nIndex++;
        }
        bRet = true;
        for (int i = 0; i < pInfo->nAudioBlocks; i++) {
            ALOGV("AUDIO DESC BLOCK # %d\n",i);

            pInfo->AudioBlocksArray[i].nChannels = channels[i];
            ALOGV("pInfo->AudioBlocksArray[i].nChannels %d\n", pInfo->AudioBlocksArray[i].nChannels);

            ALOGV("Format Byte %d\n", formats[i]);
            pInfo->AudioBlocksArray[i].nFormatId = (EDID_AUDIO_FORMAT_ID)printFormatFromEDID(formats[i]);
            ALOGV("pInfo->AudioBlocksArray[i].nFormatId %d",pInfo->AudioBlocksArray[i].nFormatId);

            ALOGV("Frequency Byte %d\n", frequency[i]);
            pInfo->AudioBlocksArray[i].nSamplingFreq = getSamplingFrequencyFromEDID(frequency[i]);
            ALOGV("pInfo->AudioBlocksArray[i].nSamplingFreq %d",pInfo->AudioBlocksArray[i].nSamplingFreq);

            ALOGV("BitsPerSample Byte %d\n", bitrate[i]);
            pInfo->AudioBlocksArray[i].nBitsPerSample = getBitsPerSampleFromEDID(bitrate[i],formats[i]);
            ALOGV("pInfo->AudioBlocksArray[i].nBitsPerSample %d",pInfo->AudioBlocksArray[i].nBitsPerSample);
        }
            getSpeakerAllocation(pInfo);
    }
    if (original_data_ptr)
        free(original_data_ptr);

    return bRet;
}

bool AudioUtil::getHDMIAudioSinkCaps(EDID_AUDIO_INFO* pInfo, char *hdmiEDIDData) {
    unsigned char channels[16];
    unsigned char formats[16];
    unsigned char frequency[16];
    unsigned char bitrate[16];
    unsigned char* data = NULL;
    unsigned char* original_data_ptr = NULL;
    if (pInfo && hdmiEDIDData) {
        int length = 0, nCountDesc = 0;

        length = (int) *hdmiEDIDData++;
        ALOGV("Total length is %d",length);

        nCountDesc = length/MIN_AUDIO_DESC_LENGTH;

        memset(pInfo, 0, sizeof(EDID_AUDIO_INFO));
        pInfo->nAudioBlocks = nCountDesc-1;
        ALOGV("Total # of audio descriptors %d",nCountDesc);

        for(int i=0; i<nCountDesc-1; i++) {
                 // last block for speaker allocation;
              channels [i]   = (*hdmiEDIDData & 0x7) + 1;
              formats  [i]   = (*hdmiEDIDData++) >> 3;
              frequency[i]   = *hdmiEDIDData++;
              bitrate  [i]   = *hdmiEDIDData++;
        }
        pInfo->nSpeakerAllocation[0] = *hdmiEDIDData++;
        pInfo->nSpeakerAllocation[1] = *hdmiEDIDData++;
        pInfo->nSpeakerAllocation[2] = *hdmiEDIDData++;

        updateChannelMap(pInfo);
        updateChannelAllocation(pInfo);
        updateChannelMapLPASS(pInfo);

        for (int i = 0; i < pInfo->nAudioBlocks; i++) {
            ALOGV("AUDIO DESC BLOCK # %d\n",i);

            pInfo->AudioBlocksArray[i].nChannels = channels[i];
            ALOGV("pInfo->AudioBlocksArray[i].nChannels %d\n", pInfo->AudioBlocksArray[i].nChannels);

            ALOGV("Format Byte %d\n", formats[i]);
            pInfo->AudioBlocksArray[i].nFormatId = (EDID_AUDIO_FORMAT_ID)printFormatFromEDID(formats[i]);
            ALOGV("pInfo->AudioBlocksArray[i].nFormatId %d",pInfo->AudioBlocksArray[i].nFormatId);

            ALOGV("Frequency Byte %d\n", frequency[i]);
            pInfo->AudioBlocksArray[i].nSamplingFreq = getSamplingFrequencyFromEDID(frequency[i]);
            ALOGV("pInfo->AudioBlocksArray[i].nSamplingFreq %d",pInfo->AudioBlocksArray[i].nSamplingFreq);

            ALOGV("BitsPerSample Byte %d\n", bitrate[i]);
            pInfo->AudioBlocksArray[i].nBitsPerSample = getBitsPerSampleFromEDID(bitrate[i],formats[i]);
            ALOGV("pInfo->AudioBlocksArray[i].nBitsPerSample %d",pInfo->AudioBlocksArray[i].nBitsPerSample);
        }
        printSpeakerAllocation(pInfo);
        return true;
    } else {
        ALOGE("No valid EDID");
        return false;
    }
}

bool AudioUtil::getSpeakerAllocation(EDID_AUDIO_INFO* pInfo) {
    int count = 0;
    bool bRet = false;
    unsigned char* data = NULL;
    unsigned char* original_data_ptr = NULL;
    const char* spkrfile = "/sys/class/graphics/fb1/spkr_alloc_data_block";
    FILE* fpspkrfile = fopen(spkrfile, "rb");
    if(fpspkrfile) {
        ALOGV("opened spkr_alloc_data_block successfully...");
        fseek(fpspkrfile,0,SEEK_END);
        long size = ftell(fpspkrfile);
        ALOGV("fpspkrfile size is %ld\n",size);
        data = (unsigned char*)malloc(size);
        if(data) {
            original_data_ptr = data;
            fseek(fpspkrfile,0,SEEK_SET);
            fread(data,1,size,fpspkrfile);
        }
        fclose(fpspkrfile);
    } else {
        ALOGE("failed to open fpspkrfile");
    }

    if(pInfo && data) {
        int length = 0;
        memcpy(&count,  data, sizeof(int));
        ALOGV("Count is %d",count);
        data += sizeof(int);
        memcpy(&length, data, sizeof(int));
        ALOGV("Total length is %d",length);
        data+= sizeof(int);
        ALOGV("Total speaker allocation Block count # %d\n",count);
        bRet = true;
        for (int i = 0; i < count; i++) {
            ALOGV("Speaker Allocation BLOCK # %d\n",i);
            pInfo->nSpeakerAllocation[0] = data[0];
            pInfo->nSpeakerAllocation[1] = data[1];
            pInfo->nSpeakerAllocation[2] = data[2];
            ALOGV("pInfo->nSpeakerAllocation %x %x %x\n", data[0],data[1],data[2]);


            if (pInfo->nSpeakerAllocation[0] & BIT(7)) {
                 ALOGV("FLW/FRW");
            } else if (pInfo->nSpeakerAllocation[0] & BIT(6)) {
                 ALOGV("RLC/RRC");
            } else if (pInfo->nSpeakerAllocation[0] & BIT(5)) {
                 ALOGV("FLC/FRC");
            } else if (pInfo->nSpeakerAllocation[0] & BIT(4)) {
                ALOGV("RC");
            } else if (pInfo->nSpeakerAllocation[0] & BIT(3)) {
                ALOGV("RL/RR");
            } else if (pInfo->nSpeakerAllocation[0] & BIT(2)) {
                ALOGV("FC");
            } else if (pInfo->nSpeakerAllocation[0] & BIT(1)) {
                ALOGV("LFE");
            } else if (pInfo->nSpeakerAllocation[0] & BIT(0)) {
                ALOGV("FL/FR");
            }

            if (pInfo->nSpeakerAllocation[1] & BIT(2)) {
                ALOGV("FCH");
            } else if (pInfo->nSpeakerAllocation[1] & BIT(1)) {
                ALOGV("TC");
            } else if (pInfo->nSpeakerAllocation[1] & BIT(0)) {
                ALOGV("FLH/FRH");
            }
        }
    }
    if (original_data_ptr)
        free(original_data_ptr);
    return bRet;
}

void AudioUtil::updateChannelMap(EDID_AUDIO_INFO* pInfo)
{
    if(pInfo) {
        memset(pInfo->channelMap, 0, MAX_CHANNELS_SUPPORTED);
        if(pInfo->nSpeakerAllocation[0] & BIT(0)) {
            pInfo->channelMap[0] = PCM_CHANNEL_FL;
            pInfo->channelMap[1] = PCM_CHANNEL_FR;
        }
        if(pInfo->nSpeakerAllocation[0] & BIT(1)) {
            pInfo->channelMap[2] = PCM_CHANNEL_LFE;
        }
        if(pInfo->nSpeakerAllocation[0] & BIT(2)) {
            pInfo->channelMap[3] = PCM_CHANNEL_FC;
        }
        if(pInfo->nSpeakerAllocation[0] & BIT(3)) {
            pInfo->channelMap[4] = PCM_CHANNEL_LB;
            pInfo->channelMap[5] = PCM_CHANNEL_RB;
        }
        if(pInfo->nSpeakerAllocation[0] & BIT(4)) {
            if(pInfo->nSpeakerAllocation[0] & BIT(3)) {
                pInfo->channelMap[6] = PCM_CHANNEL_CS;
                pInfo->channelMap[7] = 0;
            } else if (pInfo->nSpeakerAllocation[1] & BIT(1)) {
                pInfo->channelMap[6] = PCM_CHANNEL_CS;
                pInfo->channelMap[7] = PCM_CHANNEL_TS;
            } else if (pInfo->nSpeakerAllocation[1] & BIT(2)) {
                pInfo->channelMap[6] = PCM_CHANNEL_CS;
                pInfo->channelMap[7] = PCM_CHANNEL_CVH;
            } else {
                pInfo->channelMap[4] = PCM_CHANNEL_CS;
                pInfo->channelMap[5] = 0;
            }
        }
        if(pInfo->nSpeakerAllocation[0] & BIT(5)) {
            pInfo->channelMap[6] = PCM_CHANNEL_FLC;
            pInfo->channelMap[7] = PCM_CHANNEL_FRC;
        }
        if(pInfo->nSpeakerAllocation[0] & BIT(6)) {
            pInfo->nSpeakerAllocation[0] &= 0xef;
            // If RLC/RRC is present, RC is invalid as per specification
            pInfo->channelMap[6] = PCM_CHANNEL_RLC;
            pInfo->channelMap[7] = PCM_CHANNEL_RRC;
        }
        if(pInfo->nSpeakerAllocation[0] & BIT(7)) {
            pInfo->channelMap[6] = 0; // PCM_CHANNEL_FLW; but not defined by LPASS
            pInfo->channelMap[7] = 0; // PCM_CHANNEL_FRW; but not defined by LPASS
        }
        if(pInfo->nSpeakerAllocation[1] & BIT(0)) {
            pInfo->channelMap[6] = 0; // PCM_CHANNEL_FLH; but not defined by LPASS
            pInfo->channelMap[7] = 0; // PCM_CHANNEL_FRH; but not defined by LPASS
        }
    }
}

void AudioUtil::printSpeakerAllocation(EDID_AUDIO_INFO* pInfo) {
    if(pInfo) {
        if (pInfo->nSpeakerAllocation[0] & BIT(7))
            ALOGV("FLW/FRW");
        if (pInfo->nSpeakerAllocation[0] & BIT(6))
            ALOGV("RLC/RRC");
        if (pInfo->nSpeakerAllocation[0] & BIT(5))
            ALOGV("FLC/FRC");
        if (pInfo->nSpeakerAllocation[0] & BIT(4))
            ALOGV("RC");
        if (pInfo->nSpeakerAllocation[0] & BIT(3))
            ALOGV("RL/RR");
        if (pInfo->nSpeakerAllocation[0] & BIT(2))
            ALOGV("FC");
        if (pInfo->nSpeakerAllocation[0] & BIT(1))
            ALOGV("LFE");
        if (pInfo->nSpeakerAllocation[0] & BIT(0))
            ALOGV("FL/FR");

        if (pInfo->nSpeakerAllocation[1] & BIT(2))
            ALOGV("FCH");
        if (pInfo->nSpeakerAllocation[1] & BIT(1))
            ALOGV("TC");
        if (pInfo->nSpeakerAllocation[1] & BIT(0))
            ALOGV("FLH/FRH");
    }
}

void AudioUtil::updateChannelAllocation(EDID_AUDIO_INFO* pInfo)
{
    if(pInfo) {
        int16_t ca = 0;
        int16_t spkAlloc = ((pInfo->nSpeakerAllocation[1]) << 8) |
                           (pInfo->nSpeakerAllocation[0]);
        ALOGV("pInfo->nSpeakerAllocation %x %x\n", pInfo->nSpeakerAllocation[0],
                                                   pInfo->nSpeakerAllocation[1]);
        ALOGV("spkAlloc: %x", spkAlloc);

        switch(spkAlloc) {
        case (BIT(0)):                                           ca = 0x00; break;
        case (BIT(0)|BIT(1)):                                    ca = 0x01; break;
        case (BIT(0)|BIT(2)):                                    ca = 0x02; break;
        case (BIT(0)|BIT(1)|BIT(2)):                             ca = 0x03; break;
        case (BIT(0)|BIT(4)):                                    ca = 0x04; break;
        case (BIT(0)|BIT(1)|BIT(4)):                             ca = 0x05; break;
        case (BIT(0)|BIT(2)|BIT(4)):                             ca = 0x06; break;
        case (BIT(0)|BIT(1)|BIT(2)|BIT(4)):                      ca = 0x07; break;
        case (BIT(0)|BIT(3)):                                    ca = 0x08; break;
        case (BIT(0)|BIT(1)|BIT(3)):                             ca = 0x09; break;
        case (BIT(0)|BIT(2)|BIT(3)):                             ca = 0x0A; break;
        case (BIT(0)|BIT(1)|BIT(2)|BIT(3)):                      ca = 0x0B; break;
        case (BIT(0)|BIT(3)|BIT(4)):                             ca = 0x0C; break;
        case (BIT(0)|BIT(1)|BIT(3)|BIT(4)):                      ca = 0x0D; break;
        case (BIT(0)|BIT(2)|BIT(3)|BIT(4)):                      ca = 0x0E; break;
        case (BIT(0)|BIT(1)|BIT(2)|BIT(3)|BIT(4)):               ca = 0x0F; break;
        case (BIT(0)|BIT(3)|BIT(6)):                             ca = 0x10; break;
        case (BIT(0)|BIT(1)|BIT(3)|BIT(6)):                      ca = 0x11; break;
        case (BIT(0)|BIT(2)|BIT(3)|BIT(6)):                      ca = 0x12; break;
        case (BIT(0)|BIT(1)|BIT(2)|BIT(3)|BIT(6)):               ca = 0x13; break;
        case (BIT(0)|BIT(5)):                                    ca = 0x14; break;
        case (BIT(0)|BIT(1)|BIT(5)):                             ca = 0x15; break;
        case (BIT(0)|BIT(2)|BIT(5)):                             ca = 0x16; break;
        case (BIT(0)|BIT(1)|BIT(2)|BIT(5)):                      ca = 0x17; break;
        case (BIT(0)|BIT(4)|BIT(5)):                             ca = 0x18; break;
        case (BIT(0)|BIT(1)|BIT(4)|BIT(5)):                      ca = 0x19; break;
        case (BIT(0)|BIT(2)|BIT(4)|BIT(5)):                      ca = 0x1A; break;
        case (BIT(0)|BIT(1)|BIT(2)|BIT(4)|BIT(5)):               ca = 0x1B; break;
        case (BIT(0)|BIT(3)|BIT(5)):                             ca = 0x1C; break;
        case (BIT(0)|BIT(1)|BIT(3)|BIT(5)):                      ca = 0x1D; break;
        case (BIT(0)|BIT(2)|BIT(3)|BIT(5)):                      ca = 0x1E; break;
        case (BIT(0)|BIT(1)|BIT(2)|BIT(3)|BIT(5)):               ca = 0x1F; break;
        case (BIT(0)|BIT(2)|BIT(3)|BIT(10)):                     ca = 0x20; break;
        case (BIT(0)|BIT(1)|BIT(2)|BIT(3)|BIT(10)):              ca = 0x21; break;
        case (BIT(0)|BIT(2)|BIT(3)|BIT(9)):                      ca = 0x22; break;
        case (BIT(0)|BIT(1)|BIT(2)|BIT(3)|BIT(9)):               ca = 0x23; break;
        case (BIT(0)|BIT(3)|BIT(8)):                             ca = 0x24; break;
        case (BIT(0)|BIT(1)|BIT(3)|BIT(8)):                      ca = 0x25; break;
        case (BIT(0)|BIT(3)|BIT(7)):                             ca = 0x26; break;
        case (BIT(0)|BIT(1)|BIT(3)|BIT(7)):                      ca = 0x27; break;
        case (BIT(0)|BIT(2)|BIT(3)|BIT(4)|BIT(9)):               ca = 0x28; break;
        case (BIT(0)|BIT(1)|BIT(2)|BIT(3)|BIT(4)|BIT(9)):        ca = 0x29; break;
        case (BIT(0)|BIT(2)|BIT(3)|BIT(4)|BIT(10)):              ca = 0x2A; break;
        case (BIT(0)|BIT(1)|BIT(2)|BIT(3)|BIT(4)|BIT(10)):       ca = 0x2B; break;
        case (BIT(0)|BIT(2)|BIT(3)|BIT(9)|BIT(10)):              ca = 0x2C; break;
        case (BIT(0)|BIT(1)|BIT(2)|BIT(3)|BIT(9)|BIT(10)):       ca = 0x2D; break;
        case (BIT(0)|BIT(2)|BIT(3)|BIT(8)):                      ca = 0x2E; break;
        case (BIT(0)|BIT(1)|BIT(2)|BIT(3)|BIT(8)):               ca = 0x2F; break;
        case (BIT(0)|BIT(2)|BIT(3)|BIT(7)):                      ca = 0x30; break;
        case (BIT(0)|BIT(1)|BIT(2)|BIT(3)|BIT(7)):               ca = 0x31; break;
        default:                                                 ca = 0x0;  break;
        }
        ALOGV("channel Allocation: %x", ca);
        pInfo->channelAllocation = ca;
    }
}

void AudioUtil::updateChannelMapLPASS(EDID_AUDIO_INFO* pInfo)
{
    if(pInfo) {
        if(pInfo->channelAllocation <= 0x1f)
            memset(pInfo->channelMap, 0, MAX_CHANNELS_SUPPORTED);
        switch(pInfo->channelAllocation) {
        case 0x0:
            pInfo->channelMap[0] = PCM_CHANNEL_FL;
            pInfo->channelMap[1] = PCM_CHANNEL_FR;
            break;
        case 0x1:
            pInfo->channelMap[0] = PCM_CHANNEL_FL;
            pInfo->channelMap[1] = PCM_CHANNEL_FR;
            pInfo->channelMap[2] = PCM_CHANNEL_LFE;
            break;
        case 0x2:
            pInfo->channelMap[0] = PCM_CHANNEL_FL;
            pInfo->channelMap[1] = PCM_CHANNEL_FR;
            pInfo->channelMap[2] = PCM_CHANNEL_FC;
            break;
        case 0x3:
            pInfo->channelMap[0] = PCM_CHANNEL_FL;
            pInfo->channelMap[1] = PCM_CHANNEL_FR;
            pInfo->channelMap[2] = PCM_CHANNEL_LFE;
            pInfo->channelMap[3] = PCM_CHANNEL_FC;
            break;
        case 0x4:
            pInfo->channelMap[0] = PCM_CHANNEL_FL;
            pInfo->channelMap[1] = PCM_CHANNEL_FR;
            pInfo->channelMap[2] = PCM_CHANNEL_CS;
            break;
        case 0x5:
            pInfo->channelMap[0] = PCM_CHANNEL_FL;
            pInfo->channelMap[1] = PCM_CHANNEL_FR;
            pInfo->channelMap[2] = PCM_CHANNEL_LFE;
            pInfo->channelMap[3] = PCM_CHANNEL_CS;
            break;
        case 0x6:
            pInfo->channelMap[0] = PCM_CHANNEL_FL;
            pInfo->channelMap[1] = PCM_CHANNEL_FR;
            pInfo->channelMap[2] = PCM_CHANNEL_FC;
            pInfo->channelMap[3] = PCM_CHANNEL_CS;
            break;
        case 0x7:
            pInfo->channelMap[0] = PCM_CHANNEL_FL;
            pInfo->channelMap[1] = PCM_CHANNEL_FR;
            pInfo->channelMap[2] = PCM_CHANNEL_LFE;
            pInfo->channelMap[3] = PCM_CHANNEL_FC;
            pInfo->channelMap[4] = PCM_CHANNEL_CS;
            break;
        case 0x8:
            pInfo->channelMap[0] = PCM_CHANNEL_FL;
            pInfo->channelMap[1] = PCM_CHANNEL_FR;
            pInfo->channelMap[2] = PCM_CHANNEL_LB;
            pInfo->channelMap[3] = PCM_CHANNEL_RB;
            break;
        case 0x9:
            pInfo->channelMap[0] = PCM_CHANNEL_FL;
            pInfo->channelMap[1] = PCM_CHANNEL_FR;
            pInfo->channelMap[2] = PCM_CHANNEL_LFE;
            pInfo->channelMap[3] = PCM_CHANNEL_LB;
            pInfo->channelMap[4] = PCM_CHANNEL_RB;
            break;
        case 0xa:
            pInfo->channelMap[0] = PCM_CHANNEL_FL;
            pInfo->channelMap[1] = PCM_CHANNEL_FR;
            pInfo->channelMap[2] = PCM_CHANNEL_FC;
            pInfo->channelMap[3] = PCM_CHANNEL_LB;
            pInfo->channelMap[4] = PCM_CHANNEL_RB;
            break;
        case 0xb:
            pInfo->channelMap[0] = PCM_CHANNEL_FL;
            pInfo->channelMap[1] = PCM_CHANNEL_FR;
            pInfo->channelMap[2] = PCM_CHANNEL_LFE;
            pInfo->channelMap[3] = PCM_CHANNEL_FC;
            pInfo->channelMap[4] = PCM_CHANNEL_LB;
            pInfo->channelMap[5] = PCM_CHANNEL_RB;
            break;
        case 0xc:
            pInfo->channelMap[0] = PCM_CHANNEL_FL;
            pInfo->channelMap[1] = PCM_CHANNEL_FR;
            pInfo->channelMap[2] = PCM_CHANNEL_LB;
            pInfo->channelMap[3] = PCM_CHANNEL_RB;
            pInfo->channelMap[4] = PCM_CHANNEL_CS;
            break;
        case 0xd:
            pInfo->channelMap[0] = PCM_CHANNEL_FL;
            pInfo->channelMap[1] = PCM_CHANNEL_FR;
            pInfo->channelMap[2] = PCM_CHANNEL_LFE;
            pInfo->channelMap[3] = PCM_CHANNEL_LB;
            pInfo->channelMap[4] = PCM_CHANNEL_RB;
            pInfo->channelMap[5] = PCM_CHANNEL_CS;
            break;
        case 0xe:
            pInfo->channelMap[0] = PCM_CHANNEL_FL;
            pInfo->channelMap[1] = PCM_CHANNEL_FR;
            pInfo->channelMap[2] = PCM_CHANNEL_FC;
            pInfo->channelMap[3] = PCM_CHANNEL_LB;
            pInfo->channelMap[4] = PCM_CHANNEL_RB;
            pInfo->channelMap[5] = PCM_CHANNEL_CS;
            break;
        case 0xf:
            pInfo->channelMap[0] = PCM_CHANNEL_FL;
            pInfo->channelMap[1] = PCM_CHANNEL_FR;
            pInfo->channelMap[2] = PCM_CHANNEL_LFE;
            pInfo->channelMap[3] = PCM_CHANNEL_FC;
            pInfo->channelMap[4] = PCM_CHANNEL_LB;
            pInfo->channelMap[5] = PCM_CHANNEL_RB;
            pInfo->channelMap[6] = PCM_CHANNEL_CS;
            break;
        case 0x10:
            pInfo->channelMap[0] = PCM_CHANNEL_FL;
            pInfo->channelMap[1] = PCM_CHANNEL_FR;
            pInfo->channelMap[2] = PCM_CHANNEL_LB;
            pInfo->channelMap[3] = PCM_CHANNEL_RB;
            pInfo->channelMap[4] = PCM_CHANNEL_RLC;
            pInfo->channelMap[5] = PCM_CHANNEL_RRC;
            break;
        case 0x11:
            pInfo->channelMap[0] = PCM_CHANNEL_FL;
            pInfo->channelMap[1] = PCM_CHANNEL_FR;
            pInfo->channelMap[2] = PCM_CHANNEL_LFE;
            pInfo->channelMap[3] = PCM_CHANNEL_LB;
            pInfo->channelMap[4] = PCM_CHANNEL_RB;
            pInfo->channelMap[5] = PCM_CHANNEL_RLC;
            pInfo->channelMap[6] = PCM_CHANNEL_RRC;
            break;
        case 0x12:
            pInfo->channelMap[0] = PCM_CHANNEL_FL;
            pInfo->channelMap[1] = PCM_CHANNEL_FR;
            pInfo->channelMap[2] = PCM_CHANNEL_FC;
            pInfo->channelMap[3] = PCM_CHANNEL_LB;
            pInfo->channelMap[4] = PCM_CHANNEL_RB;
            pInfo->channelMap[5] = PCM_CHANNEL_RLC;
            pInfo->channelMap[6] = PCM_CHANNEL_RRC;
            break;
        case 0x13:
            pInfo->channelMap[0] = PCM_CHANNEL_FL;
            pInfo->channelMap[1] = PCM_CHANNEL_FR;
            pInfo->channelMap[2] = PCM_CHANNEL_LFE;
            pInfo->channelMap[3] = PCM_CHANNEL_FC;
            pInfo->channelMap[4] = PCM_CHANNEL_LB;
            pInfo->channelMap[5] = PCM_CHANNEL_RB;
            pInfo->channelMap[6] = PCM_CHANNEL_RLC;
            pInfo->channelMap[7] = PCM_CHANNEL_RRC;
            break;
        case 0x14:
            pInfo->channelMap[0] = PCM_CHANNEL_FL;
            pInfo->channelMap[1] = PCM_CHANNEL_FR;
            pInfo->channelMap[2] = PCM_CHANNEL_FLC;
            pInfo->channelMap[3] = PCM_CHANNEL_FRC;
            break;
        case 0x15:
            pInfo->channelMap[0] = PCM_CHANNEL_FL;
            pInfo->channelMap[1] = PCM_CHANNEL_FR;
            pInfo->channelMap[2] = PCM_CHANNEL_LFE;
            pInfo->channelMap[3] = PCM_CHANNEL_FLC;
            pInfo->channelMap[4] = PCM_CHANNEL_FRC;
            break;
        case 0x16:
            pInfo->channelMap[0] = PCM_CHANNEL_FL;
            pInfo->channelMap[1] = PCM_CHANNEL_FR;
            pInfo->channelMap[2] = PCM_CHANNEL_FC;
            pInfo->channelMap[3] = PCM_CHANNEL_FLC;
            pInfo->channelMap[4] = PCM_CHANNEL_FRC;
            break;
        case 0x17:
            pInfo->channelMap[0] = PCM_CHANNEL_FL;
            pInfo->channelMap[1] = PCM_CHANNEL_FR;
            pInfo->channelMap[2] = PCM_CHANNEL_LFE;
            pInfo->channelMap[3] = PCM_CHANNEL_FC;
            pInfo->channelMap[4] = PCM_CHANNEL_FLC;
            pInfo->channelMap[5] = PCM_CHANNEL_FRC;
            break;
        case 0x18:
            pInfo->channelMap[0] = PCM_CHANNEL_FL;
            pInfo->channelMap[1] = PCM_CHANNEL_FR;
            pInfo->channelMap[2] = PCM_CHANNEL_CS;
            pInfo->channelMap[3] = PCM_CHANNEL_FLC;
            pInfo->channelMap[4] = PCM_CHANNEL_FRC;
            break;
        case 0x19:
            pInfo->channelMap[0] = PCM_CHANNEL_FL;
            pInfo->channelMap[1] = PCM_CHANNEL_FR;
            pInfo->channelMap[2] = PCM_CHANNEL_LFE;
            pInfo->channelMap[3] = PCM_CHANNEL_CS;
            pInfo->channelMap[4] = PCM_CHANNEL_FLC;
            pInfo->channelMap[5] = PCM_CHANNEL_FRC;
            break;
        case 0x1a:
            pInfo->channelMap[0] = PCM_CHANNEL_FL;
            pInfo->channelMap[1] = PCM_CHANNEL_FR;
            pInfo->channelMap[2] = PCM_CHANNEL_FC;
            pInfo->channelMap[3] = PCM_CHANNEL_CS;
            pInfo->channelMap[4] = PCM_CHANNEL_FLC;
            pInfo->channelMap[5] = PCM_CHANNEL_FRC;
            break;
        case 0x1b:
            pInfo->channelMap[0] = PCM_CHANNEL_FL;
            pInfo->channelMap[1] = PCM_CHANNEL_FR;
            pInfo->channelMap[2] = PCM_CHANNEL_LFE;
            pInfo->channelMap[3] = PCM_CHANNEL_FC;
            pInfo->channelMap[4] = PCM_CHANNEL_CS;
            pInfo->channelMap[5] = PCM_CHANNEL_FLC;
            pInfo->channelMap[6] = PCM_CHANNEL_FRC;
            break;
        case 0x1c:
            pInfo->channelMap[0] = PCM_CHANNEL_FL;
            pInfo->channelMap[1] = PCM_CHANNEL_FR;
            pInfo->channelMap[2] = PCM_CHANNEL_LB;
            pInfo->channelMap[3] = PCM_CHANNEL_RB;
            pInfo->channelMap[4] = PCM_CHANNEL_FLC;
            pInfo->channelMap[5] = PCM_CHANNEL_FRC;
            break;
        case 0x1d:
            pInfo->channelMap[0] = PCM_CHANNEL_FL;
            pInfo->channelMap[1] = PCM_CHANNEL_FR;
            pInfo->channelMap[2] = PCM_CHANNEL_LFE;
            pInfo->channelMap[3] = PCM_CHANNEL_LB;
            pInfo->channelMap[4] = PCM_CHANNEL_RB;
            pInfo->channelMap[5] = PCM_CHANNEL_FLC;
            pInfo->channelMap[6] = PCM_CHANNEL_FRC;
            break;
        case 0x1e:
            pInfo->channelMap[0] = PCM_CHANNEL_FL;
            pInfo->channelMap[1] = PCM_CHANNEL_FR;
            pInfo->channelMap[2] = PCM_CHANNEL_FC;
            pInfo->channelMap[3] = PCM_CHANNEL_LB;
            pInfo->channelMap[4] = PCM_CHANNEL_RB;
            pInfo->channelMap[5] = PCM_CHANNEL_FLC;
            pInfo->channelMap[6] = PCM_CHANNEL_FRC;
            break;
        case 0x1f:
            pInfo->channelMap[0] = PCM_CHANNEL_FL;
            pInfo->channelMap[1] = PCM_CHANNEL_FR;
            pInfo->channelMap[2] = PCM_CHANNEL_LFE;
            pInfo->channelMap[3] = PCM_CHANNEL_FC;
            pInfo->channelMap[4] = PCM_CHANNEL_LB;
            pInfo->channelMap[5] = PCM_CHANNEL_RB;
            pInfo->channelMap[6] = PCM_CHANNEL_FLC;
            pInfo->channelMap[7] = PCM_CHANNEL_FRC;
            break;
        default:
            break;
        }
    }
}
