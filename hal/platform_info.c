/*
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

#define LOG_TAG "platform_info"
#define LOG_NDDEBUG 0

#include <errno.h>
#include <stdio.h>
#include <expat.h>
#include <log/log.h>
#include <audio_hw.h>
#include "platform_api.h"
#include <platform.h>
#include <math.h>
#include <pthread.h>

/*
 * Mandatory microphone characteristics include: device_id, type, address, location, group,
 * index_in_the_group, directionality, num_frequency_responses, frequencies and responses.
 * MANDATORY_MICROPHONE_CHARACTERISTICS should be updated when mandatory microphone
 * characteristics are changed.
 */
#define MANDATORY_MICROPHONE_CHARACTERISTICS (1 << 10) - 1

typedef enum {
    ROOT,
    ACDB,
    MODULE,
    AEC,
    NS,
    PCM_ID,
    BACKEND_NAME,
    CONFIG_PARAMS,
    OPERATOR_SPECIFIC,
    GAIN_LEVEL_MAPPING,
    APP_TYPE,
    MICROPHONE_CHARACTERISTIC,
    SND_DEVICES,
    INPUT_SND_DEVICE,
    INPUT_SND_DEVICE_TO_MIC_MAPPING,
    SND_DEV,
    MIC_INFO,
    ACDB_METAINFO_KEY,
    EXTERNAL_DEVICE_SPECIFIC,
    AUDIO_SOURCE_DELAY,
    AUDIO_OUTPUT_USECASE_DELAY,
} section_t;

typedef void (* section_process_fn)(const XML_Char **attr);

static void process_acdb_id(const XML_Char **attr);
static void process_audio_effect(const XML_Char **attr, effect_type_t effect_type);
static void process_effect_aec(const XML_Char **attr);
static void process_effect_ns(const XML_Char **attr);
static void process_pcm_id(const XML_Char **attr);
static void process_backend_name(const XML_Char **attr);
static void process_config_params(const XML_Char **attr);
static void process_root(const XML_Char **attr);
static void process_operator_specific(const XML_Char **attr);
static void process_gain_db_to_level_map(const XML_Char **attr);
static void process_app_type(const XML_Char **attr);
static void process_microphone_characteristic(const XML_Char **attr);
static void process_snd_dev(const XML_Char **attr);
static void process_mic_info(const XML_Char **attr);
static void process_acdb_metainfo_key(const XML_Char **attr);
static void process_external_dev(const XML_Char **attr);
static void process_audio_source_delay(const XML_Char **attr);
static void process_audio_usecase_delay(const XML_Char **attr);

static section_process_fn section_table[] = {
    [ROOT] = process_root,
    [ACDB] = process_acdb_id,
    [AEC] = process_effect_aec,
    [NS] = process_effect_ns,
    [PCM_ID] = process_pcm_id,
    [BACKEND_NAME] = process_backend_name,
    [CONFIG_PARAMS] = process_config_params,
    [OPERATOR_SPECIFIC] = process_operator_specific,
    [GAIN_LEVEL_MAPPING] = process_gain_db_to_level_map,
    [APP_TYPE] = process_app_type,
    [MICROPHONE_CHARACTERISTIC] = process_microphone_characteristic,
    [SND_DEV] = process_snd_dev,
    [MIC_INFO] = process_mic_info,
    [ACDB_METAINFO_KEY] = process_acdb_metainfo_key,
    [EXTERNAL_DEVICE_SPECIFIC] = process_external_dev,
    [AUDIO_SOURCE_DELAY] = process_audio_source_delay,
    [AUDIO_OUTPUT_USECASE_DELAY] = process_audio_usecase_delay,
};

static section_t section;

struct platform_info {
    pthread_mutex_t   lock;
    bool              do_full_parse;
    void             *platform;
    struct str_parms *kvpairs;
    set_parameters_fn set_parameters;
};

static struct platform_info my_data = {PTHREAD_MUTEX_INITIALIZER,
                                       true, NULL, NULL,
                                       &platform_set_parameters};

struct audio_string_to_enum {
    const char* name;
    unsigned int value;
};

static snd_device_t in_snd_device;

static const struct audio_string_to_enum mic_locations[AUDIO_MICROPHONE_LOCATION_CNT] = {
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_MICROPHONE_LOCATION_UNKNOWN),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_MICROPHONE_LOCATION_MAINBODY),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_MICROPHONE_LOCATION_MAINBODY_MOVABLE),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_MICROPHONE_LOCATION_PERIPHERAL),
};

static const struct audio_string_to_enum mic_directionalities[AUDIO_MICROPHONE_DIRECTIONALITY_CNT] = {
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_MICROPHONE_DIRECTIONALITY_OMNI),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_MICROPHONE_DIRECTIONALITY_BI_DIRECTIONAL),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_MICROPHONE_DIRECTIONALITY_UNKNOWN),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_MICROPHONE_DIRECTIONALITY_CARDIOID),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_MICROPHONE_DIRECTIONALITY_HYPER_CARDIOID),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_MICROPHONE_DIRECTIONALITY_SUPER_CARDIOID),
};

