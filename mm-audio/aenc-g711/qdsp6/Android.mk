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
libOmxG711Enc-def += -Wno-sign-conversion -Wno-self-assign -Wno-format -Wno-macro-redefined

# ---------------------------------------------------------------------------------
#             Make the Shared library (libOmxG711Enc)
# ---------------------------------------------------------------------------------

include $(CLEAR_VARS)

libOmxG711Enc-inc       := $(LOCAL_PATH)/inc
libOmxG711Enc-inc       += $(TARGET_OUT_HEADERS)/mm-core/omxcore

LOCAL_MODULE             := libOmxG711Enc
LOCAL_MODULE_TAGS        := optional
LOCAL_VENDOR_MODULE      := true
LOCAL_CFLAGS            := $(libOmxG711Enc-def)
LOCAL_C_INCLUDES        := $(libOmxG711Enc-inc)
LOCAL_PRELINK_MODULE    := false
LOCAL_SHARED_LIBRARIES  := libutils liblog libcutils

LOCAL_SRC_FILES         := src/aenc_svr.c
LOCAL_SRC_FILES         += src/omx_g711_aenc.cpp
LOCAL_SRC_FILES         += src/omx_log.cpp

LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/techpack/audio/include
LOCAL_ADDITIONAL_DEPENDENCIES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr
LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/vendor/qcom/opensource/audio-kernel/include
LOCAL_ADDITIONAL_DEPENDENCIES += $(BOARD_VENDOR_KERNEL_MODULES)


include $(BUILD_SHARED_LIBRARY)

endif

# ---------------------------------------------------------------------------------
#                     END
# ---------------------------------------------------------------------------------

