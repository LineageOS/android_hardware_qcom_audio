#BOARD_USES_GENERIC_AUDIO := true
#
#AUDIO_FEATURE_FLAGS
BOARD_USES_ALSA_AUDIO := true

ifneq ($(TARGET_USES_AOSP), true)
USE_CUSTOM_AUDIO_POLICY := 1
AUDIO_FEATURE_ENABLED_COMPRESS_CAPTURE := false
AUDIO_FEATURE_ENABLED_COMPRESS_VOIP := true
AUDIO_FEATURE_ENABLED_EXTN_FORMATS := true
AUDIO_FEATURE_ENABLED_EXTN_FLAC_DECODER := true
AUDIO_FEATURE_ENABLED_EXTN_RESAMPLER := true
AUDIO_FEATURE_ENABLED_FM_POWER_OPT := true
AUDIO_FEATURE_ENABLED_HDMI_SPK := true
AUDIO_FEATURE_ENABLED_PCM_OFFLOAD := true
AUDIO_FEATURE_ENABLED_PCM_OFFLOAD_24 := true
AUDIO_FEATURE_ENABLED_FLAC_OFFLOAD := true
AUDIO_FEATURE_ENABLED_VORBIS_OFFLOAD := true
AUDIO_FEATURE_ENABLED_WMA_OFFLOAD := true
AUDIO_FEATURE_ENABLED_ALAC_OFFLOAD := true
AUDIO_FEATURE_ENABLED_APE_OFFLOAD := true
AUDIO_FEATURE_ENABLED_AAC_ADTS_OFFLOAD := true
AUDIO_FEATURE_ENABLED_PROXY_DEVICE := true
AUDIO_FEATURE_ENABLED_SSR := true
AUDIO_FEATURE_ENABLED_DTS_EAGLE := false
BOARD_USES_SRS_TRUEMEDIA := false
DTS_CODEC_M_ := false
MM_AUDIO_ENABLED_SAFX := true
AUDIO_FEATURE_ENABLED_HW_ACCELERATED_EFFECTS := false
AUDIO_FEATURE_ENABLED_AUDIOSPHERE := true
AUDIO_FEATURE_ENABLED_USB_TUNNEL_AUDIO := true
AUDIO_FEATURE_ENABLED_SPLIT_A2DP := true
AUDIO_FEATURE_ENABLED_3D_AUDIO := true
endif

USE_XML_AUDIO_POLICY_CONF := 1
BOARD_SUPPORTS_SOUND_TRIGGER_HAL := true
AUDIO_USE_LL_AS_PRIMARY_OUTPUT := true
AUDIO_FEATURE_ENABLED_VBAT_MONITOR := true
AUDIO_FEATURE_ENABLED_ANC_HEADSET := true
AUDIO_FEATURE_ENABLED_CUSTOMSTEREO := true
AUDIO_FEATURE_ENABLED_FLUENCE := true
AUDIO_FEATURE_ENABLED_HDMI_EDID := true
AUDIO_FEATURE_ENABLED_HDMI_PASSTHROUGH := true
#AUDIO_FEATURE_ENABLED_KEEP_ALIVE := true
AUDIO_FEATURE_ENABLED_DISPLAY_PORT := true
AUDIO_FEATURE_ENABLED_DS2_DOLBY_DAP := true
AUDIO_FEATURE_ENABLED_HFP := true
AUDIO_FEATURE_ENABLED_INCALL_MUSIC := false
AUDIO_FEATURE_ENABLED_MULTI_VOICE_SESSIONS := true
AUDIO_FEATURE_ENABLED_KPI_OPTIMIZE := true
AUDIO_FEATURE_ENABLED_SPKR_PROTECTION := true
AUDIO_FEATURE_ENABLED_ACDB_LICENSE := true
AUDIO_FEATURE_ENABLED_DEV_ARBI := false
MM_AUDIO_ENABLED_FTM := true
TARGET_USES_QCOM_MM_AUDIO := true
AUDIO_FEATURE_ENABLED_SOURCE_TRACKING := true
AUDIO_FEATURE_ENABLED_GEF_SUPPORT := true
BOARD_SUPPORTS_QAHW := true
AUDIO_FEATURE_ENABLED_RAS := true
##AUDIO_FEATURE_FLAGS

#Audio Specific device overlays
DEVICE_PACKAGE_OVERLAYS += hardware/qcom/audio/configs/common/overlay

