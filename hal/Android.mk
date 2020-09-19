ifeq ($(strip $(BOARD_USES_ALSA_AUDIO)),true)

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm

AUDIO_PLATFORM := $(TARGET_BOARD_PLATFORM)

ifneq ($(filter msm8974 msm8226 msm8610 apq8084 msm8994 msm8992 msm8996 msm8998 apq8098_latv sdm845 sdm710 qcs605 msmnile $(MSMSTEPPE) $(TRINKET) kona,$(TARGET_BOARD_PLATFORM)),)
  # B-family platform uses msm8974 code base
  AUDIO_PLATFORM = msm8974
  MULTIPLE_HW_VARIANTS_ENABLED := true
ifneq ($(filter msm8610,$(TARGET_BOARD_PLATFORM)),)
  LOCAL_CFLAGS := -DPLATFORM_MSM8610
endif
ifneq ($(filter msm8226,$(TARGET_BOARD_PLATFORM)),)
  LOCAL_CFLAGS := -DPLATFORM_MSM8x26
endif
ifneq ($(filter apq8084,$(TARGET_BOARD_PLATFORM)),)
  LOCAL_CFLAGS := -DPLATFORM_APQ8084
endif
ifneq ($(filter msm8994,$(TARGET_BOARD_PLATFORM)),)
  LOCAL_CFLAGS := -DPLATFORM_MSM8994
endif
ifneq ($(filter msm8992,$(TARGET_BOARD_PLATFORM)),)
  LOCAL_CFLAGS := -DPLATFORM_MSM8994
endif
ifneq ($(filter msm8996,$(TARGET_BOARD_PLATFORM)),)
  LOCAL_CFLAGS := -DPLATFORM_MSM8996
endif
ifneq ($(filter msm8998 apq8098_latv,$(TARGET_BOARD_PLATFORM)),)
  LOCAL_CFLAGS := -DPLATFORM_MSM8998
endif
ifneq ($(filter sdm845,$(TARGET_BOARD_PLATFORM)),)
  LOCAL_CFLAGS := -DPLATFORM_SDM845
endif
ifneq ($(filter sdm710,$(TARGET_BOARD_PLATFORM)),)
  LOCAL_CFLAGS := -DPLATFORM_SDM710
endif
ifneq ($(filter qcs605,$(TARGET_BOARD_PLATFORM)),)
  LOCAL_CFLAGS := -DPLATFORM_QCS605
endif
ifneq ($(filter msmnile,$(TARGET_BOARD_PLATFORM)),)
  LOCAL_CFLAGS := -DPLATFORM_MSMNILE
endif
ifneq ($(filter $(MSMSTEPPE) ,$(TARGET_BOARD_PLATFORM)),)
  LOCAL_CFLAGS := -DPLATFORM_MSMSTEPPE
endif
ifneq ($(filter $(TRINKET) ,$(TARGET_BOARD_PLATFORM)),)
  LOCAL_CFLAGS := -DPLATFORM_TRINKET
endif
ifneq ($(filter kona,$(TARGET_BOARD_PLATFORM)),)
  LOCAL_CFLAGS := -DPLATFORM_KONA
endif
endif

ifneq ($(filter msm8916 msm8909 msm8952 msm8937 thorium msm8953 msmgold sdm660,$(TARGET_BOARD_PLATFORM)),)
  AUDIO_PLATFORM = msm8916
  MULTIPLE_HW_VARIANTS_ENABLED := true
  LOCAL_CFLAGS := -DPLATFORM_MSM8916
ifneq ($(filter msm8909,$(TARGET_BOARD_PLATFORM)),)
  LOCAL_CFLAGS := -DPLATFORM_MSM8909
endif
ifneq ($(filter sdm660,$(TARGET_BOARD_PLATFORM)),)
  LOCAL_CFLAGS := -DPLATFORM_MSMFALCON
endif
endif

ifeq ($(TARGET_BOARD_AUTO),true)
  LOCAL_CFLAGS += -DPLATFORM_AUTO
endif

LOCAL_CFLAGS += -Wno-macro-redefined

LOCAL_HEADER_LIBRARIES := libhardware_headers

LOCAL_SRC_FILES := \
	audio_hw.c \
	voice.c \
	platform_info.c \
	$(AUDIO_PLATFORM)/platform.c \
        acdb.c

LOCAL_SRC_FILES += audio_extn/audio_extn.c \
                   audio_extn/utils.c
LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/techpack/audio/include
LOCAL_ADDITIONAL_DEPENDENCIES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_DLKM)),true)
  LOCAL_HEADER_LIBRARIES += audio_kernel_headers
  LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/vendor/qcom/opensource/audio-kernel/include
  LOCAL_ADDITIONAL_DEPENDENCIES += $(BOARD_VENDOR_KERNEL_MODULES)
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_EXTENDED_COMPRESS_FORMAT)),true)
  LOCAL_CFLAGS += -DENABLE_EXTENDED_COMPRESS_FORMAT
endif

LOCAL_CFLAGS += -DUSE_VENDOR_EXTN

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_HDMI_EDID)),true)
    LOCAL_CFLAGS += -DHDMI_EDID
    LOCAL_SRC_FILES += edid.c
endif

ifeq ($(strip $(AUDIO_USE_LL_AS_PRIMARY_OUTPUT)),true)
    LOCAL_CFLAGS += -DUSE_LL_AS_PRIMARY_OUTPUT
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_PCM_OFFLOAD)),true)
    LOCAL_CFLAGS += -DPCM_OFFLOAD_ENABLED
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_ANC_HEADSET)),true)
    LOCAL_CFLAGS += -DANC_HEADSET_ENABLED
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_HIFI_AUDIO)),true)
    LOCAL_CFLAGS += -DHIFI_AUDIO_ENABLED
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_RAS)),true)
    LOCAL_CFLAGS += -DRAS_ENABLED
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_VBAT_MONITOR)),true)
    LOCAL_CFLAGS += -DVBAT_MONITOR_ENABLED
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_FLUENCE)),true)
    LOCAL_CFLAGS += -DFLUENCE_ENABLED
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_PROXY_DEVICE)),true)
    LOCAL_CFLAGS += -DAFE_PROXY_ENABLED
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_KPI_OPTIMIZE)),true)
    LOCAL_CFLAGS += -DKPI_OPTIMIZE_ENABLED
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_FM_POWER_OPT)),true)
    LOCAL_CFLAGS += -DFM_POWER_OPT
    LOCAL_SRC_FILES += audio_extn/fm.c
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_USB_TUNNEL_AUDIO)),true)
    LOCAL_CFLAGS += -DUSB_HEADSET_ENABLED
    LOCAL_SRC_FILES += audio_extn/usb.c
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_HFP)),true)
    LOCAL_CFLAGS += -DHFP_ENABLED
    LOCAL_SRC_FILES += audio_extn/hfp.c
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_CUSTOMSTEREO)),true)
    LOCAL_CFLAGS += -DCUSTOM_STEREO_ENABLED
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_SSR)),true)
    LOCAL_CFLAGS += -DSSR_ENABLED
    LOCAL_SRC_FILES += audio_extn/ssr.c
    LOCAL_C_INCLUDES += $(TARGET_OUT_HEADERS)/mm-audio/surround_sound_3mic/
    LOCAL_C_INCLUDES += $(TARGET_OUT_HEADERS)/common/inc/
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_MULTI_VOICE_SESSIONS)),true)
    LOCAL_CFLAGS += -DMULTI_VOICE_SESSION_ENABLED
    LOCAL_SRC_FILES += voice_extn/voice_extn.c

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_INCALL_MUSIC)),true)
    LOCAL_CFLAGS += -DINCALL_MUSIC_ENABLED
endif
ifeq ($(strip $(AUDIO_FEATURE_ENABLED_COMPRESS_VOIP)),true)
    LOCAL_CFLAGS += -DCOMPRESS_VOIP_ENABLED
    LOCAL_SRC_FILES += voice_extn/compress_voip.c
endif

endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_EXTN_FORMATS)),true)
LOCAL_CFLAGS += -DAUDIO_EXTN_FORMATS_ENABLED
endif


ifeq ($(strip $(AUDIO_FEATURE_ENABLED_SPKR_PROTECTION)),true)
    LOCAL_CFLAGS += -DSPKR_PROT_ENABLED
    LOCAL_SRC_FILES += audio_extn/spkr_protection.c
endif

ifdef MULTIPLE_HW_VARIANTS_ENABLED
  LOCAL_CFLAGS += -DHW_VARIANTS_ENABLED
  LOCAL_SRC_FILES += $(AUDIO_PLATFORM)/hw_info.c
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_COMPRESS_CAPTURE)),true)
    LOCAL_CFLAGS += -DCOMPRESS_CAPTURE_ENABLED
    LOCAL_SRC_FILES += audio_extn/compress_capture.c
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_DTS_EAGLE)),true)
    LOCAL_CFLAGS += -DDTS_EAGLE
    LOCAL_SRC_FILES += audio_extn/dts_eagle.c
