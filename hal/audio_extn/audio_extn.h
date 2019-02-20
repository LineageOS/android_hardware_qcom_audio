/*
 * Copyright (c) 2013-2019, The Linux Foundation. All rights reserved.
 * Not a Contribution.
 *
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
 *
 * This file was modified by DTS, Inc. The portions of the
 * code modified by DTS, Inc are copyrighted and
 * licensed separately, as follows:
 *
 * (C) 2014 DTS, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef AUDIO_EXTN_H
#define AUDIO_EXTN_H

#include <cutils/str_parms.h>
#include "adsp_hdlr.h"
#include "ip_hdlr_intf.h"
#include "battery_listener.h"

#define AUDIO_PARAMETER_DUAL_MONO  "dual_mono"

#ifndef AFE_PROXY_ENABLED
#define AUDIO_DEVICE_OUT_PROXY 0x40000
#endif

#ifndef AUDIO_DEVICE_IN_PROXY
#define AUDIO_DEVICE_IN_PROXY (AUDIO_DEVICE_BIT_IN | 0x1000000)
#endif

#ifndef INCALL_MUSIC_ENABLED
#define AUDIO_OUTPUT_FLAG_INCALL_MUSIC 0x80000000 //0x8000
#endif

#ifndef AUDIO_DEVICE_OUT_FM_TX
#define AUDIO_DEVICE_OUT_FM_TX 0x8000000
#endif

#ifndef FLAC_OFFLOAD_ENABLED
#define AUDIO_FORMAT_FLAC 0x1B000000UL
#endif

#ifndef WMA_OFFLOAD_ENABLED
#define AUDIO_FORMAT_WMA 0x12000000UL
#define AUDIO_FORMAT_WMA_PRO 0x13000000UL
#endif

#ifndef ALAC_OFFLOAD_ENABLED
#define AUDIO_FORMAT_ALAC 0x1C000000UL
#endif

#ifndef APE_OFFLOAD_ENABLED
#define AUDIO_FORMAT_APE 0x1D000000UL
#endif

#ifndef AAC_ADTS_OFFLOAD_ENABLED
#define AUDIO_FORMAT_AAC_ADTS 0x1E000000UL
#define AUDIO_FORMAT_AAC_ADTS_LC   (AUDIO_FORMAT_AAC_ADTS |\
                                      AUDIO_FORMAT_AAC_SUB_LC)
#define AUDIO_FORMAT_AAC_ADTS_HE_V1 (AUDIO_FORMAT_AAC_ADTS |\
                                      AUDIO_FORMAT_AAC_SUB_HE_V1)
#define AUDIO_FORMAT_AAC_ADTS_HE_V2  (AUDIO_FORMAT_AAC_ADTS |\
                                      AUDIO_FORMAT_AAC_SUB_HE_V2)
#endif

#ifndef AUDIO_FORMAT_AAC_LATM
#define AUDIO_FORMAT_AAC_LATM 0x80000000UL
#define AUDIO_FORMAT_AAC_LATM_LC   (AUDIO_FORMAT_AAC_LATM |\
                                      AUDIO_FORMAT_AAC_SUB_LC)
#define AUDIO_FORMAT_AAC_LATM_HE_V1 (AUDIO_FORMAT_AAC_LATM |\
                                      AUDIO_FORMAT_AAC_SUB_HE_V1)
#define AUDIO_FORMAT_AAC_LATM_HE_V2  (AUDIO_FORMAT_AAC_LATM |\
                                      AUDIO_FORMAT_AAC_SUB_HE_V2)
#endif

#ifndef AUDIO_FORMAT_AC4
#define AUDIO_FORMAT_AC4  0x22000000UL
#endif

#ifndef AUDIO_FORMAT_LDAC
#define AUDIO_FORMAT_LDAC 0x23000000UL
#endif

#ifndef AUDIO_OUTPUT_FLAG_MAIN
#define AUDIO_OUTPUT_FLAG_MAIN 0x8000000
#endif

#ifndef AUDIO_OUTPUT_FLAG_ASSOCIATED
#define AUDIO_OUTPUT_FLAG_ASSOCIATED 0x10000000
#endif

#ifndef AUDIO_OUTPUT_FLAG_TIMESTAMP
#define AUDIO_OUTPUT_FLAG_TIMESTAMP 0x20000000
#endif

#ifndef AUDIO_OUTPUT_FLAG_BD
#define AUDIO_OUTPUT_FLAG_BD 0x40000000
#endif

#ifndef AUDIO_OUTPUT_FLAG_INTERACTIVE
#define AUDIO_OUTPUT_FLAG_INTERACTIVE 0x4000000
#endif

#ifndef COMPRESS_METADATA_NEEDED
#define audio_extn_parse_compress_metadata(out, parms) (0)
#else
int audio_extn_parse_compress_metadata(struct stream_out *out,
                                       struct str_parms *parms);
#endif

#ifdef AUDIO_EXTN_FORMATS_ENABLED
#define AUDIO_OUTPUT_BIT_WIDTH ((config->offload_info.bit_width == 32) ? 24\
                                   :config->offload_info.bit_width)
#else
#define AUDIO_OUTPUT_BIT_WIDTH (CODEC_BACKEND_DEFAULT_BIT_WIDTH)
#define compress_set_next_track_param(compress, codec_options) (0)
#endif

#ifndef AUDIO_HW_EXTN_API_ENABLED
#define compress_set_metadata(compress, metadata) (0)
#define compress_get_metadata(compress, metadata) (0)
#endif

#define MAX_LENGTH_MIXER_CONTROL_IN_INT                  (128)

void audio_extn_set_parameters(struct audio_device *adev,
                               struct str_parms *parms);

void audio_extn_get_parameters(const struct audio_device *adev,
                               struct str_parms *query,
                               struct str_parms *reply);

#ifndef ANC_HEADSET_ENABLED
#define audio_extn_get_anc_enabled()                     (0)
#define audio_extn_should_use_fb_anc()                   (0)
#define audio_extn_should_use_handset_anc(in_channels)   (0)
#define audio_extn_set_aanc_noise_level(adev, parms)     (0)
#else
bool audio_extn_get_anc_enabled(void);
bool audio_extn_should_use_fb_anc(void);
bool audio_extn_should_use_handset_anc(int in_channels);
void audio_extn_set_aanc_noise_level(struct audio_device *adev,
                                     struct str_parms *parms);
#endif

#ifndef VBAT_MONITOR_ENABLED
#define audio_extn_is_vbat_enabled()                     (0)
#define audio_extn_can_use_vbat()                        (0)
#define audio_extn_is_bcl_enabled()                     (0)
#define audio_extn_can_use_bcl()                        (0)
#else
bool audio_extn_is_vbat_enabled(void);
bool audio_extn_can_use_vbat(void);
bool audio_extn_is_bcl_enabled(void);
bool audio_extn_can_use_bcl(void);
#endif

#ifndef RAS_ENABLED
#define audio_extn_is_ras_enabled()                      (0)
#define audio_extn_can_use_ras()                         (0)
#else
bool audio_extn_is_ras_enabled(void);
bool audio_extn_can_use_ras(void);
#endif

#ifndef HIFI_AUDIO_ENABLED
#define audio_extn_is_hifi_audio_enabled()               (0)
#define audio_extn_is_hifi_audio_supported()             (0)
#else
bool audio_extn_is_hifi_audio_enabled(void);
bool audio_extn_is_hifi_audio_supported(void);
#endif

#ifndef FLUENCE_ENABLED
#define audio_extn_set_fluence_parameters(adev, parms) (0)
#define audio_extn_get_fluence_parameters(adev, query, reply) (0)
#else
void audio_extn_set_fluence_parameters(struct audio_device *adev,
                                           struct str_parms *parms);
int audio_extn_get_fluence_parameters(const struct audio_device *adev,
                  struct str_parms *query, struct str_parms *reply);
#endif

#ifndef AFE_PROXY_ENABLED
#define audio_extn_set_afe_proxy_channel_mixer(adev,channel_count)     (0)
#define audio_extn_read_afe_proxy_channel_masks(out)                   (0)
#define audio_extn_get_afe_proxy_channel_count()                       (0)
#else
int32_t audio_extn_set_afe_proxy_channel_mixer(struct audio_device *adev,
                                                    int channel_count);
int32_t audio_extn_read_afe_proxy_channel_masks(struct stream_out *out);
int32_t audio_extn_get_afe_proxy_channel_count();

#endif

#ifndef USB_HEADSET_ENABLED
#define audio_extn_usb_init(adev)                                      (0)
#define audio_extn_usb_deinit()                                        (0)
#define audio_extn_usb_add_device(device, card)                        (0)
#define audio_extn_usb_remove_device(device, card)                     (0)
#define audio_extn_usb_is_config_supported(bit_width, sample_rate, ch, pb) \
                        (*bit_width=0, *sample_rate=0, *ch=0, 0)
#define audio_extn_usb_enable_sidetone(device, enable)                 (0)
#define audio_extn_usb_set_sidetone_gain(parms, value, len)            (0)
#define audio_extn_usb_is_capture_supported()                          (0)
#define audio_extn_usb_get_max_channels(p)                             (0)
#define audio_extn_usb_get_max_bit_width(p)                            (0)
#define audio_extn_usb_get_sup_sample_rates(t, s, l)                   (0)
#define audio_extn_usb_is_tunnel_supported()                           (0)
#define audio_extn_usb_alive(adev)                                     (false)
#define audio_extn_usb_connected(parms)                                (0)
#undef USB_BURST_MODE_ENABLED
#else
void audio_extn_usb_init(void *adev);
void audio_extn_usb_deinit();
void audio_extn_usb_add_device(audio_devices_t device, int card);
void audio_extn_usb_remove_device(audio_devices_t device, int card);
bool audio_extn_usb_is_config_supported(unsigned int *bit_width,
                                        unsigned int *sample_rate,
                                        unsigned int *ch,
                                        bool is_playback);
int audio_extn_usb_enable_sidetone(int device, bool enable);
int audio_extn_usb_set_sidetone_gain(struct str_parms *parms,
                                     char *value, int len);
bool audio_extn_usb_is_capture_supported();
int audio_extn_usb_get_max_channels(bool playback);
int audio_extn_usb_get_max_bit_width(bool playback);
int audio_extn_usb_get_sup_sample_rates(int type, uint32_t *sr, uint32_t l);
bool audio_extn_usb_is_tunnel_supported();
bool audio_extn_usb_alive(int card);
bool audio_extn_usb_connected(struct str_parms *parms);
#endif

#ifndef USB_BURST_MODE_ENABLED
#define audio_extn_usb_find_service_interval(m, p)                     (0)
#define audio_extn_usb_altset_for_service_interval(p, si, bw, sr, ch)  (-1)
#define audio_extn_usb_set_service_interval(p, si, recfg)              (-1)
#define audio_extn_usb_get_service_interval(p, si)                     (-1)
#define audio_extn_usb_check_and_set_svc_int(uc,ss)                    (0)
#define audio_extn_usb_is_reconfig_req()                               (0)
#define audio_extn_usb_set_reconfig(isreq)                             (0)
#else
unsigned long audio_extn_usb_find_service_interval(bool min, bool playback);
int audio_extn_usb_altset_for_service_interval(bool is_playback,
                                               unsigned long service_interval,
                                               uint32_t *bit_width,
                                               uint32_t *sample_rate,
                                               uint32_t *channel_count);
int audio_extn_usb_set_service_interval(bool playback,
                                        unsigned long service_interval,
                                        bool *reconfig);
int audio_extn_usb_get_service_interval(bool playback,
                                        unsigned long *service_interval);
int audio_extn_usb_check_and_set_svc_int(struct audio_usecase *uc_info,
                                         bool starting_output_stream);
bool audio_extn_usb_is_reconfig_req();
void audio_extn_usb_set_reconfig(bool is_required);
#endif

#ifndef SPLIT_A2DP_ENABLED
#define audio_extn_a2dp_init(adev)                       (0)
#define audio_extn_a2dp_start_playback()                 (0)
#define audio_extn_a2dp_stop_playback()                  (0)
#define audio_extn_a2dp_set_parameters(parms)            (0)
#define audio_extn_a2dp_is_force_device_switch()         (0)
#define audio_extn_a2dp_set_handoff_mode(is_on)          (0)
#define audio_extn_a2dp_get_sample_rate(sample_rate)     (0)
#define audio_extn_a2dp_get_encoder_latency()            (0)
#define audio_extn_a2dp_is_ready()                       (0)
#define audio_extn_a2dp_is_suspended()                   (0)
#else
void audio_extn_a2dp_init(void *adev);
int audio_extn_a2dp_start_playback();
int audio_extn_a2dp_stop_playback();
void audio_extn_a2dp_set_parameters(struct str_parms *parms);
bool audio_extn_a2dp_is_force_device_switch();
void audio_extn_a2dp_set_handoff_mode(bool is_on);
void audio_extn_a2dp_get_sample_rate(int *sample_rate);
uint32_t audio_extn_a2dp_get_encoder_latency();
bool audio_extn_a2dp_is_ready();
bool audio_extn_a2dp_is_suspended();
#endif

#ifndef SSR_ENABLED
#define audio_extn_ssr_check_usecase(in)                                  (0)
#define audio_extn_ssr_set_usecase(in, config, channel_mask_updated)      (0)
#define audio_extn_ssr_init(in, num_out_chan)                             (0)
#define audio_extn_ssr_deinit()                                           (0)
#define audio_extn_ssr_update_enabled()                                   (0)
#define audio_extn_ssr_get_enabled()                                      (0)
#define audio_extn_ssr_read(stream, buffer, bytes)                        (0)
#define audio_extn_ssr_set_parameters(adev, parms)                        (0)
#define audio_extn_ssr_get_parameters(adev, parms, reply)                 (0)
#define audio_extn_ssr_get_stream()                                       (0)
#else
bool audio_extn_ssr_check_usecase(struct stream_in *in);
int audio_extn_ssr_set_usecase(struct stream_in *in,
                                         struct audio_config *config,
                                         bool *channel_mask_updated);
int32_t audio_extn_ssr_init(struct stream_in *in,
                            int num_out_chan);
int32_t audio_extn_ssr_deinit();
void audio_extn_ssr_update_enabled();
bool audio_extn_ssr_get_enabled();
int32_t audio_extn_ssr_read(struct audio_stream_in *stream,
                       void *buffer, size_t bytes);
void audio_extn_ssr_set_parameters(struct audio_device *adev,
                                   struct str_parms *parms);
void audio_extn_ssr_get_parameters(const struct audio_device *adev,
                                   struct str_parms *query,
                                   struct str_parms *reply);
struct stream_in *audio_extn_ssr_get_stream();
#endif
int audio_extn_check_and_set_multichannel_usecase(struct audio_device *adev,
                                                  struct stream_in *in,
                                                  struct audio_config *config,
                                                  bool *update_params);

#ifndef HW_VARIANTS_ENABLED
#define hw_info_init(snd_card_name)                      (0)
#define hw_info_deinit(hw_info)                          (0)
#define hw_info_append_hw_type(hw_info,\
        snd_device, device_name)                         (0)
#define hw_info_enable_wsa_combo_usecase_support(hw_info)   (0)
#define hw_info_is_stereo_spkr(hw_info)   (0)

#else
void *hw_info_init(const char *snd_card_name);
void hw_info_deinit(void *hw_info);
void hw_info_append_hw_type(void *hw_info, snd_device_t snd_device,
                             char *device_name);
void hw_info_enable_wsa_combo_usecase_support(void *hw_info);
bool hw_info_is_stereo_spkr(void *hw_info);

#endif

#ifndef AUDIO_LISTEN_ENABLED
#define audio_extn_listen_init(adev, snd_card)                  (0)
#define audio_extn_listen_deinit(adev)                          (0)
#define audio_extn_listen_update_device_status(snd_dev, event)  (0)
#define audio_extn_listen_update_stream_status(uc_info, event)  (0)
#define audio_extn_listen_set_parameters(adev, parms)           (0)
#else
enum listen_event_type {
    LISTEN_EVENT_SND_DEVICE_FREE,
    LISTEN_EVENT_SND_DEVICE_BUSY,
    LISTEN_EVENT_STREAM_FREE,
    LISTEN_EVENT_STREAM_BUSY
};
typedef enum listen_event_type listen_event_type_t;

int audio_extn_listen_init(struct audio_device *adev, unsigned int snd_card);
void audio_extn_listen_deinit(struct audio_device *adev);
void audio_extn_listen_update_device_status(snd_device_t snd_device,
                                     listen_event_type_t event);
void audio_extn_listen_update_stream_status(struct audio_usecase *uc_info,
                                     listen_event_type_t event);
void audio_extn_listen_set_parameters(struct audio_device *adev,
                                      struct str_parms *parms);
#endif /* AUDIO_LISTEN_ENABLED */

