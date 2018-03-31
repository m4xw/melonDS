LOCAL_PATH := $(call my-dir)

CORE_FLAGS :=
MELON_DIR  := ../src
CORE_DIR   := $(MELON_DIR)/libretro

include ../Makefile.common

CORE_FLAGS += -D__LIBRETRO__ $(INCFLAGS)

include $(CLEAR_VARS)
LOCAL_MODULE    := retro
LOCAL_SRC_FILES    += $(SOURCES_C) $(SOURCES_CXX)
LOCAL_CFLAGS += $(CORE_FLAGS)
LOCAL_CPPFLAGS += -std=c++11 $(CORE_FLAGS)

include $(BUILD_SHARED_LIBRARY)
