ifneq ($(BUILD_TINY_ANDROID),true)

LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

# ---------------------------------------------------------------------------------
#                 Common definitons
# ---------------------------------------------------------------------------------

libOmxG711Enc-def := -g -O3
libOmxG711Enc-def += -DQC_MODIFIED
libOmxG711Enc-def += -D_ANDROID_
libOmxG711Enc-def += -D_ENABLE_QC_MSG_LOG_
libOmxG711Enc-def += -DVERBOSE
libOmxG711Enc-def += -D_DEBUG
libOmxG711Enc-def += -Wconversion
libOmxG711Enc-def += -DAUDIOV2

# ---------------------------------------------------------------------------------
#             Make the Shared library (libOmxG711Enc)
# ---------------------------------------------------------------------------------

include $(CLEAR_VARS)

libOmxG711Enc-inc       := $(LOCAL_PATH)/inc
libOmxG711Enc-inc       += $(TARGET_OUT_HEADERS)/mm-core/omxcore

LOCAL_MODULE            := libOmxG711Enc
LOCAL_MODULE_TAGS       := optional
LOCAL_CFLAGS            := $(libOmxG711Enc-def)
LOCAL_C_INCLUDES        := $(libOmxG711Enc-inc)
LOCAL_PRELINK_MODULE    := false
LOCAL_SHARED_LIBRARIES  := libutils liblog libcutils

LOCAL_SRC_FILES         := src/aenc_svr.c
LOCAL_SRC_FILES         += src/omx_g711_aenc.cpp
LOCAL_SRC_FILES         += src/omx_log.cpp

LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
LOCAL_ADDITIONAL_DEPENDENCIES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr


include $(BUILD_SHARED_LIBRARY)


# ---------------------------------------------------------------------------------
#             Make the apps-test (mm-aenc-omxg711-test)
# ---------------------------------------------------------------------------------

include $(CLEAR_VARS)

mm-g711-enc-test-inc   := $(LOCAL_PATH)/inc
mm-g711-enc-test-inc   += $(LOCAL_PATH)/test
mm-g711-enc-test-inc   += $(TARGET_OUT_HEADERS)/mm-core/omxcore

LOCAL_MODULE            := mm-aenc-omxg711-test
LOCAL_MODULE_TAGS       := optional
LOCAL_CFLAGS            := $(libOmxG711Enc-def)
LOCAL_C_INCLUDES        := $(mm-g711-enc-test-inc)
LOCAL_PRELINK_MODULE    := false
LOCAL_SHARED_LIBRARIES  := libmm-omxcore
LOCAL_SHARED_LIBRARIES  += libOmxG711Enc
LOCAL_SRC_FILES         := test/omx_g711_enc_test.c

include $(BUILD_EXECUTABLE)

endif

# ---------------------------------------------------------------------------------
#                     END
# ---------------------------------------------------------------------------------

