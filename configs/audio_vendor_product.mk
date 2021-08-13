#MM_AUDIO product packages
MM_AUDIO := libcapiv2uvvendor
MM_AUDIO += liblsmclient


PRODUCT_PACKAGES += $(MM_AUDIO)

#----------------------------------------------------------------------
# audio specific
#----------------------------------------------------------------------
TARGET_USES_AOSP := false
TARGET_USES_AOSP_FOR_AUDIO := false
ifeq ($(TARGET_USES_QMAA_OVERRIDE_AUDIO), false)
ifeq ($(TARGET_USES_QMAA),true)
AUDIO_USE_STUB_HAL := true
TARGET_USES_AOSP_FOR_AUDIO := true
-include $(TOPDIR)vendor/qcom/opensource/audio-hal/primary-hal/configs/common/default.mk
else
# Audio hal configuration file
-include $(TOPDIR)vendor/qcom/opensource/audio-hal/primary-hal/configs/$(TARGET_BOARD_PLATFORM)/$(TARGET_BOARD_PLATFORM).mk
endif
else
# Audio hal configuration file
-include $(TOPDIR)vendor/qcom/opensource/audio-hal/primary-hal/configs/$(TARGET_BOARD_PLATFORM)/$(TARGET_BOARD_PLATFORM).mk
endif

ifeq ($(AUDIO_USE_STUB_HAL), true)
PRODUCT_COPY_FILES += \
    frameworks/av/services/audiopolicy/config/audio_policy_configuration_generic.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio/audio_policy_configuration.xml \
    frameworks/av/services/audiopolicy/config/primary_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio/primary_audio_policy_configuration.xml \
    frameworks/av/services/audiopolicy/config/r_submix_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio/r_submix_audio_policy_configuration.xml \
    frameworks/av/services/audiopolicy/config/audio_policy_volumes.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio/audio_policy_volumes.xml \
    frameworks/av/services/audiopolicy/config/default_volume_tables.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio/default_volume_tables.xml \
    frameworks/av/services/audiopolicy/config/surround_sound_configuration_5_0.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio/surround_sound_configuration_5_0.xml
endif

# Pro Audio feature
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.audio.pro.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.audio.pro.xml
