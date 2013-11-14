ifeq ($(TARGET_ARCH),arm)


AENC_QCELP13_PATH:= $(call my-dir)

ifeq ($(TARGET_BOARD_PLATFORM),msm8660)
include $(AENC_QCELP13_PATH)/qdsp6/Android.mk
endif
ifeq ($(TARGET_BOARD_PLATFORM),msm8960)
include $(AENC_QCELP13_PATH)/qdsp6/Android.mk
endif
ifeq ($(TARGET_BOARD_PLATFORM),msm8974)
include $(AENC_QCELP13_PATH)/qdsp6/Android.mk
endif

endif
