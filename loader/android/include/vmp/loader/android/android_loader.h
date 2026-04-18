#pragma once

#if __has_include(<jni.h>)
#include <jni.h>
#else
struct JavaVM;
using jint = int;
#ifndef JNI_VERSION_1_6
#define JNI_VERSION_1_6 0x00010006
#endif
#endif

namespace vmp::loader::android {

struct LoaderFacade {
  const char* status() const noexcept;
};

}  // namespace vmp::loader::android

extern "C" void vmp_android_init(void);
extern "C" jint JNI_OnLoad(JavaVM* vm, void* reserved);
