ifneq ($(strip $(TARGET_PROVIDES_AUDIO_EXTNS)),true)

#--------------------------------------------
#          Build SND_MONITOR LIB
#--------------------------------------------
LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := libsndmonitor
LOCAL_MODULE_OWNER := third_party
LOCAL_VENDOR_MODULE := true

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
    hardware/qcom-caf/sm8350/audio/hal \
    $(call include-path-for, audio-effects)

LOCAL_HEADER_LIBRARIES += qti_kernel_headers

LOCAL_HEADER_LIBRARIES += libhardware_headers
LOCAL_HEADER_LIBRARIES += libsystem_headers
ifneq ($(filter kona lahaina holi,$(TARGET_BOARD_PLATFORM)),)
LOCAL_SANITIZE := integer_overflow
endif
include $(BUILD_SHARED_LIBRARY)

#--------------------------------------------
#          Build COMPRESS_CAPTURE LIB
#--------------------------------------------
include $(CLEAR_VARS)

LOCAL_MODULE := libcomprcapture
LOCAL_MODULE_OWNER := third_party
LOCAL_VENDOR_MODULE := true

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
    hardware/qcom-caf/sm8350/audio/hal \
    hardware/qcom-caf/sm8350/audio/hal/msm8974 \
    $(call include-path-for, audio-effects)

LOCAL_HEADER_LIBRARIES += qti_kernel_headers

LOCAL_HEADER_LIBRARIES += libhardware_headers
LOCAL_HEADER_LIBRARIES += libsystem_headers
ifneq ($(filter kona lahaina holi,$(TARGET_BOARD_PLATFORM)),)
LOCAL_SANITIZE := integer_overflow
endif
include $(BUILD_SHARED_LIBRARY)

#-------------------------------------------
#            Build SSREC LIB
#-------------------------------------------
include $(CLEAR_VARS)

LOCAL_MODULE := libssrec
LOCAL_VENDOR_MODULE := true

LOCAL_SRC_FILES:= ssr.c \
                  device_utils.c

LOCAL_CFLAGS += \
    -Wall \
    -Werror \
    -Wno-unused-function \
    -Wno-unused-variable

ifeq ($(QCPATH),)
  LOCAL_CFLAGS += -D_OSS
endif

LOCAL_SHARED_LIBRARIES := \
    libaudioutils \
    libcutils \
    liblog \
    libtinyalsa \
    libtinycompress \
    libaudioroute \
    libdl \
    libexpat \
    libprocessgroup

LOCAL_C_INCLUDES := \
    hardware/qcom-caf/sm8350/audio/hal \
    hardware/qcom-caf/sm8350/audio/hal/msm8974 \
    external/tinyalsa/include \
    external/tinycompress/include \
    external/expat/lib \
    system/media/audio_utils/include \
    $(call include-path-for, audio-route) \
    $(call include-path-for, audio-effects) \

LOCAL_HEADER_LIBRARIES += qti_kernel_headers

LOCAL_HEADER_LIBRARIES += libhardware_headers
LOCAL_HEADER_LIBRARIES += libsystem_headers
ifneq ($(filter kona lahaina holi,$(TARGET_BOARD_PLATFORM)),)
LOCAL_SANITIZE := integer_overflow
endif
include $(BUILD_SHARED_LIBRARY)

#--------------------------------------------
#          Build HDMI_EDID LIB
#--------------------------------------------
include $(CLEAR_VARS)

LOCAL_MODULE := libhdmiedid
LOCAL_MODULE_OWNER := third_party
LOCAL_VENDOR_MODULE := true

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
    hardware/qcom-caf/sm8350/audio/hal \
    hardware/qcom-caf/sm8350/audio/hal/msm8974 \
    $(call include-path-for, audio-effects)

LOCAL_HEADER_LIBRARIES += qti_kernel_headers

LOCAL_HEADER_LIBRARIES += libhardware_headers
LOCAL_HEADER_LIBRARIES += libsystem_headers
ifneq ($(filter kona lahaina holi,$(TARGET_BOARD_PLATFORM)),)
LOCAL_SANITIZE := integer_overflow
endif
include $(BUILD_SHARED_LIBRARY)

