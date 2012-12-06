ifneq ($(BUILD_TINY_ANDROID),true)
ifneq ($(BUILD_WITHOUT_PV),true)

LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

# ---------------------------------------------------------------------------------
#                 Common definitons
# ---------------------------------------------------------------------------------

libOmxMp3Dec-def := -g -O3
libOmxMp3Dec-def += -DQC_MODIFIED
libOmxMp3Dec-def += -D_ANDROID_
libOmxMp3Dec-def += -D_ENABLE_QC_MSG_LOG_
libOmxMp3Dec-def += -DVERBOSE
libOmxMp3Dec-def += -D_DEBUG
libOmxMp3Dec-def += -DAUDIOV2

ifeq ($(BOARD_USES_QCOM_AUDIO_V2), true)
libOmxMp3Dec-def += -DAUDIOV2
endif

# ---------------------------------------------------------------------------------
#             Make the apps-test (mm-adec-omxmp3-test)
# ---------------------------------------------------------------------------------

include $(CLEAR_VARS)

ifeq ($(BOARD_USES_QCOM_AUDIO_V2), true)
mm-mp3-dec-test-inc   += $(TARGET_OUT_HEADERS)/mm-audio/audio-alsa
mm-mp3-dec-test-inc   += $(TARGET_OUT_HEADERS)/mm-core/omxcore
mm-mp3-dec-test-inc   += $(PV_TOP)/codecs_v2/omx/omx_mastercore/include \
        		 $(PV_TOP)/codecs_v2/omx/omx_common/include \
        		 $(PV_TOP)/extern_libs_v2/khronos/openmax/include \
        		 $(PV_TOP)/codecs_v2/omx/omx_baseclass/include \
        		 $(PV_TOP)/codecs_v2/omx/omx_mp3/include \
        		 $(PV_TOP)/codecs_v2/audio/mp3/dec/include \

LOCAL_MODULE            := sw-adec-omxmp3-test
LOCAL_MODULE_TAGS       := optional
LOCAL_CFLAGS            := $(libOmxMp3Dec-def)
LOCAL_C_INCLUDES        := $(mm-mp3-dec-test-inc)
LOCAL_PRELINK_MODULE    := false
LOCAL_SHARED_LIBRARIES  := libopencore_common
LOCAL_SHARED_LIBRARIES  += libomx_sharedlibrary
LOCAL_SHARED_LIBRARIES  += libomx_mp3dec_sharedlibrary
LOCAL_SHARED_LIBRARIES  += libaudioalsa

LOCAL_SRC_FILES         := test/omx_mp3_dec_test.c

include $(BUILD_EXECUTABLE)
endif

endif #BUILD_WITHOUT_PV
endif #BUILD_TINY_ANDROID

# ---------------------------------------------------------------------------------
#                     END
# ---------------------------------------------------------------------------------
