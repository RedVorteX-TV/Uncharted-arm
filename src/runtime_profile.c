/* SPDX-License-Identifier: GPL-2.0-or-later */
/* BB_GUEST_PROFILE=prefix: a sampling profiler of guest threads whose name starts with `prefix`
 * (e.g. NdJob). Every 4 ms each such thread that is running gets a SIGPROF; the handler counts
 * where it was: guest code (image offset), host code called by the guest (host PC and the guest
 * return address), or neither (host PC: the guest CPU's own code). Threads that are asleep are
 * counted without a signal, so blocking calls are not interrupted. Every 10 s the hottest places
 * are printed with the guest code bytes there. BB_GUEST_PROFILE_HZ overrides the 250 Hz rate. */
#define _GNU_SOURCE
#include "runtime.h"
#include "guest_cpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
void runtime_profile_start(const void *image, uint64_t size) { (void)image; (void)size; }
#else
#include <dlfcn.h>
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

enum { SLOTS = 16384, TOP = 40, MAX_THREADS = 64 };
/* Kinds, in the key's top bits. */
#define KIND_SHIFT 60
enum { K_GUEST = 1, K_HOST_CALL = 2, K_CALLER = 3, K_OTHER = 4 };
static const char *const kind_names[] = {"", "guest", "host call", "  from guest", "host (no guest)"};

static uint64_t keys[SLOTS];
static uint32_t counts[SLOTS];
static uint64_t lost, total, asleep;
static uintptr_t image_base;
static uint64_t image_size;

