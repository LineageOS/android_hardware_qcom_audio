ifeq ($(TARGET_ARCH),arm)


AENC_AMR_PATH:= $(call my-dir)

ifeq ($(call is-board-platform,msm8660),true)
include $(AENC_AMR_PATH)/qdsp6/Android.mk
endif
ifeq ($(call is-board-platform,msm8960),true)
include $(AENC_AMR_PATH)/qdsp6/Android.mk
endif
ifeq ($(call is-board-platform,copper),true)
include $(AENC_AMR_PATH)/qdsp6/Android.mk
endif

endif