static const struct audio_string_to_enum mic_channel_mapping[AUDIO_MICROPHONE_CHANNEL_MAPPING_CNT] = {
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_MICROPHONE_CHANNEL_MAPPING_UNUSED),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_MICROPHONE_CHANNEL_MAPPING_DIRECT),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_MICROPHONE_CHANNEL_MAPPING_PROCESSED),
};

static const struct audio_string_to_enum device_in_types[] = {
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_AMBIENT),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_COMMUNICATION),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_BUILTIN_MIC),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_WIRED_HEADSET),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_AUX_DIGITAL),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_HDMI),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_VOICE_CALL),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_TELEPHONY_RX),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_BACK_MIC),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_REMOTE_SUBMIX),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_ANLG_DOCK_HEADSET),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_DGTL_DOCK_HEADSET),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_USB_ACCESSORY),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_USB_DEVICE),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_FM_TUNER),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_TV_TUNER),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_LINE),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_SPDIF),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_BLUETOOTH_A2DP),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_LOOPBACK),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_IP),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_BUS),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_PROXY),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_USB_HEADSET),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_BLUETOOTH_BLE),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_DEFAULT),
};

static bool find_enum_by_string(const struct audio_string_to_enum * table, const char * name,
                                int32_t len, unsigned int *value)
{
    if (table == NULL) {
        ALOGE("%s: table is NULL", __func__);
        return false;
    }

    if (name == NULL) {
        ALOGE("null key");
        return false;
    }

    for (int i = 0; i < len; i++) {
        if (!strcmp(table[i].name, name)) {
            *value = table[i].value;
            return true;
        }
    }
    return false;
}

/*
 * <audio_platform_info>
 * <acdb_ids>
 * <device name="???" acdb_id="???"/>
 * ...
 * ...
 * </acdb_ids>
 * <module_ids>
 * <device name="???" module_id="???"/>
 * ...
 * ...
 * </module_ids>
 * <backend_names>
 * <device name="???" backend="???"/>
 * ...
 * ...
 * </backend_names>
 * <pcm_ids>
 * <usecase name="???" type="in/out" id="???"/>
 * ...
 * ...
 * </pcm_ids>
 * <config_params>
 *      <param key="snd_card_name" value="msm8994-tomtom-mtp-snd-card"/>
 *      <param key="operator_info" value="tmus;aa;bb;cc"/>
 *      <param key="operator_info" value="sprint;xx;yy;zz"/>
 *      ...
 *      ...
 * </config_params>
 *
 * <operator_specific>
 *      <device name="???" operator="???" mixer_path="???" acdb_id="???"/>
 *      ...
 *      ...
 * </operator_specific>
 *
 * </audio_platform_info>
 */

static void process_root(const XML_Char **attr __unused)
{
}

static void process_audio_effect(const XML_Char **attr, effect_type_t effect_type)
{
    int index;
    struct audio_effect_config effect_config;

    if (strncmp(attr[0], "name", strlen("name")) != 0) {
        ALOGE("%s: 'name' not found, no MODULE ID set!", __func__);
        goto done;
    }

    index = platform_get_snd_device_index((char *)attr[1]);
    if (index < 0) {
        ALOGE("%s: Device %s in platform info xml not found, no MODULE ID set!",
              __func__, attr[1]);
        goto done;
    }

    if (strncmp(attr[2], "module_id", strlen("module_id")) != 0) {
        ALOGE("%s: Device %s in platform info xml has no module_id, no MODULE ID set!",
              __func__, attr[2]);
        goto done;
    }

    if (strncmp(attr[4], "instance_id", strlen("instance_id")) != 0) {
        ALOGE("%s: Device %s in platform info xml has no instance_id, no INSTANCE ID set!",
              __func__, attr[4]);
        goto done;
    }

    if (strncmp(attr[6], "param_id", strlen("param_id")) != 0) {
        ALOGE("%s: Device %s in platform info xml has no param_id, no PARAM ID set!",
              __func__, attr[6]);
        goto done;
    }

    if (strncmp(attr[8], "param_value", strlen("param_value")) != 0) {
        ALOGE("%s: Device %s in platform info xml has no param_value, no PARAM VALUE set!",
              __func__, attr[8]);
        goto done;
    }

    effect_config = (struct audio_effect_config){strtol((char *)attr[3], NULL, 0),
                                                 strtol((char *)attr[5], NULL, 0),
                                                 strtol((char *)attr[7], NULL, 0),
                                                 strtol((char *)attr[9], NULL, 0)};


    if (platform_set_effect_config_data(index, effect_config, effect_type) < 0) {
        ALOGE("%s: Effect = %d Device %s, MODULE/INSTANCE/PARAM ID %u %u %u %u was not set!",
              __func__, effect_type, attr[1], effect_config.module_id,
              effect_config.instance_id, effect_config.param_id,
              effect_config.param_value);
        goto done;
    }

done:
    return;
}

static void process_effect_aec(const XML_Char **attr)
{
    process_audio_effect(attr, EFFECT_AEC);
    return;
}

static void process_effect_ns(const XML_Char **attr)
{
    process_audio_effect(attr, EFFECT_NS);
    return;
}

