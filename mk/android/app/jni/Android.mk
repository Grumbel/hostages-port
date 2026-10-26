LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
LOCAL_MODULE := main
LOCAL_SRC_FILES := \
  src/main.cpp \
  src/amiga/machine.cpp \
  src/amiga/custom.cpp \
  src/amiga/cia.cpp \
  src/amiga/disk.cpp \
  src/amiga/hle.cpp \
  src/m68k/cpu.cpp
LOCAL_C_INCLUDES := $(LOCAL_PATH)/src $(LOCAL_PATH)/include
LOCAL_CFLAGS := -DHOSTAGES_VERSION=\"$(HOSTAGES_VERSION)\" -DHOSTAGES_DATADIR=\".\"
LOCAL_CPPFLAGS := -std=c++20
LOCAL_SHARED_LIBRARIES := SDL2
LOCAL_LDLIBS := -llog -landroid
include $(BUILD_SHARED_LIBRARY)
