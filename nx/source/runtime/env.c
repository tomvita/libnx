// Copyright 2018 plutoo
#include <string.h>
#include "runtime/env.h"
#include "runtime/hosversion.h"
#include "services/sm.h"
#include "services/applet.h"
#include "services/acc.h"
#include "runtime/diag.h"

void NX_NORETURN __nx_exit(Result rc, LoaderReturnFn retaddr);

static bool   g_isNso = false;
static const char* g_loaderInfo = NULL;
static u64    g_loaderInfoSize = 0;
static Handle g_mainThreadHandle = INVALID_HANDLE;
static LoaderReturnFn g_loaderRetAddr = NULL;
static void*  g_overrideHeapAddr = NULL;
static u64    g_overrideHeapSize = 0;
static void*  g_overrideArgv = NULL;
static u64    g_syscallHints[3];
static Handle g_processHandle = INVALID_HANDLE;
static char*  g_nextLoadPath = NULL;
static char*  g_nextLoadArgv = NULL;
static Result g_lastLoadResult = 0;
static bool   g_hasRandomSeed = false;
static u64    g_randomSeed[2] = { 0, 0 };
static AccountUid*  g_userIdStorage = NULL;

extern __attribute__((weak)) u32 __nx_applet_type;

void envSetup(void* ctx, Handle main_thread, LoaderReturnFn saved_lr)
{
    // Detect and set up NSO environment.
    if (ctx == NULL)
    {
        // Under NSO, we use svcExitProcess as the return address and hint all syscalls as available.
        g_isNso = true;
        g_mainThreadHandle = main_thread;
        g_loaderRetAddr = (LoaderReturnFn) &svcExitProcess;
        g_syscallHints[0] = g_syscallHints[1] = g_syscallHints[2] = UINT64_MAX;
        return;
    }

    // Save the loader return address.
    g_loaderRetAddr = saved_lr;

    // Parse homebrew ABI config entries.
    ConfigEntry* ent = ctx;

    while (ent->Key != EntryType_EndOfList)
    {
        switch (ent->Key)
        {
        case EntryType_MainThreadHandle:
            g_mainThreadHandle = ent->Value[0];
            break;

        case EntryType_NextLoadPath:
            g_nextLoadPath = (char*) ent->Value[0];
            g_nextLoadArgv = (char*) ent->Value[1];
            break;

        case EntryType_OverrideHeap:
            g_overrideHeapAddr = (void*) ent->Value[0];
            g_overrideHeapSize = ent->Value[1];
            break;

        case EntryType_OverrideService:
            smAddOverrideHandle(smServiceNameFromU64(ent->Value[0]), ent->Value[1]);
            break;

        case EntryType_Argv:
            g_overrideArgv = (void*) ent->Value[1];
            break;

        case EntryType_SyscallAvailableHint:
            g_syscallHints[0] = ent->Value[0];
            g_syscallHints[1] = ent->Value[1];
            break;

        case EntryType_AppletType:
            __nx_applet_type = ent->Value[0];
            if ((ent->Value[1] & EnvAppletFlags_ApplicationOverride) && __nx_applet_type == AppletType_SystemApplication) __nx_applet_type = AppletType_Application;
            break;

        case EntryType_ProcessHandle:
            g_processHandle = ent->Value[0];
            break;

        case EntryType_LastLoadResult:
            g_lastLoadResult = ent->Value[0];
            break;

        case EntryType_RandomSeed:
            g_hasRandomSeed = true;
            g_randomSeed[0] = ent->Value[0];
            g_randomSeed[1] = ent->Value[1];
            break;

        case EntryType_UserIdStorage:
            g_userIdStorage = (AccountUid*)(uintptr_t)ent->Value[0];
            break;

        case EntryType_HosVersion: {
            u32 version = ent->Value[0];
            if (ent->Value[1] == 0x41544d4f53504852UL) { // 'ATMOSPHR'
                version |= BIT(31);
            }
            hosversionSet(version);
            break;
        }

        case EntryType_SyscallAvailableHint2:
            g_syscallHints[2] = ent->Value[0];
            break;

        default:
            if (ent->Flags & EntryFlag_IsMandatory)
            {
                // Encountered unknown but mandatory key, bail back to loader.
                __nx_exit(MAKERESULT(Module_HomebrewAbi, 100 + ent->Key), g_loaderRetAddr);
            }

            break;
        }

        ent++;
    }

    g_loaderInfoSize = ent->Value[1];
    if (g_loaderInfoSize) {
        g_loaderInfo = (const char*)(uintptr_t)ent->Value[0];
    }

}

const char* envGetLoaderInfo(void) {
    return g_loaderInfo;
}

u64 envGetLoaderInfoSize(void) {
    return g_loaderInfoSize;
}

Handle envGetMainThreadHandle(void) {
    if (g_mainThreadHandle != INVALID_HANDLE) {
        return g_mainThreadHandle;
    }

    diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_HandleTooEarly));
}

bool envIsNso(void) {
    return g_isNso;
}

bool envHasHeapOverride(void) {
    return g_overrideHeapAddr != NULL;
}

void* envGetHeapOverrideAddr(void) {
    return g_overrideHeapAddr;
}

u64 envGetHeapOverrideSize(void) {
    return g_overrideHeapSize;
}

bool envHasArgv(void) {
    return g_overrideArgv != NULL;
}

void* envGetArgv(void) {
    return g_overrideArgv;
}

bool envIsSyscallHinted(unsigned svc) {
    if (svc >= 0xC0) return false;
    return !!(g_syscallHints[svc/64] & (1ull << (svc%64)));
}

Handle envGetOwnProcessHandle(void) {
    return g_processHandle;
}

LoaderReturnFn envGetExitFuncPtr(void) {
    return g_loaderRetAddr;
}

void envSetExitFuncPtr(LoaderReturnFn addr) {
    g_loaderRetAddr = addr;
}

Result envSetNextLoad(const char* path, const char* argv)
{
    if (g_nextLoadPath == NULL)
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);

    strcpy(g_nextLoadPath, path);

    if (g_nextLoadArgv != NULL)
    {
        if (argv == NULL)
            g_nextLoadArgv[0] = '\0';
        else
            strcpy(g_nextLoadArgv, argv);
    }

    return 0;
}

bool envHasNextLoad(void) {
    return g_nextLoadPath != NULL;
}

Result envGetLastLoadResult(void) {
    return g_lastLoadResult;
}

bool envHasRandomSeed(void) {
    return g_hasRandomSeed;
}

void envGetRandomSeed(u64 out[2]) {
    out[0] = g_randomSeed[0];
    out[1] = g_randomSeed[1];
}

AccountUid* envGetUserIdStorage(void) {
    return g_userIdStorage;
}
