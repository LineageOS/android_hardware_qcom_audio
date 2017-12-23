LOCAL_PATH := $(call my-dir)

# audio_hal_multi_record_test
# ==============================================================================
include $(CLEAR_VARS)
LOCAL_SRC_FILES := audio_hal_multi_record_test.cpp
LOCAL_MODULE := hal_multi_rec
ifdef BRILLO
LOCAL_MODULE_TAGS := eng
endif
LOCAL_CFLAGS += -Wall -Werror -Wno-sign-compare
LOCAL_SHARED_LIBRARIES := \
  libhardware \
  libaudioutils \
  libutils

LOCAL_C_INCLUDES := \
  $(TOP)/system/media/audio_utils/include \
  $(TARGET_OUT_HEADERS)/mm-audio
include $(BUILD_EXECUTABLE)