# Audio configuration file
ifeq ($(TARGET_USES_AOSP), true)
PRODUCT_COPY_FILES += \
    device/qcom/common/media/audio_policy.conf:system/etc/audio_policy.conf
else
PRODUCT_COPY_FILES += \
    hardware/qcom/audio/configs/msm8998/audio_policy.conf:system/etc/audio_policy.conf
endif

PRODUCT_COPY_FILES += \
    hardware/qcom/audio/configs/msm8998/audio_output_policy.conf:system/vendor/etc/audio_output_policy.conf \
    hardware/qcom/audio/configs/msm8998/audio_effects.conf:system/vendor/etc/audio_effects.conf \
    hardware/qcom/audio/configs/msm8998/mixer_paths.xml:system/etc/mixer_paths.xml \
    hardware/qcom/audio/configs/msm8998/mixer_paths_tasha.xml:system/etc/mixer_paths_tasha.xml \
    hardware/qcom/audio/configs/msm8998/mixer_paths_tavil.xml:system/etc/mixer_paths_tavil.xml \
    hardware/qcom/audio/configs/msm8998/mixer_paths_skuk.xml:system/etc/mixer_paths_skuk.xml \
    hardware/qcom/audio/configs/msm8998/mixer_paths_skuk.xml:system/etc/mixer_paths_qvr.xml \
    hardware/qcom/audio/configs/msm8998/mixer_paths_dtp.xml:system/etc/mixer_paths_dtp.xml \
    hardware/qcom/audio/configs/msm8998/mixer_paths_i2s.xml:system/etc/mixer_paths_i2s.xml \
    hardware/qcom/audio/configs/msm8998/audio_tuning_mixer.txt:system/etc/audio_tuning_mixer.txt \
    hardware/qcom/audio/configs/msm8998/audio_tuning_mixer_tavil.txt:system/etc/audio_tuning_mixer_tavil.txt \
    hardware/qcom/audio/configs/msm8998/audio_platform_info_i2s.xml:system/etc/audio_platform_info_i2s.xml \
    hardware/qcom/audio/configs/msm8998/sound_trigger_mixer_paths.xml:system/etc/sound_trigger_mixer_paths.xml \
    hardware/qcom/audio/configs/msm8998/sound_trigger_mixer_paths_wcd9330.xml:system/etc/sound_trigger_mixer_paths_wcd9330.xml \
    hardware/qcom/audio/configs/msm8998/sound_trigger_mixer_paths_wcd9340.xml:system/etc/sound_trigger_mixer_paths_wcd9340.xml \
    hardware/qcom/audio/configs/msm8998/sound_trigger_platform_info.xml:system/etc/sound_trigger_platform_info.xml \
    hardware/qcom/audio/configs/msm8998/graphite_ipc_platform_info.xml:system/etc/graphite_ipc_platform_info.xml \
    hardware/qcom/audio/configs/msm8998/audio_platform_info.xml:system/etc/audio_platform_info.xml

#XML Audio configuration files
ifeq ($(USE_XML_AUDIO_POLICY_CONF), 1)
ifeq ($(TARGET_USES_AOSP), true)
PRODUCT_COPY_FILES += \
    $(TOPDIR)hardware/qcom/audio/configs/common/audio_policy_configuration.xml:/system/etc/audio_policy_configuration.xml
else
PRODUCT_COPY_FILES += \
    $(TOPDIR)hardware/qcom/audio/configs/msm8998/audio_policy_configuration.xml:system/etc/audio_policy_configuration.xml
endif
PRODUCT_COPY_FILES += \
    $(TOPDIR)frameworks/av/services/audiopolicy/config/a2dp_audio_policy_configuration.xml:/system/etc/a2dp_audio_policy_configuration.xml \
    $(TOPDIR)frameworks/av/services/audiopolicy/config/audio_policy_volumes.xml:/system/etc/audio_policy_volumes.xml \
    $(TOPDIR)frameworks/av/services/audiopolicy/config/default_volume_tables.xml:/system/etc/default_volume_tables.xml \
    $(TOPDIR)frameworks/av/services/audiopolicy/config/r_submix_audio_policy_configuration.xml:/system/etc/r_submix_audio_policy_configuration.xml \
    $(TOPDIR)frameworks/av/services/audiopolicy/config/usb_audio_policy_configuration.xml:/system/etc/usb_audio_policy_configuration.xml
endif

# Listen configuration file
PRODUCT_COPY_FILES += \
    hardware/qcom/audio/configs/msm8998/listen_platform_info.xml:system/etc/listen_platform_info.xml

