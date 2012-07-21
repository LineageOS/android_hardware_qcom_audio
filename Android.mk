ifneq ($(BOARD_USES_AUDIO_LEGACY),true)
ifeq ($(BOARD_USES_QCOM_HARDWARE),true)

AUDIO_HW_ROOT := $(call my-dir)

ifneq ($(TARGET_BOARD_PLATFORM),msm8960)
ifeq ($(strip $(BOARD_USES_ALSA_AUDIO)),true)
    include $(AUDIO_HW_ROOT)/alsa_sound/Android.mk
    include $(AUDIO_HW_ROOT)/libalsa-intf/Android.mk
endif
endif
ifeq ($(TARGET_BOARD_PLATFORM),msm7x27a)
    include $(AUDIO_HW_ROOT)/msm7627a/Android.mk
endif
ifeq ($(TARGET_BOARD_PLATFORM),msm8660)
    include $(AUDIO_HW_ROOT)/msm8660/Android.mk
endif

endif
endif