#ifndef SOUND_TRIGGER_ENABLED
#define audio_extn_sound_trigger_init(adev)                            (0)
#define audio_extn_sound_trigger_deinit(adev)                          (0)
#define audio_extn_sound_trigger_update_device_status(snd_dev, event)  (0)
#define audio_extn_sound_trigger_update_stream_status(uc_info, event)  (0)
#define audio_extn_sound_trigger_update_battery_status(charging)       (0)
#define audio_extn_sound_trigger_set_parameters(adev, parms)           (0)
#define audio_extn_sound_trigger_get_parameters(adev, query, reply)    (0)
#define audio_extn_sound_trigger_check_and_get_session(in)             (0)
#define audio_extn_sound_trigger_stop_lab(in)                          (0)
#define audio_extn_sound_trigger_read(in, buffer, bytes)               (0)
#define audio_extn_sound_trigger_check_ec_ref_enable()                 (0)
#define audio_extn_sound_trigger_update_ec_ref_status(on)              (0)
#else

enum st_event_type {
    ST_EVENT_SND_DEVICE_FREE,
    ST_EVENT_SND_DEVICE_BUSY,
    ST_EVENT_STREAM_FREE,
    ST_EVENT_STREAM_BUSY
};
typedef enum st_event_type st_event_type_t;

