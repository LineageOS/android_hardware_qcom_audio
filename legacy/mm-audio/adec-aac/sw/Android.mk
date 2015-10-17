ifneq ($(BUILD_TINY_ANDROID),true)
ifneq ($(BUILD_WITHOUT_PV),true)

LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

# ---------------------------------------------------------------------------------
#                 Common definitons
# ---------------------------------------------------------------------------------

libOmxAacDec-def := -g -O3
libOmxAacDec-def += -DQC_MODIFIED
libOmxAacDec-def += -D_ANDROID_
libOmxAacDec-def += -D_ENABLE_QC_MSG_LOG_
libOmxAacDec-def += -DVERBOSE
libOmxAacDec-def += -D_DEBUG

ifeq ($(BOARD_USES_QCOM_AUDIO_V2), true)
libOmxAacDec-def += -DAUDIOV2
endif

# ---------------------------------------------------------------------------------
#             Make the apps-test (mm-adec-omxaac-test)
# ---------------------------------------------------------------------------------

include $(CLEAR_VARS)

ifeq ($(BOARD_USES_QCOM_AUDIO_V2), true)
mm-aac-dec-test-inc   += $(TARGET_OUT_HEADERS)/mm-audio/audio-alsa
mm-aac-dec-test-inc   += $(TARGET_OUT_HEADERS)/mm-core/omxcore
mm-aac-dec-test-inc   += $(PV_TOP)/codecs_v2/omx/omx_mastercore/include \
        		 $(PV_TOP)/codecs_v2/omx/omx_common/include \
        		 $(PV_TOP)/extern_libs_v2/khronos/openmax/include \
        		 $(PV_TOP)/codecs_v2/omx/omx_baseclass/include \
        		 $(PV_TOP)/codecs_v2/omx/omx_aac/include \
        		 $(PV_TOP)/codecs_v2/audio/aac/dec/include \

LOCAL_MODULE            := sw-adec-omxaac-test
LOCAL_MODULE_TAGS       := optional
LOCAL_CFLAGS            := $(libOmxAacDec-def)
LOCAL_C_INCLUDES        := $(mm-aac-dec-test-inc)
LOCAL_SHARED_LIBRARIES  := libopencore_common
LOCAL_SHARED_LIBRARIES  += libomx_sharedlibrary
LOCAL_SHARED_LIBRARIES  += libomx_aacdec_sharedlibrary
LOCAL_SHARED_LIBRARIES  += libaudioalsa

LOCAL_SRC_FILES         := test/omx_aac_dec_test.c

include $(BUILD_EXECUTABLE)
endif

endif #BUILD_WITHOUT_PV
endif #BUILD_TINY_ANDROID

# ---------------------------------------------------------------------------------
#                     END
# ---------------------------------------------------------------------------------