endif

ifeq ($(strip $(DOLBY_DDP)),true)
    LOCAL_CFLAGS += -DDS1_DOLBY_DDP_ENABLED
    LOCAL_SRC_FILES += audio_extn/dolby.c
endif

ifeq ($(strip $(DS1_DOLBY_DAP)),true)
    LOCAL_CFLAGS += -DDS1_DOLBY_DAP_ENABLED
ifneq ($(strip $(DOLBY_DDP)),true)
    LOCAL_SRC_FILES += audio_extn/dolby.c
endif
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_FLAC_OFFLOAD)),true)
    LOCAL_CFLAGS += -DFLAC_OFFLOAD_ENABLED
    LOCAL_CFLAGS += -DCOMPRESS_METADATA_NEEDED
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_EXTN_FLAC_DECODER)),true)
    LOCAL_CFLAGS += -DFLAC_OFFLOAD_ENABLED
    LOCAL_CFLAGS += -DCOMPRESS_METADATA_NEEDED
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_VORBIS_OFFLOAD)),true)
    LOCAL_CFLAGS += -DVORBIS_OFFLOAD_ENABLED
    LOCAL_CFLAGS += -DCOMPRESS_METADATA_NEEDED

endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_WMA_OFFLOAD)),true)
    LOCAL_CFLAGS += -DWMA_OFFLOAD_ENABLED
    LOCAL_CFLAGS += -DCOMPRESS_METADATA_NEEDED
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_ALAC_OFFLOAD)),true)
    LOCAL_CFLAGS += -DALAC_OFFLOAD_ENABLED
    LOCAL_CFLAGS += -DCOMPRESS_METADATA_NEEDED
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_APE_OFFLOAD)),true)
    LOCAL_CFLAGS += -DAPE_OFFLOAD_ENABLED
    LOCAL_CFLAGS += -DCOMPRESS_METADATA_NEEDED
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_PCM_OFFLOAD_24)),true)
       LOCAL_CFLAGS += -DPCM_OFFLOAD_ENABLED_24
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_AAC_ADTS_OFFLOAD)),true)
    LOCAL_CFLAGS += -DAAC_ADTS_OFFLOAD_ENABLED
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_DEV_ARBI)),true)
    LOCAL_CFLAGS += -DDEV_ARBI_ENABLED
    LOCAL_SRC_FILES += audio_extn/dev_arbi.c
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_RECORD_PLAY_CONCURRENCY)),true)
    LOCAL_CFLAGS += -DRECORD_PLAY_CONCURRENCY
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_ACDB_LICENSE)), true)
    LOCAL_CFLAGS += -DDOLBY_ACDB_LICENSE
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_DS2_DOLBY_DAP)),true)
    LOCAL_CFLAGS += -DDS2_DOLBY_DAP_ENABLED
    LOCAL_CFLAGS += -DDS1_DOLBY_DDP_ENABLED
ifneq ($(strip $(DOLBY_DDP)),true)
    ifneq ($(strip $(DS1_DOLBY_DAP)),true)
        LOCAL_SRC_FILES += audio_extn/dolby.c
    endif
endif
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_HDMI_PASSTHROUGH)),true)
    LOCAL_CFLAGS += -DHDMI_PASSTHROUGH_ENABLED
    LOCAL_SRC_FILES += audio_extn/passthru.c
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_KEEP_ALIVE)),true)
    LOCAL_CFLAGS += -DKEEP_ALIVE_ENABLED
    LOCAL_SRC_FILES += audio_extn/keep_alive.c
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_SOURCE_TRACKING)),true)
    LOCAL_CFLAGS += -DSOURCE_TRACKING_ENABLED
    LOCAL_SRC_FILES += audio_extn/source_track.c
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_SPLIT_A2DP)),true)
    LOCAL_CFLAGS += -DSPLIT_A2DP_ENABLED
    LOCAL_SRC_FILES += audio_extn/a2dp.c
endif