int audio_extn_sound_trigger_init(struct audio_device *adev);
void audio_extn_sound_trigger_deinit(struct audio_device *adev);
void audio_extn_sound_trigger_update_device_status(snd_device_t snd_device,
                                     st_event_type_t event);
void audio_extn_sound_trigger_update_stream_status(struct audio_usecase *uc_info,
                                     st_event_type_t event);
void audio_extn_sound_trigger_update_battery_status(bool charging);
void audio_extn_sound_trigger_set_parameters(struct audio_device *adev,
                                             struct str_parms *parms);
void audio_extn_sound_trigger_check_and_get_session(struct stream_in *in);
void audio_extn_sound_trigger_stop_lab(struct stream_in *in);
int audio_extn_sound_trigger_read(struct stream_in *in, void *buffer,
                                  size_t bytes);
void audio_extn_sound_trigger_get_parameters(const struct audio_device *adev,
                     struct str_parms *query, struct str_parms *reply);
bool audio_extn_sound_trigger_check_ec_ref_enable();
void audio_extn_sound_trigger_update_ec_ref_status(bool on);
#endif

#ifndef AUXPCM_BT_ENABLED
#define audio_extn_read_xml(adev, mixer_card, MIXER_XML_PATH, \
                            MIXER_XML_PATH_AUXPCM)               (-ENOSYS)
#else
int32_t audio_extn_read_xml(struct audio_device *adev, uint32_t mixer_card,
                            const char* mixer_xml_path,
                            const char* mixer_xml_path_auxpcm);
#endif /* AUXPCM_BT_ENABLED */
#ifndef SPKR_PROT_ENABLED
#define audio_extn_spkr_prot_init(adev)       (0)
#define audio_extn_spkr_prot_deinit()         (0)
#define audio_extn_spkr_prot_start_processing(snd_device)    (-EINVAL)
#define audio_extn_spkr_prot_calib_cancel(adev) (0)
#define audio_extn_spkr_prot_stop_processing(snd_device)     (0)
#define audio_extn_spkr_prot_is_enabled() (false)
#define audio_extn_spkr_prot_set_parameters(parms, value, len)   (0)
#define audio_extn_fbsp_set_parameters(parms)   (0)
#define audio_extn_fbsp_get_parameters(query, reply)   (0)
#else
void audio_extn_spkr_prot_init(void *adev);
int audio_extn_spkr_prot_deinit();
int audio_extn_spkr_prot_start_processing(snd_device_t snd_device);
void audio_extn_spkr_prot_stop_processing(snd_device_t snd_device);
bool audio_extn_spkr_prot_is_enabled();
void audio_extn_spkr_prot_calib_cancel(void *adev);
void audio_extn_spkr_prot_set_parameters(struct str_parms *parms,
                                         char *value, int len);
int audio_extn_fbsp_set_parameters(struct str_parms *parms);
int audio_extn_fbsp_get_parameters(struct str_parms *query,
                                   struct str_parms *reply);
#endif