/* mapping from usecase to pcm dev id */
static void process_pcm_id(const XML_Char **attr)
{
    int index;

    if (strcmp(attr[0], "name") != 0) {
        ALOGE("%s: 'name' not found, no pcm_id set!", __func__);
        goto done;
    }

    index = platform_get_usecase_index((char *)attr[1]);
    if (index < 0) {
        ALOGE("%s: usecase %s in %s not found!",
              __func__, attr[1], PLATFORM_INFO_XML_PATH);
        goto done;
    }

    if (strcmp(attr[2], "type") != 0) {
        ALOGE("%s: usecase type not mentioned", __func__);
        goto done;
    }

    int type = -1;

    if (!strcasecmp((char *)attr[3], "in")) {
        type = 1;
    } else if (!strcasecmp((char *)attr[3], "out")) {
        type = 0;
    } else {
        ALOGE("%s: type must be IN or OUT", __func__);
        goto done;
    }

    if (strcmp(attr[4], "id") != 0) {
        ALOGE("%s: usecase id not mentioned", __func__);
        goto done;
    }

    int id = atoi((char *)attr[5]);

    if (platform_set_usecase_pcm_id(index, type, id) < 0) {
        ALOGE("%s: usecase %s in %s, type %d id %d was not set!",
              __func__, attr[1], PLATFORM_INFO_XML_PATH, type, id);
        goto done;
    }

done:
    return;
}

/* backend to be used for a device */
static void process_backend_name(const XML_Char **attr)
{
    int index;
    char *hw_interface = NULL;

    if (strcmp(attr[0], "name") != 0) {
        ALOGE("%s: 'name' not found, no ACDB ID set!", __func__);
        goto done;
    }

    index = platform_get_snd_device_index((char *)attr[1]);
    if (index < 0) {
        ALOGE("%s: Device %s in %s not found, no ACDB ID set!",
              __func__, attr[1], PLATFORM_INFO_XML_PATH);
        goto done;
    }

    if (strcmp(attr[2], "backend") != 0) {
        ALOGE("%s: Device %s in %s has no backed set!",
              __func__, attr[1], PLATFORM_INFO_XML_PATH);
        goto done;
    }

    if (attr[4] != NULL) {
        if (strcmp(attr[4], "interface") != 0) {
            hw_interface = NULL;
        } else {
            hw_interface = (char *)attr[5];
        }
    }

    if (platform_set_snd_device_backend(index, attr[3], hw_interface) < 0) {
        ALOGE("%s: Device %s in %s, backend %s was not set!",
              __func__, attr[1], PLATFORM_INFO_XML_PATH, attr[3]);
        goto done;
    }

done:
    return;
}

static void process_gain_db_to_level_map(const XML_Char **attr)
{
    struct amp_db_and_gain_table tbl_entry;

    if ((strcmp(attr[0], "db") != 0) ||
        (strcmp(attr[2], "level") != 0)) {
        ALOGE("%s: invalid attribute passed  %s %sexpected amp db level",
               __func__, attr[0], attr[2]);
        goto done;
    }

    tbl_entry.db = atof(attr[1]);
    tbl_entry.amp = exp(tbl_entry.db * 0.115129f);
    tbl_entry.level = atoi(attr[3]);

    //custome level should be > 0. Level 0 is fixed for default
    CHECK(tbl_entry.level > 0);

    ALOGV("%s: amp [%f]  db [%f] level [%d]", __func__,
           tbl_entry.amp, tbl_entry.db, tbl_entry.level);
    platform_add_gain_level_mapping(&tbl_entry);

done:
    return;
}

static void process_acdb_id(const XML_Char **attr)
{
    int index;

    if (strcmp(attr[0], "name") != 0) {
        ALOGE("%s: 'name' not found, no ACDB ID set!", __func__);
        goto done;
    }

    index = platform_get_snd_device_index((char *)attr[1]);
    if (index < 0) {
        ALOGE("%s: Device %s in %s not found, no ACDB ID set!",
              __func__, attr[1], PLATFORM_INFO_XML_PATH);
        goto done;
    }

    if (strcmp(attr[2], "acdb_id") != 0) {
        ALOGE("%s: Device %s in %s has no acdb_id, no ACDB ID set!",
              __func__, attr[1], PLATFORM_INFO_XML_PATH);
        goto done;
    }

    if (platform_set_snd_device_acdb_id(index, atoi((char *)attr[3])) < 0) {
        ALOGE("%s: Device %s in %s, ACDB ID %d was not set!",
              __func__, attr[1], PLATFORM_INFO_XML_PATH, atoi((char *)attr[3]));
        goto done;
    }

done:
    return;
}