# Reduce client buffer size for fast audio output tracks
PRODUCT_PROPERTY_OVERRIDES += \
    af.fast_track_multiplier=1

# Low latency audio buffer size in frames
PRODUCT_PROPERTY_OVERRIDES += \
    audio_hal.period_size=192

##fluencetype can be "fluence" or "fluencepro" or "none"
PRODUCT_PROPERTY_OVERRIDES += \
ro.qc.sdk.audio.fluencetype=none\
persist.audio.fluence.voicecall=true\
persist.audio.fluence.voicerec=false\
persist.audio.fluence.speaker=true

#disable tunnel encoding
PRODUCT_PROPERTY_OVERRIDES += \
tunnel.audio.encode=false

#Disable RAS Feature by default
PRODUCT_PROPERTY_OVERRIDES += \
persist.audio.ras.enabled=false

#Buffer size in kbytes for compress offload playback
PRODUCT_PROPERTY_OVERRIDES += \
audio.offload.buffer.size.kb=32

#Enable offload audio video playback by default
PRODUCT_PROPERTY_OVERRIDES += \
audio.offload.video=true

#Enable 16 bit PCM offload by default
PRODUCT_PROPERTY_OVERRIDES += \
audio.offload.pcm.16bit.enable=true

#Enable 24 bit PCM offload by default
PRODUCT_PROPERTY_OVERRIDES += \
audio.offload.pcm.24bit.enable=true

#Enable audio track offload by default
PRODUCT_PROPERTY_OVERRIDES += \
audio.offload.track.enable=true

#Enable music through deep buffer
PRODUCT_PROPERTY_OVERRIDES += \
audio.deep_buffer.media=true

#QC property used when calculating client heap size in audio flinger
PRODUCT_PROPERTY_OVERRIDES += \
audio.heap.size.multiplier=7

#enable voice path for PCM VoIP by default
PRODUCT_PROPERTY_OVERRIDES += \
use.voice.path.for.pcm.voip=true

#Enable multi channel aac through offload
PRODUCT_PROPERTY_OVERRIDES += \
audio.offload.multiaac.enable=true

#Enable DS2, Hardbypass feature for Dolby
PRODUCT_PROPERTY_OVERRIDES += \
audio.dolby.ds2.enabled=true\
audio.dolby.ds2.hardbypass=true

#Disable Multiple offload sesison
PRODUCT_PROPERTY_OVERRIDES += \
audio.offload.multiple.enabled=false

#Disable Compress passthrough playback
PRODUCT_PROPERTY_OVERRIDES += \
audio.offload.passthrough=false

#Disable surround sound recording
PRODUCT_PROPERTY_OVERRIDES += \
ro.qc.sdk.audio.ssr=false

#enable dsp gapless mode by default
PRODUCT_PROPERTY_OVERRIDES += \
audio.offload.gapless.enabled=true

#enable pbe effects
PRODUCT_PROPERTY_OVERRIDES += \
audio.safx.pbe.enabled=true

#parser input buffer size(256kb) in byte stream mode
PRODUCT_PROPERTY_OVERRIDES += \
audio.parser.ip.buffer.size=262144

#flac sw decoder 24 bit decode capability
PRODUCT_PROPERTY_OVERRIDES += \
flac.sw.decoder.24bit.support=true

#split a2dp DSP supported encoder list
PRODUCT_PROPERTY_OVERRIDES += \
persist.bt.a2dp_offload_cap=sbc-aptx-aptxhd-aac

#enable software decoders for ALAC and APE
PRODUCT_PROPERTY_OVERRIDES += \
use.qti.sw.alac.decoder=true
PRODUCT_PROPERTY_OVERRIDES += \
use.qti.sw.ape.decoder=true

#enable hw aac encoder by default
PRODUCT_PROPERTY_OVERRIDES += \
qcom.hw.aac.encoder=true

#Disable FM a2dp concurrency
PRODUCT_PROPERTY_OVERRIDES += \
fm.a2dp.conc.disabled=true

#audio becoming noisy intent broadcast delay
PRODUCT_PROPERTY_OVERRIDES += \
audio.noisy.broadcast.delay=600

#offload pausetime out duration to 3 secs to inline with other outputs
PRODUCT_PROPERTY_OVERRIDES += \
audio.offload.pstimeout.secs=3

#set adm buffering to 3 ms
PRODUCT_PROPERTY_OVERRIDES += \
audio.adm.buffering.ms=3