#ifndef COMPRESS_CAPTURE_ENABLED
#define audio_extn_compr_cap_init(in)                     (0)
#define audio_extn_compr_cap_enabled()                    (0)
#define audio_extn_compr_cap_format_supported(format)     (0)
#define audio_extn_compr_cap_usecase_supported(usecase)   (0)
#define audio_extn_compr_cap_get_buffer_size(format)      (0)
#define audio_extn_compr_cap_read(in, buffer, bytes)      (0)
#define audio_extn_compr_cap_deinit()                     (0)
#else
void audio_extn_compr_cap_init(struct stream_in *in);
bool audio_extn_compr_cap_enabled();
bool audio_extn_compr_cap_format_supported(audio_format_t format);
bool audio_extn_compr_cap_usecase_supported(audio_usecase_t usecase);
size_t audio_extn_compr_cap_get_buffer_size(audio_format_t format);
size_t audio_extn_compr_cap_read(struct stream_in *in,
                                        void *buffer, size_t bytes);
void audio_extn_compr_cap_deinit();
#endif

#ifndef DTS_EAGLE
#define audio_extn_dts_eagle_set_parameters(adev, parms)     (0)
#define audio_extn_dts_eagle_get_parameters(adev, query, reply) (0)
#define audio_extn_dts_eagle_fade(adev, fade_in, out) (0)
#define audio_extn_dts_eagle_send_lic()               (0)
#define audio_extn_dts_create_state_notifier_node(stream_out) (0)
#define audio_extn_dts_notify_playback_state(stream_out, has_video, sample_rate, \
                                    channels, is_playing) (0)
#define audio_extn_dts_remove_state_notifier_node(stream_out) (0)
#define audio_extn_check_and_set_dts_hpx_state(adev)       (0)
#else
void audio_extn_dts_eagle_set_parameters(struct audio_device *adev,
                                         struct str_parms *parms);
int audio_extn_dts_eagle_get_parameters(const struct audio_device *adev,
                  struct str_parms *query, struct str_parms *reply);
int audio_extn_dts_eagle_fade(const struct audio_device *adev, bool fade_in, const struct stream_out *out);
void audio_extn_dts_eagle_send_lic();
void audio_extn_dts_create_state_notifier_node(int stream_out);
void audio_extn_dts_notify_playback_state(int stream_out, int has_video, int sample_rate,
                                  int channels, int is_playing);
void audio_extn_dts_remove_state_notifier_node(int stream_out);
void audio_extn_check_and_set_dts_hpx_state(const struct audio_device *adev);
#endif

#if defined(DS1_DOLBY_DDP_ENABLED) || defined(DS1_DOLBY_DAP_ENABLED)
void audio_extn_dolby_set_dmid(struct audio_device *adev);
#else
#define audio_extn_dolby_set_dmid(adev)                 (0)
#define AUDIO_CHANNEL_OUT_PENTA (AUDIO_CHANNEL_OUT_QUAD | AUDIO_CHANNEL_OUT_FRONT_CENTER)
#define AUDIO_CHANNEL_OUT_SURROUND (AUDIO_CHANNEL_OUT_FRONT_LEFT | AUDIO_CHANNEL_OUT_FRONT_RIGHT | \
                                    AUDIO_CHANNEL_OUT_FRONT_CENTER | AUDIO_CHANNEL_OUT_BACK_CENTER)
#endif

#if defined(DS1_DOLBY_DDP_ENABLED) || defined(DS1_DOLBY_DAP_ENABLED) || defined(DS2_DOLBY_DAP_ENABLED)
void audio_extn_dolby_set_license(struct audio_device *adev);
#else
static void __unused audio_extn_dolby_set_license(struct audio_device *adev __unused) {};
#endif

#ifndef DS1_DOLBY_DAP_ENABLED
#define audio_extn_dolby_set_endpoint(adev)                 (0)
#else
void audio_extn_dolby_set_endpoint(struct audio_device *adev);
#endif


#if defined(DS1_DOLBY_DDP_ENABLED) || defined(DS2_DOLBY_DAP_ENABLED)
int audio_extn_dolby_get_snd_codec_id(struct audio_device *adev,
                                      struct stream_out *out,
                                      audio_format_t format);
#else
#define audio_extn_dolby_get_snd_codec_id(adev, out, format)       (0)
#endif

#ifndef DS1_DOLBY_DDP_ENABLED
#define audio_extn_ddp_set_parameters(adev, parms)      (0)
#define audio_extn_dolby_send_ddp_endp_params(adev)     (0)
#else
void audio_extn_ddp_set_parameters(struct audio_device *adev,
                                   struct str_parms *parms);
void audio_extn_dolby_send_ddp_endp_params(struct audio_device *adev);

#endif

#ifndef AUDIO_OUTPUT_FLAG_COMPRESS_PASSTHROUGH
#define AUDIO_OUTPUT_FLAG_COMPRESS_PASSTHROUGH  0x1000
#endif

enum {
    EXT_DISPLAY_TYPE_NONE,
    EXT_DISPLAY_TYPE_HDMI,
    EXT_DISPLAY_TYPE_DP
};

/* Used to limit sample rate for TrueHD & EC3 */
#define HDMI_PASSTHROUGH_MAX_SAMPLE_RATE 192000

#ifndef HDMI_PASSTHROUGH_ENABLED
#define audio_extn_passthru_update_stream_configuration(adev, out, buffer, bytes)  (0)
#define audio_extn_passthru_is_convert_supported(adev, out)                   (0)
#define audio_extn_passthru_is_passt_supported(adev, out)                     (0)
#define audio_extn_passthru_is_passthrough_stream(out)                        (0)
#define audio_extn_passthru_get_buffer_size(info)                             (0)
#define audio_extn_passthru_set_volume(out, mute)                             (0)
#define audio_extn_passthru_set_latency(out, latency)                         (0)
#define audio_extn_passthru_is_supported_format(f) (0)
#define audio_extn_passthru_should_drop_data(o) (0)
#define audio_extn_passthru_on_start(o) do {} while(0)
#define audio_extn_passthru_on_stop(o) do {} while(0)
#define audio_extn_passthru_on_pause(o) do {} while(0)
#define audio_extn_passthru_is_enabled() (0)
#define audio_extn_passthru_is_active() (0)
#define audio_extn_passthru_set_parameters(a, p) (-ENOSYS)
#define audio_extn_passthru_init(a) do {} while(0)
#define audio_extn_passthru_should_standby(o) (1)
#define audio_extn_passthru_get_channel_count(out) (0)
#define audio_extn_passthru_update_dts_stream_configuration(out, buffer, bytes) (-ENOSYS)
#define audio_extn_passthru_is_direct_passthrough(out)	(0)
#define audio_extn_passthru_is_supported_backend_edid_cfg(adev, out) (0)
#else
bool audio_extn_passthru_is_convert_supported(struct audio_device *adev,
                                                 struct stream_out *out);
bool audio_extn_passthru_is_passt_supported(struct audio_device *adev,
                                         struct stream_out *out);
void audio_extn_passthru_update_stream_configuration(
        struct audio_device *adev, struct stream_out *out,
        const void *buffer, size_t bytes);
bool audio_extn_passthru_is_passthrough_stream(struct stream_out *out);
int audio_extn_passthru_get_buffer_size(audio_offload_info_t* info);
int audio_extn_passthru_set_volume(struct stream_out *out, int mute);
int audio_extn_passthru_set_latency(struct stream_out *out, int latency);
bool audio_extn_passthru_is_supported_format(audio_format_t format);
bool audio_extn_passthru_should_drop_data(struct stream_out * out);
void audio_extn_passthru_on_start(struct stream_out *out);
void audio_extn_passthru_on_stop(struct stream_out *out);
void audio_extn_passthru_on_pause(struct stream_out *out);
int audio_extn_passthru_set_parameters(struct audio_device *adev,
                                       struct str_parms *parms);