#--------------------------------------------
#          Build SPKR_PROTECT LIB
#--------------------------------------------
include $(CLEAR_VARS)

LOCAL_MODULE := libspkrprot
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
    hardware/qcom-caf/sm8350/audio/hal \
    hardware/qcom-caf/sm8350/audio/hal/audio_extn \
    hardware/qcom-caf/sm8350/audio/hal/msm8974 \
    $(call include-path-for, audio-effects)

LOCAL_HEADER_LIBRARIES += qti_kernel_headers

LOCAL_HEADER_LIBRARIES += libhardware_headers
LOCAL_HEADER_LIBRARIES += libsystem_headers
ifneq ($(filter kona lahaina holi,$(TARGET_BOARD_PLATFORM)),)
LOCAL_SANITIZE := integer_overflow
endif
include $(BUILD_SHARED_LIBRARY)
#====================================================================================================
# --- enable 3rd Party Spkr-prot lib
#====================================================================================================

include $(CLEAR_VARS)

LOCAL_MODULE := libcirrusspkrprot
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
    hardware/qcom-caf/sm8350/audio/hal \
    hardware/qcom-caf/sm8350/audio/hal/audio_extn \
    hardware/qcom-caf/sm8350/audio/hal/msm8974 \
    $(call include-path-for, audio-effects)

LOCAL_HEADER_LIBRARIES += qti_kernel_headers

LOCAL_HEADER_LIBRARIES += libhardware_headers
LOCAL_HEADER_LIBRARIES += libsystem_headers
ifneq ($(filter kona lahaina holi,$(TARGET_BOARD_PLATFORM)),)
LOCAL_SANITIZE := integer_overflow
endif
include $(BUILD_SHARED_LIBRARY)

#-------------------------------------------
#            Build A2DP_OFFLOAD LIB
#-------------------------------------------
include $(CLEAR_VARS)

LOCAL_MODULE := liba2dpoffload
LOCAL_VENDOR_MODULE := true

LOCAL_SRC_FILES:= \
        a2dp.c \
        device_utils.c

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
    hardware/qcom-caf/sm8350/audio/hal \
    hardware/qcom-caf/sm8350/audio/hal/msm8974 \
    external/tinyalsa/include \
    external/tinycompress/include \
    external/expat/lib \
    system/media/audio_utils/include \
    $(call include-path-for, audio-route) \

LOCAL_HEADER_LIBRARIES += qti_kernel_headers

LOCAL_HEADER_LIBRARIES += libhardware_headers
LOCAL_HEADER_LIBRARIES += libsystem_headers
ifneq ($(filter kona lahaina holi,$(TARGET_BOARD_PLATFORM)),)
LOCAL_SANITIZE := integer_overflow
endif
include $(BUILD_SHARED_LIBRARY)

#-------------------------------------------

#            Build EXT_HW_PLUGIN LIB
#-------------------------------------------
include $(CLEAR_VARS)

LOCAL_MODULE := libexthwplugin

LOCAL_VENDOR_MODULE := true

LOCAL_SRC_FILES:= \
        ext_hw_plugin.c

LOCAL_CFLAGS += \
    -Wall \
    -Werror \
    -Wno-unused-function \
    -Wno-unused-variable

LOCAL_SHARED_LIBRARIES := \
    libaudioroute \
    libaudioutils \
    libcutils \
    libdl \
    libexpat \
    liblog \
    libtinyalsa \
    libtinycompress

LOCAL_C_INCLUDES := \
    hardware/qcom-caf/sm8350/audio/hal \
    hardware/qcom-caf/sm8350/audio/hal/msm8974 \
    external/tinyalsa/include \
    external/tinycompress/include \
    external/expat/lib \
    system/media/audio_utils/include \
    $(call include-path-for, audio-route) \

LOCAL_HEADER_LIBRARIES += qti_kernel_headers

