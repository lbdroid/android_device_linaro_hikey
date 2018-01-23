ifndef TARGET_KERNEL_USE
TARGET_KERNEL_USE=4.9
endif
TARGET_PREBUILT_KERNEL := device/linaro/hikey-kernel/Image.gz-hikey960-$(TARGET_KERNEL_USE)
TARGET_PREBUILT_DTB := device/linaro/hikey-kernel/hi3660-hikey960.dtb-$(TARGET_KERNEL_USE)

ifeq ($(TARGET_KERNEL_USE), 4.4)
  HIKEY_USE_LEGACY_TI_BLUETOOTH := true
else
  HIKEY_USE_LEGACY_TI_BLUETOOTH := false
endif

#
# Inherit the full_base and device configurations
$(call inherit-product, device/linaro/hikey/hikey960/device-hikey960.mk)
$(call inherit-product, device/linaro/hikey/device-common.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base.mk)

$(call inherit-product, packages/services/Car/car_product/build/car.mk)
PRODUCT_PACKAGE_OVERLAYS := packages/services/Car/car_product/overlay

PRODUCT_PACKAGES += vehicle.default \
	CarSettings \
	Launcher3 \
	tinymix \
	tinypcminfo \
	tinyhostless \
	android.hardware.automotive.vehicle@2.0 \
	android.hardware.automotive.vehicle@2.0-service \
	car-radio-service

PRODUCT_COPY_FILES += \
    device/generic/car/common/bootanimations/bootanimation-832.zip:system/media/bootanimation.zip \
    device/generic/car/common/android.hardware.dummy.xml:system/etc/permissions/handheld_core_hardware.xml \
    packages/services/Car/car_product/init/init.car.rc:root/init.car.rc \
    packages/services/Car/car_product/init/init.bootstat.rc:root/init.bootstat.rc

PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.type.automotive.xml:system/etc/permissions/android.hardware.type.automotive.xml \
    frameworks/native/data/etc/android.hardware.screen.landscape.xml:system/etc/permissions/android.hardware.screen.landscape.xml

#
# Overrides
PRODUCT_NAME := hikey960
PRODUCT_DEVICE := hikey960
PRODUCT_BRAND := Android
PRODUCT_MODEL := AOSP CAR on hikey960

PRODUCT_PROPERTY_OVERRIDES += \
    android.car.drawer.unlimited=true \
    android.car.hvac.demo=true \
    com.android.car.radio.demo=true \
    com.android.car.radio.demo.dual=true
