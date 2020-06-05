#ifndef LOGGING_HPP
#define LOGGING_HPP

#ifdef ANDROID

#include <android/log.h>
#define ANDROID_LOG_TAG "VioTesterNative"
#define log_debug(...) ((void)__android_log_print(ANDROID_LOG_DEBUG, ANDROID_LOG_TAG, __VA_ARGS__))
#define log_info(...) ((void)__android_log_print(ANDROID_LOG_INFO, ANDROID_LOG_TAG, __VA_ARGS__))
#define log_warn(...) ((void)__android_log_print(ANDROID_LOG_WARN, ANDROID_LOG_TAG, __VA_ARGS__))
#define log_error(...) ((void)__android_log_print(ANDROID_LOG_ERROR, ANDROID_LOG_TAG, __VA_ARGS__))

#elif defined(USE_LOGURU)

#include <loguru.hpp>

#define log_debug(fmt, ...) (LOG_F(1, fmt, ## __VA_ARGS__))
#define log_info(fmt, ...) (LOG_F(INFO, fmt, ## __VA_ARGS__))
#define log_warn(fmt, ...) (LOG_F(WARNING, fmt, ## __VA_ARGS__))
#define log_error(fmt, ...) (LOG_F(ERROR, fmt, ## __VA_ARGS__))

#else

// ## is a "gcc" hack for allowing empty __VA_ARGS__
// https://stackoverflow.com/questions/5891221/variadic-macros-with-zero-arguments
#define log_debug(fmt, ...) ((void)printf(fmt"\n", ## __VA_ARGS__))
#define log_info(fmt, ...) ((void)printf(fmt"\n", ## __VA_ARGS__))
#define log_warn(fmt, ...) ((void)printf(fmt"\n", ## __VA_ARGS__))
#define log_error(fmt, ...) ((void)printf(fmt"\n", ## __VA_ARGS__))

#endif

#endif // define LOGGING_HPP
