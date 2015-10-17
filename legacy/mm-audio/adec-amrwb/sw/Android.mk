ifneq ($(BUILD_TINY_ANDROID),true)
ifneq ($(BUILD_WITHOUT_PV),true)

LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

# ---------------------------------------------------------------------------------
#                 Common definitons
# ---------------------------------------------------------------------------------

libOmxAmrDec-def := -g -O3
libOmxAmrDec-def += -DQC_MODIFIED
libOmxAmrDec-def += -D_ANDROID_
libOmxAmrDec-def += -D_ENABLE_QC_MSG_LOG_
libOmxAmrDec-def += -DVERBOSE
libOmxAmrDec-def += -D_DEBUG
libOmxAmrDec-def += -DAUDIOV2

ifeq ($(BOARD_USES_QCOM_AUDIO_V2), true)
libOmxAmrDec-def += -DAUDIOV2
endif

# ---------------------------------------------------------------------------------
#             Make the apps-test (sw-adec-omxamr-test)
# ---------------------------------------------------------------------------------

include $(CLEAR_VARS)

ifeq ($(BOARD_USES_QCOM_AUDIO_V2), true)
mm-amr-dec-test-inc   += $(TARGET_OUT_HEADERS)/mm-audio/audio-alsa
mm-amr-dec-test-inc   += $(TARGET_OUT_HEADERS)/mm-core/omxcore
mm-amr-dec-test-inc   += $(PV_TOP)/codecs_v2/omx/omx_mastercore/include \
        		 $(PV_TOP)/codecs_v2/omx/omx_common/include \
        		 $(PV_TOP)/extern_libs_v2/khronos/openmax/include \
        		 $(PV_TOP)/codecs_v2/omx/omx_baseclass/include \
        		 $(PV_TOP)/codecs_v2/omx/omx_amr/include \
        		 $(PV_TOP)/codecs_v2/audio/amr/dec/include \

LOCAL_MODULE            := sw-adec-omxamrwb-test
LOCAL_MODULE_TAGS       := optional
LOCAL_CFLAGS            := $(libOmxAmrDec-def)
LOCAL_C_INCLUDES        := $(mm-amr-dec-test-inc)
LOCAL_SHARED_LIBRARIES  := libopencore_common
LOCAL_SHARED_LIBRARIES  += libomx_sharedlibrary
LOCAL_SHARED_LIBRARIES  += libomx_amrdec_sharedlibrary
LOCAL_SHARED_LIBRARIES  += libaudioalsa
LOCAL_SRC_FILES         := test/omx_amrwb_dec_test.c

include $(BUILD_EXECUTABLE)
endif

endif #BUILD_WITHOUT_PV
endif #BUILD_TINY_ANDROID

# ---------------------------------------------------------------------------------
#                     END
# ---------------------------------------------------------------------------------
