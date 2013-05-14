ifeq ($(strip $(BOARD_USES_ALSA_AUDIO)),true)

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm

LOCAL_SRC_FILES := \
	audio_hw.c \
	edid.c

LOCAL_SHARED_LIBRARIES := \
	liblog \
	libcutils \
	libtinyalsa \
	libaudioroute \
	libdl

ifeq ($(TARGET_BOARD_PLATFORM), msm8974)
LOCAL_CFLAGS += -DMSM8974
endif

LOCAL_C_INCLUDES += \
	external/tinyalsa/include \
	$(call include-path-for, audio-route) \
	$(call include-path-for, audio-effects)

LOCAL_MODULE := audio.primary.$(TARGET_BOARD_PLATFORM)

LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw

LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)

endif