bool audio_extn_passthru_is_enabled();
bool audio_extn_passthru_is_active();
void audio_extn_passthru_init(struct audio_device *adev);
bool audio_extn_passthru_should_standby(struct stream_out *out);
int audio_extn_passthru_get_channel_count(struct stream_out *out);
int audio_extn_passthru_update_dts_stream_configuration(struct stream_out *out,
        const void *buffer, size_t bytes);
bool audio_extn_passthru_is_direct_passthrough(struct stream_out *out);
bool audio_extn_passthru_is_supported_backend_edid_cfg(struct audio_device *adev,
                                                   struct stream_out *out);
#endif

#ifndef HFP_ENABLED
#define audio_extn_hfp_is_active(adev)                  (0)
#define audio_extn_hfp_get_usecase()                    (-1)
#define hfp_set_mic_mute(dev, state)                    (0)
#define audio_extn_hfp_set_parameters(adev, parms)      (0)
#else
bool audio_extn_hfp_is_active(struct audio_device *adev);
audio_usecase_t audio_extn_hfp_get_usecase();
int hfp_set_mic_mute(struct audio_device *dev, bool state);
void audio_extn_hfp_set_parameters(struct audio_device *adev,
                                           struct str_parms *parms);
#endif

#ifndef DEV_ARBI_ENABLED
#define audio_extn_dev_arbi_init()                  (0)
#define audio_extn_dev_arbi_deinit()                (0)
#define audio_extn_dev_arbi_acquire(snd_device)     (0)
#define audio_extn_dev_arbi_release(snd_device)     (0)
#else
int audio_extn_dev_arbi_init();
int audio_extn_dev_arbi_deinit();
int audio_extn_dev_arbi_acquire(snd_device_t snd_device);
int audio_extn_dev_arbi_release(snd_device_t snd_device);
#endif

#ifndef PM_SUPPORT_ENABLED
#define audio_extn_pm_set_parameters(params) (0)
#define audio_extn_pm_vote(void) (0)
#define audio_extn_pm_unvote(void) (0)
#else
void audio_extn_pm_set_parameters(struct str_parms *parms);
int audio_extn_pm_vote (void);
void audio_extn_pm_unvote(void);
#endif

void audio_extn_init(struct audio_device *adev);
void audio_extn_utils_update_streams_cfg_lists(void *platform,
                                  struct mixer *mixer,
                                  struct listnode *streams_output_cfg_list,
                                  struct listnode *streams_input_cfg_list);
void audio_extn_utils_dump_streams_cfg_lists(
                                  struct listnode *streams_output_cfg_list,
                                  struct listnode *streams_input_cfg_list);
void audio_extn_utils_release_streams_cfg_lists(
                                  struct listnode *streams_output_cfg_list,
                                  struct listnode *streams_input_cfg_list);
void audio_extn_utils_update_stream_output_app_type_cfg(void *platform,
                                  struct listnode *streams_output_cfg_list,
                                  audio_devices_t devices,
                                  audio_output_flags_t flags,
                                  audio_format_t format,
                                  uint32_t sample_rate,
                                  uint32_t bit_width,
                                  audio_channel_mask_t channel_mask,
                                  char *profile,
                                  struct stream_app_type_cfg *app_type_cfg);
void audio_extn_utils_update_stream_input_app_type_cfg(void *platform,
                                  struct listnode *streams_input_cfg_list,
                                  audio_devices_t devices,
                                  audio_input_flags_t flags,
                                  audio_format_t format,
                                  uint32_t sample_rate,
                                  uint32_t bit_width,
                                  char *profile,
                                  struct stream_app_type_cfg *app_type_cfg);
int audio_extn_utils_send_app_type_cfg(struct audio_device *adev,
                                       struct audio_usecase *usecase);
void audio_extn_utils_send_audio_calibration(struct audio_device *adev,
                                             struct audio_usecase *usecase);
void audio_extn_utils_update_stream_app_type_cfg_for_usecase(
                                  struct audio_device *adev,
                                  struct audio_usecase *usecase);
int audio_extn_utils_get_snd_card_num();
int audio_extn_utils_open_snd_mixer(struct mixer **mixer_handle);
void audio_extn_utils_close_snd_mixer(struct mixer *mixer);
bool audio_extn_is_dsp_bit_width_enforce_mode_supported(audio_output_flags_t flags);
bool audio_extn_utils_is_dolby_format(audio_format_t format);
int audio_extn_utils_get_bit_width_from_string(const char *);
int audio_extn_utils_get_sample_rate_from_string(const char *);
int audio_extn_utils_get_channels_from_string(const char *);
void audio_extn_utils_release_snd_device(snd_device_t snd_device);

#ifdef DS2_DOLBY_DAP_ENABLED
#define LIB_DS2_DAP_HAL "vendor/lib/libhwdaphal.so"
#define SET_HW_INFO_FUNC "dap_hal_set_hw_info"
typedef enum {
    SND_CARD            = 0,
    HW_ENDPOINT         = 1,
    DMID                = 2,
    DEVICE_BE_ID_MAP    = 3,
    DAP_BYPASS          = 4,
} dap_hal_hw_info_t;
typedef int (*dap_hal_set_hw_info_t)(int32_t hw_info, void* data);
typedef struct {
     int (*device_id_to_be_id)[2];
     int len;
} dap_hal_device_be_id_map_t;

int audio_extn_dap_hal_init(int snd_card);
int audio_extn_dap_hal_deinit();
void audio_extn_dolby_ds2_set_endpoint(struct audio_device *adev);
int audio_extn_ds2_enable(struct audio_device *adev);
int audio_extn_dolby_set_dap_bypass(struct audio_device *adev, int state);
void audio_extn_ds2_set_parameters(struct audio_device *adev,
                                   struct str_parms *parms);

#else
#define audio_extn_dap_hal_init(snd_card)                             (0)
#define audio_extn_dap_hal_deinit()                                   (0)
#define audio_extn_dolby_ds2_set_endpoint(adev)                       (0)
#define audio_extn_ds2_enable(adev)                                   (0)
#define audio_extn_dolby_set_dap_bypass(adev, state)                  (0)
#define audio_extn_ds2_set_parameters(adev, parms);                   (0)
#endif
typedef enum {
    DAP_STATE_ON = 0,
    DAP_STATE_BYPASS,
} dap_state;
#ifndef AUDIO_FORMAT_E_AC3_JOC
#define AUDIO_FORMAT_E_AC3_JOC  0x19000000UL
#endif
#ifndef AUDIO_FORMAT_DTS_LBR
#define AUDIO_FORMAT_DTS_LBR 0x1E000000UL
#endif

#define DIV_ROUND_UP(x, y) (((x) + (y) - 1)/(y))
#define ALIGN(x, y) ((y) * DIV_ROUND_UP((x), (y)))