static void process_operator_specific(const XML_Char **attr)
{
    snd_device_t snd_device = SND_DEVICE_NONE;

    if (strcmp(attr[0], "name") != 0) {
        ALOGE("%s: 'name' not found", __func__);
        goto done;
    }

    snd_device = platform_get_snd_device_index((char *)attr[1]);
    if (snd_device < 0) {
        ALOGE("%s: Device %s in %s not found, no ACDB ID set!",
              __func__, (char *)attr[3], PLATFORM_INFO_XML_PATH);
        goto done;
    }

    if (strcmp(attr[2], "operator") != 0) {
        ALOGE("%s: 'operator' not found", __func__);
        goto done;
    }

    if (strcmp(attr[4], "mixer_path") != 0) {
        ALOGE("%s: 'mixer_path' not found", __func__);
        goto done;
    }

    if (strcmp(attr[6], "acdb_id") != 0) {
        ALOGE("%s: 'acdb_id' not found", __func__);
        goto done;
    }

    platform_add_operator_specific_device(snd_device, (char *)attr[3], (char *)attr[5], atoi((char *)attr[7]));

done:
    return;
}

static void process_external_dev(const XML_Char **attr)
{
    snd_device_t snd_device = SND_DEVICE_NONE;

    if (strcmp(attr[0], "name") != 0) {
        ALOGE("%s: 'name' not found", __func__);
        goto done;
    }

    snd_device = platform_get_snd_device_index((char *)attr[1]);
    if (snd_device < 0) {
        ALOGE("%s: Device %s in %s not found, no ACDB ID set!",
              __func__, (char *)attr[3], PLATFORM_INFO_XML_PATH);
        goto done;
    }

    if (strcmp(attr[2], "usbid") != 0) {
        ALOGE("%s: 'usbid' not found", __func__);
        goto done;
    }

    if (strcmp(attr[4], "acdb_id") != 0) {
        ALOGE("%s: 'acdb_id' not found", __func__);
        goto done;
    }

    platform_add_external_specific_device(snd_device, (char *)attr[3], atoi((char *)attr[5]));

done:
    return;
}

static void process_audio_source_delay(const XML_Char **attr)
{
    audio_source_t audio_source = -1;

    if (strcmp(attr[0], "name") != 0) {
        ALOGE("%s: 'name' not found", __func__);
        goto done;
    }

    audio_source = platform_get_audio_source_index((const char *)attr[1]);

    if (audio_source < 0) {
        ALOGE("%s: audio_source %s is not defined",
              __func__, (char *)attr[1]);
        goto done;
    }

    if (strcmp(attr[2], "delay") != 0) {
        ALOGE("%s: 'delay' not found", __func__);
        goto done;
    }

    platform_set_audio_source_delay(audio_source, atoi((char *)attr[3]));

done:
    return;
}

static void process_audio_usecase_delay(const XML_Char **attr)
{
    int index;

    if (strcmp(attr[0], "name") != 0) {
        ALOGE("%s: 'name' not found", __func__);
        goto done;
    }

    index = platform_get_usecase_index((char *)attr[1]);
    if (index < 0) {
        ALOGE("%s: usecase %s in %s not found!",
              __func__, attr[1], PLATFORM_INFO_XML_PATH);
        goto done;
    }

    if (strcmp(attr[2], "delay") != 0) {
        ALOGE("%s: 'delay' not found", __func__);
        goto done;
    }

    platform_set_audio_usecase_delay(index, atoi((char *)attr[3]));

done:
    return;
}

/* platform specific configuration key-value pairs */
static void process_config_params(const XML_Char **attr)
{
    if (strcmp(attr[0], "key") != 0) {
        ALOGE("%s: 'key' not found", __func__);
        goto done;
    }

    if (strcmp(attr[2], "value") != 0) {
        ALOGE("%s: 'value' not found", __func__);
        goto done;
    }

    str_parms_add_str(my_data.kvpairs, (char*)attr[1], (char*)attr[3]);
    my_data.set_parameters(my_data.platform, my_data.kvpairs);
done:
    return;
}

static void process_app_type(const XML_Char **attr)
{
    if (strcmp(attr[0], "uc_type")) {
        ALOGE("%s: uc_type not found", __func__);
        goto done;
    }

    if (strcmp(attr[2], "mode")) {
        ALOGE("%s: mode not found", __func__);
        goto done;
    }

    if (strcmp(attr[4], "bit_width")) {
        ALOGE("%s: bit_width not found", __func__);
        goto done;
    }

    if (strcmp(attr[6], "id")) {
        ALOGE("%s: id not found", __func__);
        goto done;
    }

    if (strcmp(attr[8], "max_rate")) {
        ALOGE("%s: max rate not found", __func__);
        goto done;
    }

    platform_add_app_type(attr[1], attr[3], atoi(attr[5]), atoi(attr[7]),
                          atoi(attr[9]));
done:
    return;
}

