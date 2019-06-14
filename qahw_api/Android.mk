ifeq ($(strip $(BOARD_SUPPORTS_QAHW)),true)

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

libqahwapi-inc := $(LOCAL_PATH)/inc

LOCAL_MODULE := libqahw
LOCAL_MODULE_TAGS := optional
LOCAL_C_INCLUDES   := $(libqahwapi-inc)
LOCAL_C_INCLUDES += $(TARGET_OUT_HEADERS)/mm-audio/qahw/inc

LOCAL_SRC_FILES := \
    src/qahw_api.cpp

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_GCOV)),true)
LOCAL_CFLAGS += --coverage -fprofile-arcs -ftest-coverage
LOCAL_CPPFLAGS += --coverage -fprofile-arcs -ftest-coverage
LOCAL_STATIC_LIBRARIES += libprofile_rt
endif

LOCAL_SHARED_LIBRARIES := \
    liblog \
    libcutils \
    libhardware \
    libdl \
    libqahwwrapper

LOCAL_CFLAGS += -Wall -Werror

LOCAL_COPY_HEADERS_TO   := mm-audio/qahw_api/inc
LOCAL_COPY_HEADERS      := inc/qahw_api.h
LOCAL_COPY_HEADERS      += inc/qahw_effect_audiosphere.h
LOCAL_COPY_HEADERS      += inc/qahw_effect_bassboost.h
LOCAL_COPY_HEADERS      += inc/qahw_effect_environmentalreverb.h
LOCAL_COPY_HEADERS      += inc/qahw_effect_equalizer.h
LOCAL_COPY_HEADERS      += inc/qahw_effect_presetreverb.h
LOCAL_COPY_HEADERS      += inc/qahw_effect_virtualizer.h
LOCAL_COPY_HEADERS      += inc/qahw_effect_visualizer.h

LOCAL_PRELINK_MODULE    := false
LOCAL_VENDOR_MODULE     := true

include $(BUILD_SHARED_LIBRARY)

#test app compilation
include $(LOCAL_PATH)/test/Android.mk

endif
