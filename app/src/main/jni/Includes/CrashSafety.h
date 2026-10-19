#ifndef CRASH_SAFETY_H
#define CRASH_SAFETY_H

#include <csignal>
#include <csetjmp>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <ucontext.h>
#include <unistd.h>
#include <sys/stat.h>
#include "FileLogger.h"

// Thread-local safe jump buffer for non-fatal memory scanning
extern "C" {
    extern thread_local sigjmp_buf g_safeJmpBuf;
    extern thread_local bool g_inSafeBlock;
}

inline thread_local sigjmp_buf g_safeJmpBuf;
inline thread_local bool g_inSafeBlock = false;

class CrashSafety {
private:
    static struct sigaction s_oldSegv;
    static struct sigaction s_oldBus;
    static struct sigaction s_oldFpe;
    static struct sigaction s_oldIll;
    static struct sigaction s_oldAbrt;

    static const char* GetSignalName(int sig) {
        switch (sig) {
            case SIGSEGV: return "SIGSEGV (Segmentation Fault)";
            case SIGBUS:  return "SIGBUS (Bus Error / Misaligned Access)";
            case SIGFPE:  return "SIGFPE (Floating Point Exception)";
            case SIGILL:  return "SIGILL (Illegal Instruction)";
            case SIGABRT: return "SIGABRT (Abort)";
            default:      return "UNKNOWN SIGNAL";
        }
    }

    static void CrashHandler(int sig, siginfo_t* info, void* ucontext) {
        // If we are currently inside a guarded safe memory block, recover immediately!
        if (g_inSafeBlock) {
            g_inSafeBlock = false;
            FILE_LOGW("Safely intercepted %s at address %p during memory scan (Game crash prevented)",
                      GetSignalName(sig), info->si_addr);
            siglongjmp(g_safeJmpBuf, 1);
            return;
        }

        // Otherwise, write comprehensive crash report to file
        char timeBuf[64];
        time_t now = time(nullptr);
        struct tm* tmInfo = localtime(&now);
        strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", tmInfo);

        mkdir("/sdcard/Documents", 0777);
        mkdir("/storage/emulated/0/Documents", 0777);

        const char* crashPaths[] = {
            "/sdcard/Documents/crash_dump.log",
            "/storage/emulated/0/Documents/crash_dump.log",
            "/sdcard/crash_dump.log"
        };

        FILE* fp = nullptr;
        for (const char* path : crashPaths) {
            fp = fopen(path, "a+");
            if (fp) break;
        }

        if (fp) {
            fprintf(fp, "=================================================================\n");
            fprintf(fp, " MOD MENU CRASH DUMP - %s\n", timeBuf);
            fprintf(fp, "=================================================================\n");
            fprintf(fp, "Signal:         %s (%d)\n", GetSignalName(sig), sig);
            fprintf(fp, "Fault Address:  %p\n", info->si_addr);
            fprintf(fp, "Signal Code:    %d\n", info->si_code);

#if defined(__aarch64__)
            ucontext_t* uc = reinterpret_cast<ucontext_t*>(ucontext);
            if (uc) {
                uintptr_t pc = uc->uc_mcontext.pc;
                uintptr_t sp = uc->uc_mcontext.sp;
                uintptr_t lr = uc->uc_mcontext.regs[30];
                fprintf(fp, "Program Counter (PC): 0x%016lx\n", (unsigned long)pc);
                fprintf(fp, "Link Register (LR):    0x%016lx\n", (unsigned long)lr);
                fprintf(fp, "Stack Pointer (SP):    0x%016lx\n", (unsigned long)sp);
                fprintf(fp, "\n--- CPU REGISTERS (ARM64) ---\n");
                for (int i = 0; i < 31; i += 2) {
                    fprintf(fp, "X%-2d: 0x%016lx   X%-2d: 0x%016lx\n",
                            i, (unsigned long)uc->uc_mcontext.regs[i],
                            i + 1, (i + 1 < 31) ? (unsigned long)uc->uc_mcontext.regs[i + 1] : 0UL);
                }
            }
#elif defined(__arm__)
            ucontext_t* uc = reinterpret_cast<ucontext_t*>(ucontext);
            if (uc) {
                fprintf(fp, "PC: 0x%08lx  LR: 0x%08lx  SP: 0x%08lx\n",
                        (unsigned long)uc->uc_mcontext.arm_pc,
                        (unsigned long)uc->uc_mcontext.arm_lr,
                        (unsigned long)uc->uc_mcontext.arm_sp);
            }
#endif
            fprintf(fp, "=================================================================\n\n");
            fflush(fp);
            fclose(fp);
        }

        FILE_LOGF("CRASH TRIGGERED: %s at %p. Dump saved to /sdcard/Documents/crash_dump.log",
                  GetSignalName(sig), info->si_addr);

        // Chain to default or previous handler
        struct sigaction* oldAction = nullptr;
        switch (sig) {
            case SIGSEGV: oldAction = &s_oldSegv; break;
            case SIGBUS:  oldAction = &s_oldBus;  break;
            case SIGFPE:  oldAction = &s_oldFpe;  break;
            case SIGILL:  oldAction = &s_oldIll;  break;
            case SIGABRT: oldAction = &s_oldAbrt; break;
        }

        if (oldAction && oldAction->sa_sigaction) {
            oldAction->sa_sigaction(sig, info, ucontext);
        } else {
            signal(sig, SIG_DFL);
            raise(sig);
        }
    }

public:
    static void Install() {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = CrashHandler;
        sa.sa_flags = SA_SIGINFO | SA_ONSTACK;

        sigaction(SIGSEGV, &sa, &s_oldSegv);
        sigaction(SIGBUS,  &sa, &s_oldBus);
        sigaction(SIGFPE,  &sa, &s_oldFpe);
        sigaction(SIGILL,  &sa, &s_oldIll);
        sigaction(SIGABRT, &sa, &s_oldAbrt);

        FILE_LOGI("Crash safety handlers successfully installed.");
    }
};

inline struct sigaction CrashSafety::s_oldSegv;
inline struct sigaction CrashSafety::s_oldBus;
inline struct sigaction CrashSafety::s_oldFpe;
inline struct sigaction CrashSafety::s_oldIll;
inline struct sigaction CrashSafety::s_oldAbrt;

// Macro for safe memory reads that prevents crashes
#define SAFE_BLOCK_BEGIN \
    if (sigsetjmp(g_safeJmpBuf, 1) == 0) { \
        g_inSafeBlock = true;

#define SAFE_BLOCK_END \
        g_inSafeBlock = false; \
    } else { \
        g_inSafeBlock = false; \
    }

#endif // CRASH_SAFETY_H