static void process_microphone_characteristic(const XML_Char **attr) {
    struct audio_microphone_characteristic_t microphone;
    uint32_t index = 0;
    uint32_t found_mandatory_characteristics = 0;
    uint32_t num_frequencies = 0;
    uint32_t num_responses = 0;
    microphone.sensitivity = AUDIO_MICROPHONE_SENSITIVITY_UNKNOWN;
    microphone.max_spl = AUDIO_MICROPHONE_SPL_UNKNOWN;
    microphone.min_spl = AUDIO_MICROPHONE_SPL_UNKNOWN;
    microphone.orientation.x = 0.0f;
    microphone.orientation.y = 0.0f;
    microphone.orientation.z = 0.0f;
    microphone.geometric_location.x = AUDIO_MICROPHONE_COORDINATE_UNKNOWN;
    microphone.geometric_location.y = AUDIO_MICROPHONE_COORDINATE_UNKNOWN;
    microphone.geometric_location.z = AUDIO_MICROPHONE_COORDINATE_UNKNOWN;

    while (attr[index] != NULL) {
        const char *attribute = attr[index++];
        char value[strlen(attr[index]) + 1];
        strcpy(value, attr[index++]);
        if (strcmp(attribute, "device_id") == 0) {
            if (strlen(value) > AUDIO_MICROPHONE_ID_MAX_LEN) {
                ALOGE("%s: device_id %s is too long", __func__, value);
                goto done;
            }
            strcpy(microphone.device_id, value);
            found_mandatory_characteristics |= 1;
        } else if (strcmp(attribute, "type") == 0) {
            if (!find_enum_by_string(device_in_types, value,
                    ARRAY_SIZE(device_in_types), &microphone.device)) {
                ALOGE("%s: type %s in %s not found!",
                        __func__, value, PLATFORM_INFO_XML_PATH);
                goto done;
            }
            found_mandatory_characteristics |= (1 << 1);
        } else if (strcmp(attribute, "address") == 0) {
            if (strlen(value) > AUDIO_DEVICE_MAX_ADDRESS_LEN) {
                ALOGE("%s, address %s is too long", __func__, value);
                goto done;
            }
            strcpy(microphone.address, value);
            if (strlen(microphone.address) == 0) {
                // If the address is empty, populate the address according to device type.
                if (microphone.device == AUDIO_DEVICE_IN_BUILTIN_MIC) {
                    strcpy(microphone.address, AUDIO_BOTTOM_MICROPHONE_ADDRESS);
                } else if (microphone.device == AUDIO_DEVICE_IN_BACK_MIC) {
                    strcpy(microphone.address, AUDIO_BACK_MICROPHONE_ADDRESS);
                }
            }
            found_mandatory_characteristics |= (1 << 2);
        } else if (strcmp(attribute, "location") == 0) {
            if (!find_enum_by_string(mic_locations, value,
                    AUDIO_MICROPHONE_LOCATION_CNT, &microphone.location)) {
                ALOGE("%s: location %s in %s not found!",
                        __func__, value, PLATFORM_INFO_XML_PATH);
                goto done;
            }
            found_mandatory_characteristics |= (1 << 3);
        } else if (strcmp(attribute, "group") == 0) {
            microphone.group = atoi(value);
            found_mandatory_characteristics |= (1 << 4);
        } else if (strcmp(attribute, "index_in_the_group") == 0) {
            microphone.index_in_the_group = atoi(value);
            found_mandatory_characteristics |= (1 << 5);
        } else if (strcmp(attribute, "directionality") == 0) {
            if (!find_enum_by_string(mic_directionalities, value,
                    AUDIO_MICROPHONE_DIRECTIONALITY_CNT, &microphone.directionality)) {
                ALOGE("%s: directionality %s in %s not found!",
                      __func__, attr[index], PLATFORM_INFO_XML_PATH);
                goto done;
            }
            found_mandatory_characteristics |= (1 << 6);
        } else if (strcmp(attribute, "num_frequency_responses") == 0) {
            microphone.num_frequency_responses = atoi(value);
            if (microphone.num_frequency_responses > AUDIO_MICROPHONE_MAX_FREQUENCY_RESPONSES) {
                ALOGE("%s: num_frequency_responses is too large", __func__);
                goto done;
            }
            found_mandatory_characteristics |= (1 << 7);
        } else if (strcmp(attribute, "frequencies") == 0) {
            char *token = strtok(value, " ");
            while (token) {
                microphone.frequency_responses[0][num_frequencies++] = atof(token);
                if (num_frequencies > AUDIO_MICROPHONE_MAX_FREQUENCY_RESPONSES) {
                    ALOGE("%s: num %u of frequency is too large", __func__, num_frequencies);
                    goto done;
                }
                token = strtok(NULL, " ");
            }
            found_mandatory_characteristics |= (1 << 8);
        } else if (strcmp(attribute, "responses") == 0) {
            char *token = strtok(value, " ");
            while (token) {
                microphone.frequency_responses[1][num_responses++] = atof(token);
                if (num_responses > AUDIO_MICROPHONE_MAX_FREQUENCY_RESPONSES) {
                    ALOGE("%s: num %u of response is too large", __func__, num_responses);
                    goto done;
                }
                token = strtok(NULL, " ");
            }
            found_mandatory_characteristics |= (1 << 9);
        } else if (strcmp(attribute, "sensitivity") == 0) {
            microphone.sensitivity = atof(value);
        } else if (strcmp(attribute, "max_spl") == 0) {
            microphone.max_spl = atof(value);
        } else if (strcmp(attribute, "min_spl") == 0) {
            microphone.min_spl = atof(value);
        } else if (strcmp(attribute, "orientation") == 0) {
            char *token = strtok(value, " ");
            float orientation[3];
            uint32_t idx = 0;
            while (token) {
                orientation[idx++] = atof(token);
                if (idx > 3) {
                    ALOGE("%s: orientation invalid", __func__);
                    goto done;
                }
                token = strtok(NULL, " ");
            }
            if (idx != 3) {
                ALOGE("%s: orientation invalid", __func__);
                goto done;
            }
            microphone.orientation.x = orientation[0];
            microphone.orientation.y = orientation[1];
            microphone.orientation.z = orientation[2];
        } else if (strcmp(attribute, "geometric_location") == 0) {
            char *token = strtok(value, " ");
            float geometric_location[3];
            uint32_t idx = 0;
            while (token) {
                geometric_location[idx++] = atof(token);
                if (idx > 3) {
                    ALOGE("%s: geometric_location invalid", __func__);
                    goto done;
                }
                token = strtok(NULL, " ");
            }
            if (idx != 3) {
                ALOGE("%s: geometric_location invalid", __func__);
                goto done;
            }
            microphone.geometric_location.x = geometric_location[0];
            microphone.geometric_location.y = geometric_location[1];
            microphone.geometric_location.z = geometric_location[2];
        } else {
            ALOGW("%s: unknown attribute of microphone characteristics: %s",
                    __func__, attribute);
        }
    }

    if (num_frequencies != num_responses
            || num_frequencies != microphone.num_frequency_responses) {
        ALOGE("%s: num of frequency and response not match: %u, %u, %u",
              __func__, num_frequencies, num_responses, microphone.num_frequency_responses);
        goto done;
    }

    if (found_mandatory_characteristics != MANDATORY_MICROPHONE_CHARACTERISTICS) {
        ALOGE("%s: some of mandatory microphone characteriscts are missed: %u",
                __func__, found_mandatory_characteristics);
    }

    platform_set_microphone_characteristic(my_data.platform, microphone);
done:
    return;
}

