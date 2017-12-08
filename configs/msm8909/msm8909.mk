#AUDIO_FEATURE_FLAGS
USE_CUSTOM_AUDIO_POLICY := 1
BOARD_USES_ALSA_AUDIO := true
BOARD_SUPPORTS_SOUND_TRIGGER := true

AUDIO_FEATURE_ENABLED_SOURCE_TRACKING:= true
#AUDIO_FEATURE_ENABLED_ANC_HEADSET := true
#AUDIO_FEATURE_ENABLED_COMPRESS_CAPTURE := true
ifneq ($(TARGET_SUPPORTS_WEARABLES),true)
AUDIO_FEATURE_ENABLED_COMPRESS_VOIP := true
endif
ifneq ($(TARGET_SUPPORTS_WEARABLES),true)
AUDIO_FEATURE_ENABLED_EXTN_FORMATS := true
endif
ifneq ($(TARGET_SUPPORTS_WEARABLES),true)
AUDIO_FEATURE_ENABLED_EXTN_FLAC_DECODER := true
endif

ifneq ($(TARGET_SUPPORTS_WEARABLES),true)
AUDIO_FEATURE_ENABLED_EXTN_RESAMPLER := true
endif
AUDIO_FEATURE_ENABLED_FM_POWER_OPT := true
AUDIO_FEATURE_ENABLED_FLUENCE := true
AUDIO_FEATURE_ENABLED_HFP := true
#AUDIO_FEATURE_ENABLED_INCALL_MUSIC := true
AUDIO_FEATURE_ENABLED_MULTI_VOICE_SESSIONS := true
#AUDIO_FEATURE_ENABLED_PCM_OFFLOAD := true
ifneq ($(TARGET_SUPPORTS_WEARABLES),true)
AUDIO_FEATURE_ENABLED_PROXY_DEVICE := true
endif
AUDIO_FEATURE_ENABLED_SSR := true
ifneq ($(TARGET_SUPPORTS_WEARABLES),true)
AUDIO_FEATURE_NON_WEARABLE_TARGET := true
endif
#AUDIO_FEATURE_ENABLED_USBAUDIO := true
AUDIO_FEATURE_ENABLED_VOICE_CONCURRENCY := true
#AUDIO_FEATURE_ENABLED_WFD_CONCURRENCY := true
AUDIO_FEATURE_ENABLED_RECORD_PLAY_CONCURRENCY := true
ifneq ($(TARGET_SUPPORTS_WEARABLES),true)
AUDIO_FEATURE_ENABLED_KPI_OPTIMIZE := true
AUDIO_FEATURE_ENABLED_PM_SUPPORT := true
endif
ifneq ($(TARGET_SUPPORTS_WEARABLES),true)
#BOARD_USES_SRS_TRUEMEDIA := true
endif
AUDIO_FEATURE_ENABLED_DS2_DOLBY_DAP := true
AUDIO_FEATURE_ENABLED_ACDB_LICENSE := true
AUDIO_FEATURE_ENABLED_COMPRESS_INPUT := true
#DOLBY_DAP_HW_QDSP_HAL_API := true
#DOLBY_UDC_MULTICHANNEL_PCM_OFFLOAD := false
MM_AUDIO_ENABLED_FTM := true
MM_AUDIO_ENABLED_SAFX := true
TARGET_USES_QCOM_MM_AUDIO := true

#AUDIO_FEATURE_ENABLED_LISTEN := true

##not supported feature
#AUDIO_FEATURE_ENABLED_CUSTOMSTEREO := true
#AUDIO_FEATURE_ENABLED_HDMI_SPK := true
#AUDIO_FEATURE_ENABLED_HDMI_EDID := true
#AUDIO_FEATURE_ENABLED_LISTEN := true
#AUDIO_FEATURE_ENABLED_SPKR_PROTECTION := true
#AUDIO_FEATURE_FLAGS

#Audio Specific device overlays
DEVICE_PACKAGE_OVERLAYS += hardware/qcom/audio/configs/common/overlay

USE_XML_AUDIO_POLICY_CONF := 1

