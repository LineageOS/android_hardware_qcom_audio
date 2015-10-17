ifneq ($(BUILD_TINY_ANDROID),true)

LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

# ---------------------------------------------------------------------------------
#                 Common definitons
# ---------------------------------------------------------------------------------

libOmxQcelp13Enc-def := -g -O3
libOmxQcelp13Enc-def += -DQC_MODIFIED
libOmxQcelp13Enc-def += -D_ANDROID_
libOmxQcelp13Enc-def += -D_ENABLE_QC_MSG_LOG_
libOmxQcelp13Enc-def += -DVERBOSE
libOmxQcelp13Enc-def += -D_DEBUG
ifeq ($(strip $(TARGET_USES_QCOM_MM_AUDIO)),true)
libOmxQcelp13Enc-def += -DAUDIOV2
endif

# ---------------------------------------------------------------------------------
#             Make the Shared library (libOmxQcelp13Enc)
# ---------------------------------------------------------------------------------

include $(CLEAR_VARS)

libOmxQcelp13Enc-inc       := $(LOCAL_PATH)/inc
libOmxQcelp13Enc-inc       += $(TARGET_OUT_HEADERS)/mm-core/omxcore

LOCAL_MODULE            := libOmxQcelp13Enc
LOCAL_MODULE_TAGS       := optional
LOCAL_CFLAGS            := $(libOmxQcelp13Enc-def)
LOCAL_C_INCLUDES        := $(libOmxQcelp13Enc-inc)
LOCAL_SHARED_LIBRARIES  := libutils liblog

LOCAL_SRC_FILES         := src/aenc_svr.c
LOCAL_SRC_FILES         += src/omx_qcelp13_aenc.cpp

LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
LOCAL_ADDITIONAL_DEPENDENCIES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr


include $(BUILD_SHARED_LIBRARY)


# ---------------------------------------------------------------------------------
#             Make the apps-test (mm-aenc-omxqcelp13-test)
# ---------------------------------------------------------------------------------

include $(CLEAR_VARS)

mm-qcelp13-enc-test-inc    := $(LOCAL_PATH)/inc
mm-qcelp13-enc-test-inc    += $(LOCAL_PATH)/test

mm-qcelp13-enc-test-inc    += $(TARGET_OUT_HEADERS)/mm-core/omxcore
ifeq ($(strip $(TARGET_USES_QCOM_MM_AUDIO)),true)
mm-qcelp13-enc-test-inc    += $(TARGET_OUT_HEADERS)/mm-audio/audio-alsa
endif
LOCAL_MODULE            := mm-aenc-omxqcelp13-test
LOCAL_MODULE_TAGS       := optional
LOCAL_CFLAGS            := $(libOmxQcelp13Enc-def)
LOCAL_C_INCLUDES        := $(mm-qcelp13-enc-test-inc)
LOCAL_SHARED_LIBRARIES  := libmm-omxcore
LOCAL_SHARED_LIBRARIES  += libOmxQcelp13Enc
ifeq ($(strip $(TARGET_USES_QCOM_MM_AUDIO)),true)
LOCAL_SHARED_LIBRARIES  += libaudioalsa
endif
LOCAL_SRC_FILES         := test/omx_qcelp13_enc_test.c

include $(BUILD_EXECUTABLE)

endif

# ---------------------------------------------------------------------------------
#                     END
# ---------------------------------------------------------------------------------

