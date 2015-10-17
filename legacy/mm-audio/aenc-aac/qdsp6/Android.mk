ifneq ($(BUILD_TINY_ANDROID),true)

LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

# ---------------------------------------------------------------------------------
#                 Common definitons
# ---------------------------------------------------------------------------------

libOmxAacEnc-def := -g -O3
libOmxAacEnc-def += -DQC_MODIFIED
libOmxAacEnc-def += -D_ANDROID_
libOmxAacEnc-def += -D_ENABLE_QC_MSG_LOG_
libOmxAacEnc-def += -DVERBOSE
ifeq ($(strip $(TARGET_USES_QCOM_MM_AUDIO)),true)
libOmxAacEnc-def += -DAUDIOV2
endif

# ---------------------------------------------------------------------------------
#             Make the Shared library (libOmxAacEnc)
# ---------------------------------------------------------------------------------

include $(CLEAR_VARS)

libOmxAacEnc-inc       := $(LOCAL_PATH)/inc
libOmxAacEnc-inc       += $(TARGET_OUT_HEADERS)/mm-core/omxcore

LOCAL_MODULE            := libOmxAacEnc
LOCAL_MODULE_TAGS       := optional
LOCAL_CFLAGS            := $(libOmxAacEnc-def)
LOCAL_C_INCLUDES        := $(libOmxAacEnc-inc)
LOCAL_SHARED_LIBRARIES  := libutils liblog

LOCAL_SRC_FILES         := src/aenc_svr.c
LOCAL_SRC_FILES         += src/omx_aac_aenc.cpp

LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
LOCAL_ADDITIONAL_DEPENDENCIES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr

include $(BUILD_SHARED_LIBRARY)

# ---------------------------------------------------------------------------------
#             Make the apps-test (mm-aenc-omxaac-test)
# ---------------------------------------------------------------------------------

include $(CLEAR_VARS)

mm-aac-enc-test-inc    := $(LOCAL_PATH)/inc
mm-aac-enc-test-inc    += $(LOCAL_PATH)/test
ifeq ($(strip $(TARGET_USES_QCOM_MM_AUDIO)),true)
mm-aac-enc-test-inc    += $(TARGET_OUT_HEADERS)/mm-audio/audio-alsa 
endif
mm-aac-enc-test-inc    += $(TARGET_OUT_HEADERS)/mm-core/omxcore

LOCAL_MODULE            := mm-aenc-omxaac-test
LOCAL_MODULE_TAGS       := optional
LOCAL_CFLAGS            := $(libOmxAacEnc-def)
LOCAL_C_INCLUDES        := $(mm-aac-enc-test-inc)
LOCAL_SHARED_LIBRARIES  := libmm-omxcore
LOCAL_SHARED_LIBRARIES  += libOmxAacEnc
ifeq ($(strip $(TARGET_USES_QCOM_MM_AUDIO)),true)
LOCAL_SHARED_LIBRARIES  += libaudioalsa
endif
LOCAL_SRC_FILES         := test/omx_aac_enc_test.c

include $(BUILD_EXECUTABLE)

endif

# ---------------------------------------------------------------------------------
#                     END
# ---------------------------------------------------------------------------------

