#--------------------------------------------
#          Build SND_MONITOR LIB
#--------------------------------------------
LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE:= libsndmonitor
LOCAL_MODULE_OWNER := third_party
LOCAL_VENDOR_MODULE := true

AUDIO_PLATFORM := $(TARGET_BOARD_PLATFORM)

LOCAL_SRC_FILES:= \
        sndmonitor.c

LOCAL_CFLAGS += \
    -Wall \
    -Werror \
    -Wno-unused-function \
    -Wno-unused-variable \

LOCAL_SHARED_LIBRARIES := \
	libaudioutils \
	libcutils \
	liblog \
	libtinyalsa \
	libtinycompress \
	libaudioroute \
	libdl \
	libexpat

LOCAL_C_INCLUDES := \
	external/tinyalsa/include \
	external/tinycompress/include \
	system/media/audio_utils/include \
	external/expat/lib \
	$(call include-path-for, audio-route) \
	vendor/qcom/opensource/audio-hal/primary-hal/hal \
	$(call include-path-for, audio-effects)

LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/techpack/audio/include
LOCAL_ADDITIONAL_DEPENDENCIES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_DLKM)),true)
  LOCAL_HEADER_LIBRARIES += audio_kernel_headers
  LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/vendor/qcom/opensource/audio-kernel/include
  LOCAL_ADDITIONAL_DEPENDENCIES += $(BOARD_VENDOR_KERNEL_MODULES)
endif

LOCAL_HEADER_LIBRARIES += libhardware_headers
LOCAL_HEADER_LIBRARIES += libsystem_headers
include $(BUILD_SHARED_LIBRARY)

#--------------------------------------------
#          Build COMPRESS_CAPTURE LIB
#--------------------------------------------
include $(CLEAR_VARS)

LOCAL_MODULE:= libcomprcapture
LOCAL_MODULE_OWNER := third_party
LOCAL_VENDOR_MODULE := true

AUDIO_PLATFORM := $(TARGET_BOARD_PLATFORM)

ifneq ($(filter sdm845 sdm710 qcs605 msmnile kona $(MSMSTEPPE),$(TARGET_BOARD_PLATFORM)),)
  # B-family platform uses msm8974 code base
  AUDIO_PLATFORM = msm8974
  MULTIPLE_HW_VARIANTS_ENABLED := true
endif

LOCAL_SRC_FILES:= \
        compress_capture.c

LOCAL_CFLAGS += \
    -Wall \
    -Werror \
    -Wno-unused-function \
    -Wno-unused-variable \

LOCAL_SHARED_LIBRARIES := \
	libaudioutils \
	libcutils \
	liblog \
	libtinyalsa \
	libtinycompress \
	libaudioroute \
	libdl \
	libexpat

LOCAL_C_INCLUDES := \
	external/tinyalsa/include \
	external/tinycompress/include \
	system/media/audio_utils/include \
	external/expat/lib \
	$(call include-path-for, audio-route) \
	vendor/qcom/opensource/audio-hal/primary-hal/hal \
    vendor/qcom/opensource/audio-hal/primary-hal/hal/$(AUDIO_PLATFORM) \
	$(call include-path-for, audio-effects)

LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/techpack/audio/include
LOCAL_ADDITIONAL_DEPENDENCIES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_DLKM)),true)
  LOCAL_HEADER_LIBRARIES += audio_kernel_headers
  LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/vendor/qcom/opensource/audio-kernel/include
  LOCAL_ADDITIONAL_DEPENDENCIES += $(BOARD_VENDOR_KERNEL_MODULES)
endif

LOCAL_HEADER_LIBRARIES += libhardware_headers
LOCAL_HEADER_LIBRARIES += libsystem_headers
include $(BUILD_SHARED_LIBRARY)

#-------------------------------------------
#            Build SSREC LIB
#-------------------------------------------
include $(CLEAR_VARS)

LOCAL_MODULE:= libssrec
LOCAL_VENDOR_MODULE := true

AUDIO_PLATFORM := $(TARGET_BOARD_PLATFORM)

ifneq ($(filter sdm845 sdm710 msmnile kona $(MSMSTEPPE),$(TARGET_BOARD_PLATFORM)),)
  # B-family platform uses msm8974 code base
  AUDIO_PLATFORM = msm8974
  MULTIPLE_HW_VARIANTS_ENABLED := true
endif

LOCAL_SRC_FILES:= ssr.c

LOCAL_CFLAGS += \
    -Wall \
    -Werror \
    -Wno-unused-function \
    -Wno-unused-variable

