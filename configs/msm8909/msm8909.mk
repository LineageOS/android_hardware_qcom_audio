#Audio Specific device overlays
DEVICE_PACKAGE_OVERLAYS += hardware/qcom/audio/configs/common/overlay

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
    hardware/qcom/audio/configs/msm8909/sound_trigger_platform_info.xml:system/etc/sound_trigger_platform_info.xml \
    hardware/qcom/audio/configs/msm8909/audio_platform_info.xml:system/etc/audio_platform_info.xml

#XML Audio configuration files
ifeq ($(USE_XML_AUDIO_POLICY_CONF), 1)
ifeq ($(TARGET_USES_AOSP), true)
PRODUCT_COPY_FILES += \
    $(TOPDIR)hardware/qcom/audio/configs/common/audio_policy_configuration.xml:/system/etc/audio_policy_configuration.xml
else
PRODUCT_COPY_FILES += \
    $(TOPDIR)hardware/qcom/audio/configs/msm8909/audio_policy_configuration.xml:system/etc/audio_policy_configuration.xml
endif
endif
