#include <list>
#include <vector>
#include <string.h>
#include <pthread.h>
#include <thread>
#include <cstring>
#include <jni.h>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <dlfcn.h>
#include "Includes/Logger.h"
#include "Includes/obfuscate.h"
#include "Includes/Utils.h"
#include "Includes/FileLogger.h"
#include "Includes/CrashSafety.h"
#include "KittyMemory/MemoryPatch.h"
#include "Menu/Setup.h"
#include "GTAPedPool.h"

// Default target library name (supports dynamic detection)
#define targetLibName OBFUSCATE("libbulletecho.so")

#include "Includes/Macros.h"

// Background thread monitoring library load
void *hack_thread(void *) {
    FILE_LOGI("hack_thread started: initializing crash safety and detecting game library...");

    // Install POSIX signal crash handlers immediately
    CrashSafety::Install();

    int waitSeconds = 0;
    while (!UnifiedGameScanner::Instance().DetectGame()) {
        sleep(1);
        waitSeconds++;
        if (waitSeconds % 5 == 0) {
            FILE_LOGD("Waiting for target game library (Bullet Echo / GTA SA)... [%d s]", waitSeconds);
        }
    }

    FILE_LOGI("Target library successfully detected! Active engine: %d",
              (int)UnifiedGameScanner::Instance().m_activeEngine);

    return NULL;
}

// JNI method to return live HUD status counts to Java UI
jintArray GetHudStatusData(JNIEnv *env, jobject thiz) {
    HudStats stats = UnifiedGameScanner::Instance().Scan();
    jintArray result = env->NewIntArray(7);
    if (!result) return nullptr;

    jint fill[7];
    fill[0] = stats.teamCount;
    fill[1] = stats.enemyCount;
    fill[2] = stats.botCount;
    fill[3] = stats.totalPeds;
    fill[4] = stats.isLibLoaded;
    fill[5] = stats.localPlayerHp;
    fill[6] = stats.gameEngine;

    env->SetIntArrayRegion(result, 0, 7, fill);
    return result;
}

// JNI method returning formatted string
jstring GetHudStatusFormatted(JNIEnv *env, jobject thiz) {
    HudStats stats = UnifiedGameScanner::Instance().Scan();
    char buf[256];
    snprintf(buf, sizeof(buf),
             "[%s] <font color='#00E676'>Team: %d</font> | "
             "<font color='#FF5252'>Enemy: %d</font> | "
             "<font color='#40C4FF'>Bots: %d</font> | "
             "<font color='#E0E0E0'>Total: %d</font>",
             stats.gameName, stats.teamCount, stats.enemyCount, stats.botCount, stats.totalPeds);
    return env->NewStringUTF(buf);
}

// JNI method returning active game name
jstring GetActiveGameName(JNIEnv *env, jobject thiz) {
    HudStats stats = UnifiedGameScanner::Instance().Scan();
    return env->NewStringUTF(stats.gameName);
}

// JNI method to log message from Java to /sdcard/Documents/mod_menu_debug.log
void LogToDebugFile(JNIEnv *env, jclass clazz, jstring tag, jstring msg) {
    const char *tagStr = tag ? env->GetStringUTFChars(tag, 0) : "Java";
    const char *msgStr = msg ? env->GetStringUTFChars(msg, 0) : "";

    FILE_LOGI("[%s] %s", tagStr, msgStr);

    if (tag) env->ReleaseStringUTFChars(tag, tagStr);
    if (msg) env->ReleaseStringUTFChars(msg, msgStr);
}

// LGL Mod Menu Features List
jobjectArray GetFeatureList(JNIEnv *env, jobject context) {
    jobjectArray ret;

    const char *features[13] = {
            OBFUSCATE("Category_HUD STATUS PANEL"),
            OBFUSCATE("100_Toggle_True_Show HUD Status Panel"),
            OBFUSCATE("101_Toggle_True_Show Team Breakdown"),
            OBFUSCATE("102_Toggle_True_Show Bot / NPC Count"),
            OBFUSCATE("103_Toggle_Compact HUD Mode"),
            OBFUSCATE("104_SeekBar_HUD Transparency_10_100"),
            OBFUSCATE("105_SeekBar_HUD Text Size_10_26"),
            OBFUSCATE("106_Button_Reset HUD Position"),
            OBFUSCATE("Category_GAME CHEATS"),
            OBFUSCATE("0_Toggle_Godmode / Immortality"),
            OBFUSCATE("1_Toggle_Infinite Ammo / Rapid"),
            OBFUSCATE("2_Toggle_Radar / Wallhack Vision"),
            OBFUSCATE("3_SeekBar_Speed Multiplier_1_5"),
    };

    int Total_Feature = 13;
    ret = (jobjectArray)
            env->NewObjectArray(Total_Feature, env->FindClass(OBFUSCATE("java/lang/String")),
                                env->NewStringUTF(""));

    for (int i = 0; i < Total_Feature; i++)
        env->SetObjectArrayElement(ret, i, env->NewStringUTF(features[i]));

    return (ret);
}

