# Copyright (C) 2012 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := audio.usb.$(TARGET_BOARD_PLATFORM)
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_PROPRIETARY_MODULE := true
LOCAL_SRC_FILES := \
	audio_hal.c \
	webrtc_wrapper.cpp
LOCAL_C_INCLUDES += \
	external/tinyalsa/include \
	$(call include-path-for, audio-utils) \
	$(call include-path-for, alsa-utils)
LOCAL_SHARED_LIBRARIES := liblog libcutils libtinyalsa libaudioutils libalsautils libwebrtc_audio_preprocessing
LOCAL_MODULE_TAGS := optional
LOCAL_CFLAGS := -Wno-unused-parameter -DWEBRTC_POSIX -DWEBRTC_LINUX -DWEBRTC_THREAD_RR -DWEBRTC_CLOCK_TYPE_REALTIME -DWEBRTC_ANDROID

LOCAL_HEADER_LIBRARIES += libhardware_headers libwebrtc_headers
include $(BUILD_SHARED_LIBRARY)