LOCAL_SHARED_LIBRARIES := \
    libaudioutils \
    libcutils \
    liblog \
    libtinyalsa \
    libtinycompress \
    libaudioroute \
    libdl \
    libexpat

LOCAL_C_INCLUDES := \
    vendor/qcom/opensource/audio-hal/primary-hal/hal \
    vendor/qcom/opensource/audio-hal/primary-hal/hal/$(AUDIO_PLATFORM) \
    external/tinyalsa/include \
    external/tinycompress/include \
    external/expat/lib \
    system/media/audio_utils/include \
    $(call include-path-for, audio-route) \
    $(call include-path-for, audio-effects) \
    $(TARGET_OUT_HEADERS)/mm-audio/surround_sound_3mic/ \
    $(TARGET_OUT_HEADERS)/common/inc/

LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/techpack/audio/include
LOCAL_ADDITIONAL_DEPENDENCIES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_DLKM)),true)
  LOCAL_HEADER_LIBRARIES += audio_kernel_headers
  LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/vendor/qcom/opensource/audio-kernel/include
  LOCAL_ADDITIONAL_DEPENDENCIES += $(BOARD_VENDOR_KERNEL_MODULES)
endif

LOCAL_HEADER_LIBRARIES += libhardware_headers
LOCAL_HEADER_LIBRARIES += libsystem_headers
#include $(BUILD_SHARED_LIBRARY)

#--------------------------------------------
#          Build HDMI_EDID LIB
#--------------------------------------------
include $(CLEAR_VARS)

LOCAL_MODULE:= libhdmiedid
LOCAL_MODULE_OWNER := third_party
LOCAL_VENDOR_MODULE := true

PRIMARY_HAL_PATH := vendor/qcom/opensource/audio-hal/primary-hal/hal
AUDIO_PLATFORM := $(TARGET_BOARD_PLATFORM)

ifneq ($(filter sdm845 sdm710 msmnile kona $(MSMSTEPPE) $(TRINKET),$(TARGET_BOARD_PLATFORM)),)
  # B-family platform uses msm8974 code base
  AUDIO_PLATFORM = msm8974
endif

LOCAL_SRC_FILES:= \
        edid.c

LOCAL_CFLAGS += \
    -Wall \
    -Werror \
    -Wno-unused-function \
    -Wno-unused-variable \

LOCAL_SHARED_LIBRARIES := \
	libaudioutils \
	libcutils \
	liblog \
	libtinyalsa \
	libtinycompress \
	libaudioroute \
	libdl \
	libexpat

LOCAL_C_INCLUDES := \
	external/tinyalsa/include \
	external/tinycompress/include \
	system/media/audio_utils/include \
	external/expat/lib \
	$(call include-path-for, audio-route) \
	$(PRIMARY_HAL_PATH) \
	$(PRIMARY_HAL_PATH)/$(AUDIO_PLATFORM) \
	$(call include-path-for, audio-effects)

LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/techpack/audio/include
LOCAL_ADDITIONAL_DEPENDENCIES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_DLKM)),true)
  LOCAL_HEADER_LIBRARIES += audio_kernel_headers
  LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/vendor/qcom/opensource/audio-kernel/include
  LOCAL_ADDITIONAL_DEPENDENCIES += $(BOARD_VENDOR_KERNEL_MODULES)
endif

LOCAL_HEADER_LIBRARIES += libhardware_headers
LOCAL_HEADER_LIBRARIES += libsystem_headers
include $(BUILD_SHARED_LIBRARY)

#--------------------------------------------
#          Build SPKR_PROTECT LIB
#--------------------------------------------
include $(CLEAR_VARS)

ifneq ($(filter sdm845 sdm710 msmnile kona $(MSMSTEPPE) $(TRINKET),$(TARGET_BOARD_PLATFORM)),)
  # B-family platform uses msm8974 code base
  AUDIO_PLATFORM = msm8974
endif

LOCAL_MODULE:= libspkrprot
LOCAL_MODULE_OWNER := third_party
LOCAL_VENDOR_MODULE := true

LOCAL_SRC_FILES:= \
        spkr_protection.c

LOCAL_CFLAGS += \
    -Wall \
    -Werror \
    -Wno-unused-function \
    -Wno-unused-variable \

LOCAL_CFLAGS += -DSPKR_PROT_ENABLED

LOCAL_SHARED_LIBRARIES := \
    libaudioutils \
    libcutils \
    liblog \
    libtinyalsa \
    libtinycompress \
    libaudioroute \
    libdl \
    libexpat

