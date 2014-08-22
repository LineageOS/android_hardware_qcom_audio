/*
 * Copyright (c) 2014, The Linux Foundation. All rights reserved.
 * Not a Contribution.
 *
 * Copyright (C) 2014 The Android Open Source Project
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

#define LOG_TAG "audio_hw_edid"
//#define LOG_NDEBUG 0
//#define LOG_NDDEBUG 0

#include <errno.h>
#include <cutils/properties.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <cutils/str_parms.h>
#include <cutils/log.h>

#include "audio_hw.h"
#include "platform.h"
#include "platform_api.h"
#include "edid.h"

static int get_edid_format(unsigned char format) {
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

static int get_edid_sf(unsigned char byte) {
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

static int get_edid_bps(unsigned char byte,
    unsigned char format) {
    int bits_per_sample = 0;
    if (format == 1) {
        if (byte & BIT(2)) {
            ALOGV("24bit");
            bits_per_sample = 24;
        } else if (byte & BIT(1)) {
            ALOGV("20bit");
            bits_per_sample = 20;
        } else if (byte & BIT(0)) {
            ALOGV("16bit");
            bits_per_sample = 16;
        }
    } else {
        ALOGV("not lpcm format, return 0");
        return 0;
    }
    return bits_per_sample;
}


static bool get_speaker_allocation(edid_audio_info* pInfo) {
    int count = 0;
    int i = 0;
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
        for (i = 0; i < count; i++) {
            ALOGV("Speaker Allocation BLOCK # %d\n",i);
            pInfo->speaker_allocation[0] = data[0];
            pInfo->speaker_allocation[1] = data[1];
            pInfo->speaker_allocation[2] = data[2];
            ALOGV("pInfo->speaker_allocation %x %x %x\n", data[0],data[1],data[2]);


            if (pInfo->speaker_allocation[0] & BIT(7))
                 ALOGV("FLW/FRW");
            if (pInfo->speaker_allocation[0] & BIT(6))
                 ALOGV("RLC/RRC");
            if (pInfo->speaker_allocation[0] & BIT(5))
                 ALOGV("FLC/FRC");
            if (pInfo->speaker_allocation[0] & BIT(4))
                ALOGV("RC");
            if (pInfo->speaker_allocation[0] & BIT(3))
                ALOGV("RL/RR");
            if (pInfo->speaker_allocation[0] & BIT(2))
                ALOGV("FC");
            if (pInfo->speaker_allocation[0] & BIT(1))
                ALOGV("LFE");
            if (pInfo->speaker_allocation[0] & BIT(0))
                ALOGV("FL/FR");
            if (pInfo->speaker_allocation[1] & BIT(2))
                ALOGV("FCH");
            if (pInfo->speaker_allocation[1] & BIT(1))
                ALOGV("TC");
            if (pInfo->speaker_allocation[1] & BIT(0))
                ALOGV("FLH/FRH");
        }
    }
    if (original_data_ptr)
        free(original_data_ptr);
    return bRet;
}

static void update_channel_map(edid_audio_info* pInfo)
{
    if(pInfo) {
        memset(pInfo->channel_map, 0, MAX_CHANNELS_SUPPORTED);
        if(pInfo->speaker_allocation[0] & BIT(0)) {
            pInfo->channel_map[0] = PCM_CHANNEL_FL;
            pInfo->channel_map[1] = PCM_CHANNEL_FR;
        }
        if(pInfo->speaker_allocation[0] & BIT(1)) {
            pInfo->channel_map[2] = PCM_CHANNEL_LFE;
        }
        if(pInfo->speaker_allocation[0] & BIT(2)) {
            pInfo->channel_map[3] = PCM_CHANNEL_FC;
        }
        if(pInfo->speaker_allocation[0] & BIT(3)) {
            pInfo->channel_map[4] = PCM_CHANNEL_LB;
            pInfo->channel_map[5] = PCM_CHANNEL_RB;
        }
        if(pInfo->speaker_allocation[0] & BIT(4)) {
            if(pInfo->speaker_allocation[0] & BIT(3)) {
                pInfo->channel_map[6] = PCM_CHANNEL_CS;
                pInfo->channel_map[7] = 0;
            } else if (pInfo->speaker_allocation[1] & BIT(1)) {
                pInfo->channel_map[6] = PCM_CHANNEL_CS;
                pInfo->channel_map[7] = PCM_CHANNEL_TS;
            } else if (pInfo->speaker_allocation[1] & BIT(2)) {
                pInfo->channel_map[6] = PCM_CHANNEL_CS;
                pInfo->channel_map[7] = PCM_CHANNEL_CVH;
            } else {
                pInfo->channel_map[4] = PCM_CHANNEL_CS;
                pInfo->channel_map[5] = 0;
            }
        }
        if(pInfo->speaker_allocation[0] & BIT(5)) {
            pInfo->channel_map[6] = PCM_CHANNEL_FLC;
            pInfo->channel_map[7] = PCM_CHANNEL_FRC;
        }
        if(pInfo->speaker_allocation[0] & BIT(6)) {
            pInfo->speaker_allocation[0] &= 0xef;
            // If RLC/RRC is present, RC is invalid as per specification
            pInfo->channel_map[6] = PCM_CHANNEL_RLC;
            pInfo->channel_map[7] = PCM_CHANNEL_RRC;
        }
        // higher channel are not defined by LPASS
        //pInfo->nSpeakerAllocation[0] &= 0x3f;
        if(pInfo->speaker_allocation[0] & BIT(7)) {
            pInfo->channel_map[6] = 0; // PCM_CHANNEL_FLW; but not defined by LPASS
            pInfo->channel_map[7] = 0; // PCM_CHANNEL_FRW; but not defined by LPASS
        }
        if(pInfo->speaker_allocation[1] & BIT(0)) {
            pInfo->channel_map[6] = 0; // PCM_CHANNEL_FLH; but not defined by LPASS
            pInfo->channel_map[7] = 0; // PCM_CHANNEL_FRH; but not defined by LPASS
        }
    }
    ALOGD("%s channel map updated to [%d %d %d %d %d %d %d %d ]  [%x %x %x]", __func__
        , pInfo->channel_map[0], pInfo->channel_map[1], pInfo->channel_map[2]
        , pInfo->channel_map[3], pInfo->channel_map[4], pInfo->channel_map[5]
        , pInfo->channel_map[6], pInfo->channel_map[7]
        , pInfo->speaker_allocation[0], pInfo->speaker_allocation[1]
        , pInfo->speaker_allocation[2]);
}

static void dump_speaker_allocation(edid_audio_info* pInfo) {
    if(pInfo) {
        if (pInfo->speaker_allocation[0] & BIT(7))
            ALOGV("FLW/FRW");
        if (pInfo->speaker_allocation[0] & BIT(6))
            ALOGV("RLC/RRC");
        if (pInfo->speaker_allocation[0] & BIT(5))
            ALOGV("FLC/FRC");
        if (pInfo->speaker_allocation[0] & BIT(4))
            ALOGV("RC");
        if (pInfo->speaker_allocation[0] & BIT(3))
            ALOGV("RL/RR");
        if (pInfo->speaker_allocation[0] & BIT(2))
            ALOGV("FC");
        if (pInfo->speaker_allocation[0] & BIT(1))
            ALOGV("LFE");
        if (pInfo->speaker_allocation[0] & BIT(0))
            ALOGV("FL/FR");

        if (pInfo->speaker_allocation[1] & BIT(2))
            ALOGV("FCH");
        if (pInfo->speaker_allocation[1] & BIT(1))
            ALOGV("TC");
        if (pInfo->speaker_allocation[1] & BIT(0))
            ALOGV("FLH/FRH");
    }
}

static void update_channel_allocation(edid_audio_info* pInfo)
{
    if(pInfo) {
        int16_t ca = 0;
        int16_t spkAlloc = ((pInfo->speaker_allocation[1]) << 8) |
                           (pInfo->speaker_allocation[0]);
        ALOGV("pInfo->nSpeakerAllocation %x %x\n", pInfo->speaker_allocation[0],
                                                   pInfo->speaker_allocation[1]);
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
        ALOGD("%s channel Allocation: %x", __func__, ca);
        pInfo->channel_allocation = ca;
    }
}

static void update_channel_map_lpass(edid_audio_info* pInfo)
{
    if(pInfo) {
        if(pInfo->channel_allocation <= 0x1f)
            memset(pInfo->channel_map, 0, MAX_CHANNELS_SUPPORTED);
        switch(pInfo->channel_allocation) {
        case 0x0:
            pInfo->channel_map[0] = PCM_CHANNEL_FL;
            pInfo->channel_map[1] = PCM_CHANNEL_FR;
            break;
        case 0x1:
            pInfo->channel_map[0] = PCM_CHANNEL_FL;
            pInfo->channel_map[1] = PCM_CHANNEL_FR;
            pInfo->channel_map[2] = PCM_CHANNEL_LFE;
            break;
        case 0x2:
            pInfo->channel_map[0] = PCM_CHANNEL_FL;
            pInfo->channel_map[1] = PCM_CHANNEL_FR;
            pInfo->channel_map[2] = PCM_CHANNEL_FC;
            break;
        case 0x3:
            pInfo->channel_map[0] = PCM_CHANNEL_FL;
            pInfo->channel_map[1] = PCM_CHANNEL_FR;
            pInfo->channel_map[2] = PCM_CHANNEL_LFE;
            pInfo->channel_map[3] = PCM_CHANNEL_FC;
            break;
        case 0x4:
            pInfo->channel_map[0] = PCM_CHANNEL_FL;
            pInfo->channel_map[1] = PCM_CHANNEL_FR;
            pInfo->channel_map[2] = PCM_CHANNEL_CS;
            break;
        case 0x5:
            pInfo->channel_map[0] = PCM_CHANNEL_FL;
            pInfo->channel_map[1] = PCM_CHANNEL_FR;
            pInfo->channel_map[2] = PCM_CHANNEL_LFE;
            pInfo->channel_map[3] = PCM_CHANNEL_CS;
            break;
        case 0x6:
            pInfo->channel_map[0] = PCM_CHANNEL_FL;
            pInfo->channel_map[1] = PCM_CHANNEL_FR;
            pInfo->channel_map[2] = PCM_CHANNEL_FC;
            pInfo->channel_map[3] = PCM_CHANNEL_CS;
            break;
        case 0x7:
            pInfo->channel_map[0] = PCM_CHANNEL_FL;
            pInfo->channel_map[1] = PCM_CHANNEL_FR;
            pInfo->channel_map[2] = PCM_CHANNEL_LFE;
            pInfo->channel_map[3] = PCM_CHANNEL_FC;
            pInfo->channel_map[4] = PCM_CHANNEL_CS;
            break;
        case 0x8:
            pInfo->channel_map[0] = PCM_CHANNEL_FL;
            pInfo->channel_map[1] = PCM_CHANNEL_FR;
            pInfo->channel_map[2] = PCM_CHANNEL_LB;
            pInfo->channel_map[3] = PCM_CHANNEL_RB;
            break;
        case 0x9:
            pInfo->channel_map[0] = PCM_CHANNEL_FL;
            pInfo->channel_map[1] = PCM_CHANNEL_FR;
            pInfo->channel_map[2] = PCM_CHANNEL_LFE;
            pInfo->channel_map[3] = PCM_CHANNEL_LB;
            pInfo->channel_map[4] = PCM_CHANNEL_RB;
            break;
        case 0xa:
            pInfo->channel_map[0] = PCM_CHANNEL_FL;
            pInfo->channel_map[1] = PCM_CHANNEL_FR;
            pInfo->channel_map[2] = PCM_CHANNEL_FC;
            pInfo->channel_map[3] = PCM_CHANNEL_LB;
            pInfo->channel_map[4] = PCM_CHANNEL_RB;
            break;
        case 0xb:
            pInfo->channel_map[0] = PCM_CHANNEL_FL;
            pInfo->channel_map[1] = PCM_CHANNEL_FR;
            pInfo->channel_map[2] = PCM_CHANNEL_LFE;
            pInfo->channel_map[3] = PCM_CHANNEL_FC;
            pInfo->channel_map[4] = PCM_CHANNEL_LB;
            pInfo->channel_map[5] = PCM_CHANNEL_RB;
            break;
        case 0xc:
            pInfo->channel_map[0] = PCM_CHANNEL_FL;
            pInfo->channel_map[1] = PCM_CHANNEL_FR;
            pInfo->channel_map[2] = PCM_CHANNEL_LB;
            pInfo->channel_map[3] = PCM_CHANNEL_RB;
            pInfo->channel_map[4] = PCM_CHANNEL_CS;
            break;
        case 0xd:
            pInfo->channel_map[0] = PCM_CHANNEL_FL;
            pInfo->channel_map[1] = PCM_CHANNEL_FR;
            pInfo->channel_map[2] = PCM_CHANNEL_LFE;
            pInfo->channel_map[3] = PCM_CHANNEL_LB;
            pInfo->channel_map[4] = PCM_CHANNEL_RB;
            pInfo->channel_map[5] = PCM_CHANNEL_CS;
            break;
        case 0xe:
            pInfo->channel_map[0] = PCM_CHANNEL_FL;
            pInfo->channel_map[1] = PCM_CHANNEL_FR;
            pInfo->channel_map[2] = PCM_CHANNEL_FC;
            pInfo->channel_map[3] = PCM_CHANNEL_LB;
            pInfo->channel_map[4] = PCM_CHANNEL_RB;
            pInfo->channel_map[5] = PCM_CHANNEL_CS;
            break;
        case 0xf:
            pInfo->channel_map[0] = PCM_CHANNEL_FL;
            pInfo->channel_map[1] = PCM_CHANNEL_FR;
            pInfo->channel_map[2] = PCM_CHANNEL_LFE;
            pInfo->channel_map[3] = PCM_CHANNEL_FC;
            pInfo->channel_map[4] = PCM_CHANNEL_LB;
            pInfo->channel_map[5] = PCM_CHANNEL_RB;
            pInfo->channel_map[6] = PCM_CHANNEL_CS;
            break;
        case 0x10:
            pInfo->channel_map[0] = PCM_CHANNEL_FL;
            pInfo->channel_map[1] = PCM_CHANNEL_FR;
            pInfo->channel_map[2] = PCM_CHANNEL_LB;
            pInfo->channel_map[3] = PCM_CHANNEL_RB;
            pInfo->channel_map[4] = PCM_CHANNEL_RLC;
            pInfo->channel_map[5] = PCM_CHANNEL_RRC;
            break;
        case 0x11:
            pInfo->channel_map[0] = PCM_CHANNEL_FL;
            pInfo->channel_map[1] = PCM_CHANNEL_FR;
            pInfo->channel_map[2] = PCM_CHANNEL_LFE;
            pInfo->channel_map[3] = PCM_CHANNEL_LB;
            pInfo->channel_map[4] = PCM_CHANNEL_RB;
            pInfo->channel_map[5] = PCM_CHANNEL_RLC;
            pInfo->channel_map[6] = PCM_CHANNEL_RRC;
            break;
        case 0x12:
            pInfo->channel_map[0] = PCM_CHANNEL_FL;
            pInfo->channel_map[1] = PCM_CHANNEL_FR;
            pInfo->channel_map[2] = PCM_CHANNEL_FC;
            pInfo->channel_map[3] = PCM_CHANNEL_LB;
            pInfo->channel_map[4] = PCM_CHANNEL_RB;
            pInfo->channel_map[5] = PCM_CHANNEL_RLC;
            pInfo->channel_map[6] = PCM_CHANNEL_RRC;
            break;
        case 0x13:
            pInfo->channel_map[0] = PCM_CHANNEL_FL;
            pInfo->channel_map[1] = PCM_CHANNEL_FR;
            pInfo->channel_map[2] = PCM_CHANNEL_LFE;
            pInfo->channel_map[3] = PCM_CHANNEL_FC;
            pInfo->channel_map[4] = PCM_CHANNEL_LB;
            pInfo->channel_map[5] = PCM_CHANNEL_RB;
            pInfo->channel_map[6] = PCM_CHANNEL_RLC;
            pInfo->channel_map[7] = PCM_CHANNEL_RRC;
            break;
        case 0x14:
            pInfo->channel_map[0] = PCM_CHANNEL_FL;
            pInfo->channel_map[1] = PCM_CHANNEL_FR;
            pInfo->channel_map[2] = PCM_CHANNEL_FLC;
            pInfo->channel_map[3] = PCM_CHANNEL_FRC;
            break;
        case 0x15:
            pInfo->channel_map[0] = PCM_CHANNEL_FL;
            pInfo->channel_map[1] = PCM_CHANNEL_FR;
            pInfo->channel_map[2] = PCM_CHANNEL_LFE;
            pInfo->channel_map[3] = PCM_CHANNEL_FLC;
            pInfo->channel_map[4] = PCM_CHANNEL_FRC;
            break;
        case 0x16:
            pInfo->channel_map[0] = PCM_CHANNEL_FL;
            pInfo->channel_map[1] = PCM_CHANNEL_FR;
            pInfo->channel_map[2] = PCM_CHANNEL_FC;
            pInfo->channel_map[3] = PCM_CHANNEL_FLC;
            pInfo->channel_map[4] = PCM_CHANNEL_FRC;
            break;
        case 0x17:
            pInfo->channel_map[0] = PCM_CHANNEL_FL;
            pInfo->channel_map[1] = PCM_CHANNEL_FR;
            pInfo->channel_map[2] = PCM_CHANNEL_LFE;
            pInfo->channel_map[3] = PCM_CHANNEL_FC;
            pInfo->channel_map[4] = PCM_CHANNEL_FLC;
            pInfo->channel_map[5] = PCM_CHANNEL_FRC;
            break;
        case 0x18:
            pInfo->channel_map[0] = PCM_CHANNEL_FL;
            pInfo->channel_map[1] = PCM_CHANNEL_FR;
            pInfo->channel_map[2] = PCM_CHANNEL_CS;
            pInfo->channel_map[3] = PCM_CHANNEL_FLC;
            pInfo->channel_map[4] = PCM_CHANNEL_FRC;
            break;
        case 0x19:
            pInfo->channel_map[0] = PCM_CHANNEL_FL;
            pInfo->channel_map[1] = PCM_CHANNEL_FR;
            pInfo->channel_map[2] = PCM_CHANNEL_LFE;
            pInfo->channel_map[3] = PCM_CHANNEL_CS;
            pInfo->channel_map[4] = PCM_CHANNEL_FLC;
            pInfo->channel_map[5] = PCM_CHANNEL_FRC;
            break;
        case 0x1a:
            pInfo->channel_map[0] = PCM_CHANNEL_FL;
            pInfo->channel_map[1] = PCM_CHANNEL_FR;
            pInfo->channel_map[2] = PCM_CHANNEL_FC;
            pInfo->channel_map[3] = PCM_CHANNEL_CS;
            pInfo->channel_map[4] = PCM_CHANNEL_FLC;
            pInfo->channel_map[5] = PCM_CHANNEL_FRC;
            break;
        case 0x1b:
            pInfo->channel_map[0] = PCM_CHANNEL_FL;
            pInfo->channel_map[1] = PCM_CHANNEL_FR;
            pInfo->channel_map[2] = PCM_CHANNEL_LFE;
            pInfo->channel_map[3] = PCM_CHANNEL_FC;
            pInfo->channel_map[4] = PCM_CHANNEL_CS;
            pInfo->channel_map[5] = PCM_CHANNEL_FLC;
            pInfo->channel_map[6] = PCM_CHANNEL_FRC;
            break;
        case 0x1c:
            pInfo->channel_map[0] = PCM_CHANNEL_FL;
            pInfo->channel_map[1] = PCM_CHANNEL_FR;
            pInfo->channel_map[2] = PCM_CHANNEL_LB;
            pInfo->channel_map[3] = PCM_CHANNEL_RB;
            pInfo->channel_map[4] = PCM_CHANNEL_FLC;
            pInfo->channel_map[5] = PCM_CHANNEL_FRC;
            break;
        case 0x1d:
            pInfo->channel_map[0] = PCM_CHANNEL_FL;
            pInfo->channel_map[1] = PCM_CHANNEL_FR;
            pInfo->channel_map[2] = PCM_CHANNEL_LFE;
            pInfo->channel_map[3] = PCM_CHANNEL_LB;
            pInfo->channel_map[4] = PCM_CHANNEL_RB;
            pInfo->channel_map[5] = PCM_CHANNEL_FLC;
            pInfo->channel_map[6] = PCM_CHANNEL_FRC;
            break;
        case 0x1e:
            pInfo->channel_map[0] = PCM_CHANNEL_FL;
            pInfo->channel_map[1] = PCM_CHANNEL_FR;
            pInfo->channel_map[2] = PCM_CHANNEL_FC;
            pInfo->channel_map[3] = PCM_CHANNEL_LB;
            pInfo->channel_map[4] = PCM_CHANNEL_RB;
            pInfo->channel_map[5] = PCM_CHANNEL_FLC;
            pInfo->channel_map[6] = PCM_CHANNEL_FRC;
            break;
        case 0x1f:
            pInfo->channel_map[0] = PCM_CHANNEL_FL;
            pInfo->channel_map[1] = PCM_CHANNEL_FR;
            pInfo->channel_map[2] = PCM_CHANNEL_LFE;
            pInfo->channel_map[3] = PCM_CHANNEL_FC;
            pInfo->channel_map[4] = PCM_CHANNEL_LB;
            pInfo->channel_map[5] = PCM_CHANNEL_RB;
            pInfo->channel_map[6] = PCM_CHANNEL_FLC;
            pInfo->channel_map[7] = PCM_CHANNEL_FRC;
            break;
        default:
            break;
        }

    ALOGD("%s channel map updated to [%d %d %d %d %d %d %d %d ]", __func__
        , pInfo->channel_map[0], pInfo->channel_map[1], pInfo->channel_map[2]
        , pInfo->channel_map[3], pInfo->channel_map[4], pInfo->channel_map[5]
        , pInfo->channel_map[6], pInfo->channel_map[7]);
    }
}

static int32_t get_disp_dev_fb_Index()
{
    FILE *displayDeviceFP = NULL;
    char fbType[MAX_FRAME_BUFFER_NAME_SIZE];
    char msmFbTypePath[MAX_FRAME_BUFFER_NAME_SIZE];
    int index = -1;
    int i = 0;

    for(i = 1; i < MAX_DISPLAY_DEVICES; i++) {
        snprintf (msmFbTypePath, sizeof(msmFbTypePath),
                  "/sys/class/graphics/fb%d/msm_fb_type", i);
        displayDeviceFP = fopen(msmFbTypePath, "r");
        if(displayDeviceFP) {
            fread(fbType, sizeof(char), MAX_FRAME_BUFFER_NAME_SIZE, displayDeviceFP);
            if(strncmp(fbType, "dtv panel", strlen("dtv panel")) == 0) {
                index = i;
                fclose(displayDeviceFP);
                break;
            }
            fclose(displayDeviceFP);
        }
    }
    return index;
}

static bool disconnect_received()
{
    char fbType[MAX_FRAME_BUFFER_NAME_SIZE];
    char msmFbTypePath[MAX_FRAME_BUFFER_NAME_SIZE];
    FILE* fpHdmiConnected = NULL;
    char data[MAX_CHAR_PER_INT], index = get_disp_dev_fb_Index();

    snprintf(msmFbTypePath, sizeof(msmFbTypePath),
                 "/sys/class/graphics/fb%d/connected", index);
    fpHdmiConnected = fopen(msmFbTypePath, "rb");
    ALOGV("%s",__func__);
    if(fpHdmiConnected) {
        ALOGV("connected node open successful");
        if(fread(data, 1, MAX_CHAR_PER_INT, fpHdmiConnected)) {
            ALOGV("data in the node - %d", atoi(data));
            fclose(fpHdmiConnected);
            return atoi(data) ? false : true;
        } else {
            fclose(fpHdmiConnected);
            return true;
        }
    } else {
        return true;
    }
}

static void dump_edid_data(edid_audio_info *info) {

    int i;
    for (i = 0; i < info->audio_blocks && i < MAX_EDID_BLOCKS; i++) {
        ALOGV("%s:FormatId:%d rate:%d bps:%d channels:%d", __func__,
              info->audio_blocks_array[i].format_id,
              info->audio_blocks_array[i].sampling_freq,
              info->audio_blocks_array[i].bits_per_sample,
              info->audio_blocks_array[i].channels);
    }
    ALOGV("%s:nAudioBlocks:%d", __func__, info->audio_blocks);
    ALOGV("%s:nSpeakerAllocation:[%x %x %x]", __func__,
           info->speaker_allocation[0], info->speaker_allocation[1],
           info->speaker_allocation[2]);
    ALOGV("%s:channelMap:[%x %x %x %x %x %x %x %x]", __func__,
           info->channel_map[0], info->channel_map[1],
           info->channel_map[2], info->channel_map[3],
           info->channel_map[4], info->channel_map[5],
           info->channel_map[6], info->channel_map[7]);
    ALOGV("%s:channelAllocation:%d", __func__, info->channel_allocation);
    ALOGV("%s:[%d %d %d %d %d %d %d %d ]", __func__,
           info->channel_map[0], info.channel_map[1],
           info->channel_map[2], info->channel_map[3],
           info->channel_map[4], info->channel_map[5],
           info->channel_map[6], info->channel_map[7]);
}

bool edid_get_sink_caps(edid_audio_info* pInfo, char *hdmiEDIDData) {
    unsigned char channels[16];
    unsigned char formats[16];
    unsigned char frequency[16];
    unsigned char bitrate[16];
    unsigned char* data = NULL;
    unsigned char* original_data_ptr = NULL;
    int i = 0;

    if (pInfo && hdmiEDIDData) {
        int length = 0, count_desc = 0;

        length = (int) *hdmiEDIDData++;
        ALOGV("Total length is %d",length);

        count_desc = length/MIN_AUDIO_DESC_LENGTH;

        memset(pInfo, 0, sizeof(edid_audio_info));
        pInfo->audio_blocks = count_desc-1;
        ALOGV("Total # of audio descriptors %d",count_desc);

        for(i=0; i<count_desc-1; i++) {
                 // last block for speaker allocation;
              channels [i]   = (*hdmiEDIDData & 0x7) + 1;
              formats  [i]   = (*hdmiEDIDData++) >> 3;
              frequency[i]   = *hdmiEDIDData++;
              bitrate  [i]   = *hdmiEDIDData++;
        }
        pInfo->speaker_allocation[0] = *hdmiEDIDData++;
        pInfo->speaker_allocation[1] = *hdmiEDIDData++;
        pInfo->speaker_allocation[2] = *hdmiEDIDData++;

        update_channel_map(pInfo);
        update_channel_allocation(pInfo);
        update_channel_map_lpass(pInfo);

        for (i = 0; i < pInfo->audio_blocks; i++) {
            ALOGV("AUDIO DESC BLOCK # %d\n",i);

            pInfo->audio_blocks_array[i].channels = channels[i];
            ALOGV("pInfo->audio_blocks_array[i].channels %d\n", pInfo->audio_blocks_array[i].channels);

            ALOGV("Format Byte %d\n", formats[i]);
            pInfo->audio_blocks_array[i].format_id = (edid_audio_format_id)get_edid_format(formats[i]);
            ALOGV("pInfo->audio_blocks_array[i].format_id %d",pInfo->audio_blocks_array[i].format_id);

            ALOGV("Frequency Byte %d\n", frequency[i]);
            pInfo->audio_blocks_array[i].sampling_freq = get_edid_sf(frequency[i]);
            ALOGV("pInfo->audio_blocks_array[i].sampling_freq %d",pInfo->audio_blocks_array[i].sampling_freq);

            ALOGV("BitsPerSample Byte %d\n", bitrate[i]);
            pInfo->audio_blocks_array[i].bits_per_sample = get_edid_bps(bitrate[i],formats[i]);
            ALOGV("pInfo->audio_blocks_array[i].bits_per_sample %d",pInfo->audio_blocks_array[i].bits_per_sample);
        }
        dump_speaker_allocation(pInfo);
        dump_edid_data(pInfo);
        return true;
    } else {
        ALOGE("No valid EDID");
        return false;
    }
}
