// JNI surface of the native core. Keep this thin: parsing and validation
// live in keys.cpp / nsp.cpp / import_check.cpp (also built and tested on the
// host).
#include <android/log.h>
#include <android/native_window_jni.h>
#include <jni.h>

#include <cstdio>
#include <sstream>
#include <string>

#include "import_check.h"
#include "keys.h"
#include "nsp.h"
#include "vulkan_present.h"

namespace {

constexpr const char* kTag = "nxnative-android";

jstring toJava(JNIEnv* env, const std::string& text) {
    return env->NewStringUTF(text.c_str());
}

std::string jsonString(const std::string& value) {
    std::string out;
    for (char c : value) {
        if (c == '"' || c == '\\') out += '\\';
        if (static_cast<unsigned char>(c) >= 0x20) out += c;
    }
    return out;
}

}  // namespace

extern "C" {

JNIEXPORT jstring JNICALL
Java_org_nx2a_launcher_Native_nativeVersion(JNIEnv* env, jobject /*thiz*/) {
    return toJava(env, "nxnative-android core 0.1.0");
}

JNIEXPORT jstring JNICALL
Java_org_nx2a_launcher_Native_inspectNsp(JNIEnv* env, jobject /*thiz*/, jint fd) {
    nx2a::NspInfo info;
    nx2a::KeySet empty;
    nx2a::inspectNspFd(static_cast<int>(fd), &info);
    __android_log_print(ANDROID_LOG_INFO, kTag, "NSP inspect: code=%s title=%s gen=%d",
                        info.code.c_str(), info.titleId.c_str(), info.keyGeneration);
    return toJava(env, nx2a::analyzeImport(info, empty));
}

JNIEXPORT jstring JNICALL
Java_org_nx2a_launcher_Native_loadKeys(JNIEnv* env, jobject /*thiz*/, jstring path) {
    const char* raw = env->GetStringUTFChars(path, nullptr);
    std::string error;
    nx2a::KeySet keys;
    const bool ok = keys.loadFile(raw ? raw : "", &error);
    if (raw) env->ReleaseStringUTFChars(path, raw);

    std::ostringstream j;
    j << "{\"ok\":" << (ok ? "true" : "false") << ",\"count\":" << keys.size()
      << ",\"maxGeneration\":" << keys.maxGeneration("master_key")
      << ",\"error\":\"" << jsonString(error) << "\"}";
    // Names and counts only: never a key value.
    __android_log_print(ANDROID_LOG_INFO, kTag, "keys loaded=%d count=%zu", ok ? 1 : 0,
                        keys.size());
    return toJava(env, j.str());
}

JNIEXPORT jstring JNICALL
Java_org_nx2a_launcher_Native_analyzeImport(JNIEnv* env, jobject /*thiz*/, jint fd,
                                            jstring keysPath) {
    nx2a::NspInfo info;
    nx2a::inspectNspFd(static_cast<int>(fd), &info);

    const char* raw = env->GetStringUTFChars(keysPath, nullptr);
    std::string error;
    nx2a::KeySet keys;
    const std::string path = raw ? raw : "";
    if (raw) env->ReleaseStringUTFChars(keysPath, raw);
    if (!path.empty()) keys.loadFile(path, &error);

    return toJava(env, nx2a::analyzeImport(info, keys));
}

JNIEXPORT jstring JNICALL
Java_org_nx2a_launcher_Native_rendererStart(JNIEnv* env, jobject /*thiz*/, jobject surface,
                                            jint width, jint height) {
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    std::string error;
    const bool ok = nx2a::rendererStart(window, width, height, &error);
    // rendererStart keeps its own ANativeWindow reference.
    if (window) ANativeWindow_release(window);
    std::ostringstream j;
    j << "{\"ok\":" << (ok ? "true" : "false") << ",\"error\":\"" << jsonString(error)
      << "\"}";
    __android_log_print(ANDROID_LOG_INFO, kTag, "renderer start ok=%d %s", ok ? 1 : 0,
                        error.c_str());
    return toJava(env, j.str());
}

JNIEXPORT void JNICALL
Java_org_nx2a_launcher_Native_rendererStop(JNIEnv* /*env*/, jobject /*thiz*/) {
    nx2a::rendererStop();
}

JNIEXPORT jstring JNICALL
Java_org_nx2a_launcher_Native_rendererStats(JNIEnv* env, jobject /*thiz*/) {
    return toJava(env, nx2a::rendererStatsJson());
}

JNIEXPORT void JNICALL
Java_org_nx2a_launcher_Native_rendererInput(JNIEnv* /*env*/, jobject /*thiz*/, jint action,
                                            jfloat x, jfloat y, jint code) {
    nx2a::rendererInput(static_cast<int>(action), x, y, static_cast<int>(code));
}

}  // extern "C"