LOCAL_HEADER_LIBRARIES += libhardware_headers
LOCAL_HEADER_LIBRARIES += libsystem_headers
ifneq ($(filter kona lahaina holi,$(TARGET_BOARD_PLATFORM)),)
LOCAL_SANITIZE := integer_overflow
endif
include $(BUILD_SHARED_LIBRARY)

#-------------------------------------------
#            Build HFP LIB
#-------------------------------------------
include $(CLEAR_VARS)

LOCAL_MODULE := libhfp
LOCAL_VENDOR_MODULE := true

LOCAL_SRC_FILES:= \
        hfp.c \
        device_utils.c

LOCAL_CFLAGS += \
    -Wall \
    -Werror \
    -Wno-unused-function \
    -Wno-unused-variable

LOCAL_SHARED_LIBRARIES := \
    libaudioroute \
    libaudioutils \
    libcutils \
    libdl \
    libexpat \
    liblog \
    libtinyalsa \
    libtinycompress

LOCAL_C_INCLUDES := \
    hardware/qcom-caf/sm8350/audio/hal \
    hardware/qcom-caf/sm8350/audio/hal/msm8974 \
    external/tinyalsa/include \
    external/tinycompress/include \
    external/expat/lib \
    system/media/audio_utils/include \
    $(call include-path-for, audio-route) \

LOCAL_HEADER_LIBRARIES += qti_kernel_headers

LOCAL_HEADER_LIBRARIES += libhardware_headers
LOCAL_HEADER_LIBRARIES += libsystem_headers
ifneq ($(filter kona lahaina holi,$(TARGET_BOARD_PLATFORM)),)
LOCAL_SANITIZE := integer_overflow
endif
include $(BUILD_SHARED_LIBRARY)

#-------------------------------------------
#            Build ICC LIB
#-------------------------------------------
include $(CLEAR_VARS)

LOCAL_MODULE := libicc
LOCAL_VENDOR_MODULE := true

LOCAL_SRC_FILES:= \
        icc.c \
        device_utils.c

LOCAL_CFLAGS += \
    -Wall \
    -Werror \
    -Wno-unused-function \
    -Wno-unused-variable

LOCAL_SHARED_LIBRARIES := \
    libaudioroute \
    libaudioutils \
    libcutils \
    libdl \
    libexpat \
    liblog \
    libtinyalsa \
    libtinycompress

LOCAL_C_INCLUDES := \
    hardware/qcom-caf/sm8350/audio/hal \
    hardware/qcom-caf/sm8350/audio/hal/msm8974 \
    external/tinyalsa/include \
    external/tinycompress/include \
    external/expat/lib \
    system/media/audio_utils/include \
    $(call include-path-for, audio-route) \

LOCAL_HEADER_LIBRARIES += qti_kernel_headers

LOCAL_HEADER_LIBRARIES += libhardware_headers
LOCAL_HEADER_LIBRARIES += libsystem_headers
ifneq ($(filter kona lahaina holi,$(TARGET_BOARD_PLATFORM)),)
LOCAL_SANITIZE := integer_overflow
endif
include $(BUILD_SHARED_LIBRARY)

#-------------------------------------------
#            Build SYNTH LIB
#-------------------------------------------
include $(CLEAR_VARS)

LOCAL_MODULE := libsynth
LOCAL_VENDOR_MODULE := true

LOCAL_SRC_FILES:= \
        synth.c  \
        device_utils.c

LOCAL_CFLAGS += \
    -Wall \
    -Werror \
    -Wno-unused-function \
    -Wno-unused-variable

LOCAL_SHARED_LIBRARIES := \
    libaudioroute \
    libaudioutils \
    libcutils \
    libdl \
    libexpat \
    liblog \
    libtinyalsa \
    libtinycompress

LOCAL_C_INCLUDES := \
    hardware/qcom-caf/sm8350/audio/hal \
    hardware/qcom-caf/sm8350/audio/hal/msm8974 \
    external/tinyalsa/include \
    external/tinycompress/include \
    external/expat/lib \
    system/media/audio_utils/include \
    $(call include-path-for, audio-route) \

LOCAL_HEADER_LIBRARIES += qti_kernel_headers