# Audio configuration file
ifeq ($(TARGET_USES_AOSP), true)
PRODUCT_COPY_FILES += \
    hardware/qcom/audio/configs/common/media/audio_policy.conf:system/etc/audio_policy.conf
else
PRODUCT_COPY_FILES += \
    hardware/qcom/audio/configs/msm8909/audio_policy.conf:system/etc/audio_policy.conf
endif
PRODUCT_COPY_FILES += \
    hardware/qcom/audio/configs/msm8909/audio_policy.conf:system/etc/audio_policy.conf \
    hardware/qcom/audio/configs/msm8909/audio_effects.conf:system/vendor/etc/audio_effects.conf \
    hardware/qcom/audio/configs/msm8909/mixer_paths_qrd_skuh.xml:system/etc/mixer_paths_qrd_skuh.xml \
    hardware/qcom/audio/configs/msm8909/mixer_paths_qrd_skui.xml:system/etc/mixer_paths_qrd_skui.xml \
    hardware/qcom/audio/configs/msm8909/mixer_paths.xml:system/etc/mixer_paths.xml \
    hardware/qcom/audio/configs/msm8909/mixer_paths_msm8909_pm8916.xml:system/etc/mixer_paths_msm8909_pm8916.xml \
    hardware/qcom/audio/configs/msm8909/mixer_paths_wcd9326_i2s.xml:system/etc/mixer_paths_wcd9326_i2s.xml \
    hardware/qcom/audio/configs/msm8909/mixer_paths_skua.xml:system/etc/mixer_paths_skua.xml \
    hardware/qcom/audio/configs/msm8909/mixer_paths_skuc.xml:system/etc/mixer_paths_skuc.xml \
    hardware/qcom/audio/configs/msm8909/mixer_paths_skue.xml:system/etc/mixer_paths_skue.xml \
    hardware/qcom/audio/configs/msm8909/mixer_paths_qrd_skut.xml:system/etc/mixer_paths_qrd_skut.xml \
    hardware/qcom/audio/configs/msm8909/sound_trigger_mixer_paths.xml:system/etc/sound_trigger_mixer_paths.xml \
    hardware/qcom/audio/configs/msm8909/sound_trigger_mixer_paths_wcd9326.xml:system/etc/sound_trigger_mixer_paths_wcd9326.xml \
    hardware/qcom/audio/configs/msm8909/sound_trigger_platform_info.xml:system/etc/sound_trigger_platform_info.xml \
    hardware/qcom/audio/configs/msm8909/audio_platform_info.xml:system/etc/audio_platform_info.xml \
    hardware/qcom/audio/configs/msm8909/audio_io_policy.conf:system/vendor/etc/audio_io_policy.conf

#XML Audio configuration files
ifeq ($(USE_XML_AUDIO_POLICY_CONF), 1)
ifeq ($(TARGET_USES_AOSP), true)
PRODUCT_COPY_FILES += \
    $(TOPDIR)hardware/qcom/audio/configs/common/audio_policy_configuration.xml:/system/etc/audio_policy_configuration.xml
else
PRODUCT_COPY_FILES += \
    $(TOPDIR)hardware/qcom/audio/configs/msm8909/audio_policy_configuration.xml:system/etc/audio_policy_configuration.xml
endif
PRODUCT_COPY_FILES += \
    $(TOPDIR)frameworks/av/services/audiopolicy/config/a2dp_audio_policy_configuration.xml:/system/etc/a2dp_audio_policy_configuration.xml \
    $(TOPDIR)frameworks/av/services/audiopolicy/config/audio_policy_volumes.xml:/system/etc/audio_policy_volumes.xml \
    $(TOPDIR)frameworks/av/services/audiopolicy/config/default_volume_tables.xml:/system/etc/default_volume_tables.xml \
    $(TOPDIR)frameworks/av/services/audiopolicy/config/r_submix_audio_policy_configuration.xml:/system/etc/r_submix_audio_policy_configuration.xml \
    $(TOPDIR)frameworks/av/services/audiopolicy/config/usb_audio_policy_configuration.xml:/system/etc/usb_audio_policy_configuration.xml
endif