int b64decode(char *inp, int ilen, uint8_t* outp);
int b64encode(uint8_t *inp, int ilen, char* outp);
int read_line_from_file(const char *path, char *buf, size_t count);
int audio_extn_utils_get_codec_version(const char *snd_card_name, int card_num, char *codec_version);
audio_format_t alsa_format_to_hal(uint32_t alsa_format);
uint32_t hal_format_to_alsa(audio_format_t hal_format);
audio_format_t pcm_format_to_hal(uint32_t pcm_format);
uint32_t hal_format_to_pcm(audio_format_t hal_format);

void audio_extn_utils_update_direct_pcm_fragment_size(struct stream_out *out);
size_t audio_extn_utils_convert_format_24_8_to_8_24(void *buf, size_t bytes);
int get_snd_codec_id(audio_format_t format);

#ifndef KPI_OPTIMIZE_ENABLED
#define audio_extn_perf_lock_init() (0)
#define audio_extn_perf_lock_acquire(handle, duration, opts, size) (0)
#define audio_extn_perf_lock_release(handle) (0)
#else
int audio_extn_perf_lock_init(void);
void audio_extn_perf_lock_acquire(int *handle, int duration,
                                 int *opts, int size);
void audio_extn_perf_lock_release(int *handle);

#endif /* KPI_OPTIMIZE_ENABLED */

#ifndef AUDIO_EXTERNAL_HDMI_ENABLED
#define audio_utils_set_hdmi_channel_status(out, buffer, bytes) (0)
#else
void audio_utils_set_hdmi_channel_status(struct stream_out *out, char * buffer, size_t bytes);
#endif

#ifdef QAF_EXTN_ENABLED
bool audio_extn_qaf_is_enabled();
void audio_extn_qaf_deinit();
void audio_extn_qaf_close_output_stream(struct audio_hw_device *dev __unused,
                                        struct audio_stream_out *stream);
int audio_extn_qaf_open_output_stream(struct audio_hw_device *dev,
                                   audio_io_handle_t handle,
                                   audio_devices_t devices,
                                   audio_output_flags_t flags,
                                   struct audio_config *config,
                                   struct audio_stream_out **stream_out,
                                   const char *address __unused);
int audio_extn_qaf_init(struct audio_device *adev);
int audio_extn_qaf_set_parameters(struct audio_device *adev, struct str_parms *parms);
int audio_extn_qaf_out_set_param_data(struct stream_out *out,
                           audio_extn_param_id param_id,
                           audio_extn_param_payload *payload);
int audio_extn_qaf_out_get_param_data(struct stream_out *out,
                             audio_extn_param_id param_id,
                             audio_extn_param_payload *payload);
bool audio_extn_is_qaf_stream(struct stream_out *out);
#else
#define audio_extn_qaf_is_enabled()                                     (0)
#define audio_extn_qaf_deinit()                                         (0)
#define audio_extn_qaf_close_output_stream         adev_close_output_stream
#define audio_extn_qaf_open_output_stream           adev_open_output_stream
#define audio_extn_qaf_init(adev)                                       (0)
#define audio_extn_qaf_set_parameters(adev, parms)                      (0)
#define audio_extn_qaf_out_set_param_data(out, param_id, payload)       (0)
#define audio_extn_qaf_out_get_param_data(out, param_id, payload)       (0)
#define audio_extn_is_qaf_stream(out)                                   (0)
#endif

#ifdef AUDIO_EXTN_BT_HAL_ENABLED
int audio_extn_bt_hal_load(void **handle);
int audio_extn_bt_hal_open_output_stream(void *handle, int in_rate, audio_channel_mask_t channel_mask, int bit_width);
int audio_extn_bt_hal_unload(void *handle);
int audio_extn_bt_hal_close_output_stream(void *handle);
int audio_extn_bt_hal_out_write(void *handle, void *buf, int size);
struct audio_stream_out *audio_extn_bt_hal_get_output_stream(void *handle);
void *audio_extn_bt_hal_get_device(void *handle);
int audio_extn_bt_hal_get_latency(void *handle);
#else
#define audio_extn_bt_hal_load(...)                   (-EINVAL)
#define audio_extn_bt_hal_unload(...)                 (-EINVAL)
#define audio_extn_bt_hal_open_output_stream(...)     (-EINVAL)
#define audio_extn_bt_hal_close_output_stream(...)    (-EINVAL)
#define audio_extn_bt_hal_out_write(...)              (-EINVAL)
#define audio_extn_bt_hal_get_latency(...)            (-EINVAL)
#define audio_extn_bt_hal_get_output_stream(...)      NULL
#define audio_extn_bt_hal_get_device(...)             NULL
#endif

#ifndef KEEP_ALIVE_ENABLED
#define audio_extn_keep_alive_init(adev) do {} while(0)
#define audio_extn_keep_alive_deinit() do {} while(0)
#define audio_extn_keep_alive_start(ka_mode) do {} while(0)
#define audio_extn_keep_alive_stop(ka_mode) do {} while(0)
#define audio_extn_keep_alive_is_active() (false)
#define audio_extn_keep_alive_set_parameters(adev, parms) (0)
#else
void audio_extn_keep_alive_init(struct audio_device *adev);
void audio_extn_keep_alive_deinit();
void audio_extn_keep_alive_start(ka_mode_t ka_mode);
void audio_extn_keep_alive_stop(ka_mode_t ka_mode);
bool audio_extn_keep_alive_is_active();
int audio_extn_keep_alive_set_parameters(struct audio_device *adev,
                                         struct str_parms *parms);
#endif

#ifndef AUDIO_GENERIC_EFFECT_FRAMEWORK_ENABLED

#define audio_extn_gef_init(adev) (0)
#define audio_extn_gef_deinit() (0)
#define audio_extn_gef_notify_device_config(devices, cmask, sample_rate, acdb_id) (0)

#ifndef INSTANCE_ID_ENABLED
#define audio_extn_gef_send_audio_cal(dev, acdb_dev_id, acdb_device_type,\
    app_type, topology_id, sample_rate, module_id, param_id, data, length, persist) (0)
#define audio_extn_gef_get_audio_cal(adev, acdb_dev_id, acdb_device_type,\
    app_type, topology_id, sample_rate, module_id, param_id, data, length, persist) (0)
#define audio_extn_gef_store_audio_cal(adev, acdb_dev_id, acdb_device_type,\
    app_type, topology_id, sample_rate, module_id, param_id, data, length) (0)
#define audio_extn_gef_retrieve_audio_cal(adev, acdb_dev_id, acdb_device_type,\
    app_type, topology_id, sample_rate, module_id, param_id, data, length) (0)
#else
#define audio_extn_gef_send_audio_cal(dev, acdb_dev_id, acdb_device_type,\
    app_type, topology_id, sample_rate, module_id, instance_id, param_id, data,\
    length, persist) (0)
#define audio_extn_gef_get_audio_cal(adev, acdb_dev_id, acdb_device_type,\
    app_type, topology_id, sample_rate, module_id, instance_id, param_id, data,\
    length, persist) (0)
#define audio_extn_gef_store_audio_cal(adev, acdb_dev_id, acdb_device_type,\
    app_type, topology_id, sample_rate, module_id, instance_id, param_id, data,\
    length) (0)
#define audio_extn_gef_retrieve_audio_cal(adev, acdb_dev_id, acdb_device_type,\
    app_type, topology_id, sample_rate, module_id, instance_id, param_id, data,\
    length) (0)
#endif

#else

