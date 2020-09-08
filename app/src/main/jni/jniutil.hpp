#ifndef JNIUTIL_HPP
#define JNIUTIL_HPP

#include <string>
#include <jni.h>

inline std::string getStringOrEmpty(JNIEnv *env, jstring s) {
    if (s == nullptr) {
        return "";
    }
    const char *cstr = env->GetStringUTFChars(s, nullptr);
    std::string result(cstr);
    env->ReleaseStringUTFChars(s, cstr);
    return result;
}

#endif