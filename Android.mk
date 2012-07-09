AUDIO_HW_ROOT := $(call my-dir)

ifeq ($(strip $(BOARD_USES_ALSA_AUDIO)),true)
    include $(AUDIO_HW_ROOT)/alsa_sound/Android.mk
endif

ifeq ($(strip $(BOARD_USES_ALSA_AUDIO)),true)
    include $(AUDIO_HW_ROOT)/libalsa-intf/Android.mk
endif
ifeq ($(call is-board-platform,msm7627a),true)
    include $(AUDIO_HW_ROOT)/msm7627a/Android.mk
endif
ifeq ($(call is-board-platform,msm8660),true)
    include $(AUDIO_HW_ROOT)/msm8660/Android.mk
endif
