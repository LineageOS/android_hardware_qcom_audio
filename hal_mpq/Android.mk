ifeq ($(strip $(BOARD_USES_ALSA_AUDIO)),true)

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm

AUDIO_PLATFORM := $(TARGET_BOARD_PLATFORM)

LOCAL_SRC_FILES := \
	audio_hw.c \
	audio_stream_out.c \
	audio_bitstream_sm.c \
	$(AUDIO_PLATFORM)/hw_info.c \
	$(AUDIO_PLATFORM)/platform.c

ifneq ($(strip $(AUDIO_FEATURE_DISABLED_ANC_HEADSET)),true)
    LOCAL_CFLAGS += -DANC_HEADSET_ENABLED
endif

ifneq ($(strip $(AUDIO_FEATURE_DISABLED_PROXY_DEVICE)),true)
    LOCAL_CFLAGS += -DAFE_PROXY_ENABLED
endif


ifdef MULTIPLE_HW_VARIANTS_ENABLED
  LOCAL_CFLAGS += -DHW_VARIANTS_ENABLED
  LOCAL_SRC_FILES += $(AUDIO_PLATFORM)/hw_info.c
endif

LOCAL_SHARED_LIBRARIES := \
	liblog \
	libcutils \
	libtinyalsa \
	libtinycompress \
	libaudioroute \
	libdl

LOCAL_C_INCLUDES := \
	external/tinyalsa/include \
	external/tinycompress/include \
	$(call include-path-for, audio-route) \
	$(call include-path-for, audio-effects) \
	$(LOCAL_PATH)/$(AUDIO_PLATFORM)


LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
LOCAL_ADDITIONAL_DEPENDENCIES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr

LOCAL_MODULE := audio.primary.$(TARGET_BOARD_PLATFORM)

LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw

LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)

endif
