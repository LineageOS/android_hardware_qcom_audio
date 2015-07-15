/*
 * Copyright (c) 2013-2014, The Linux Foundation. All rights reserved.
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
 */

#ifndef AUDIO_EXTN_H
#define AUDIO_EXTN_H

#include <cutils/str_parms.h>

#ifndef PCM_OFFLOAD_ENABLED
#define AUDIO_FORMAT_PCM_OFFLOAD 0x17000000UL
#define AUDIO_FORMAT_PCM_16_BIT_OFFLOAD (AUDIO_FORMAT_PCM_OFFLOAD | AUDIO_FORMAT_PCM_SUB_16_BIT)
#define AUDIO_FORMAT_PCM_24_BIT_OFFLOAD (AUDIO_FORMAT_PCM_OFFLOAD | AUDIO_FORMAT_PCM_SUB_8_24_BIT)
#define AUDIO_OFFLOAD_CODEC_FORMAT  "music_offload_codec_format"
#define audio_is_offload_pcm(format) (0)
#endif

#ifndef AFE_PROXY_ENABLED
#define AUDIO_DEVICE_OUT_PROXY 0x40000
#endif

#ifndef INCALL_MUSIC_ENABLED
#define AUDIO_OUTPUT_FLAG_INCALL_MUSIC 0x8000
#endif

#ifndef AUDIO_DEVICE_OUT_FM_TX
#define AUDIO_DEVICE_OUT_FM_TX 0x8000000
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
#else
bool audio_extn_get_anc_enabled(void);
bool audio_extn_should_use_fb_anc(void);
bool audio_extn_should_use_handset_anc(int in_channels);
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
#define audio_extn_usb_init(adev)                        (0)
#define audio_extn_usb_deinit()                          (0)
#define audio_extn_usb_start_playback(adev)              (0)
#define audio_extn_usb_stop_playback()                   (0)
#define audio_extn_usb_start_capture(adev)               (0)
#define audio_extn_usb_stop_capture()                    (0)
#define audio_extn_usb_set_proxy_sound_card(sndcard_idx) (0)
#define audio_extn_usb_is_proxy_inuse()                  (0)
#else
void initPlaybackVolume();
void audio_extn_usb_init(void *adev);
void audio_extn_usb_deinit();
void audio_extn_usb_start_playback(void *adev);
void audio_extn_usb_stop_playback();
void audio_extn_usb_start_capture(void *adev);
void audio_extn_usb_stop_capture();
void audio_extn_usb_set_proxy_sound_card(uint32_t sndcard_idx);
bool audio_extn_usb_is_proxy_inuse();
#endif

#ifndef SSR_ENABLED
#define audio_extn_ssr_init(in)                       (0)
#define audio_extn_ssr_deinit()                       (0)
#define audio_extn_ssr_update_enabled()               (0)
#define audio_extn_ssr_get_enabled()                  (0)
#define audio_extn_ssr_read(stream, buffer, bytes)    (0)
#else
int32_t audio_extn_ssr_init(struct stream_in *in);
int32_t audio_extn_ssr_deinit();
void audio_extn_ssr_update_enabled();
bool audio_extn_ssr_get_enabled();
int32_t audio_extn_ssr_read(struct audio_stream_in *stream,
                       void *buffer, size_t bytes);
#endif

#ifndef HW_VARIANTS_ENABLED
#define hw_info_init(snd_card_name)                  (0)
#define hw_info_deinit(hw_info)                      (0)
#define hw_info_append_hw_type(hw_info,\
        snd_device, device_name)                     (0)
#else
void *hw_info_init(const char *snd_card_name);
void hw_info_deinit(void *hw_info);
void hw_info_append_hw_type(void *hw_info, snd_device_t snd_device,
                             char *device_name);
#endif

#ifndef AUDIO_LISTEN_ENABLED
#define audio_extn_listen_init(adev, snd_card)                  (0)
#define audio_extn_listen_deinit(adev)                          (0)
#define audio_extn_listen_update_status(uc_info, event)         (0)
#define audio_extn_listen_set_parameters(adev, parms)           (0)
#else
enum listen_event_type {
    LISTEN_EVENT_SND_DEVICE_FREE,
    LISTEN_EVENT_SND_DEVICE_BUSY
};
typedef enum listen_event_type listen_event_type_t;

int audio_extn_listen_init(struct audio_device *adev, unsigned int snd_card);
void audio_extn_listen_deinit(struct audio_device *adev);
void audio_extn_listen_update_status(snd_device_t snd_device,
                                     listen_event_type_t event);
void audio_extn_listen_set_parameters(struct audio_device *adev,
                                      struct str_parms *parms);
#endif /* AUDIO_LISTEN_ENABLED */

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
#define audio_extn_spkr_prot_start_processing(snd_device)    (-EINVAL)
#define audio_extn_spkr_prot_stop_processing()     (0)
#define audio_extn_spkr_prot_is_enabled() (false)
#else
void audio_extn_spkr_prot_init(void *adev);
int audio_extn_spkr_prot_start_processing(snd_device_t snd_device);
void audio_extn_spkr_prot_stop_processing();
bool audio_extn_spkr_prot_is_enabled();
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

#if defined(DS1_DOLBY_DDP_ENABLED) || defined(DS1_DOLBY_DAP_ENABLED)
void audio_extn_dolby_set_dmid(struct audio_device *adev);
#else
#define audio_extn_dolby_set_dmid(adev)                 (0)
#endif

#ifndef DS1_DOLBY_DDP_ENABLED
#define audio_extn_dolby_set_endpoint(adev)                 (0)
#else
void audio_extn_dolby_set_endpoint(struct audio_device *adev);
#endif

#ifndef DS1_DOLBY_DDP_ENABLED
#define audio_extn_ddp_set_parameters(adev, parms)      (0)
#define audio_extn_is_dolby_format(format)              (0)
#define audio_extn_dolby_get_snd_codec_id(adev, out, format)       (0)
#define audio_extn_dolby_send_ddp_endp_params(adev)     (0)
#else
bool audio_extn_is_dolby_format(audio_format_t format);
int audio_extn_dolby_get_snd_codec_id(struct audio_device *adev,
                                      struct stream_out *out,
                                      audio_format_t format);
void audio_extn_ddp_set_parameters(struct audio_device *adev,
                                   struct str_parms *parms);
void audio_extn_dolby_send_ddp_endp_params(struct audio_device *adev);
#endif

#ifndef HFP_ENABLED
#define audio_extn_hfp_is_active(adev)                  (0)
#define audio_extn_hfp_get_usecase()                    (-1)
#else
bool audio_extn_hfp_is_active(struct audio_device *adev);
audio_usecase_t audio_extn_hfp_get_usecase();
#endif

#ifndef HFP_ENABLED
#define audio_extn_hfp_is_active(adev)                  (0)
#else
bool audio_extn_hfp_is_active(struct audio_device *adev);
#endif

#endif /* AUDIO_EXTN_H */
