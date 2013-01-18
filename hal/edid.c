/*
 * Copyright (C) 2013 The Android Open Source Project
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

#define LOG_TAG "audio_hw_primary"

#include <stdlib.h>
#include <stdio.h>

#include <cutils/log.h>

/*
 * This is the sysfs path for the HDMI audio data block
 */
#define AUDIO_DATA_BLOCK_PATH "/sys/class/graphics/fb1/audio_data_block"

/*
 * This file will have a maximum of 38 bytes:
 *
 * 4 bytes: number of audio blocks
 * 4 bytes: total length of Short Audio Descriptor (SAD) blocks
 * Maximum 10 * 3 bytes: SAD blocks
 */
#define MAX_SAD_BLOCKS		10
#define SAD_BLOCK_SIZE		3

/* EDID format ID for LPCM audio */
#define EDID_FORMAT_LPCM	1

struct audio_block_header
{
    int reserved;
    int length;
};

int edid_get_max_channels(void)
{
    FILE *file;
    struct audio_block_header header;
    char block[MAX_SAD_BLOCKS * SAD_BLOCK_SIZE];
    char *sad = block;
    int num_audio_blocks;
    int channel_count;
    int max_channels = 0;
    int i;

    file = fopen(AUDIO_DATA_BLOCK_PATH, "rb");
    if (file == NULL) {
        ALOGE("Unable to open '%s'", AUDIO_DATA_BLOCK_PATH);
        return 0;
    }

    /* Read audio block header */
    fread(&header, 1, sizeof(header), file);

    /* Read SAD blocks, clamping the maximum size for safety */
    if (header.length > (int)sizeof(block))
        header.length = (int)sizeof(block);
    fread(&block, header.length, 1, file);

    fclose(file);

    /* Calculate the number of SAD blocks */
    num_audio_blocks = header.length / SAD_BLOCK_SIZE;

    for (i = 0; i < num_audio_blocks; i++) {
        /* Only consider LPCM blocks */
        if ((sad[0] >> 3) != EDID_FORMAT_LPCM)
            continue;

        channel_count = (sad[0] & 0x7) + 1;
        if (channel_count > max_channels)
            max_channels = channel_count;

        /* Advance to next block */
        sad += 3;
    }

    return max_channels;
}