void audio_extn_gef_init(struct audio_device *adev);
void audio_extn_gef_deinit();

void audio_extn_gef_notify_device_config(audio_devices_t audio_device,
    audio_channel_mask_t channel_mask, int sample_rate, int acdb_id);
#ifndef INSTANCE_ID_ENABLED
int audio_extn_gef_send_audio_cal(void* adev, int acdb_dev_id, int acdb_device_type,
    int app_type, int topology_id, int sample_rate, uint32_t module_id,
    uint32_t param_id, void* data, int length, bool persist);
int audio_extn_gef_get_audio_cal(void* adev, int acdb_dev_id, int acdb_device_type,
    int app_type, int topology_id, int sample_rate, uint32_t module_id,
    uint32_t param_id, void* data, int* length, bool persist);
int audio_extn_gef_store_audio_cal(void* adev, int acdb_dev_id, int acdb_device_type,
    int app_type, int topology_id, int sample_rate, uint32_t module_id,
    uint32_t param_id, void* data, int length);
int audio_extn_gef_retrieve_audio_cal(void* adev, int acdb_dev_id, int acdb_device_type,
    int app_type, int topology_id, int sample_rate, uint32_t module_id,
    uint32_t param_id, void* data, int* length);
#else
int audio_extn_gef_send_audio_cal(void* adev, int acdb_dev_id, int acdb_device_type,
    int app_type, int topology_id, int sample_rate, uint32_t module_id,
    uint16_t instance_id, uint32_t param_id, void* data, int length, bool persist);
int audio_extn_gef_get_audio_cal(void* adev, int acdb_dev_id, int acdb_device_type,
    int app_type, int topology_id, int sample_rate, uint32_t module_id,
    uint16_t instance_id, uint32_t param_id, void* data, int* length, bool persist);
int audio_extn_gef_store_audio_cal(void* adev, int acdb_dev_id, int acdb_device_type,
    int app_type, int topology_id, int sample_rate, uint32_t module_id,
    uint16_t instance_id, uint32_t param_id, void* data, int length);
int audio_extn_gef_retrieve_audio_cal(void* adev, int acdb_dev_id, int acdb_device_type,
    int app_type, int topology_id, int sample_rate, uint32_t module_id,
    uint16_t instance_id, uint32_t param_id, void* data, int* length);
#endif

#endif /* AUDIO_GENERIC_EFFECT_FRAMEWORK_ENABLED */

typedef void (* snd_mon_cb)(void * stream, struct str_parms * parms);
#ifndef SND_MONITOR_ENABLED
#define audio_extn_snd_mon_init()           (0)
#define audio_extn_snd_mon_deinit()         (0)
#define audio_extn_snd_mon_register_listener(stream, cb) (0)
#define audio_extn_snd_mon_unregister_listener(stream) (0)
#else
int audio_extn_snd_mon_init();
int audio_extn_snd_mon_deinit();
int audio_extn_snd_mon_register_listener(void *stream, snd_mon_cb cb);
int audio_extn_snd_mon_unregister_listener(void *stream);
#endif

#ifdef COMPRESS_INPUT_ENABLED
bool audio_extn_cin_applicable_stream(struct stream_in *in);
bool audio_extn_cin_attached_usecase(audio_usecase_t uc_id);
size_t audio_extn_cin_get_buffer_size(struct stream_in *in);
int audio_extn_cin_start_input_stream(struct stream_in *in);
void audio_extn_cin_stop_input_stream(struct stream_in *in);
void audio_extn_cin_close_input_stream(struct stream_in *in);
int audio_extn_cin_read(struct stream_in *in, void *buffer,
                        size_t bytes, size_t *bytes_read);
int audio_extn_cin_configure_input_stream(struct stream_in *in);
#else
#define audio_extn_cin_applicable_stream(in) (false)
#define audio_extn_cin_attached_usecase(uc_id) (false)
#define audio_extn_cin_get_buffer_size(in) (0)
#define audio_extn_cin_start_input_stream(in) (0)
#define audio_extn_cin_stop_input_stream(in) (0)
#define audio_extn_cin_close_input_stream(in) (0)
#define audio_extn_cin_read(in, buffer, bytes, bytes_read) (0)
#define audio_extn_cin_configure_input_stream(in) (0)
#endif

#ifndef SOURCE_TRACKING_ENABLED
static int __unused audio_extn_get_soundfocus_data(
                                   const struct audio_device *adev __unused,
                                   struct sound_focus_param *payload __unused)
{
    return -ENOSYS;
}
static int __unused audio_extn_get_sourcetrack_data(
                                   const struct audio_device *adev __unused,
                                   struct source_tracking_param *payload __unused)
{
    return -ENOSYS;
}
static int __unused audio_extn_set_soundfocus_data(
                                   struct audio_device *adev __unused,
                                   struct sound_focus_param *payload __unused)
{
    return -ENOSYS;
}
#else
int audio_extn_get_soundfocus_data(const struct audio_device *adev,
                                   struct sound_focus_param *payload);
int audio_extn_get_sourcetrack_data(const struct audio_device *adev,
                                    struct source_tracking_param *payload);
int audio_extn_set_soundfocus_data(struct audio_device *adev,
                                   struct sound_focus_param *payload);
#endif

#ifndef FM_POWER_OPT
#define audio_extn_fm_set_parameters(adev, parms) (0)
#define audio_extn_fm_get_parameters(query, reply) (0)
#else
void audio_extn_fm_set_parameters(struct audio_device *adev,
                                   struct str_parms *parms);
void audio_extn_fm_get_parameters(struct str_parms *query, struct str_parms *reply);
#endif

#ifndef APTX_DECODER_ENABLED
#define audio_extn_send_aptx_dec_bt_addr_to_dsp(out) (0)
#define audio_extn_set_aptx_dec_params(payload) (0)
#else
void audio_extn_send_aptx_dec_bt_addr_to_dsp(struct stream_out *out);
int audio_extn_set_aptx_dec_params(struct aptx_dec_param *payload);
#endif
int audio_extn_out_set_param_data(struct stream_out *out,
                             audio_extn_param_id param_id,
                             audio_extn_param_payload *payload);
int audio_extn_out_get_param_data(struct stream_out *out,
                             audio_extn_param_id param_id,
                             audio_extn_param_payload *payload);
int audio_extn_set_device_cfg_params(struct audio_device *adev,
                                     struct audio_device_cfg_param *payload);
int audio_extn_utils_get_avt_device_drift(
                struct audio_usecase *usecase,
                struct audio_avt_device_drift_param *drift_param);
int audio_extn_utils_compress_get_dsp_latency(struct stream_out *out);
int audio_extn_utils_compress_set_render_mode(struct stream_out *out);
int audio_extn_utils_compress_set_clk_rec_mode(struct audio_usecase *usecase);
int audio_extn_utils_compress_set_render_window(
            struct stream_out *out,
            struct audio_out_render_window_param *render_window);
int audio_extn_utils_compress_set_start_delay(
            struct stream_out *out,
            struct audio_out_start_delay_param *start_delay_param);
int audio_extn_utils_compress_enable_drift_correction(
            struct stream_out *out,
            struct audio_out_enable_drift_correction *drift_enable);
int audio_extn_utils_compress_correct_drift(
            struct stream_out *out,
            struct audio_out_correct_drift *drift_correction_param);
