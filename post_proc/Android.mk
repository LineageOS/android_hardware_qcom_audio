
LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

ifneq ($(strip $(AUDIO_FEATURE_ENABLED_PROXY_DEVICE)),false)
    LOCAL_CFLAGS += -DAFE_PROXY_ENABLED
endif

LOCAL_SRC_FILES:= \
	bundle.c \
	equalizer.c \
	bass_boost.c \
	virtualizer.c \
	reverb.c \
	effect_api.c \
	effect_util.c

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_HW_ACCELERATED_EFFECTS)),true)
    LOCAL_CFLAGS += -DHW_ACCELERATED_EFFECTS
    LOCAL_SRC_FILES += hw_accelerator.c
endif

LOCAL_CFLAGS+= -O2 -fvisibility=hidden
LOCAL_CFLAGS += -Wno-sign-compare -Wno-unused-variable -Wno-unused-label -Wno-format

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_DTS_EAGLE)), true)
    LOCAL_CFLAGS += -DDTS_EAGLE
endif

LOCAL_SHARED_LIBRARIES := \
	libcutils \
	liblog \
	libtinyalsa

LOCAL_MODULE_TAGS := optional

LOCAL_MODULE_RELATIVE_PATH := soundfx
LOCAL_MODULE:= libqcompostprocbundle
LOCAL_VENDOR_MODULE := true

LOCAL_C_INCLUDES := \
	external/tinyalsa/include \
	$(call include-path-for, audio-effects)

LOCAL_HEADER_LIBRARIES := generated_kernel_headers

include $(BUILD_SHARED_LIBRARY)


ifeq ($(strip $(AUDIO_FEATURE_ENABLED_HW_ACCELERATED_EFFECTS)),true)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := EffectsHwAcc.cpp

LOCAL_C_INCLUDES := \
    $(call include-path-for, audio-effects)

LOCAL_SHARED_LIBRARIES := \
    liblog \
    libeffects

LOCAL_MODULE_TAGS := optional

LOCAL_CFLAGS += -O2 -fvisibility=hidden
LOCAL_CFLAGS += -Wno-sign-compare -Wno-unused-variable -Wno-unused-label -Wno-format

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_DTS_EAGLE)), true)
LOCAL_CFLAGS += -DHW_ACC_HPX
endif

LOCAL_MODULE:= libhwacceffectswrapper
LOCAL_VENDOR_MODULE := true

include $(BUILD_STATIC_LIBRARY)
endif
