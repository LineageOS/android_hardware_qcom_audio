##AUDIO_FEATURE_FLAGS
USE_CUSTOM_AUDIO_POLICY := 1
BOARD_USES_ALSA_AUDIO := true
BOARD_SUPPORTS_SOUND_TRIGGER := true
#
AUDIO_FEATURE_ENABLED_ANC_HEADSET := true
#AUDIO_FEATURE_ENABLED_COMPRESS_CAPTURE := true
AUDIO_FEATURE_ENABLED_COMPRESS_VOIP := true
AUDIO_FEATURE_ENABLED_EXTN_FORMATS := true
AUDIO_FEATURE_ENABLED_EXTN_FLAC_DECODER := true
AUDIO_FEATURE_ENABLED_EXTN_RESAMPLER := true
AUDIO_FEATURE_ENABLED_FM_POWER_OPT := true
AUDIO_FEATURE_ENABLED_FLUENCE := true
AUDIO_FEATURE_ENABLED_HFP := true
##AUDIO_FEATURE_ENABLED_INCALL_MUSIC := true
AUDIO_FEATURE_ENABLED_MULTI_VOICE_SESSIONS := true
#AUDIO_FEATURE_ENABLED_PCM_OFFLOAD := true
AUDIO_FEATURE_ENABLED_PROXY_DEVICE := true
AUDIO_FEATURE_ENABLED_SSR := true
AUDIO_FEATURE_NON_WEARABLE_TARGET := true
AUDIO_FEATURE_ENABLED_VOICE_CONCURRENCY := true
AUDIO_FEATURE_ENABLED_WFD_CONCURRENCY := true
AUDIO_FEATURE_ENABLED_KPI_OPTIMIZE := true
AUDIO_FEATURE_ENABLED_PM_SUPPORT := true
BOARD_USES_SRS_TRUEMEDIA := false
##DOLBY_DAP := true
##DOLBY_DDP := true
##DOLBY_UDC := true
##DOLBY_UDC_MULTICHANNEL := true
##DOLBY_UDC_STREAMING_HLS := true
AUDIO_FEATURE_ENABLED_DS2_DOLBY_DAP := true
AUDIO_FEATURE_ENABLED_ACDB_LICENSE := true
#DOLBY_DAP_HW_QDSP_HAL_API := true
#DOLBY_UDC_MULTICHANNEL_PCM_OFFLOAD := false
MM_AUDIO_ENABLED_FTM := true
MM_AUDIO_ENABLED_SAFX := true
TARGET_USES_QCOM_MM_AUDIO := true
##AUDIO_FEATURE_ENABLED_LISTEN := true
#
###not supported feature
##AUDIO_FEATURE_ENABLED_CUSTOMSTEREO := true
##AUDIO_FEATURE_ENABLED_HDMI_SPK := true
##AUDIO_FEATURE_ENABLED_HDMI_EDID := true
##AUDIO_FEATURE_ENABLED_LISTEN := true
#AUDIO_FEATURE_ENABLED_SPKR_PROTECTION := true
##AUDIO_FEATURE_FLAGS

#Audio Specific device overlays
DEVICE_PACKAGE_OVERLAYS += hardware/qcom/audio/configs/common/overlay


USE_XML_AUDIO_POLICY_CONF := 1

# Audio configuration file
ifeq ($(TARGET_USES_AOSP), true)
PRODUCT_COPY_FILES += \
    hardware/qcom/audio/configs/common/media/audio_policy.conf:system/etc/audio_policy.conf
else
PRODUCT_COPY_FILES += \
    hardware/qcom/audio/configs/msm8916_32/audio_policy.conf:system/etc/audio_policy.conf
endif

PRODUCT_COPY_FILES += \
    hardware/qcom/audio/configs/msm8916_32/audio_policy.conf:system/etc/audio_policy.conf \
    hardware/qcom/audio/configs/msm8916_32/audio_effects.conf:system/vendor/etc/audio_effects.conf \
    hardware/qcom/audio/configs/msm8916_32/mixer_paths_mtp.xml:system/etc/mixer_paths_mtp.xml \
    hardware/qcom/audio/configs/msm8916_32/mixer_paths_sbc.xml:system/etc/mixer_paths_sbc.xml \
    hardware/qcom/audio/configs/msm8916_32/mixer_paths_qrd_skuh.xml:system/etc/mixer_paths_qrd_skuh.xml \
    hardware/qcom/audio/configs/msm8916_32/mixer_paths_qrd_skui.xml:system/etc/mixer_paths_qrd_skui.xml \
    hardware/qcom/audio/configs/msm8916_32/mixer_paths_qrd_skuhf.xml:system/etc/mixer_paths_qrd_skuhf.xml \
    hardware/qcom/audio/configs/msm8916_32/mixer_paths_wcd9306.xml:system/etc/mixer_paths_wcd9306.xml \
    hardware/qcom/audio/configs/msm8916_32/mixer_paths_skuk.xml:system/etc/mixer_paths_skuk.xml \
    hardware/qcom/audio/configs/msm8916_32/mixer_paths_skul.xml:system/etc/mixer_paths_skul.xml \
    hardware/qcom/audio/configs/msm8916_32/mixer_paths.xml:system/etc/mixer_paths.xml \
    hardware/qcom/audio/configs/msm8916_32/sound_trigger_mixer_paths.xml:system/etc/sound_trigger_mixer_paths.xml \
    hardware/qcom/audio/configs/msm8916_32/sound_trigger_mixer_paths_wcd9306.xml:system/etc/sound_trigger_mixer_paths_wcd9306.xml \
    hardware/qcom/audio/configs/msm8916_32/sound_trigger_platform_info.xml:system/etc/sound_trigger_platform_info.xml \
    hardware/qcom/audio/configs/msm8916_64/aanc_tuning_mixer.txt:system/etc/aanc_tuning_mixer.txt

#XML Audio configuration files
ifeq ($(USE_XML_AUDIO_POLICY_CONF), 1)
ifeq ($(TARGET_USES_AOSP), true)
PRODUCT_COPY_FILES += \
    $(TOPDIR)hardware/qcom/audio/configs/common/audio_policy_configuration.xml:/system/etc/audio_policy_configuration.xml
else
PRODUCT_COPY_FILES += \
    $(TOPDIR)hardware/qcom/audio/configs/msm8916_32/audio_policy_configuration.xml:system/etc/audio_policy_configuration.xml
endif
PRODUCT_COPY_FILES += \
    $(TOPDIR)frameworks/av/services/audiopolicy/config/a2dp_audio_policy_configuration.xml:/system/etc/a2dp_audio_policy_configuration.xml \
    $(TOPDIR)frameworks/av/services/audiopolicy/config/audio_policy_volumes.xml:/system/etc/audio_policy_volumes.xml \
    $(TOPDIR)frameworks/av/services/audiopolicy/config/default_volume_tables.xml:/system/etc/default_volume_tables.xml \
    $(TOPDIR)frameworks/av/services/audiopolicy/config/r_submix_audio_policy_configuration.xml:/system/etc/r_submix_audio_policy_configuration.xml \
    $(TOPDIR)frameworks/av/services/audiopolicy/config/usb_audio_policy_configuration.xml:/system/etc/usb_audio_policy_configuration.xml
endif

