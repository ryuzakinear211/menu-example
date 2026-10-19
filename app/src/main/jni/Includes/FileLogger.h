#ifndef FILE_LOGGER_H
#define FILE_LOGGER_H

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <ctime>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <android/log.h>
#include <mutex>

class FileLogger {
private:
    static FILE* s_logFile;
    static std::mutex s_mutex;
    static char s_logPath[256];

    static void EnsureDirectory() {
        mkdir("/sdcard/Documents", 0777);
        mkdir("/storage/emulated/0/Documents", 0777);
    }

    static FILE* GetFileHandle() {
        if (s_logFile) return s_logFile;

        EnsureDirectory();

        // Try primary path
        const char* paths[] = {
            "/sdcard/Documents/mod_menu_debug.log",
            "/storage/emulated/0/Documents/mod_menu_debug.log",
            "/sdcard/mod_menu_debug.log"
        };

        for (const char* path : paths) {
            FILE* f = fopen(path, "a+");
            if (f) {
                s_logFile = f;
                strncpy(s_logPath, path, sizeof(s_logPath) - 1);
                return s_logFile;
            }
        }
        return nullptr;
    }

public:
    static void Log(int level, const char* tag, const char* format, ...) {
        std::lock_guard<std::mutex> lock(s_mutex);

        // Prepare timestamp
        char timeBuf[64];
        time_t now = time(nullptr);
        struct tm* tmInfo = localtime(&now);
        strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", tmInfo);

        const char* levelStr = "INFO";
        android_LogPriority prio = ANDROID_LOG_INFO;
        switch (level) {
            case 3: levelStr = "DEBUG"; prio = ANDROID_LOG_DEBUG; break;
            case 4: levelStr = "INFO";  prio = ANDROID_LOG_INFO;  break;
            case 5: levelStr = "WARN";  prio = ANDROID_LOG_WARN;  break;
            case 6: levelStr = "ERROR"; prio = ANDROID_LOG_ERROR; break;
            case 7: levelStr = "FATAL"; prio = ANDROID_LOG_FATAL; break;
        }

        // Format message
        char msgBuf[1024];
        va_list args;
        va_start(args, format);
        vsnprintf(msgBuf, sizeof(msgBuf), format, args);
        va_end(args);

        // 1. Android Logcat output
        __android_log_write(prio, tag ? tag : "ModMenu", msgBuf);

        // 2. File output
        FILE* fp = GetFileHandle();
        if (fp) {
            fprintf(fp, "[%s] [%s] [%s] %s\n", timeBuf, levelStr, tag ? tag : "ModMenu", msgBuf);
            fflush(fp);
        }
    }
};

// Inline static member initializations
inline FILE* FileLogger::s_logFile = nullptr;
inline std::mutex FileLogger::s_mutex;
inline char FileLogger::s_logPath[256] = {0};

#define FILE_LOGD(...) FileLogger::Log(3, "ModMenu", __VA_ARGS__)
#define FILE_LOGI(...) FileLogger::Log(4, "ModMenu", __VA_ARGS__)
#define FILE_LOGW(...) FileLogger::Log(5, "ModMenu", __VA_ARGS__)
#define FILE_LOGE(...) FileLogger::Log(6, "ModMenu", __VA_ARGS__)
#define FILE_LOGF(...) FileLogger::Log(7, "ModMenu", __VA_ARGS__)

#endif // FILE_LOGGER_H
