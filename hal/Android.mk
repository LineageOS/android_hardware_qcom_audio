ifeq ($(strip $(BOARD_USES_ALSA_AUDIO)),true)
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

ifneq ($(filter lahaina,$(TARGET_BOARD_PLATFORM)),)
  LOCAL_CFLAGS := -DPLATFORM_LAHAINA
  LOCAL_CFLAGS += -DMAX_TARGET_SPECIFIC_CHANNEL_CNT="4"
  LOCAL_CFLAGS += -DINCALL_STEREO_CAPTURE_ENABLED
endif
ifneq ($(filter holi,$(TARGET_BOARD_PLATFORM)),)
  LOCAL_CFLAGS := -DPLATFORM_HOLI
  LOCAL_CFLAGS += -DMAX_TARGET_SPECIFIC_CHANNEL_CNT="4"
  LOCAL_CFLAGS += -DINCALL_STEREO_CAPTURE_ENABLED
endif

LOCAL_CFLAGS += -Wno-macro-redefined

LOCAL_HEADER_LIBRARIES := libhardware_headers

ifeq ($(AUDIO_FEATURE_ENABLED_HAL_V7), true)
  LOCAL_CFLAGS += -DANDROID_U_HAL7
endif

LOCAL_SRC_FILES := \
    audio_hw.c \
    audio_hw_lvacfs.c \
    audio_hw_lvimfs.c \
    acdb.c \
    platform_info.c \
    msm8974/platform.c \
    voice.c

LOCAL_SRC_FILES += audio_extn/audio_extn.c \
                   audio_extn/audio_hidl.cpp \
                   audio_extn/compress_in.c \
                   audio_extn/fm.c \
                   audio_extn/keep_alive.c \
                   audio_extn/source_track.c \
                   audio_extn/usb.c \
                   audio_extn/utils.c \
                   audio_extn/device_utils.c \
                   voice_extn/compress_voip.c \
                   voice_extn/voice_extn.c

LOCAL_SHARED_LIBRARIES := \
    libbase \
    liblog \
    libcutils \
    libtinyalsa \
    libtinycompress \
    libaudioroute \
    libdl \
    libaudioutils \
    libexpat \
    libhidlbase \
    libprocessgroup \
    libutils

LOCAL_C_INCLUDES += \
    external/tinyalsa/include \
    external/tinycompress/include \
    system/media/audio_utils/include \
    external/expat/lib \
    $(call include-path-for, audio-route) \
    $(call include-path-for, audio-effects) \
    $(LOCAL_PATH)/msm8974 \
    $(LOCAL_PATH)/audio_extn \
    $(LOCAL_PATH)/voice_extn

LOCAL_HEADER_LIBRARIES += qti_kernel_headers

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_EXTENDED_COMPRESS_FORMAT)),true)
  LOCAL_CFLAGS += -DENABLE_EXTENDED_COMPRESS_FORMAT
endif

# Legacy feature
LOCAL_CFLAGS += -DHW_VARIANTS_ENABLED
LOCAL_SRC_FILES += msm8974/hw_info.c

# Hardware specific feature
ifeq ($(strip $(BOARD_SUPPORTS_SOUND_TRIGGER)),true)
    ST_FEATURE_ENABLE := true
endif

# Hardware specific feature
ifeq ($(strip $(BOARD_SUPPORTS_SOUND_TRIGGER_HAL)),true)
    ST_FEATURE_ENABLE := true
endif

# Hardware specific feature
ifeq ($(ST_FEATURE_ENABLE), true)
    LOCAL_CFLAGS += -DSOUND_TRIGGER_ENABLED
    LOCAL_CFLAGS += -DSOUND_TRIGGER_PLATFORM_NAME=$(TARGET_BOARD_PLATFORM)
    LOCAL_C_INCLUDES += $(TARGET_OUT_HEADERS)/mm-audio/sound_trigger
    LOCAL_SRC_FILES += audio_extn/soundtrigger.c
    LOCAL_HEADER_LIBRARIES += sound_trigger.primary_headers
endif

# Hardare specific featre
ifeq ($(strip $(AUDIO_FEATURE_ENABLED_GEF_SUPPORT)),true)
    LOCAL_CFLAGS += -DAUDIO_GENERIC_EFFECT_FRAMEWORK_ENABLED
    LOCAL_SRC_FILES += audio_extn/gef.c
endif

# Hardware specific feature
ifeq ($(strip $(AUDIO_FEATURE_ENABLED_INSTANCE_ID)), true)
    LOCAL_CFLAGS += -DINSTANCE_ID_ENABLED
endif

# Kernel specific feature
ifeq ($(strip $(AUDIO_FEATURE_ENABLED_GKI)), true)
    LOCAL_CFLAGS += -DAUDIO_GKI_ENABLED
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_EXT_AMPLIFIER)),true)
    LOCAL_CFLAGS += -DEXT_AMPLIFIER_ENABLED
    LOCAL_SRC_FILES += audio_extn/audio_amplifier.c
    LOCAL_SHARED_LIBRARIES += libhardware
endif

LOCAL_CFLAGS += -D_GNU_SOURCE
LOCAL_CFLAGS += -Wall -Werror

LOCAL_SHARED_LIBRARIES += libbase libhidlbase libutils android.hardware.power@1.2 liblog

LOCAL_SHARED_LIBRARIES += android.hardware.power-V1-ndk
LOCAL_SHARED_LIBRARIES += libbinder_ndk

LOCAL_SRC_FILES += audio_perf.cpp

LOCAL_MODULE := audio.primary.$(TARGET_BOARD_PLATFORM)

LOCAL_MODULE_RELATIVE_PATH := hw

LOCAL_MODULE_TAGS := optional

LOCAL_MODULE_OWNER := qti

LOCAL_VENDOR_MODULE := true

ifneq ($(filter kona lahaina holi,$(TARGET_BOARD_PLATFORM)),)
LOCAL_SANITIZE := integer_overflow
endif
include $(BUILD_SHARED_LIBRARY)

LOCAL_CFLAGS += -Wno-unused-variable
LOCAL_CFLAGS += -Wno-sign-compare
LOCAL_CFLAGS += -Wno-unused-parameter
LOCAL_CFLAGS += -Wno-unused-label
LOCAL_CFLAGS += -Wno-gnu-designator
LOCAL_CFLAGS += -Wno-typedef-redefinition
LOCAL_CFLAGS += -Wno-shorten-64-to-32
LOCAL_CFLAGS += -Wno-tautological-compare
LOCAL_CFLAGS += -Wno-unused-function
LOCAL_CFLAGS += -Wno-unused-local-typedef

endif
