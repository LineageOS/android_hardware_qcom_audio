/*
 * Copyright (c) 2013, The Linux Foundation. All rights reserved.
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

#ifndef AFE_PROXY_ENABLED
#define audio_extn_set_afe_proxy_channel_mixer(adev)     (0)
#define audio_extn_read_afe_proxy_channel_masks(out)     (0)
#else
int32_t audio_extn_set_afe_proxy_channel_mixer(struct audio_device *adev);
int32_t audio_extn_read_afe_proxy_channel_masks(struct stream_out *out);
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
#define audio_extn_ssr_init(adev, in)                 (0)
#define audio_extn_ssr_deinit()                       (0)
#define audio_extn_ssr_update_enabled()               (0)
#define audio_extn_ssr_get_enabled()                  (0)
#define audio_extn_ssr_read(stream, buffer, bytes)    (0)
#else
int32_t audio_extn_ssr_init(struct audio_device *adev,
                            struct stream_in *in);
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
#define audio_extn_compr_cap_init(adev,in)                (0)
#define audio_extn_compr_cap_enabled()                    (0)
#define audio_extn_compr_cap_format_supported(format)     (0)
#define audio_extn_compr_cap_usecase_supported(usecase)   (0)
#define audio_extn_compr_cap_get_buffer_size(format)      (0)
#define audio_extn_compr_cap_read(in, buffer, bytes)      (0)
#define audio_extn_compr_cap_deinit()                     (0)
#else
void audio_extn_compr_cap_init(struct audio_device *adev,
                                    struct stream_in *in);
bool audio_extn_compr_cap_enabled();
bool audio_extn_compr_cap_format_supported(audio_format_t format);
bool audio_extn_compr_cap_usecase_supported(audio_usecase_t usecase);
size_t audio_extn_compr_cap_get_buffer_size(audio_format_t format);
size_t audio_extn_compr_cap_read(struct stream_in *in,
                                        void *buffer, size_t bytes);
void audio_extn_compr_cap_deinit();
#endif

#ifndef DS1_DOLBY_DDP_ENABLED
#define audio_extn_dolby_is_supported_format(format)    (0)
#define audio_extn_dolby_get_snd_codec_id(format)       (0)
#define audio_extn_dolby_set_DMID(adev)                 (0)
#else
bool audio_extn_dolby_is_supported_format(audio_format_t format);
int audio_extn_dolby_get_snd_codec_id(audio_format_t format);
int audio_extn_dolby_set_DMID(struct audio_device *adev);
#endif

#endif /* AUDIO_EXTN_H */
