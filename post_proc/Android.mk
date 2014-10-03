ifneq ($(filter msm8974 msm8226 msm8084 msm8992 msm8994 msm8996 msm8909 msm8998 sdm845 sdm710 msmnile,$(TARGET_BOARD_PLATFORM)),)

LOCAL_PATH:= $(call my-dir)

qcom_post_proc_common_cflags := \
    -O2 -fvisibility=hidden \
    -Wall -Werror \
    -Wno-unused-function \
    -Wno-unused-variable \

include $(CLEAR_VARS)

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
LOCAL_MODULE_OWNER := qcom
LOCAL_PROPRIETARY_MODULE := true

LOCAL_MODULE_RELATIVE_PATH := soundfx
LOCAL_MODULE:= libqcompostprocbundle

LOCAL_C_INCLUDES := \
	external/tinyalsa/include \
	$(call include-path-for, audio-effects)

LOCAL_HEADER_LIBRARIES += libhardware_headers
LOCAL_HEADER_LIBRARIES += libsystem_headers
include $(BUILD_SHARED_LIBRARY)
endif

################################################################################

ifneq ($(filter msm8992 msm8994 msm8996 msm8909 msm8998 sdm845 sdm710 msmnile,$(TARGET_BOARD_PLATFORM)),)

include $(CLEAR_VARS)

LOCAL_CFLAGS := -DLIB_AUDIO_HAL="audio.primary."$(TARGET_BOARD_PLATFORM)".so"

LOCAL_SRC_FILES:= \
	volume_listener.c

LOCAL_CFLAGS += $(qcom_post_proc_common_cflags)

LOCAL_SHARED_LIBRARIES := \
	libcutils \
	liblog \
	libdl

LOCAL_MODULE_RELATIVE_PATH := soundfx
LOCAL_MODULE:= libvolumelistener
LOCAL_MODULE_OWNER := qcom
LOCAL_PROPRIETARY_MODULE := true

LOCAL_C_INCLUDES := \
	$(call project-path-for,qcom-audio)/hal \
	$(call include-path-for, audio-effects)

LOCAL_HEADER_LIBRARIES += libhardware_headers
LOCAL_HEADER_LIBRARIES += libsystem_headers
include $(BUILD_SHARED_LIBRARY)

endif

################################################################################
ifeq ($(strip $(AUDIO_FEATURE_ENABLED_MAXX_AUDIO)), true)

include $(CLEAR_VARS)

LOCAL_CFLAGS := -D HAL_LIB_NAME=\"audio.primary."$(TARGET_BOARD_PLATFORM)".so\"

LOCAL_SRC_FILES:= \
	ma_listener.c

LOCAL_CFLAGS += $(qcom_post_proc_common_cflags)

LOCAL_SHARED_LIBRARIES := \
	libcutils \
	liblog \
	libdl

LOCAL_MODULE_RELATIVE_PATH := soundfx
LOCAL_MODULE:= libmalistener
LOCAL_MODULE_OWNER := google
LOCAL_PROPRIETARY_MODULE := true

LOCAL_C_INCLUDES := \
	$(call project-path-for,qcom-audio)/hal \
	system/media/audio/include/system \
	$(call include-path-for, audio-effects)

LOCAL_HEADER_LIBRARIES += libhardware_headers
LOCAL_HEADER_LIBRARIES += libsystem_headers
include $(BUILD_SHARED_LIBRARY)

endif
