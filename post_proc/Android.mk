ifeq ($(strip $(AUDIO_FEATURE_ENABLED_EXTN_POST_PROC)),true)
LOCAL_PATH:= $(call my-dir)

qcom_post_proc_common_cflags := \
    -O2 -fvisibility=hidden \
    -Wall -Werror \
    -Wno-unused-function \
    -Wno-unused-variable

include $(CLEAR_VARS)

ifneq ($(strip $(AUDIO_FEATURE_ENABLED_PROXY_DEVICE)),false)
    LOCAL_CFLAGS += -DAFE_PROXY_ENABLED
endif

LOCAL_SRC_FILES:= \
	bundle.c \
	equalizer.c \
	bass_boost.c \
	virtualizer.c \
	reverb.c \
	effect_api.c

LOCAL_CFLAGS += $(qcom_post_proc_common_cflags)

LOCAL_SHARED_LIBRARIES := \
	libcutils \
	liblog \
	libtinyalsa

LOCAL_MODULE_TAGS := optional
LOCAL_VENDOR_MODULE := true

LOCAL_MODULE_RELATIVE_PATH := soundfx
LOCAL_MODULE:= libqcompostprocbundle

LOCAL_C_INCLUDES := \
	external/tinyalsa/include \
	$(call include-path-for, audio-effects)

LOCAL_HEADER_LIBRARIES := generated_kernel_headers

include $(BUILD_SHARED_LIBRARY)

endif
