ifneq ($(strip $(AUDIO_FEATURE_DISABLED_EXTN_MM_AUDIO)),true)
include $(call all-subdir-makefiles)
endif
