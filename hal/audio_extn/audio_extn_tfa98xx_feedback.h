#ifndef AUDIO_EXT_TFA98XX_FEEDBACK_H
#define AUDIO_EXT_TFA98XX_FEEDBACK_H

int audio_extn_tfa98xx_start_feedback(struct audio_device *adev,
                                      snd_device_t snd_device);

int audio_extn_tfa98xx_stop_feedback(struct audio_device *adev,
                                     snd_device_t snd_device);
#endif
