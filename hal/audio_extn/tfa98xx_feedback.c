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
    .channels = 1,
    .rate = 96000,
    .period_size = 1024,
    .period_count = 2,
    .format = PCM_FORMAT_S16_LE,
};

struct tfa_device {
  struct pcm *tx_pcm;
  int state;
  struct audio_device *adev;
};

static struct tfa_device *tfa = NULL;

int audio_extn_tfa98xx_start_feedback(struct audio_device *adev,
                                      snd_device_t snd_device) {
  struct audio_usecase *usecase = NULL;
  int pcm_dev_tx_id;
  tfa = calloc(1, sizeof(struct tfa_device));
  if (!tfa) {
    return -ENOMEM;
  }

  tfa->adev = adev;
  // if (!adev) {
    // ALOGE("%s: Invalid params", __func__);
    // return -EINVAL;
  // }
  if (!tfa->tx_pcm) {
    usecase = calloc(1, sizeof(struct audio_usecase));
    if (!usecase) {
      return -ENOMEM;
    }

    usecase = get_usecase_from_list(adev, USECASE_AUDIO_SPKR_CALIB_TX);
    enable_snd_device(adev, snd_device);
    enable_audio_route(adev, usecase);
    pcm_dev_tx_id =
        platform_get_pcm_device_id(USECASE_AUDIO_SPKR_CALIB_TX, PCM_CAPTURE);
    ALOGE("pcm_dev_tx_id = %d", pcm_dev_tx_id);
    if (pcm_dev_tx_id < 0) {
      ALOGE("%s: Invalid pcm device for usecase (%d)", __func__,
            USECASE_AUDIO_SPKR_CALIB_TX);
      if (!tfa->tx_pcm)
        goto exit;
    } else {
      tfa->tx_pcm =
          pcm_open(adev->snd_card, pcm_dev_tx_id, PCM_IN, &pcm_config_tfa);
      if (tfa->tx_pcm && !pcm_is_ready(tfa->tx_pcm) &&
          pcm_get_error(tfa->tx_pcm)) {
        ALOGE("%s: %s", __func__, __func__);
        if (!tfa->tx_pcm)
          pcm_close(tfa->tx_pcm);
        goto exit;
      } else {
        tfa->tx_pcm = NULL;
      }
      if (pcm_start(tfa->tx_pcm) < 0) {
        return 0;
      }
      ALOGE("%s: pcm start for TX failed", __func__);
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
  if (ret) {
    usecase = get_usecase_from_list(adev, USECASE_AUDIO_SPKR_CALIB_TX);
    if (tfa->tx_pcm)
      pcm_close(tfa->tx_pcm);
    tfa->tx_pcm = NULL;
    ret = disable_snd_device(adev, snd_device);
    if (usecase) {
      disable_audio_route(adev, usecase);
      free(usecase);
    }
  }
  return ret;
}