int audio_extn_utils_set_channel_map(
            struct stream_out *out,
            struct audio_out_channel_map_param *channel_map_param);
int audio_extn_utils_set_pan_scale_params(
            struct stream_out *out,
            struct mix_matrix_params *mm_params);
int audio_extn_utils_set_downmix_params(
            struct stream_out *out,
            struct mix_matrix_params *mm_params);
#ifdef AUDIO_HW_LOOPBACK_ENABLED
/* API to create audio patch */
int audio_extn_hw_loopback_create_audio_patch(struct audio_hw_device *dev,
                                     unsigned int num_sources,
                                     const struct audio_port_config *sources,
                                     unsigned int num_sinks,
                                     const struct audio_port_config *sinks,
                                     audio_patch_handle_t *handle);
/* API to release audio patch */
int audio_extn_hw_loopback_release_audio_patch(struct audio_hw_device *dev,
                                             audio_patch_handle_t handle);

int audio_extn_hw_loopback_set_audio_port_config(struct audio_hw_device *dev,
                                    const struct audio_port_config *config);
int audio_extn_hw_loopback_get_audio_port(struct audio_hw_device *dev,
                                    struct audio_port *port_in);
int audio_extn_hw_loopback_init(struct audio_device *adev);
void audio_extn_hw_loopback_deinit(struct audio_device *adev);
#else
static int __unused audio_extn_hw_loopback_create_audio_patch(struct audio_hw_device *dev __unused,
                                     unsigned int num_sources __unused,
                                     const struct audio_port_config *sources __unused,
                                     unsigned int num_sinks __unused,
                                     const struct audio_port_config *sinks __unused,
                                     audio_patch_handle_t *handle __unused)
{
    return -ENOSYS;
}
static int __unused audio_extn_hw_loopback_release_audio_patch(struct audio_hw_device *dev __unused,
                                             audio_patch_handle_t handle __unused)
{
    return -ENOSYS;
}
static int __unused audio_extn_hw_loopback_set_audio_port_config(struct audio_hw_device *dev __unused,
                                    const struct audio_port_config *config __unused)
{
    return -ENOSYS;
}
static int __unused audio_extn_hw_loopback_get_audio_port(struct audio_hw_device *dev __unused,
                                    struct audio_port *port_in __unused)
{
    return -ENOSYS;
}
static int __unused audio_extn_hw_loopback_init(struct audio_device *adev __unused)
{
    return -ENOSYS;
}
static void __unused audio_extn_hw_loopback_deinit(struct audio_device *adev __unused)
{
}
#endif

#ifndef FFV_ENABLED
#define audio_extn_ffv_init(adev) (0)
#define audio_extn_ffv_deinit() (0)
#define audio_extn_ffv_check_usecase(in) (0)
#define audio_extn_ffv_set_usecase(in) (0)
#define audio_extn_ffv_stream_init(in) (0)
#define audio_extn_ffv_stream_deinit() (0)
#define audio_extn_ffv_update_enabled() (0)
#define audio_extn_ffv_get_enabled() (0)
#define audio_extn_ffv_read(stream, buffer, bytes) (0)
#define audio_extn_ffv_set_parameters(adev, parms) (0)
#define audio_extn_ffv_get_stream() (0)
#define audio_extn_ffv_update_pcm_config(config) (0)
#define audio_extn_ffv_init_ec_ref_loopback(adev, snd_device) (0)
#define audio_extn_ffv_deinit_ec_ref_loopback(adev, snd_device) (0)
#define audio_extn_ffv_check_and_append_ec_ref_dev(device_name) (0)
#define audio_extn_ffv_get_capture_snd_device() (0)
#define audio_extn_ffv_append_ec_ref_dev_name(device_name) (0)
#else
int32_t audio_extn_ffv_init(struct audio_device *adev);
int32_t audio_extn_ffv_deinit();
bool audio_extn_ffv_check_usecase(struct stream_in *in);
int audio_extn_ffv_set_usecase(struct stream_in *in);
int32_t audio_extn_ffv_stream_init(struct stream_in *in);
int32_t audio_extn_ffv_stream_deinit();
void audio_extn_ffv_update_enabled();
bool audio_extn_ffv_get_enabled();
int32_t audio_extn_ffv_read(struct audio_stream_in *stream,
                       void *buffer, size_t bytes);
void audio_extn_ffv_set_parameters(struct audio_device *adev,
                                   struct str_parms *parms);
struct stream_in *audio_extn_ffv_get_stream();
void audio_extn_ffv_update_pcm_config(struct pcm_config *config);
int audio_extn_ffv_init_ec_ref_loopback(struct audio_device *adev,
                                        snd_device_t snd_device);
int audio_extn_ffv_deinit_ec_ref_loopback(struct audio_device *adev,
                                          snd_device_t snd_device);
void audio_extn_ffv_check_and_append_ec_ref_dev(char *device_name);
snd_device_t audio_extn_ffv_get_capture_snd_device();
void audio_extn_ffv_append_ec_ref_dev_name(char *device_name);
#endif

#ifndef EXT_HW_PLUGIN_ENABLED
#define audio_extn_ext_hw_plugin_init(adev)                (0)
#define audio_extn_ext_hw_plugin_deinit(plugin)              (0)
#define audio_extn_ext_hw_plugin_usecase_start(plugin, usecase) (0)
#define audio_extn_ext_hw_plugin_usecase_stop(plugin, usecase) (0)
#define audio_extn_ext_hw_plugin_set_parameters(plugin, parms) (0)
#define audio_extn_ext_hw_plugin_get_parameters(plugin, query, reply) (0)
#define audio_extn_ext_hw_plugin_set_mic_mute(plugin, mute) (0)
#define audio_extn_ext_hw_plugin_get_mic_mute(plugin, mute) (0)
#define audio_extn_ext_hw_plugin_set_audio_gain(plugin, usecase, gain) (0)
#else
void* audio_extn_ext_hw_plugin_init(struct audio_device *adev);
int audio_extn_ext_hw_plugin_deinit(void *plugin);
int audio_extn_ext_hw_plugin_usecase_start(void *plugin, struct audio_usecase *usecase);
int audio_extn_ext_hw_plugin_usecase_stop(void *plugin, struct audio_usecase *usecase);
int audio_extn_ext_hw_plugin_set_parameters(void *plugin,
                                           struct str_parms *parms);
int audio_extn_ext_hw_plugin_get_parameters(void *plugin,
                  struct str_parms *query, struct str_parms *reply);
int audio_extn_ext_hw_plugin_set_mic_mute(void *plugin, bool mute);
int audio_extn_ext_hw_plugin_get_mic_mute(void *plugin, bool *mute);
int audio_extn_ext_hw_plugin_set_audio_gain(void *plugin,
            struct audio_usecase *usecase, uint32_t gain);
#endif
#ifndef CUSTOM_STEREO_ENABLED
#define audio_extn_send_dual_mono_mixing_coefficients(out) (0)
#else
void audio_extn_send_dual_mono_mixing_coefficients(struct stream_out *out);
#endif
void audio_extn_set_custom_mtmx_params(struct audio_device *adev,
                                        struct audio_usecase *usecase,
                                        bool enable);
#endif /* AUDIO_EXTN_H */
