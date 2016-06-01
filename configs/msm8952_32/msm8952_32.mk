#Audio Specific device overlays
DEVICE_PACKAGE_OVERLAYS += hardware/qcom/audio/configs/common/overlay
#enable software decoders for ALAC and APE
PRODUCT_PROPERTY_OVERRIDES += \
use.qti.sw.alac.decoder=true
PRODUCT_PROPERTY_OVERRIDES += \
use.qti.sw.ape.decoder=true

USE_XML_AUDIO_POLICY_CONF := 1

# Audio configuration file
ifeq ($(TARGET_USES_AOSP), true)
PRODUCT_COPY_FILES += \
    device/qcom/common/media/audio_policy.conf:system/etc/audio_policy.conf
else
PRODUCT_COPY_FILES += \
    hardware/qcom/audio/configs/msm8952_32/audio_policy.conf:system/etc/audio_policy.conf
endif

PRODUCT_COPY_FILES += \
    hardware/qcom/audio/configs/msm8952_32/audio_output_policy.conf:system/vendor/etc/audio_output_policy.conf \
    hardware/qcom/audio/configs/msm8952_32/audio_effects.conf:system/vendor/etc/audio_effects.conf \
    hardware/qcom/audio/configs/msm8952_32/mixer_paths_mtp.xml:system/etc/mixer_paths_mtp.xml \
    hardware/qcom/audio/configs/msm8952_32/mixer_paths_qrd_skuh.xml:system/etc/mixer_paths_qrd_skuh.xml \
    hardware/qcom/audio/configs/msm8952_32/mixer_paths_qrd_skui.xml:system/etc/mixer_paths_qrd_skui.xml \
    hardware/qcom/audio/configs/msm8952_32/mixer_paths_qrd_skuhf.xml:system/etc/mixer_paths_qrd_skuhf.xml \
    hardware/qcom/audio/configs/msm8952_32/mixer_paths_wcd9306.xml:system/etc/mixer_paths_wcd9306.xml \
    hardware/qcom/audio/configs/msm8952_32/mixer_paths_skuk.xml:system/etc/mixer_paths_skuk.xml \
    hardware/qcom/audio/configs/msm8952_32/mixer_paths_qrd_skum.xml:system/etc/mixer_paths_qrd_skum.xml \
    hardware/qcom/audio/configs/msm8952_32/mixer_paths_qrd_skun_cajon.xml:system/etc/mixer_paths_qrd_skun_cajon.xml \
    hardware/qcom/audio/configs/msm8952_32/mixer_paths_msm8952_polaris.xml:system/etc/mixer_paths_msm8952_polaris.xml \
    hardware/qcom/audio/configs/msm8952_32/mixer_paths.xml:system/etc/mixer_paths.xml \
    hardware/qcom/audio/configs/msm8952_32/sound_trigger_mixer_paths.xml:system/etc/sound_trigger_mixer_paths.xml \
    hardware/qcom/audio/configs/msm8952_32/sound_trigger_mixer_paths_wcd9306.xml:system/etc/sound_trigger_mixer_paths_wcd9306.xml \
    hardware/qcom/audio/configs/msm8952_32/sound_trigger_mixer_paths_wcd9330.xml:system/etc/sound_trigger_mixer_paths_wcd9330.xml \
    hardware/qcom/audio/configs/msm8952_32/sound_trigger_platform_info.xml:system/etc/sound_trigger_platform_info.xml \
    hardware/qcom/audio/configs/msm8952_32/mixer_paths_wcd9330.xml:system/etc/mixer_paths_wcd9330.xml \
    hardware/qcom/audio/configs/msm8952_32/mixer_paths_wcd9335.xml:system/etc/mixer_paths_wcd9335.xml \
    hardware/qcom/audio/configs/msm8952_32/mixer_paths_wcd9326.xml:system/etc/mixer_paths_wcd9326.xml \
    hardware/qcom/audio/configs/msm8952_32/mixer_paths_qrd_skun.xml:system/etc/mixer_paths_qrd_skun.xml \
    hardware/qcom/audio/configs/msm8952_32/audio_platform_info.xml:system/etc/audio_platform_info.xml \
    hardware/qcom/audio/configs/msm8952_32/audio_platform_info_extcodec.xml:system/etc/audio_platform_info_extcodec.xml

#XML Audio configuration files
ifeq ($(USE_XML_AUDIO_POLICY_CONF), 1)
ifeq ($(TARGET_USES_AOSP), true)
PRODUCT_COPY_FILES += \
    $(TOPDIR)hardware/qcom/audio/configs/common/audio_policy_configuration.xml:/system/etc/audio_policy_configuration.xml
else
PRODUCT_COPY_FILES += \
    $(TOPDIR)hardware/qcom/audio/configs/msm8952_32/audio_policy_configuration.xml:system/etc/audio_policy_configuration.xml
endif
endif

PRODUCT_PACKAGES += \
    libqcomvisualizer \
    libqcompostprocbundle \
    libqcomvoiceprocessing

# Reduce client buffer size for fast audio output tracks
PRODUCT_PROPERTY_OVERRIDES += \
     af.fast_track_multiplier=1

# Low latency audio buffer size in frames
PRODUCT_PROPERTY_OVERRIDES += \
    audio_hal.period_size=192
