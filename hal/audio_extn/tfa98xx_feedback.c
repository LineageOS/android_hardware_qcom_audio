/*
 * Copyright (C) 2020 The LineageOS Project
 * Copyright (C) 2020 Pig <pig.priv@gmail.com>
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

#include "tfa98xx_feedback.h"

struct pcm_config pcm_config_tfa = {
    .channels = 2,
    .rate = 48000,
    .period_size = 256,
    .period_count = 4,
    .format = PCM_FORMAT_S24_LE,
    .start_threshold = 0,
    .stop_threshold = INT_MAX,
    .silence_threshold = 0,
};

struct tfa_device {
  struct pcm *tx_pcm;
  int state;
  struct audio_device *adev;
};

static struct tfa_device *tfa = NULL;

bool shouldEnable(snd_device_t snd_device) {
  ALOGI("%s: snd_device is %d", __func__, snd_device);
  switch (snd_device) {
  case SND_DEVICE_OUT_SPEAKER:
  case SND_DEVICE_OUT_SPEAKER_REVERSE:
  case SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES:
  case SND_DEVICE_OUT_SPEAKER_AND_LINE:
  case SND_DEVICE_OUT_VOICE_SPEAKER:
  case SND_DEVICE_OUT_VOICE_SPEAKER_2:
  case SND_DEVICE_OUT_SPEAKER_AND_HDMI:
  case SND_DEVICE_OUT_SPEAKER_AND_USB_HEADSET:
  case SND_DEVICE_OUT_SPEAKER_AND_ANC_HEADSET:
    return true;
  default:
    return false;
  }
}

int audio_extn_tfa98xx_start_feedback(struct audio_device *adev,
                                      snd_device_t snd_device) {
  struct audio_usecase *usecase = NULL;
  int pcm_dev_tx_id;

  tfa = calloc(1, sizeof(struct tfa_device));
  if (!tfa) {
    return -ENOMEM;
  }

  if (shouldEnable(snd_device)) {
    tfa->adev = adev;
    if (!adev) {
      ALOGE("%s: Invalid params", __func__);
      return -EINVAL;
    }
    if (!tfa->tx_pcm) {
      usecase = calloc(1, sizeof(struct audio_usecase));
      if (!usecase) {
        return -ENOMEM;
      }

      snd_device = SND_DEVICE_IN_CAPTURE_VI_FEEDBACK;
      usecase = get_usecase_from_list(adev, USECASE_AUDIO_SPKR_CALIB_TX);
      enable_snd_device(adev, snd_device);
      enable_audio_route(adev, usecase);
      pcm_dev_tx_id =
          platform_get_pcm_device_id(USECASE_AUDIO_SPKR_CALIB_TX, PCM_CAPTURE);
      ALOGE("pcm_dev_tx_id = %d", pcm_dev_tx_id);
      if (pcm_dev_tx_id < 0) {
        ALOGE("%s: Invalid pcm device for usecase (%d)", __func__,
              pcm_dev_tx_id);
        if (!tfa->tx_pcm)
          goto exit;
      } else {
        tfa->tx_pcm =
            pcm_open(adev->snd_card, pcm_dev_tx_id, PCM_IN, &pcm_config_tfa);
        if (tfa->tx_pcm && !pcm_is_ready(tfa->tx_pcm) &&
            pcm_get_error(tfa->tx_pcm)) {
          ALOGE("%s: %s", __func__, pcm_get_error(tfa->tx_pcm));
          if (!tfa->tx_pcm)
            pcm_close(tfa->tx_pcm);
          goto exit;
        } else {
          tfa->tx_pcm = NULL;
        }
        if (pcm_start(tfa->tx_pcm) < 0) {
          ALOGE("%s: pcm start for TX failed", __func__);
          return 0;
        }
      }
    }
  }
exit:
  tfa->tx_pcm = NULL;
  disable_snd_device(adev, snd_device);
  disable_audio_route(adev, usecase);
  free(usecase);
  return 0;
}

int audio_extn_tfa98xx_stop_feedback(struct audio_device *adev,
                                     snd_device_t snd_device) {
  int ret = 0;
  struct audio_usecase *usecase;

  tfa->adev = adev;
  if (shouldEnable(snd_device)) {
    usecase = get_usecase_from_list(adev, USECASE_AUDIO_SPKR_CALIB_TX);
    if (tfa->tx_pcm)
      pcm_close(tfa->tx_pcm);
    snd_device = SND_DEVICE_IN_CAPTURE_VI_FEEDBACK;
    tfa->tx_pcm = NULL;
    ret = disable_snd_device(adev, snd_device);
    if (usecase) {
      disable_audio_route(adev, usecase);
      free(usecase);
    }
  }
  return ret;
}
