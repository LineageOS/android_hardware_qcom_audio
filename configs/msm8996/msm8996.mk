#BOARD_USES_GENERIC_AUDIO := true
#
#AUDIO_FEATURE_FLAGS
BOARD_USES_ALSA_AUDIO := true
USE_CUSTOM_AUDIO_POLICY := 1
BOARD_SUPPORTS_SOUND_TRIGGER := true
AUDIO_USE_LL_AS_PRIMARY_OUTPUT := true

AUDIO_FEATURE_ENABLED_VBAT_MONITOR := true
AUDIO_FEATURE_ENABLED_ANC_HEADSET := true
AUDIO_FEATURE_ENABLED_COMPRESS_CAPTURE := false
AUDIO_FEATURE_ENABLED_COMPRESS_VOIP := true
AUDIO_FEATURE_ENABLED_CUSTOMSTEREO := true
AUDIO_FEATURE_ENABLED_EXTN_FORMATS := true
AUDIO_FEATURE_ENABLED_EXTN_FLAC_DECODER := true
AUDIO_FEATURE_ENABLED_EXTN_RESAMPLER := true
AUDIO_FEATURE_ENABLED_FM_POWER_OPT := true
AUDIO_FEATURE_ENABLED_FLUENCE := true
AUDIO_FEATURE_ENABLED_HDMI_SPK := true
AUDIO_FEATURE_ENABLED_HDMI_EDID := true
AUDIO_FEATURE_ENABLED_HFP := true
AUDIO_FEATURE_ENABLED_INCALL_MUSIC := false
AUDIO_FEATURE_ENABLED_MULTI_VOICE_SESSIONS := true
AUDIO_FEATURE_ENABLED_PCM_OFFLOAD := true
AUDIO_FEATURE_ENABLED_PCM_OFFLOAD_24 := true
AUDIO_FEATURE_ENABLED_FLAC_OFFLOAD := true
AUDIO_FEATURE_ENABLED_VORBIS_OFFLOAD := true
AUDIO_FEATURE_ENABLED_WMA_OFFLOAD := true
AUDIO_FEATURE_ENABLED_ALAC_OFFLOAD := true
AUDIO_FEATURE_ENABLED_APE_OFFLOAD := true
AUDIO_FEATURE_ENABLED_AAC_ADTS_OFFLOAD := true
AUDIO_FEATURE_ENABLED_PROXY_DEVICE := true
AUDIO_FEATURE_ENABLED_KPI_OPTIMIZE := true
AUDIO_FEATURE_ENABLED_SPKR_PROTECTION := true
AUDIO_FEATURE_ENABLED_SSR := true
AUDIO_FEATURE_ENABLED_ACDB_LICENSE := true

AUDIO_FEATURE_ENABLED_DTS_EAGLE := false
BOARD_USES_SRS_TRUEMEDIA := true
DTS_CODEC_M_ := true
AUDIO_FEATURE_ENABLED_DEV_ARBI := true
MM_AUDIO_ENABLED_FTM := true
MM_AUDIO_ENABLED_SAFX := true
TARGET_USES_QCOM_MM_AUDIO := true
AUDIO_FEATURE_ENABLED_HW_ACCELERATED_EFFECTS := false

AUDIO_FEATURE_ENABLED_DS2_DOLBY_DAP := true

AUDIO_FEATURE_ENABLED_SOURCE_TRACKING := true
AUDIO_FEATURE_ENABLED_AUDIOSPHERE := true
##AUDIO_FEATURE_FLAGS
#

# Audio configuration file
ifeq ($(TARGET_USES_AOSP), true)
PRODUCT_COPY_FILES += \
    device/qcom/common/media/audio_policy.conf:system/etc/audio_policy.conf
else
PRODUCT_COPY_FILES += \
    hardware/qcom/audio/configs/msm8996/audio_policy.conf:system/etc/audio_policy.conf
endif

PRODUCT_COPY_FILES += \
    hardware/qcom/audio/configs/msm8996/audio_output_policy.conf:system/vendor/etc/audio_output_policy.conf \
    hardware/qcom/audio/configs/msm8996/audio_effects.conf:system/vendor/etc/audio_effects.conf \
    hardware/qcom/audio/configs/msm8996/mixer_paths.xml:system/etc/mixer_paths.xml \
    hardware/qcom/audio/configs/msm8996/mixer_paths_tasha.xml:system/etc/mixer_paths_tasha.xml \
    hardware/qcom/audio/configs/msm8996/mixer_paths_dtp.xml:system/etc/mixer_paths_dtp.xml \
    hardware/qcom/audio/configs/msm8996/mixer_paths_i2s.xml:system/etc/mixer_paths_i2s.xml \
    hardware/qcom/audio/configs/msm8996/aanc_tuning_mixer.txt:system/etc/aanc_tuning_mixer.txt \
    hardware/qcom/audio/configs/msm8996/audio_platform_info_i2s.xml:system/etc/audio_platform_info_i2s.xml \
    hardware/qcom/audio/configs/msm8996/sound_trigger_mixer_paths.xml:system/etc/sound_trigger_mixer_paths.xml \
    hardware/qcom/audio/configs/msm8996/sound_trigger_mixer_paths_wcd9330.xml:system/etc/sound_trigger_mixer_paths_wcd9330.xml \
    hardware/qcom/audio/configs/msm8996/sound_trigger_platform_info.xml:system/etc/sound_trigger_platform_info.xml \
    hardware/qcom/audio/configs/msm8996/audio_platform_info.xml:system/etc/audio_platform_info.xml

# Listen configuration file
PRODUCT_COPY_FILES += \
    hardware/qcom/audio/configs/msm8996/listen_platform_info.xml:system/etc/listen_platform_info.xml

# Reduce client buffer size for fast audio output tracks
PRODUCT_PROPERTY_OVERRIDES += \
    af.fast_track_multiplier=1

# Low latency audio buffer size in frames
PRODUCT_PROPERTY_OVERRIDES += \
    audio_hal.period_size=192