LOCAL_C_INCLUDES := \
    external/tinyalsa/include \
    external/tinycompress/include \
    system/media/audio_utils/include \
    external/expat/lib \
    $(call include-path-for, audio-route) \
    vendor/qcom/opensource/audio-hal/primary-hal/hal \
    vendor/qcom/opensource/audio-hal/primary-hal/hal/audio_extn \
    vendor/qcom/opensource/audio-hal/primary-hal/hal/$(AUDIO_PLATFORM) \
    vendor/qcom/opensource/audio-kernel/include/uapi/ \
    $(call include-path-for, audio-effects)

LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/techpack/audio/include

LOCAL_HEADER_LIBRARIES += libhardware_headers
LOCAL_HEADER_LIBRARIES += libsystem_headers
include $(BUILD_SHARED_LIBRARY)
#====================================================================================================
# --- enable 3rd Party Spkr-prot lib
#====================================================================================================

include $(CLEAR_VARS)

ifneq ($(filter sdm845 sdm710 msmnile kona $(MSMSTEPPE) $(TRINKET),$(TARGET_BOARD_PLATFORM)),)
  # B-family platform uses msm8974 code base
  AUDIO_PLATFORM = msm8974
endif

LOCAL_MODULE:= libcirrusspkrprot
LOCAL_MODULE_OWNER := third_party
LOCAL_VENDOR_MODULE := true

LOCAL_SRC_FILES:= \
        cirrus_playback.c

LOCAL_CFLAGS += \
    -Wall \
    -Werror \
    -Wno-unused-function \
    -Wno-unused-variable \

LOCAL_CFLAGS += -DENABLE_CIRRUS_DETECTION
LOCAL_CFLAGS += -DCIRRUS_FACTORY_CALIBRATION

LOCAL_SHARED_LIBRARIES := \
    libaudioutils \
    libcutils \
    liblog \
    libtinyalsa \
    libaudioroute \
    libdl \
    libexpat

LOCAL_C_INCLUDES := \
    external/tinyalsa/include \
    external/tinycompress/include \
    system/media/audio_utils/include \
    external/expat/lib \
    $(call include-path-for, audio-route) \
    vendor/qcom/opensource/audio-hal/primary-hal/hal \
    vendor/qcom/opensource/audio-hal/primary-hal/hal/audio_extn \
    vendor/qcom/opensource/audio-hal/primary-hal/hal/$(AUDIO_PLATFORM) \
    vendor/qcom/opensource/audio-kernel/include/uapi/ \
    $(call include-path-for, audio-effects)

LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/techpack/audio/include

LOCAL_HEADER_LIBRARIES += libhardware_headers
LOCAL_HEADER_LIBRARIES += libsystem_headers
include $(BUILD_SHARED_LIBRARY)

#-------------------------------------------
#            Build A2DP_OFFLOAD LIB
#-------------------------------------------
include $(CLEAR_VARS)

LOCAL_MODULE:= liba2dpoffload
LOCAL_VENDOR_MODULE := true

PRIMARY_HAL_PATH := vendor/qcom/opensource/audio-hal/primary-hal/hal
AUDIO_PLATFORM := $(TARGET_BOARD_PLATFORM)

ifneq ($(filter sdm845 sdm710 msmnile kona $(MSMSTEPPE),$(TARGET_BOARD_PLATFORM)),)
  # B-family platform uses msm8974 code base
  AUDIO_PLATFORM = msm8974
  MULTIPLE_HW_VARIANTS_ENABLED := true
endif

LOCAL_SRC_FILES:= \
        a2dp.c

LOCAL_CFLAGS += \
    -Wall \
    -Werror \
    -Wno-unused-function \
    -Wno-unused-variable

LOCAL_SHARED_LIBRARIES := \
    libaudioutils \
    libcutils \
    liblog \
    libtinyalsa \
    libtinycompress \
    libaudioroute \
    libdl \
    libexpat

LOCAL_C_INCLUDES := \
    $(PRIMARY_HAL_PATH) \
    $(PRIMARY_HAL_PATH)/$(AUDIO_PLATFORM) \
    external/tinyalsa/include \
    external/tinycompress/include \
    external/expat/lib \
    system/media/audio_utils/include \
    $(call include-path-for, audio-route) \

LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/techpack/audio/include
LOCAL_ADDITIONAL_DEPENDENCIES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_DLKM)),true)
  LOCAL_HEADER_LIBRARIES += audio_kernel_headers
  LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/vendor/qcom/opensource/audio-kernel/include
  LOCAL_ADDITIONAL_DEPENDENCIES += $(BOARD_VENDOR_KERNEL_MODULES)
endif

LOCAL_HEADER_LIBRARIES += libhardware_headers
LOCAL_HEADER_LIBRARIES += libsystem_headers
include $(BUILD_SHARED_LIBRARY)
