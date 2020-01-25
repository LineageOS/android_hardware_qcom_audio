
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm

AUDIO_PLATFORM := $(TARGET_BOARD_PLATFORM)

LOCAL_CFLAGS += -Wno-macro-redefined
LOCAL_CFLAGS += -DSOUND_TRIGGER_PLATFORM_NAME=$(TARGET_BOARD_PLATFORM)

LOCAL_HEADER_LIBRARIES := libhardware_headers

LOCAL_SRC_FILES := \
    AudioStream.cpp \
    AudioDevice.cpp \
    AudioVoice.cpp \
    audio_extn/soundtrigger.cpp \
    audio_extn/AudioExtn.cpp

LOCAL_SHARED_LIBRARIES := \
    libbase \
    liblog \
    libcutils \
    libdl \
    libaudioutils \
    libexpat \
    libhwbinder \
    libhidlbase \
    libhidltransport \
    libprocessgroup \
    libutils \
    libqal

LOCAL_C_INCLUDES += \
    external/tinyalsa/include \
    system/media/audio_utils/include \
    external/expat/lib \
    vendor/qcom/opensource/core-utils/fwk-detect \
    vendor/qcom/opensource/qal \
    $(call include-path-for, audio-effects) \
    $(LOCAL_PATH)/audio_extn


LOCAL_CFLAGS += -D_GNU_SOURCE
LOCAL_CFLAGS += -Wall -Werror

LOCAL_COPY_HEADERS_TO   := mm-audio
LOCAL_COPY_HEADERS      := \
                           audio_extn/audio_defs.h \
                           audio_extn/AudioExtn.h

LOCAL_MODULE := audio.primary.$(TARGET_BOARD_PLATFORM)

LOCAL_MODULE_RELATIVE_PATH := hw

LOCAL_MODULE_TAGS := optional

LOCAL_MODULE_OWNER := qti

LOCAL_VENDOR_MODULE := true

include $(BUILD_SHARED_LIBRARY)

LOCAL_CFLAGS += -Wno-unused-variable
LOCAL_CFLAGS += -Wno-sign-compare
LOCAL_CFLAGS += -Wno-unused-parameter
LOCAL_CFLAGS += -Wno-unused-label
LOCAL_CFLAGS += -Wno-gnu-designator
LOCAL_CFLAGS += -Wno-typedef-redefinition
LOCAL_CFLAGS += -Wno-shorten-64-to-32
LOCAL_CFLAGS += -Wno-tautological-compare
LOCAL_CFLAGS += -Wno-unused-function
LOCAL_CFLAGS += -Wno-unused-local-typedef