static void process_snd_dev(const XML_Char **attr)
{
    uint32_t curIdx = 0;
    in_snd_device = SND_DEVICE_NONE;

    if (strcmp(attr[curIdx++], "in_snd_device")) {
        ALOGE("%s: snd_device not found", __func__);
        return;
    }
    in_snd_device = platform_get_snd_device_index((char *)attr[curIdx++]);
    if (in_snd_device < SND_DEVICE_IN_BEGIN ||
            in_snd_device >= SND_DEVICE_IN_END) {
        ALOGE("%s: Sound device not valid", __func__);
        in_snd_device = SND_DEVICE_NONE;
    }

    return;
}

static void process_mic_info(const XML_Char **attr)
{
    uint32_t curIdx = 0;
    struct mic_info microphone;

    memset(&microphone.channel_mapping, AUDIO_MICROPHONE_CHANNEL_MAPPING_UNUSED,
               sizeof(microphone.channel_mapping));

    if (strcmp(attr[curIdx++], "mic_device_id")) {
        ALOGE("%s: mic_device_id not found", __func__);
        goto on_error;
    }
    strlcpy(microphone.device_id,
                (char *)attr[curIdx++], AUDIO_MICROPHONE_ID_MAX_LEN);

    if (strcmp(attr[curIdx++], "channel_mapping")) {
        ALOGE("%s: channel_mapping not found", __func__);
        goto on_error;
    }
    const char *token = strtok((char *)attr[curIdx++], " ");
    uint32_t idx = 0;
    while (token) {
        if (!find_enum_by_string(mic_channel_mapping, token,
                AUDIO_MICROPHONE_CHANNEL_MAPPING_CNT,
                &microphone.channel_mapping[idx++])) {
            ALOGE("%s: channel_mapping %s in %s not found!",
                      __func__, attr[--curIdx], PLATFORM_INFO_XML_PATH);
            goto on_error;
        }
        token = strtok(NULL, " ");
    }
    microphone.channel_count = idx;

    platform_set_microphone_map(my_data.platform, in_snd_device,
                                    &microphone);
    return;
on_error:
    in_snd_device = SND_DEVICE_NONE;
    return;
}

/* process acdb meta info key value */
static void process_acdb_metainfo_key(const XML_Char **attr)
{
    if (strcmp(attr[0], "name") != 0) {
        ALOGE("%s: 'name' not found", __func__);
        goto done;
    }
    if (strcmp(attr[2], "value") != 0) {
        ALOGE("%s: 'value' not found", __func__);
        goto done;
    }

    int key = atoi((char *)attr[3]);
    if (platform_set_acdb_metainfo_key(my_data.platform,
                                       (char*)attr[1], key) < 0) {
        ALOGE("%s: key %d was not set!", __func__, key);
    }

done:
    return;
}

