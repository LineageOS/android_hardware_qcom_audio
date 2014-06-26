ifeq ($(TARGET_ARCH),arm)


AENC_AMR_PATH:= $(call my-dir)

include $(AENC_AMR_PATH)/qdsp6/Android.mk


endif
