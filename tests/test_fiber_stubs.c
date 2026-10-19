/* The FEXCore-host fiber path (runtime_fiber.c without GUEST_CPU_NATIVE: guest-side x86-64 switch
 * stubs plus host bookkeeping), run on an x86-64 host where "guest" code is native code: the stubs
 * execute directly and call the host functions through plain pointers. Built with
 * -DBB_FORCE_GUEST_CPU; see tools/test_fiber_stubs.sh.
 *
 * Checks: run/switch/return round trips with arguments in both directions, a fiber suspended on
 * one thread and resumed on another (a job system's workers), the context attached at run time,
 * error codes for bad calls (no switch happens), and MXCSR carried into a new fiber. */
#define _GNU_SOURCE
#include "runtime.h"
#include "guest_cpu.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <xmmintrin.h>

/* guest_cpu.h with the guest being the host CPU. */
void guest_cpu_init(void *(*map)(size_t, int)) { (void)map; }
void guest_cpu_code(uintptr_t base, size_t size) { (void)base; (void)size; }
uintptr_t guest_cpu_host_function(void *fn) { return (uintptr_t)fn; }

/* The runtime pieces runtime_fiber.c uses. */
void *runtime_low_map(size_t size, int prot) {
    void *p = mmap(NULL, size, prot, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    return p == MAP_FAILED ? NULL : p;
}
const char *runtime_symbol(const char *nid) { return nid; } /* tests pass symbol names */
uintptr_t runtime_lookup(const RuntimeExport *table, size_t count, const char *nid) {
    for (size_t i = 0; i < count; ++i) if (!strcmp(nid, table[i].name)) return (uintptr_t)table[i].function;
    return 0;
}
void runtime_report(void) {}

typedef int32_t (ABI *Init)(void *, const char *, void *, uint64_t, void *, uint64_t, const uint32_t *, uint32_t);
typedef int32_t (ABI *Run)(void *, uint64_t, uint64_t *);
typedef int32_t (ABI *RunWith)(void *, void *, uint64_t, uint64_t, uint64_t *);
typedef int32_t (ABI *Switch)(void *, uint64_t, uint64_t *);
typedef int32_t (ABI *Return)(uint64_t, uint64_t *);
typedef int32_t (ABI *Self)(void **);
static Init init; static Run run; static RunWith run_with; static Switch switch_to; static Return return_to; static Self self;

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static _Alignas(8) unsigned char fiber_a[256], fiber_b[256], fiber_c[256];
static void *stack_a, *stack_b, *stack_c;
enum { STACK = 64 * 1024 };
static volatile uint64_t trace[64];
static volatile int traced;
static void note(uint64_t v) { trace[traced++] = v; }

/* A: returns to the thread, then switches to B, then returns again (resumed by B's switch). */
static ABI void entry_a(uint64_t init_arg, uint64_t run_arg) {
    note(0xa0 + init_arg); note(run_arg);
    void *me = NULL; CHECK(self(&me) == 0 && me == fiber_a);
    uint64_t back = 0;
    CHECK(return_to(0x111, &back) == 0);           /* thread gets 0x111 */
    note(back);                                    /* run_arg of the next sceFiberRun */
    CHECK(switch_to(fiber_b, 0x222, &back) == 0);  /* B starts with 0x222 */
    note(back);                                    /* from B's switch back */
    CHECK(return_to(0x333, &back) == 0);
    note(back);
    CHECK(return_to(0x444, NULL) == 0);
    for (;;) CHECK(return_to(0, NULL) == 0);
}
static ABI void entry_b(uint64_t init_arg, uint64_t run_arg) {
    note(0xb0 + init_arg); note(run_arg);
    uint64_t back = 0;
    /* Errors leave the caller where it is: B is running, A is idle. */
    CHECK(switch_to(fiber_b, 0, &back) == (int32_t)0x80590006); /* itself: not idle */
    CHECK(run(fiber_a, 0, NULL) == (int32_t)0x80590005);         /* sceFiberRun inside a fiber */
    CHECK(switch_to(fiber_a, 0x555, &back) == 0);                /* A resumes in its switch with 0x555 */
    for (;;) CHECK(return_to(0, NULL) == 0);
}
static ABI void entry_c(uint64_t init_arg, uint64_t run_arg) {
    note(0xc0 + init_arg); note(run_arg);
    note(_mm_getcsr() & 0xffc0);
    uint64_t back = 0;
    CHECK(return_to(0x777, &back) == 0); /* resumed on another thread */
    note(back);
    note(gettid());
    for (;;) CHECK(return_to(0x888, NULL) == 0);
}


/* Stress: workers race for the same fibers; each fiber counts its runs and returns to whichever
 * thread ran it, so it keeps moving between threads. A fiber resumed before its context was saved
 * would corrupt its counter or crash. */
enum { WORKERS = 6, FIBERS = 32, ROUNDS = 4000 };
static _Alignas(8) unsigned char pool[FIBERS][256];
static volatile uint64_t counts[FIBERS], moves[FIBERS];
static ABI void stress_entry(uint64_t index, uint64_t run_arg) {
    (void)run_arg;
    uint64_t last = (uint64_t)gettid();
    for (;;) {
        ++counts[index];
        const uint64_t now = (uint64_t)gettid();
        if (now != last) { ++moves[index]; last = now; }
        uint64_t next = 0;
        if (return_to(index, &next)) { printf("FAIL stress: sceFiberReturnToThread\n"); ++failures; }
    }
}
static _Atomic uint64_t runs;
static void *worker(void *arg) {
    unsigned seed = (unsigned)(uintptr_t)arg;
    for (int i = 0; i < ROUNDS; ++i) {
        seed = seed * 1103515245u + 12345u;
        const unsigned f = (seed >> 8) % FIBERS;
        uint64_t ret = 0;
        const int32_t e = run(pool[f], 0, &ret);
        if (e == 0) { if (ret != f) { printf("FAIL stress: fiber %u returned %lu\n", f, (unsigned long)ret); ++failures; } ++runs; }
        else if (e != (int32_t)0x80590006) { printf("FAIL stress: run error %#x\n", (unsigned)e); ++failures; }
    }
    return NULL;
}
static void stress(void) {
    for (int f = 0; f < FIBERS; ++f)
        CHECK(init(pool[f], "S", (void *)stress_entry, (uint64_t)f, aligned_alloc(16, STACK), STACK, NULL, 0) == 0);
    pthread_t t[WORKERS];
    for (int w = 0; w < WORKERS; ++w) pthread_create(&t[w], NULL, worker, (void *)(uintptr_t)(w + 1));
    for (int w = 0; w < WORKERS; ++w) pthread_join(t[w], NULL);
    uint64_t total = 0, moved = 0;
    for (int f = 0; f < FIBERS; ++f) { total += counts[f]; moved += moves[f]; }
    CHECK(total == runs);
    CHECK(moved > 0);
    printf("stress: %lu runs, %lu resumes on another thread\n", (unsigned long)total, (unsigned long)moved);
}

static void *other_thread(void *arg) {
    (void)arg;
    uint64_t ret = 0;
    CHECK(run(fiber_c, 0x999, &ret) == 0);
    CHECK(ret == 0x888);
    return NULL;
}

int main(void) {
    init = (Init)runtime_fiber_resolve("_sceFiberInitializeImpl");
    run = (Run)runtime_fiber_resolve("sceFiberRun");
    run_with = (RunWith)runtime_fiber_resolve("_sceFiberAttachContextAndRun");
    switch_to = (Switch)runtime_fiber_resolve("sceFiberSwitch");
    return_to = (Return)runtime_fiber_resolve("sceFiberReturnToThread");
    self = (Self)runtime_fiber_resolve("sceFiberGetSelf");
    CHECK(init && run && run_with && switch_to && return_to && self);
    CHECK(runtime_fiber_guest_code((uintptr_t)run) && !runtime_fiber_guest_code((uintptr_t)init));
    stack_a = aligned_alloc(16, STACK); stack_b = aligned_alloc(16, STACK); stack_c = aligned_alloc(16, STACK);

    CHECK(init(fiber_a, "A", (void *)entry_a, 1, stack_a, STACK, NULL, 0) == 0);
    CHECK(init(fiber_b, "B", (void *)entry_b, 2, stack_b, STACK, NULL, 0) == 0);
    uint64_t ret = 0;
    CHECK(run(NULL, 0, &ret) == (int32_t)0x80590001);   /* errors return without switching */

    CHECK(run(fiber_a, 0x10, &ret) == 0); CHECK(ret == 0x111);
    CHECK(run(fiber_a, 0x20, &ret) == 0); CHECK(ret == 0x333);   /* A -> B -> A -> thread */
    CHECK(run(fiber_a, 0x30, &ret) == 0); CHECK(ret == 0x444);
    static const uint64_t expected[] = {0xa1, 0x10, 0x20, 0xb2, 0x222, 0x555, 0x30};
    CHECK(traced == (int)(sizeof(expected) / sizeof(*expected)));
    for (int i = 0; i < traced && i < (int)(sizeof(expected) / sizeof(*expected)); ++i)
        if (trace[i] != expected[i]) { printf("FAIL trace[%d]=%#lx, expected %#lx\n", i, (unsigned long)trace[i], (unsigned long)expected[i]); ++failures; }

    /* C: context attached at run time, flush-to-zero MXCSR inherited, resumed on another thread. */
    traced = 0;
    CHECK(init(fiber_c, "C", (void *)entry_c, 3, NULL, 0, NULL, 0) == 0);
    const unsigned csr = _mm_getcsr();
    _mm_setcsr((csr & ~0xffc0u) | 0x8040u);
    CHECK(run_with(fiber_c, stack_c, STACK, 0x40, &ret) == 0); CHECK(ret == 0x777);
    _mm_setcsr(csr);
    pthread_t thread;
    pthread_create(&thread, NULL, other_thread, NULL);
    pthread_join(thread, NULL);
    CHECK(traced == 5);
    CHECK(trace[0] == 0xc3 && trace[1] == 0x40 && trace[2] == 0x8040 && trace[3] == 0x999);
    CHECK(trace[4] != (uint64_t)gettid());

    stress();
    runtime_fiber_report();
    printf(failures ? "fiber stubs: %d FAILED\n" : "fiber stubs: all checks passed\n", failures);
    return failures != 0;
}
