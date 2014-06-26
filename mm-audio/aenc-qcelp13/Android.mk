ifeq ($(TARGET_ARCH),arm)


AENC_QCELP13_PATH:= $(call my-dir)

include $(AENC_QCELP13_PATH)/qdsp6/Android.mk

endif