ifeq ($(strip $(AUDIO_FEATURE_IP_HDLR_ENABLED)),true)
    LOCAL_CFLAGS += -DAUDIO_EXTN_IP_HDLR_ENABLED
    LOCAL_SRC_FILES += audio_extn/ip_hdlr_intf.c
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_QAF)),true)
    LOCAL_CFLAGS += -DQAF_EXTN_ENABLED
    LOCAL_C_INCLUDES += $(TARGET_OUT_HEADERS)/mm-audio/qaf/
    LOCAL_SRC_FILES += audio_extn/qaf.c
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_COMPRESS_INPUT)),true)
    LOCAL_CFLAGS += -DCOMPRESS_INPUT_ENABLED
    LOCAL_SRC_FILES += audio_extn/compress_in.c
endif

ifeq ($(strip $(BOARD_SUPPORTS_QAHW)),true)
    LOCAL_CFLAGS += -DAUDIO_HW_EXTN_API_ENABLED
    LOCAL_SRC_FILES += audio_hw_extn_api.c
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_BT_HAL)),true)
    LOCAL_CFLAGS += -DAUDIO_EXTN_BT_HAL_ENABLED
    LOCAL_SRC_FILES += audio_extn/bt_hal.c
endif

LOCAL_SHARED_LIBRARIES := \
        liblog \
        libcutils \
        libtinyalsa \
        libhardware \
        libtinycompress \
        libaudioroute \
        libdl \
        libaudioutils \
        libexpat \
        libhidlbase \
        libprocessgroup

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_QAP)),true)
LOCAL_CFLAGS += -DQAP_EXTN_ENABLED -Wno-tautological-pointer-compare
LOCAL_SRC_FILES += audio_extn/qap.c
LOCAL_C_INCLUDES += $(TARGET_OUT_HEADERS)/mm-audio/qap_wrapper/
LOCAL_HEADER_LIBRARIES += audio_qaf_headers
LOCAL_SHARED_LIBRARIES += libqap_wrapper liblog
endif

ifneq ($(strip $(AUDIO_FEATURE_ENABLED_EXT_AMPLIFIER)),false)
    LOCAL_CFLAGS += -DEXT_AMPLIFIER_ENABLED
    LOCAL_SRC_FILES += audio_extn/audio_amplifier.c
endif

LOCAL_C_INCLUDES += \
	external/tinyalsa/include \
	external/tinycompress/include \
	system/media/audio_utils/include \
	external/expat/lib \
	hardware/libhardware/include \
	$(call include-path-for, audio-route) \
	$(call include-path-for, audio-effects) \
	$(LOCAL_PATH)/$(AUDIO_PLATFORM) \
	$(LOCAL_PATH)/audio_extn \
	$(LOCAL_PATH)/voice_extn

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_LISTEN)),true)
    LOCAL_CFLAGS += -DAUDIO_LISTEN_ENABLED
    LOCAL_C_INCLUDES += $(TARGET_OUT_HEADERS)/mm-audio/audio-listen
    LOCAL_SRC_FILES += audio_extn/listen.c
endif

ifeq ($(TARGET_COMPILE_WITH_MSM_KERNEL),true)
        LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/techpack/audio/include
        LOCAL_ADDITIONAL_DEPENDENCIES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_EXT_HDMI)),true)
    LOCAL_CFLAGS += -DAUDIO_EXTERNAL_HDMI_ENABLED
ifeq ($(strip $(AUDIO_FEATURE_ENABLED_HDMI_PASSTHROUGH)),true)
    LOCAL_C_INCLUDES += $(TARGET_OUT_HEADERS)/mm-audio/audio-parsers
    LOCAL_SHARED_LIBRARIES += libaudioparsers
endif
endif

ifeq ($(strip $(BOARD_SUPPORTS_SOUND_TRIGGER)),true)
    ST_FEATURE_ENABLE := true
endif

ifeq ($(strip $(BOARD_SUPPORTS_SOUND_TRIGGER_HAL)),true)
    ST_FEATURE_ENABLE := true
endif

ifeq ($(ST_FEATURE_ENABLE), true)
    LOCAL_CFLAGS += -DSOUND_TRIGGER_ENABLED
    LOCAL_CFLAGS += -DSOUND_TRIGGER_PLATFORM_NAME=$(TARGET_BOARD_PLATFORM)
    LOCAL_C_INCLUDES += $(TARGET_OUT_HEADERS)/mm-audio/sound_trigger
    LOCAL_SRC_FILES += audio_extn/soundtrigger.c
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_AUXPCM_BT)),true)
    LOCAL_CFLAGS += -DAUXPCM_BT_ENABLED
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_PM_SUPPORT)),true)
    LOCAL_CFLAGS += -DPM_SUPPORT_ENABLED
    LOCAL_SRC_FILES += audio_extn/pm.c
    LOCAL_C_INCLUDES += $(TARGET_OUT_HEADERS)/libperipheralclient/inc
    LOCAL_SHARED_LIBRARIES += libperipheral_client
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_DISPLAY_PORT)),true)
    LOCAL_CFLAGS += -DDISPLAY_PORT_ENABLED
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_GEF_SUPPORT)),true)
    LOCAL_CFLAGS += -DAUDIO_GENERIC_EFFECT_FRAMEWORK_ENABLED
    LOCAL_SRC_FILES += audio_extn/gef.c