static void start_tag(void *userdata __unused, const XML_Char *tag_name,
                      const XML_Char **attr)
{
    const XML_Char              *attr_name = NULL;
    const XML_Char              *attr_value = NULL;
    unsigned int                i;


    if (my_data.do_full_parse) {
        if (strcmp(tag_name, "acdb_ids") == 0) {
            section = ACDB;
        } else if (strncmp(tag_name, "module_ids", strlen("module_ids")) == 0) {
            section = MODULE;
        } else if (strcmp(tag_name, "pcm_ids") == 0) {
            section = PCM_ID;
        } else if (strcmp(tag_name, "backend_names") == 0) {
            section = BACKEND_NAME;
        } else if (strcmp(tag_name, "config_params") == 0) {
            section = CONFIG_PARAMS;
        } else if (strcmp(tag_name, "operator_specific") == 0) {
            section = OPERATOR_SPECIFIC;
        } else if (strcmp(tag_name, "gain_db_to_level_mapping") == 0) {
            section = GAIN_LEVEL_MAPPING;
        } else if (strcmp(tag_name, "app_types") == 0) {
            section = APP_TYPE;
        } else if (strcmp(tag_name, "microphone_characteristics") == 0) {
            section = MICROPHONE_CHARACTERISTIC;
        } else if (strcmp(tag_name, "snd_devices") == 0) {
            section = SND_DEVICES;
        } else if(strcmp(tag_name, "acdb_metainfo_key") == 0) {
            section = ACDB_METAINFO_KEY;
        } else if (strcmp(tag_name, "device") == 0) {
            if ((section != ACDB) && (section != AEC) && (section != NS) &&
                (section != BACKEND_NAME) && (section != OPERATOR_SPECIFIC)) {
                ALOGE("device tag only supported for acdb/backend/aec/ns/operator_specific names");
                return;
            }

            /* call into process function for the current section */
            section_process_fn fn = section_table[section];
            fn(attr);
        } else if (strcmp(tag_name, "usecase") == 0) {
            if (section != PCM_ID) {
                ALOGE("usecase tag only supported with PCM_ID section");
                return;
            }

            section_process_fn fn = section_table[PCM_ID];
            fn(attr);
        } else if (strcmp(tag_name, "param") == 0) {
            if ((section != CONFIG_PARAMS) && (section != ACDB_METAINFO_KEY)) {
                ALOGE("param tag only supported with CONFIG_PARAMS section");
                return;
            }

            section_process_fn fn = section_table[section];
            fn(attr);
        } else if (strcmp(tag_name, "gain_level_map") == 0) {
            if (section != GAIN_LEVEL_MAPPING) {
                ALOGE("gain_level_map tag only supported with GAIN_LEVEL_MAPPING section");
                return;
            }

            section_process_fn fn = section_table[GAIN_LEVEL_MAPPING];
            fn(attr);
        } else if (!strcmp(tag_name, "app")) {
            if (section != APP_TYPE) {
                ALOGE("app tag only valid in section APP_TYPE");
                return;
            }

            section_process_fn fn = section_table[APP_TYPE];
            fn(attr);
        } else if (strcmp(tag_name, "microphone") == 0) {
            if (section != MICROPHONE_CHARACTERISTIC) {
                ALOGE("microphone tag only supported with MICROPHONE_CHARACTERISTIC section");
                return;
            }
            section_process_fn fn = section_table[MICROPHONE_CHARACTERISTIC];
            fn(attr);
        } else if (strcmp(tag_name, "input_snd_device") == 0) {
            if (section != SND_DEVICES) {
                ALOGE("input_snd_device tag only supported with SND_DEVICES section");
                return;
            }
            section = INPUT_SND_DEVICE;
        } else if (strcmp(tag_name, "input_snd_device_mic_mapping") == 0) {
            if (section != INPUT_SND_DEVICE) {
                ALOGE("input_snd_device_mic_mapping tag only supported with INPUT_SND_DEVICE section");
                return;
            }
            section = INPUT_SND_DEVICE_TO_MIC_MAPPING;
        } else if (strcmp(tag_name, "snd_dev") == 0) {
            if (section != INPUT_SND_DEVICE_TO_MIC_MAPPING) {
                ALOGE("snd_dev tag only supported with INPUT_SND_DEVICE_TO_MIC_MAPPING section");
                return;
            }
            section_process_fn fn = section_table[SND_DEV];
            fn(attr);
        } else if (strcmp(tag_name, "mic_info") == 0) {
            if (section != INPUT_SND_DEVICE_TO_MIC_MAPPING) {
                ALOGE("mic_info tag only supported with INPUT_SND_DEVICE_TO_MIC_MAPPING section");
                return;
            }
            if (in_snd_device == SND_DEVICE_NONE) {
                ALOGE("%s: Error in previous tags, do not process mic info", __func__);
                return;
            }
            section_process_fn fn = section_table[MIC_INFO];
            fn(attr);
        } else if (strcmp(tag_name, "external_specific_dev") == 0) {
            section = EXTERNAL_DEVICE_SPECIFIC;
        } else if (strcmp(tag_name, "ext_device") == 0) {
            section_process_fn fn = section_table[section];
            fn(attr);
        }
        else if (strncmp(tag_name, "aec", strlen("aec")) == 0) {
            if (section != MODULE) {
                ALOGE("aec tag only supported with MODULE section");
                return;
            }
            section = AEC;
        }
        else if (strncmp(tag_name, "ns", strlen("ns")) == 0) {
            if (section != MODULE) {
                ALOGE("ns tag only supported with MODULE section");
                return;
            }
            section = NS;
        } else if (strcmp(tag_name, "audio_input_source_delay") == 0) {
            section = AUDIO_SOURCE_DELAY;
        } else if (strcmp(tag_name, "audio_source_delay") == 0) {
            section_process_fn fn = section_table[section];
            fn(attr);
        } else if (strcmp(tag_name, "audio_output_usecase_delay") == 0) {
            section = AUDIO_OUTPUT_USECASE_DELAY;
        } else if (strcmp(tag_name, "audio_usecase_delay") == 0) {
            section_process_fn fn = section_table[section];
            fn(attr);
        }
    } else {
        if(strcmp(tag_name, "config_params") == 0) {
            section = CONFIG_PARAMS;
        } else if (strcmp(tag_name, "param") == 0) {
            if (section != CONFIG_PARAMS) {
                ALOGE("param tag only supported with CONFIG_PARAMS section");
                return;
            }

            section_process_fn fn = section_table[section];
            fn(attr);
        }
    }

    return;
}

