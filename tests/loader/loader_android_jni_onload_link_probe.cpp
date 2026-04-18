#if __has_include(<jni.h>)
#include <jni.h>
#else
struct JavaVM;
using jint = int;
#endif

extern "C" jint JNI_OnLoad(JavaVM* vm, void* reserved);

extern "C" int vmp_loader_android_jni_probe() {
  return static_cast<int>(JNI_OnLoad(nullptr, nullptr));
}
