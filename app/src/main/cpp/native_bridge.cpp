// JNI surface of the native core. Keep this thin: parsing and validation
// live in keys.cpp / nsp.cpp / import_check.cpp (also built and tested on the
// host).
#include <android/log.h>
#include <android/native_window_jni.h>
#include <fcntl.h>
#include <jni.h>
#include <unistd.h>

#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

#include "content_check.h"
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
Java_org_nx2a_launcher_Native_verifyContentKeys(JNIEnv* env, jobject /*thiz*/, jstring nspPath,
                                                jstring keysPath) {
    const char* rawNsp = env->GetStringUTFChars(nspPath, nullptr);
    const char* rawKeys = env->GetStringUTFChars(keysPath, nullptr);
    const std::string nsp = rawNsp ? rawNsp : "";
    const std::string keysPathStr = rawKeys ? rawKeys : "";
    if (rawNsp) env->ReleaseStringUTFChars(nspPath, rawNsp);
    if (rawKeys) env->ReleaseStringUTFChars(keysPath, rawKeys);

    nx2a::KeySet keys;
    std::string error;
    if (!keys.loadFile(keysPathStr, &error)) {
        nx2a::ContentCheck check;
        check.code = "ERR_KEYS";
        check.error = error;
        return toJava(env, nx2a::contentCheckJson(check));
    }
    const nx2a::ContentCheck check = nx2a::checkContentKeys(nsp, keys);
    __android_log_print(ANDROID_LOG_INFO, kTag,
                        "content keys: ok=%d titleKey=%d header=%d magic=%s type=%d",
                        check.ok ? 1 : 0, check.titleKeyOk ? 1 : 0, check.headerOk ? 1 : 0,
                        check.headerMagic.c_str(), check.contentType);
    return toJava(env, nx2a::contentCheckJson(check));
}

// Copies one byte range of the already-open NSP into a file. Kotlin drives the
// loop (chunk by chunk), so progress, cancellation and resume stay in one
// place. Returns the number of bytes copied, or -1.
JNIEXPORT jlong JNICALL
Java_org_nx2a_launcher_Native_copyRange(JNIEnv* env, jobject /*thiz*/, jint fd, jlong srcOffset,
                                        jlong length, jstring destPath, jboolean append) {
    const char* raw = env->GetStringUTFChars(destPath, nullptr);
    const std::string path = raw ? raw : "";
    if (raw) env->ReleaseStringUTFChars(destPath, raw);
    if (path.empty() || length < 0) return -1;

    int flags = O_WRONLY | O_CREAT;
    flags |= append ? O_APPEND : O_TRUNC;
    const int out = ::open(path.c_str(), flags, 0600);
    if (out < 0) return -1;

    std::vector<std::uint8_t> buffer(1u << 20);
    std::int64_t done = 0;
    std::int64_t remaining = length;
    while (remaining > 0) {
        const std::size_t want =
            static_cast<std::size_t>(remaining < static_cast<std::int64_t>(buffer.size())
                                         ? remaining
                                         : static_cast<std::int64_t>(buffer.size()));
        const ssize_t got = ::pread(fd, buffer.data(), want,
                                    static_cast<off_t>(srcOffset + done));
        if (got <= 0) {
            ::close(out);
            return -1;
        }
        std::size_t written = 0;
        while (written < static_cast<std::size_t>(got)) {
            const ssize_t n = ::write(out, buffer.data() + written,
                                      static_cast<std::size_t>(got) - written);
            if (n <= 0) {
                ::close(out);
                return -1;
            }
            written += static_cast<std::size_t>(n);
        }
        done += got;
        remaining -= got;
    }
    // No fsync per chunk: Kotlin writes ~450 chunks for a 7 GB entry and
    // resume tolerates a partial tail. The file is closed (flushed) here.
    ::close(out);
    return static_cast<jlong>(done);
}

JNIEXPORT jstring JNICALL
Java_org_nx2a_launcher_Native_rendererStart(JNIEnv* env, jobject /*thiz*/, jobject surface,
                                            jint width, jint height, jstring hookDir,
                                            jstring driverDir, jstring driverName,
                                            jstring tmpDir) {
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    auto text = [env](jstring value) {
        const char* raw = value ? env->GetStringUTFChars(value, nullptr) : nullptr;
        std::string out = raw ? raw : "";
        if (raw) env->ReleaseStringUTFChars(value, raw);
        return out;
    };
    const std::string hook = text(hookDir);
    const std::string driver = text(driverDir);
    const std::string name = text(driverName);
    const std::string tmp = text(tmpDir);
    std::string error;
    const bool ok = nx2a::rendererStart(window, width, height, hook, driver, name, tmp, &error);
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
