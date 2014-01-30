ifeq ($(TARGET_ARCH),arm)


AENC_EVRC_PATH:= $(call my-dir)

ifeq ($(call is-board-platform,msm8660),true)
include $(AENC_EVRC_PATH)/qdsp6/Android.mk
endif
ifeq ($(call is-board-platform,msm8960),true)
include $(AENC_EVRC_PATH)/qdsp6/Android.mk
endif
ifeq ($(call is-board-platform,msm8974),true)
include $(AENC_EVRC_PATH)/qdsp6/Android.mk
endif
ifeq ($(call is-board-platform,msm8226),true)
include $(AENC_EVRC_PATH)/qdsp6/Android.mk
endif
ifeq ($(call is-board-platform,msm8610),true)
include $(AENC_EVRC_PATH)/qdsp6/Android.mk
endif
ifeq ($(call is-board-platform,apq8084),true)
include $(AENC_EVRC_PATH)/qdsp6/Android.mk
endif
ifeq ($(call is-board-platform,mpq8092),true)
include $(AENC_EVRC_PATH)/qdsp6/Android.mk
endif
ifeq ($(call is-board-platform,msm8916),true)
include $(AENC_EVRC_PATH)/qdsp6/Android.mk
endif

endif