// Handle user toggles and slider changes
void Changes(JNIEnv *env, jclass clazz, jobject obj,
             jint featNum, jstring featName, jint value,
             jboolean boolean, jstring str) {

    FILE_LOGI("Feature Change: FeatNum=%d | Value=%d | Bool=%d", featNum, value, boolean);

    switch (featNum) {
        case 100: // Show HUD Status Panel
            UnifiedGameScanner::Instance().m_bHudEnabled = boolean;
            break;
        case 101: // Show Team Breakdown
            UnifiedGameScanner::Instance().m_bConfigTeamFilter = boolean;
            break;
        case 102: // Show Bot Count
            UnifiedGameScanner::Instance().m_bConfigBotFilter = boolean;
            break;
        case 103: // Compact Mode
            break;
        case 104: // Transparency (10 - 100)
            UnifiedGameScanner::Instance().m_nHudOpacity = value;
            break;
        case 105: // Text Size (10 - 26)
            UnifiedGameScanner::Instance().m_nHudTextSize = value;
            break;
        case 106: // Reset HUD Position
            break;
        case 0:   // Godmode
            break;
        case 1:   // Infinite Ammo
            break;
        case 2:   // Wallhack / Vision
            break;
        case 3:   // Speed Multiplier
            break;
        default:
            break;
    }
}

__attribute__((constructor))
void lib_main() {
    pthread_t ptid;
    pthread_create(&ptid, NULL, hack_thread, NULL);
}

int RegisterMenu(JNIEnv *env) {
    JNINativeMethod methods[9] = {
            {OBFUSCATE("Icon"), OBFUSCATE("()Ljava/lang/String;"), reinterpret_cast<void *>(Icon)},
            {OBFUSCATE("IconWebViewData"),  OBFUSCATE("()Ljava/lang/String;"), reinterpret_cast<void *>(IconWebViewData)},
            {OBFUSCATE("IsGameLibLoaded"),  OBFUSCATE("()Z"), reinterpret_cast<void *>(isGameLibLoaded)},
            {OBFUSCATE("Init"),  OBFUSCATE("(Landroid/content/Context;Landroid/widget/TextView;Landroid/widget/TextView;)V"), reinterpret_cast<void *>(Init)},
            {OBFUSCATE("SettingsList"),  OBFUSCATE("()[Ljava/lang/String;"), reinterpret_cast<void *>(SettingsList)},
            {OBFUSCATE("GetFeatureList"),  OBFUSCATE("()[Ljava/lang/String;"), reinterpret_cast<void *>(GetFeatureList)},
            {OBFUSCATE("GetHudStatusData"), OBFUSCATE("()[I"), reinterpret_cast<void *>(GetHudStatusData)},
            {OBFUSCATE("GetHudStatusFormatted"), OBFUSCATE("()Ljava/lang/String;"), reinterpret_cast<void *>(GetHudStatusFormatted)},
            {OBFUSCATE("GetActiveGameName"), OBFUSCATE("()Ljava/lang/String;"), reinterpret_cast<void *>(GetActiveGameName)},
    };

    jclass clazz = env->FindClass(OBFUSCATE("com/android/support/Menu"));
    if (!clazz)
        return JNI_ERR;
    if (env->RegisterNatives(clazz, methods, 9) != 0)
        return JNI_ERR;
    return JNI_OK;
}

int RegisterPreferences(JNIEnv *env) {
    JNINativeMethod methods[2] = {
            {OBFUSCATE("Changes"), OBFUSCATE("(Landroid/content/Context;ILjava/lang/String;IZLjava/lang/String;)V"), reinterpret_cast<void *>(Changes)},
            {OBFUSCATE("LogToDebugFile"), OBFUSCATE("(Ljava/lang/String;Ljava/lang/String;)V"), reinterpret_cast<void *>(LogToDebugFile)},
    };
    jclass clazz = env->FindClass(OBFUSCATE("com/android/support/Preferences"));
    if (!clazz)
        return JNI_ERR;
    if (env->RegisterNatives(clazz, methods, 2) != 0)
        return JNI_ERR;
    return JNI_OK;
}

int RegisterMain(JNIEnv *env) {
    JNINativeMethod methods[1] = {
            {OBFUSCATE("CheckOverlayPermission"), OBFUSCATE("(Landroid/content/Context;)V"), reinterpret_cast<void *>(CheckOverlayPermission)},
    };
    jclass clazz = env->FindClass(OBFUSCATE("com/android/support/Main"));
    if (!clazz)
        return JNI_ERR;
    if (env->RegisterNatives(clazz, methods, 1) != 0)
        return JNI_ERR;

    return JNI_OK;
}

extern "C"
JNIEXPORT jint JNICALL
JNI_OnLoad(JavaVM *vm, void *reserved) {
    JNIEnv *env;
    vm->GetEnv((void **) &env, JNI_VERSION_1_6);
    if (RegisterMenu(env) != 0)
        return JNI_ERR;
    if (RegisterPreferences(env) != 0)
        return JNI_ERR;
    if (RegisterMain(env) != 0)
        return JNI_ERR;
    return JNI_VERSION_1_6;
}
