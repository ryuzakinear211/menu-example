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
#include "KittyMemory/MemoryPatch.h"
#include "Menu/Setup.h"
#include "GTAPedPool.h"

// Target game library for GTA San Andreas Android
#define targetLibName OBFUSCATE("libGTASA.so")

#include "Includes/Macros.h"

// Background thread to monitor target library load
void *hack_thread(void *) {
    LOGI(OBFUSCATE("pthread created: monitoring game library"));

    // Check if target game library is loaded (GTA SA or SA-MP)
    int waitSeconds = 0;
    while (!isLibraryLoaded(targetLibName) && 
           !isLibraryLoaded(OBFUSCATE("libsamp.so")) && 
           !isLibraryLoaded(OBFUSCATE("libbulletecho.so"))) {
        sleep(1);
        waitSeconds++;
        if (waitSeconds % 5 == 0) {
            LOGI(OBFUSCATE("Waiting for game library to load... (%d s)"), waitSeconds);
        }
    }

    LOGI(OBFUSCATE("Game library loaded! Initializing GTA Ped Pool scanner..."));
    GTAPedPoolScanner::Instance().CheckLibraries();

#if defined(__aarch64__)
    LOGI(OBFUSCATE("Running on ARM64 architecture"));
#else
    LOGI(OBFUSCATE("Running on ARM32 architecture"));
#endif

    return NULL;
}

// JNI method to return live HUD status counts to Java UI
jintArray GetHudStatusData(JNIEnv *env, jobject thiz) {
    HudStats stats = GTAPedPoolScanner::Instance().ScanGTA();
    jintArray result = env->NewIntArray(6);
    if (!result) return nullptr;

    jint fill[6];
    fill[0] = stats.teamCount;
    fill[1] = stats.enemyCount;
    fill[2] = stats.botCount;
    fill[3] = stats.totalPeds;
    fill[4] = stats.isLibLoaded;
    fill[5] = stats.localPlayerHp;

    env->SetIntArrayRegion(result, 0, 6, fill);
    return result;
}

// JNI method returning formatted string for simple display or log
jstring GetHudStatusFormatted(JNIEnv *env, jobject thiz) {
    HudStats stats = GTAPedPoolScanner::Instance().ScanGTA();
    char buf[256];
    snprintf(buf, sizeof(buf),
             "<font color='#00E676'>Team: %d</font> | "
             "<font color='#FF5252'>Enemy: %d</font> | "
             "<font color='#40C4FF'>Bots: %d</font> | "
             "<font color='#E0E0E0'>Total: %d</font>",
             stats.teamCount, stats.enemyCount, stats.botCount, stats.totalPeds);
    return env->NewStringUTF(buf);
}

// LGL Mod Menu Features List
jobjectArray GetFeatureList(JNIEnv *env, jobject context) {
    jobjectArray ret;

    const char *features[13] = {
            OBFUSCATE("Category_HUD STATUS PANEL (GTA SA)"),
            OBFUSCATE("100_Toggle_True_Show HUD Status Panel"),
            OBFUSCATE("101_Toggle_True_Show Team Breakdown"),
            OBFUSCATE("102_Toggle_True_Show Bot / NPC Count"),
            OBFUSCATE("103_Toggle_Compact HUD Mode"),
            OBFUSCATE("104_SeekBar_HUD Transparency_10_100"),
            OBFUSCATE("105_SeekBar_HUD Text Size_10_26"),
            OBFUSCATE("106_Button_Reset HUD Position"),
            OBFUSCATE("Category_GAME CHEATS"),
            OBFUSCATE("0_Toggle_Infinite Health"),
            OBFUSCATE("1_Toggle_Infinite Armor"),
            OBFUSCATE("2_Toggle_Infinite Ammo"),
            OBFUSCATE("3_SeekBar_Walk Speed_1_5"),
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

    LOGD(OBFUSCATE("Feature: %d | Val: %d | Bool: %d"), featNum, value, boolean);

    switch (featNum) {
        case 100: // Show HUD Status Panel
            GTAPedPoolScanner::Instance().m_bHudEnabled = boolean;
            LOGI(OBFUSCATE("HUD Enabled: %d"), boolean);
            break;
        case 101: // Show Team Breakdown
            GTAPedPoolScanner::Instance().m_bConfigTeamFilter = boolean;
            break;
        case 102: // Show Bot Count
            GTAPedPoolScanner::Instance().m_bConfigBotFilter = boolean;
            break;
        case 103: // Compact Mode
            break;
        case 104: // Transparency (10 - 100)
            GTAPedPoolScanner::Instance().m_nHudOpacity = value;
            break;
        case 105: // Text Size (10 - 26)
            GTAPedPoolScanner::Instance().m_nHudTextSize = value;
            break;
        case 106: // Reset HUD Position
            break;
        case 0:   // Infinite Health
            break;
        case 1:   // Infinite Armor
            break;
        case 2:   // Infinite Ammo
            break;
        case 3:   // Walk Speed
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
    JNINativeMethod methods[8] = {
            {OBFUSCATE("Icon"), OBFUSCATE("()Ljava/lang/String;"), reinterpret_cast<void *>(Icon)},
            {OBFUSCATE("IconWebViewData"),  OBFUSCATE("()Ljava/lang/String;"), reinterpret_cast<void *>(IconWebViewData)},
            {OBFUSCATE("IsGameLibLoaded"),  OBFUSCATE("()Z"), reinterpret_cast<void *>(isGameLibLoaded)},
            {OBFUSCATE("Init"),  OBFUSCATE("(Landroid/content/Context;Landroid/widget/TextView;Landroid/widget/TextView;)V"), reinterpret_cast<void *>(Init)},
            {OBFUSCATE("SettingsList"),  OBFUSCATE("()[Ljava/lang/String;"), reinterpret_cast<void *>(SettingsList)},
            {OBFUSCATE("GetFeatureList"),  OBFUSCATE("()[Ljava/lang/String;"), reinterpret_cast<void *>(GetFeatureList)},
            {OBFUSCATE("GetHudStatusData"), OBFUSCATE("()[I"), reinterpret_cast<void *>(GetHudStatusData)},
            {OBFUSCATE("GetHudStatusFormatted"), OBFUSCATE("()Ljava/lang/String;"), reinterpret_cast<void *>(GetHudStatusFormatted)},
    };

    jclass clazz = env->FindClass(OBFUSCATE("com/android/support/Menu"));
    if (!clazz)
        return JNI_ERR;
    if (env->RegisterNatives(clazz, methods, 8) != 0)
        return JNI_ERR;
    return JNI_OK;
}

int RegisterPreferences(JNIEnv *env) {
    JNINativeMethod methods[1] = {
            {OBFUSCATE("Changes"), OBFUSCATE("(Landroid/content/Context;ILjava/lang/String;IZLjava/lang/String;)V"), reinterpret_cast<void *>(Changes)},
    };
    jclass clazz = env->FindClass(OBFUSCATE("com/android/support/Preferences"));
    if (!clazz)
        return JNI_ERR;
    if (env->RegisterNatives(clazz, methods, 1) != 0)
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
