LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

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
LOCAL_CFLAGS += -Wno-format
LOCAL_CFLAGS += -Wno-unused-value

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_PROXY_DEVICE)),true)
    LOCAL_CFLAGS += -DAFE_PROXY_ENABLED
endif

LOCAL_SRC_FILES:= \
        bundle.c \
        equalizer.c \
        bass_boost.c \
        virtualizer.c \
        reverb.c \
        effect_api.c \
        effect_util.c

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_HW_ACCELERATED_EFFECTS)),true)
    LOCAL_CFLAGS += -DHW_ACCELERATED_EFFECTS
    LOCAL_SRC_FILES += hw_accelerator.c
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_AUDIOSPHERE)),true)
    LOCAL_CFLAGS += -DAUDIOSPHERE_ENABLED
    LOCAL_SRC_FILES += asphere.c
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_INSTANCE_ID)), true)
    LOCAL_CFLAGS += -DINSTANCE_ID_ENABLED
endif

LOCAL_CFLAGS+= -O2 -fvisibility=hidden

ifneq ($(strip $(AUDIO_FEATURE_DISABLED_DTS_EAGLE)),true)
    LOCAL_CFLAGS += -DDTS_EAGLE
endif

LOCAL_HEADER_LIBRARIES := libhardware_headers \
                          libsystem_headers \
                          libutils_headers

LOCAL_SHARED_LIBRARIES := \
        libcutils \
        liblog \
        libtinyalsa \
        libdl

LOCAL_MODULE_TAGS := optional

LOCAL_MODULE_RELATIVE_PATH := soundfx
LOCAL_MODULE:= libqcompostprocbundle
LOCAL_VENDOR_MODULE := true

LOCAL_ADDITIONAL_DEPENDENCIES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr

LOCAL_C_INCLUDES := \
        external/tinyalsa/include \
        $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include \
	$(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/techpack/audio/include \
        $(call include-path-for, audio-effects)

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_DLKM)),true)
  LOCAL_HEADER_LIBRARIES += audio_kernel_headers
  LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/vendor/qcom/opensource/audio-kernel/include
  LOCAL_ADDITIONAL_DEPENDENCIES += $(BOARD_VENDOR_KERNEL_MODULES)
endif

ifeq ($(TARGET_COMPILE_WITH_MSM_KERNEL),true)
        LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/techpack/audio/include
        LOCAL_ADDITIONAL_DEPENDENCIES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr
endif

include $(BUILD_SHARED_LIBRARY)


ifeq ($(strip $(AUDIO_FEATURE_ENABLED_HW_ACCELERATED_EFFECTS)),true)
include $(CLEAR_VARS)

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
LOCAL_CFLAGS += -Wno-format
LOCAL_SRC_FILES := EffectsHwAcc.cpp

LOCAL_C_INCLUDES := \
    $(call include-path-for, audio-effects)

LOCAL_HEADER_LIBRARIES := libhardware_headers \
                          libsystem_headers \
                          libutils_headers

LOCAL_SHARED_LIBRARIES := \
    liblog \
    libeffects

LOCAL_MODULE_TAGS := optional

LOCAL_CFLAGS += -O2 -fvisibility=hidden

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_DTS_EAGLE)), true)
LOCAL_CFLAGS += -DHW_ACC_HPX
endif

LOCAL_MODULE:= libhwacceffectswrapper
LOCAL_VENDOR_MODULE := true

include $(BUILD_STATIC_LIBRARY)
endif



################################################################################

ifneq ($(filter msm8992 msm8994 msm8996 msm8998 sdm660 sdm845 apq8098_latv sdm710 msm8953 msm8937 qcs605 msmnile $(MSMSTEPPE) $(TRINKET),$(TARGET_BOARD_PLATFORM)),)

include $(CLEAR_VARS)

LOCAL_CFLAGS := -DLIB_AUDIO_HAL="/vendor/lib/hw/audio.primary."$(TARGET_BOARD_PLATFORM)".so"
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
LOCAL_CFLAGS += -Wno-format

LOCAL_SRC_FILES:= \
        volume_listener.c

LOCAL_CFLAGS+= -O2 -fvisibility=hidden

LOCAL_HEADER_LIBRARIES := libhardware_headers \
                          libsystem_headers \
                          libutils_headers

LOCAL_SHARED_LIBRARIES := \
        libcutils \
        liblog \
        libdl

LOCAL_MODULE_RELATIVE_PATH := soundfx
LOCAL_MODULE:= libvolumelistener
LOCAL_VENDOR_MODULE := true

LOCAL_ADDITIONAL_DEPENDENCIES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr

LOCAL_C_INCLUDES := \
        $(call project-path-for,qcom-audio)/hal \
        $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include \
        $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/techpack/audio/include \
        external/tinyalsa/include \
        $(call include-path-for, audio-effects) \
        $(call include-path-for, audio-route) \
        $(call project-path-for,qcom-audio)/hal/audio_extn \
        external/tinycompress/include

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_DLKM)),true)
  LOCAL_HEADER_LIBRARIES += audio_kernel_headers
  LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/vendor/qcom/opensource/audio-kernel/include
  LOCAL_ADDITIONAL_DEPENDENCIES += $(BOARD_VENDOR_KERNEL_MODULES)
endif

ifeq ($(TARGET_COMPILE_WITH_MSM_KERNEL),true)
        LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/techpack/audio/include
        LOCAL_ADDITIONAL_DEPENDENCIES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr
endif

include $(BUILD_SHARED_LIBRARY)

endif