endif

ifeq ($(strip $($AUDIO_FEATURE_ADSP_HDLR_ENABLED)),true)
    LOCAL_CFLAGS += -DAUDIO_EXTN_ADSP_HDLR_ENABLED
    LOCAL_SRC_FILES += audio_extn/adsp_hdlr.c
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_DYNAMIC_LOG)), true)
    LOCAL_CFLAGS += -DDYNAMIC_LOG_ENABLED
    LOCAL_C_INCLUDES += $(TARGET_OUT_HEADERS)/mm-audio/audio-log-utils
    LOCAL_SHARED_LIBRARIES += libaudio_log_utils
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_DYNAMIC_ECNS)),true)
    LOCAL_CFLAGS += -DDYNAMIC_ECNS_ENABLED
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_INSTANCE_ID)), true)
    LOCAL_CFLAGS += -DINSTANCE_ID_ENABLED
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_USB_BURST_MODE)), true)
    LOCAL_CFLAGS += -DUSB_BURST_MODE_ENABLED
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_BATTERY_LISTENER)), true)
    LOCAL_CFLAGS += -DBATTERY_LISTENER_ENABLED
    LOCAL_SRC_FILES += audio_extn/battery_listener.cpp
    LOCAL_SHARED_LIBRARIES += android.hardware.health@1.0 android.hardware.health@2.0 \
                              libbase libhidlbase \
                              libutils android.hardware.power@1.2
    LOCAL_STATIC_LIBRARIES := libhealthhalutils
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_KEEP_ALIVE_ARM_FFV)), true)
    LOCAL_CFLAGS += -DRUN_KEEP_ALIVE_IN_ARM_FFV
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_FFV)), true)
    LOCAL_CFLAGS += -DFFV_ENABLED
    LOCAL_C_INCLUDES += $(TARGET_OUT_HEADERS)/mm-audio-noship/include/ffv
    LOCAL_SRC_FILES += audio_extn/ffv.c
endif

ifeq ($(strip $(AUDIO_FEATURE_ELLIPTIC_ULTRASOUND_SUPPORT)),true)
    LOCAL_CFLAGS += -DELLIPTIC_ULTRASOUND_ENABLED
    LOCAL_SRC_FILES += audio_extn/ultrasound.c
endif

LOCAL_CFLAGS += -D_GNU_SOURCE
LOCAL_CFLAGS += -Wall -Werror
LOCAL_CLANG_CFLAGS += -Wno-unused-variable -Wno-unused-function -Wno-missing-field-initializers

LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_PATH)

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_SND_MONITOR)), true)
    LOCAL_CFLAGS += -DSND_MONITOR_ENABLED
    LOCAL_SRC_FILES += audio_extn/sndmonitor.c
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_EXT_HW_PLUGIN)),true)
    LOCAL_CFLAGS += -DEXT_HW_PLUGIN_ENABLED
    LOCAL_SRC_FILES += audio_extn/ext_hw_plugin.c
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_GCOV)),true)
    LOCAL_CFLAGS += --coverage -fprofile-arcs -ftest-coverage
    LOCAL_CPPFLAGS += --coverage -fprofile-arcs -ftest-coverage
    LOCAL_STATIC_LIBRARIES += libprofile_rt
endif

ifeq ($(strip $(AUDIO_FEATURE_ENABLED_A2DP_DECODERS)), true)
    LOCAL_CFLAGS += -DAPTX_DECODER_ENABLED
endif

LOCAL_MODULE := audio.primary.$(TARGET_BOARD_PLATFORM)

LOCAL_MODULE_RELATIVE_PATH := hw

LOCAL_MODULE_TAGS := optional

LOCAL_VENDOR_MODULE := true
LOCAL_CFLAGS += -DANDROID_PLATFORM_SDK_VERSION=$(PLATFORM_SDK_VERSION)

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
