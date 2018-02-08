LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
LOCAL_MODULE := swid
LOCAL_LDLIBS := -llog
LOCAL_SRC_FILES := swid.c
include $(BUILD_EXECUTABLE)