static void count(uint64_t key) {
    uint64_t h = key * 0x9e3779b97f4a7c15ull;
    for (unsigned probe = 0; probe < 64; ++probe) {
        const unsigned i = (unsigned)((h >> 40) + probe) & (SLOTS - 1);
        uint64_t seen = __atomic_load_n(&keys[i], __ATOMIC_ACQUIRE);
        if (!seen) {
            uint64_t empty = 0;
            if (__atomic_compare_exchange_n(&keys[i], &empty, key, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) seen = key;
            else seen = empty;
        }
        if (seen == key) { __atomic_fetch_add(&counts[i], 1, __ATOMIC_RELAXED); return; }
    }
    __atomic_fetch_add(&lost, 1, __ATOMIC_RELAXED);
}
static uint64_t relative(uintptr_t address) {
    return address >= image_base && address - image_base < image_size ? address - image_base : address;
}
static void on_sample(int sig, siginfo_t *info, void *context) {
    (void)sig; (void)info;
    GuestRegs regs;
    const int where = guest_cpu_signal_regs(context, &regs);
    const uint64_t mask = (UINT64_C(1) << KIND_SHIFT) - 1;
    if (where == GUEST_CPU_IN_GUEST) {
        count(((uint64_t)K_GUEST << KIND_SHIFT) | (relative((uintptr_t)regs.rip) & mask));
    } else if (where == GUEST_CPU_IN_HOST_CALL) {
        count(((uint64_t)K_HOST_CALL << KIND_SHIFT) | (guest_cpu_host_pc(context) & mask));
        count(((uint64_t)K_CALLER << KIND_SHIFT) | (relative((uintptr_t)regs.rip) & mask));
    } else {
        count(((uint64_t)K_OTHER << KIND_SHIFT) | (guest_cpu_host_pc(context) & mask));
    }
    __atomic_fetch_add(&total, 1, __ATOMIC_RELAXED);
}

static char prefix[16];
static int matching(pid_t tid, char *state) {
    char path[64], buffer[256];
    snprintf(path, sizeof(path), "/proc/self/task/%d/stat", (int)tid);
    const int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    const ssize_t n = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);
    if (n <= 0) return 0;
    buffer[n] = 0;
    char *open_paren = strchr(buffer, '('), *close_paren = strrchr(buffer, ')');
    if (!open_paren || !close_paren || close_paren < open_paren) return 0;
    *close_paren = 0;
    *state = close_paren[2];
    return !strncmp(open_paren + 1, prefix, strlen(prefix));
}
static void print_code(uint64_t offset) { /* 16 bytes before the sampled instruction, `|`, 32 from it */
    unsigned char code[48];
    const uint64_t from = offset >= 16 ? offset - 16 : 0;
    struct iovec local = {code, sizeof(code)}, remote = {(void *)(image_base + from), sizeof(code)};
    if (offset >= image_size || process_vm_readv(getpid(), &local, 1, &remote, 1, 0) != (ssize_t)sizeof(code)) return;
    printf("    code %#llx:", (unsigned long long)from);
    for (size_t i = 0; i < sizeof(code); ++i) printf("%s%02x", from + i == offset ? " |" : " ", code[i]);
    printf("\n");
}
static void report(double seconds) {
    static unsigned order[SLOTS];
    unsigned used = 0;
    for (unsigned i = 0; i < SLOTS; ++i) if (keys[i] && counts[i]) order[used++] = i;
    for (unsigned a = 0; a < used && a < TOP; ++a) /* partial selection sort: the TOP largest */
        for (unsigned b = a + 1; b < used; ++b)
            if (counts[order[b]] > counts[order[a]]) { unsigned t = order[a]; order[a] = order[b]; order[b] = t; }
    const uint64_t all = __atomic_load_n(&total, __ATOMIC_RELAXED), sleeping = __atomic_load_n(&asleep, __ATOMIC_RELAXED);
    printf("Guest profile '%s': %.0f s, %llu samples running, %llu asleep (%.1f%% asleep), %llu lost\n", prefix,
           seconds, (unsigned long long)all, (unsigned long long)sleeping,
           all + sleeping ? 100.0 * (double)sleeping / (double)(all + sleeping) : 0.0, (unsigned long long)lost);
    for (unsigned a = 0; a < used && a < TOP; ++a) {
        const unsigned i = order[a];
        const unsigned kind = (unsigned)(keys[i] >> KIND_SHIFT);
        const uint64_t value = keys[i] & ((UINT64_C(1) << KIND_SHIFT) - 1);
        printf("  %5.1f%%  %-15s %#llx", all ? 100.0 * counts[i] / (double)all : 0.0,
               kind < 5 ? kind_names[kind] : "?", (unsigned long long)value);
        if (kind == K_HOST_CALL || kind == K_OTHER) {
            Dl_info where;
            if (dladdr((void *)(uintptr_t)value, &where) && where.dli_fname) {
                const char *file = strrchr(where.dli_fname, '/');
                printf("  %s+%#lx %s", file ? file + 1 : where.dli_fname,
                       (unsigned long)(value - (uintptr_t)where.dli_fbase), where.dli_sname ? where.dli_sname : "");
            }
            printf("\n");
        } else {
            printf("%s\n", value < image_size ? "  (image offset)" : "");
            if (kind == K_GUEST) print_code(value);
        }
    }
    fflush(stdout);
    for (unsigned i = 0; i < SLOTS; ++i) {
        __atomic_store_n(&keys[i], 0, __ATOMIC_RELAXED);
        __atomic_store_n(&counts[i], 0, __ATOMIC_RELAXED);
    }
    __atomic_store_n(&total, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&asleep, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&lost, 0, __ATOMIC_RELAXED);
}
static void *sampler(void *unused) {
    (void)unused;
    const char *hz_text = getenv("BB_GUEST_PROFILE_HZ");
    const long hz = hz_text && atol(hz_text) > 0 ? atol(hz_text) : 250;
    const struct timespec period = {0, 1000000000L / (hz > 1000 ? 1000 : hz)};
    struct timespec window_start;
    clock_gettime(CLOCK_MONOTONIC, &window_start);
    const pid_t pid = getpid();
    for (;;) {
        DIR *dir = opendir("/proc/self/task");
        struct dirent *entry;
        while (dir && (entry = readdir(dir))) {
            const pid_t tid = (pid_t)atoi(entry->d_name);
            char state = 0;
            if (tid <= 0 || !matching(tid, &state)) continue;
            if (state == 'R') syscall(SYS_tgkill, pid, tid, SIGPROF);
            else __atomic_fetch_add(&asleep, 1, __ATOMIC_RELAXED);
        }
        if (dir) closedir(dir);
        nanosleep(&period, NULL);
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        const double seconds = (double)(now.tv_sec - window_start.tv_sec) + (now.tv_nsec - window_start.tv_nsec) / 1e9;
        if (seconds >= 10.0) { report(seconds); window_start = now; }
    }
    return NULL;
}
void runtime_profile_start(const void *image, uint64_t size) {
    const char *spec = getenv("BB_GUEST_PROFILE");
    if (!spec || !*spec) return;
    snprintf(prefix, sizeof(prefix), "%s", spec);
    image_base = (uintptr_t)image;
    image_size = size;
    struct sigaction action = {0};
    action.sa_sigaction = on_sample;
    action.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&action.sa_mask);
    sigaction(SIGPROF, &action, NULL);
    pthread_t thread;
    if (pthread_create(&thread, NULL, sampler, NULL) == 0) pthread_detach(thread);
    printf("Guest profile: threads named '%s*', report every 10 s\n", prefix);
}
#endif
