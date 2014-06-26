ifeq ($(TARGET_ARCH),arm)


AENC_AAC_PATH:= $(call my-dir)

include $(AENC_AAC_PATH)/qdsp6/Android.mk

endif
