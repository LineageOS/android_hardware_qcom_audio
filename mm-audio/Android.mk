ifeq ($(strip $(BOARD_USES_ALSA_AUDIO)),true)
include $(call all-subdir-makefiles)
endif
