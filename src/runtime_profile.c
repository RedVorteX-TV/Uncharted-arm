/* SPDX-License-Identifier: GPL-2.0-or-later */
/* BB_GUEST_PROFILE=prefix: a sampling profiler of guest threads whose name starts with `prefix`
 * (e.g. NdJob). Every 4 ms each such thread that is running gets a SIGPROF; the handler counts
 * where it was: guest code (image offset), host code called by the guest (host PC and the guest
 * return address), or neither (host PC: the guest CPU's own code). Threads that are asleep are
 * counted without a signal, so blocking calls are not interrupted. Every 10 s the hottest places
 * are printed with the guest code bytes there. BB_GUEST_PROFILE_HZ overrides the 250 Hz rate.
 *
 * BB_THREAD_STATS=1: every 10 s the CPU time of each busy thread (user + system, % of one core)
 * and the clocks of the CPU clusters and the GPU go to the log, so a game limited by one thread
 * shows which (no terminal needed: the numbers come from /proc and /sys). */
#define _GNU_SOURCE
#include "runtime.h"
#include "guest_cpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
void runtime_profile_start(const void *image, uint64_t size) { (void)image; (void)size; }
void runtime_thread_stats_start(void) {}
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

/* ---- BB_THREAD_STATS ---- */
enum { STAT_THREADS = 256 };
static struct { int tid; unsigned long long ticks; char name[20]; } stat_seen[STAT_THREADS];
static int read_ticks(int tid, char *name, size_t name_size, unsigned long long *ticks, char *state, int *cpu) {
    char path[64], buffer[512];
    snprintf(path, sizeof(path), "/proc/self/task/%d/stat", tid);
    const int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    const ssize_t n = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);
    if (n <= 0) return 0;
    buffer[n] = 0;
    char *open_paren = strchr(buffer, '('), *close_paren = strrchr(buffer, ')');
    if (!open_paren || !close_paren || close_paren < open_paren) return 0;
    *close_paren = 0;
    snprintf(name, name_size, "%s", open_paren + 1);
    *state = close_paren[2];
    /* after ") ": state(3) ppid pgrp session tty tpgid flags minflt cminflt majflt cmajflt utime(14) stime(15) */
    unsigned long long utime = 0, stime = 0;
    if (sscanf(close_paren + 2, "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %llu %llu", &utime, &stime) != 2) return 0;
    *ticks = utime + stime;
    /* processor (field 39): the CPU the thread last ran on. */
    *cpu = -1;
    int field = 3;
    for (char *p = close_paren + 2; *p; ++p) {
        if (*p != ' ') continue;
        if (++field == 39) { *cpu = atoi(p + 1); break; }
    }
    return 1;
}
static void print_clocks(void) {
    char line[512] = "", path[320], value[64];
    size_t used = 0;
    unsigned last_policy = ~0u;
    for (unsigned cpu = 0; cpu < 16; ++cpu) { /* one value per cluster (cpufreq policy) */
        snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%u/cpufreq/related_cpus", cpu);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        unsigned first = cpu;
        if (fscanf(f, "%u", &first) != 1) first = cpu;
        fclose(f);
        if (first == last_policy) continue;
        last_policy = first;
        snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%u/cpufreq/scaling_cur_freq", cpu);
        if (!(f = fopen(path, "r"))) continue;
        unsigned long khz = 0;
        if (fscanf(f, "%lu", &khz) == 1)
            used += (size_t)snprintf(line + used, sizeof(line) - used, " cpu%u+ %lu MHz", cpu, khz / 1000);
        fclose(f);
        if (used >= sizeof(line) - 40) break;
    }
    DIR *dir = opendir("/sys/class/devfreq");
    struct dirent *entry;
    while (dir && (entry = readdir(dir))) { /* the Adreno's devfreq node: *.gpu */
        if (!strstr(entry->d_name, "gpu")) continue;
        snprintf(path, sizeof(path), "/sys/class/devfreq/%s/cur_freq", entry->d_name);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        if (fgets(value, sizeof(value), f))
            used += (size_t)snprintf(line + used, sizeof(line) - used, " gpu %llu MHz", strtoull(value, NULL, 10) / 1000000ull);
        fclose(f);
        break;
    }
    if (dir) closedir(dir);
    if (used) printf("  clocks:%s\n", line);
}
static void *thread_stats(void *unused) {
    (void)unused;
    const long hz = sysconf(_SC_CLK_TCK) > 0 ? sysconf(_SC_CLK_TCK) : 100;
    struct timespec before;
    clock_gettime(CLOCK_MONOTONIC, &before);
    for (int first = 1;; first = 0) { /* the first pass only takes the starting counts */
        if (!first) {
            const struct timespec period = {10, 0};
            nanosleep(&period, NULL);
        }
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        const double seconds = (double)(now.tv_sec - before.tv_sec) + (now.tv_nsec - before.tv_nsec) / 1e9;
        before = now;
        struct { char name[20]; double percent; char state; int cpu; } busy[STAT_THREADS];
        int busy_count = 0;
        double sum = 0;
        DIR *dir = opendir("/proc/self/task");
        struct dirent *entry;
        while (dir && (entry = readdir(dir))) {
            const int tid = atoi(entry->d_name);
            char name[20], state = 0;
            unsigned long long ticks;
            int cpu = -1;
            if (tid <= 0 || !read_ticks(tid, name, sizeof(name), &ticks, &state, &cpu)) continue;
            int slot = -1, free_slot = -1;
            for (int i = 0; i < STAT_THREADS; ++i) {
                if (stat_seen[i].tid == tid) { slot = i; break; }
                if (!stat_seen[i].tid && free_slot < 0) free_slot = i;
            }
            unsigned long long previous = 0;
            if (slot < 0) {
                if (free_slot < 0) continue;
                slot = free_slot;
                stat_seen[slot].tid = tid;
                previous = ticks; /* first sight: counted from the next report on */
            } else {
                previous = stat_seen[slot].ticks;
            }
            stat_seen[slot].ticks = ticks;
            const double percent = seconds > 0 ? 100.0 * (double)(ticks - previous) / (double)hz / seconds : 0.0;
            sum += percent;
            if (percent >= 3.0 && busy_count < STAT_THREADS) {
                snprintf(busy[busy_count].name, sizeof(busy[busy_count].name), "%s", name);
                busy[busy_count].percent = percent;
                busy[busy_count].state = state;
                busy[busy_count].cpu = cpu;
                ++busy_count;
            }
        }
        if (dir) closedir(dir);
        if (first) continue;
        for (int a = 0; a < busy_count; ++a) /* busiest first */
            for (int b = a + 1; b < busy_count; ++b)
                if (busy[b].percent > busy[a].percent) { __typeof__(busy[0]) t = busy[a]; busy[a] = busy[b]; busy[b] = t; }
        printf("Thread stats (%.0f s, %% of one core, all threads %.0f%%):\n", seconds, sum);
        for (int a = 0; a < busy_count && a < 24; ++a)
            printf("  %5.1f%%  %-16s cpu%d\n", busy[a].percent, busy[a].name, busy[a].cpu);
        print_clocks();
        fflush(stdout);
    }
    return NULL;
}
void runtime_thread_stats_start(void) {
    const char *spec = getenv("BB_THREAD_STATS");
    if (!spec || !*spec || !strcmp(spec, "0")) return;
    pthread_t thread;
    if (pthread_create(&thread, NULL, thread_stats, NULL) == 0) {
        pthread_setname_np(thread, "bb:ThreadStats");
        pthread_detach(thread);
        printf("Thread stats: CPU time per thread and clocks every 10 s (BB_THREAD_STATS)\n");
    }
}
#endif
