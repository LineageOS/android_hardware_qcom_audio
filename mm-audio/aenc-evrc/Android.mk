ifeq ($(TARGET_ARCH),arm)


AENC_EVRC_PATH:= $(call my-dir)

include $(AENC_EVRC_PATH)/qdsp6/Android.mk

endif