static void end_tag(void *userdata __unused, const XML_Char *tag_name)
{
    if (strcmp(tag_name, "acdb_ids") == 0) {
        section = ROOT;
    } else if (strncmp(tag_name, "module_ids", strlen("module_ids")) == 0) {
        section = ROOT;
    } else if (strncmp(tag_name, "aec", strlen("aec")) == 0) {
        section = MODULE;
    } else if (strncmp(tag_name, "ns", strlen("ns")) == 0) {
        section = MODULE;
    } else if (strcmp(tag_name, "pcm_ids") == 0) {
        section = ROOT;
    } else if (strcmp(tag_name, "backend_names") == 0) {
        section = ROOT;
    } else if (strcmp(tag_name, "config_params") == 0) {
        section = ROOT;
    } else if (strcmp(tag_name, "operator_specific") == 0) {
        section = ROOT;
    } else if (strcmp(tag_name, "gain_db_to_level_mapping") == 0) {
        section = ROOT;
    } else if (strcmp(tag_name, "app_types") == 0) {
        section = ROOT;
    } else if (strcmp(tag_name, "microphone_characteristics") == 0) {
        section = ROOT;
    } else if (strcmp(tag_name, "snd_devices") == 0) {
        section = ROOT;
    } else if (strcmp(tag_name, "external_specific_dev") == 0) {
        section = ROOT;
    } else if (strcmp(tag_name, "input_snd_device") == 0) {
        section = SND_DEVICES;
    } else if (strcmp(tag_name, "input_snd_device_mic_mapping") == 0) {
        section = INPUT_SND_DEVICE;
    } else if (strcmp(tag_name, "acdb_metainfo_key") == 0) {
        section = ROOT;
    }
}

int platform_info_init(const char *filename, void *platform,
                       bool do_full_parse, set_parameters_fn fn)
{
    XML_Parser      parser;
    FILE            *file;
    int             ret = 0;
    int             bytes_read;
    void            *buf;
    static const uint32_t kBufSize = 1024;
    char   platform_info_file_name[MIXER_PATH_MAX_LENGTH]= {0};

    if (filename == NULL) {
        strlcpy(platform_info_file_name, PLATFORM_INFO_XML_PATH, MIXER_PATH_MAX_LENGTH);
    } else {
        strlcpy(platform_info_file_name, filename, MIXER_PATH_MAX_LENGTH);
    }

    ALOGV("%s: platform info file name is %s", __func__, platform_info_file_name);

    file = fopen(platform_info_file_name, "r");

    if (!file) {
        ALOGD("%s: Failed to open %s, using defaults.",
            __func__, platform_info_file_name);
        ret = -ENODEV;
        goto done;
    }

    parser = XML_ParserCreate(NULL);
    if (!parser) {
        ALOGE("%s: Failed to create XML parser!", __func__);
        ret = -ENODEV;
        goto err_close_file;
    }

    pthread_mutex_lock(&my_data.lock);
    section = ROOT;
    my_data.do_full_parse = do_full_parse;
    my_data.platform = platform;
    my_data.kvpairs = str_parms_create();
    my_data.set_parameters = fn;

    XML_SetElementHandler(parser, start_tag, end_tag);

    while (1) {
        buf = XML_GetBuffer(parser, kBufSize);
        if (buf == NULL) {
            ALOGE("%s: XML_GetBuffer failed", __func__);
            ret = -ENOMEM;
            goto err_free_parser;
        }

        bytes_read = fread(buf, 1, kBufSize, file);
        if (bytes_read < 0) {
            ALOGE("%s: fread failed, bytes read = %d", __func__, bytes_read);
             ret = bytes_read;
            goto err_free_parser;
        }

        if (XML_ParseBuffer(parser, bytes_read,
                            bytes_read == 0) == XML_STATUS_ERROR) {
            ALOGE("%s: XML_ParseBuffer failed, for %s",
                __func__, platform_info_file_name);
            ret = -EINVAL;
            goto err_free_parser;
        }

        if (bytes_read == 0)
            break;
    }

err_free_parser:
    if (my_data.kvpairs != NULL) {
        str_parms_destroy(my_data.kvpairs);
        my_data.kvpairs = NULL;
    }
    pthread_mutex_unlock(&my_data.lock);
    XML_ParserFree(parser);
err_close_file:
    fclose(file);
done:
    return ret;
}