LOCAL_HEADER_LIBRARIES += libhardware_headers
LOCAL_HEADER_LIBRARIES += libsystem_headers
ifneq ($(filter kona lahaina holi,$(TARGET_BOARD_PLATFORM)),)
LOCAL_SANITIZE := integer_overflow
endif
include $(BUILD_SHARED_LIBRARY)

#-------------------------------------------
#            Build BATTERY_LISTENER
#-------------------------------------------
include $(CLEAR_VARS)

LOCAL_MODULE := libbatterylistener
LOCAL_VENDOR_MODULE := true

LOCAL_SRC_FILES:= \
        battery_listener.cpp

LOCAL_CFLAGS += \
    -Wall \
    -Werror \
    -Wno-unused-function \
    -Wno-unused-variable \
    -DDTSHD_PARSER_ENABLED

LOCAL_SHARED_LIBRARIES := \
    android.hardware.health@1.0 \
    android.hardware.health@2.0 \
    android.hardware.power@1.2 \
    libaudioroute \
    libaudioutils \
    libbase \
    libcutils \
    libdl \
    libexpat \
    libhidlbase \
    liblog \
    libtinyalsa \
    libtinycompress \
    libutils \

LOCAL_STATIC_LIBRARIES := \
    libhealthhalutils

LOCAL_C_INCLUDES := \
    hardware/qcom-caf/sm8350/audio/hal \
    hardware/qcom-caf/sm8350/audio/hal/msm8974 \
    external/tinyalsa/include \
    external/tinycompress/include \
    external/expat/lib \
    system/media/audio_utils/include \
    $(call include-path-for, audio-route) \

LOCAL_HEADER_LIBRARIES += qti_kernel_headers

LOCAL_HEADER_LIBRARIES += libhardware_headers
LOCAL_HEADER_LIBRARIES += libsystem_headers
ifneq ($(filter kona lahaina holi,$(TARGET_BOARD_PLATFORM)),)
LOCAL_SANITIZE := integer_overflow
endif
include $(BUILD_SHARED_LIBRARY)

#-------------------------------------------
#            Build MAXX_AUDIO
#-------------------------------------------
include $(CLEAR_VARS)

LOCAL_MODULE:= libmaxxaudio
LOCAL_VENDOR_MODULE := true

LOCAL_SRC_FILES:= \
        maxxaudio.c \
        device_utils.c

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
    hardware/qcom-caf/sm8350/audio/hal \
    hardware/qcom-caf/sm8350/audio/hal/msm8974 \
    external/tinyalsa/include \
    external/tinycompress/include \
    external/expat/lib \
    system/media/audio_utils/include \
    $(call include-path-for, audio-route) \

LOCAL_HEADER_LIBRARIES += qti_kernel_headers

LOCAL_HEADER_LIBRARIES += libhardware_headers
LOCAL_HEADER_LIBRARIES += libsystem_headers
ifneq ($(filter kona lahaina holi,$(TARGET_BOARD_PLATFORM)),)
LOCAL_SANITIZE := integer_overflow
endif
include $(BUILD_SHARED_LIBRARY)
#-------------------------------------------
#            Build AUDIOZOOM
#-------------------------------------------
include $(CLEAR_VARS)

LOCAL_MODULE:= libaudiozoom
LOCAL_VENDOR_MODULE := true

LOCAL_SRC_FILES:= \
        audiozoom.c \
        device_utils.c

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
    hardware/qcom-caf/sm8350/audio/hal \
    hardware/qcom-caf/sm8350/audio/hal/msm8974 \
    external/tinyalsa/include \
    external/tinycompress/include \
    external/expat/lib \
    system/media/audio_utils/include \
    $(call include-path-for, audio-route) \

LOCAL_HEADER_LIBRARIES += qti_kernel_headers

LOCAL_HEADER_LIBRARIES += libhardware_headers
LOCAL_HEADER_LIBRARIES += libsystem_headers
ifneq ($(filter kona lahaina holi,$(TARGET_BOARD_PLATFORM)),)
LOCAL_SANITIZE := integer_overflow
endif
include $(BUILD_SHARED_LIBRARY)

endif
