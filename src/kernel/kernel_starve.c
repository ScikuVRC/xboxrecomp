/*
 * kernel_starve.c - a guest thread starved on the guest core gets to run (Windows).
 *
 * The title's threads share one host core (xbox_PinToGuestCore,
 * xbox_GuestThreadPin) because the console had one CPU and its code leans on
 * that. On the console the title's own scheduler shared that CPU; here
 * Windows does, and Windows can raise a thread far above its title-given
 * priority -- the main thread, spinning in Direct3D's wait for push-buffer
 * space (Def Jam's sub_0021EC50), was seen at priority 21. A thread that high
 * and never blocking keeps every other thread pinned to its core from ever
 * running, those threads included that the spin is indirectly waiting for:
 * the game sat on its LOADING screen for good, its worker threads ready to
 * run with 0 ms of CPU each (two-game start with HD packs, 2026-11-04).
 * Allowing those workers onto other cores by hand revived it at once.
 *
 * So a watcher looks at the pinned threads every 200 ms through
 * NtQuerySystemInformation, which reports each thread's scheduling state and
 * context-switch count. One that has been Ready -- wanting the CPU -- with no
 * context switch for STARVED_MS gets every core the process may use until it
 * has run, then goes back to the guest core. Threads that are waiting, or that
 * run at all, are never touched, so the one-core rule bends only where the
 * alternative is a hang. RECOMP_STARVE_WATCH=0 turns the watcher off.
 */
#include "kernel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <sched.h>
#endif

#if defined(_WIN32)

#define WATCH_MAX       64
#define TICK_MS         200
#define STARVED_MS      600
#define FREED_MS        200

/* The x64 layout of what SystemProcessInformation (5) returns: one record per
 * process, each followed by its threads. */
typedef struct {
    LARGE_INTEGER kernel_time, user_time, create_time;
    ULONG wait_time;
    PVOID start_address;
    HANDLE unique_process, unique_thread;
    LONG priority, base_priority;
    ULONG context_switches, state, wait_reason;
} StarveThreadInfo;

typedef struct {
    ULONG next_entry_offset, number_of_threads;
    BYTE reserved1[48];
    struct { USHORT length, maximum_length; PWSTR buffer; } image_name;
    LONG base_priority;
    HANDLE unique_process_id;
    BYTE reserved2[0x100 - 0x58];
} StarveProcessInfo;

typedef LONG (NTAPI *NtQsi)(ULONG, PVOID, ULONG, PULONG);

static struct {
    DWORD tid;
    HANDLE handle;
    DWORD_PTR mask;     /* the guest core it belongs on */
    ULONG switches;     /* context switches when last seen */
    int ready_ms;       /* how long it has been Ready without a switch */
    int freed_ms;       /* > 0 while it may use every core */
} s_watch[WATCH_MAX];
static volatile LONG s_count;
static CRITICAL_SECTION s_lock;
static volatile LONG s_started;
static unsigned s_rescues;

