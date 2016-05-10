#Audio Specific device overlays
DEVICE_PACKAGE_OVERLAYS += hardware/qcom/audio/configs/common/overlay
#enable software decoders for ALAC and APE
PRODUCT_PROPERTY_OVERRIDES += \
use.qti.sw.alac.decoder=true
PRODUCT_PROPERTY_OVERRIDES += \
use.qti.sw.ape.decoder=true