static DWORD WINAPI starve_watch(LPVOID unused)
{
    NtQsi query = (NtQsi)(void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQuerySystemInformation");
    ULONG size = 1 << 20;
    BYTE *buf = (BYTE *)malloc(size);
    DWORD me = GetCurrentProcessId();
    DWORD_PTR all = 0, sys = 0;

    (void)unused;
    GetProcessAffinityMask(GetCurrentProcess(), &all, &sys);
    if (!query || !buf || !all)
        return 0;
    for (;;) {
        ULONG got = 0;
        LONG st;
        Sleep(TICK_MS);
        st = query(5, buf, size, &got);
        if (st == (LONG)0xC0000004) {           /* STATUS_INFO_LENGTH_MISMATCH */
            BYTE *bigger = (BYTE *)realloc(buf, got + (1 << 16));
            if (bigger) { buf = bigger; size = got + (1 << 16); }
            continue;
        }
        if (st < 0)
            continue;
        {
            StarveProcessInfo *p = (StarveProcessInfo *)buf;
            for (;;) {
                if ((DWORD)(ULONG_PTR)p->unique_process_id == me)
                    break;
                if (!p->next_entry_offset) { p = NULL; break; }
                p = (StarveProcessInfo *)((BYTE *)p + p->next_entry_offset);
            }
            if (!p)
                continue;
            EnterCriticalSection(&s_lock);
            /* Threads that have ended leave the list. */
            for (LONG i = 0; i < s_count; )
                if (WaitForSingleObject(s_watch[i].handle, 0) == WAIT_OBJECT_0) {
                    CloseHandle(s_watch[i].handle);
                    s_watch[i] = s_watch[--s_count];
                } else {
                    i++;
                }
            for (LONG i = 0; i < s_count; i++) {
                StarveThreadInfo *t = (StarveThreadInfo *)(p + 1);
                ULONG k;
                for (k = 0; k < p->number_of_threads; k++, t++)
                    if ((DWORD)(ULONG_PTR)t->unique_thread == s_watch[i].tid)
                        break;
                if (k == p->number_of_threads)
                    continue;                   /* gone */
                if (s_watch[i].freed_ms > 0) {
                    /* Back on the guest core once it has run, or after FREED_MS. */
                    s_watch[i].freed_ms -= TICK_MS;
                    if (t->context_switches != s_watch[i].switches || s_watch[i].freed_ms <= 0) {
                        SetThreadAffinityMask(s_watch[i].handle, s_watch[i].mask);
                        s_watch[i].freed_ms = 0;
                    }
                } else if ((t->state == 1 || t->state == 7)       /* Ready, DeferredReady */
                           && t->context_switches == s_watch[i].switches) {
                    s_watch[i].ready_ms += TICK_MS;
                    if (s_watch[i].ready_ms >= STARVED_MS
                            && SetThreadAffinityMask(s_watch[i].handle, all)) {
                        s_watch[i].freed_ms = FREED_MS;
                        s_rescues++;
                        if (s_rescues <= 10 || (s_rescues & (s_rescues - 1)) == 0) {
                            fprintf(stderr, "  [KERNEL] guest thread %lu starved on the guest core for %d ms"
                                            " (ready, priority %ld); let it run on another core (#%u)\n",
                                    (unsigned long)s_watch[i].tid, s_watch[i].ready_ms,
                                    (long)t->priority, s_rescues);
                            fflush(stderr);
                        }
                        s_watch[i].ready_ms = 0;
                    }
                } else {
                    s_watch[i].ready_ms = 0;
                }
                s_watch[i].switches = t->context_switches;
            }
            LeaveCriticalSection(&s_lock);
        }
    }
}

void xbox_StarveWatchAdd(DWORD_PTR mask)
{
    HANDLE h;
    LONG n;

    if (!mask)
        return;
    if (InterlockedCompareExchange(&s_started, 1, 0) == 0) {
        const char *e = getenv("RECOMP_STARVE_WATCH");
        InitializeCriticalSection(&s_lock);
        if (e && *e == '0') {
            InterlockedExchange(&s_started, 3);
            return;
        }
        CloseHandle(CreateThread(NULL, 0, starve_watch, NULL, 0, NULL));
        InterlockedExchange(&s_started, 2);
    }
    while (s_started == 1)
        Sleep(0);
    if (s_started != 2)
        return;
    if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &h,
                         THREAD_SET_INFORMATION | THREAD_QUERY_INFORMATION | SYNCHRONIZE, FALSE, 0))
        return;
    EnterCriticalSection(&s_lock);
    for (n = 0; n < s_count; n++)
        if (s_watch[n].tid == GetCurrentThreadId())
            break;
    if (n == s_count && s_count < WATCH_MAX)
        s_count++;
    if (n < WATCH_MAX) {
        if (s_watch[n].handle && s_watch[n].tid == GetCurrentThreadId())
            CloseHandle(s_watch[n].handle);
        s_watch[n].tid = GetCurrentThreadId();
        s_watch[n].handle = h;
        s_watch[n].mask = mask;
        s_watch[n].switches = 0;
        s_watch[n].ready_ms = s_watch[n].freed_ms = 0;
    } else {
        CloseHandle(h);
    }
    LeaveCriticalSection(&s_lock);
}

#else

void xbox_StarveWatchAdd(DWORD_PTR mask) { (void)mask; }

#endif

/* RECOMP_SPIN_YIELD (a loop back edge in a function lifted with --spin-yield): a title's
 * busy-wait, such as Def Jam's Direct3D waiting for push-buffer space, gives the core to
 * whatever else is ready on it every SPIN_EVERY passes. Windows had raised the waiting
 * thread to priority 21 and 26, above every thread it waited for, which share its core:
 * they never ran, and the game stalled loading (the watcher above only lets one out now
 * and then). SwitchToThread runs a ready thread of any priority on this processor. */
#define SPIN_EVERY      32
#if defined(_MSC_VER)
static __declspec(thread) unsigned s_spins;
#else
static _Thread_local unsigned s_spins;
#endif

void xbox_GuestSpinYield(uint32_t va)
{
    (void)va;
    if (++s_spins % SPIN_EVERY)
        return;
#if defined(_WIN32)
    SwitchToThread();
#else
    sched_yield();
#endif
}
