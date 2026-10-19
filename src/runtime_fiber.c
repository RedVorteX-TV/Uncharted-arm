/* libSceFiber: user-mode fibers on guest-allocated stacks.
 *
 * A fiber is suspended inside fiber_swap with its callee-saved registers (SysV), MXCSR and
 * the x87 control word pushed on its own stack; only that stack pointer is kept. Fibers move
 * between threads (a job system resumes them on any worker), so per-thread state is looked
 * up again after every resume and never cached across a switch.
 *
 * A fiber state becomes Idle only after its context has been saved: the fiber that runs next
 * (or the thread that gets control back) marks it, so another thread cannot resume a fiber
 * whose stack pointer is still being written.
 *
 * Fibers without their own context run on the calling thread's stack, below the frame of
 * sceFiberRun; they cannot be suspended and start again from their entry next time.
 *
 * Hosts other than x86-64 run the game's code in FEXCore (guest_cpu.h), where the host cannot
 * switch guest stacks from inside a host function: the switches are then x86-64 stubs run by the
 * guest itself (tools/fiber_stubs.S, runtime_fiber_stubs.inc), with the same saved-context layout,
 * and the host only keeps the books between them (the fiber_prepare_* and fiber_after_* below). */
#include "runtime.h"
#include "guest_cpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef _WIN32
#include <windows.h>
#endif
#ifndef GUEST_CPU_NATIVE
#include <pthread.h>
#include <sys/mman.h>
#endif

#define FIBER_ERROR_NULL ((int32_t)0x80590001)
#define FIBER_ERROR_ALIGNMENT ((int32_t)0x80590002)
#define FIBER_ERROR_RANGE ((int32_t)0x80590003)
#define FIBER_ERROR_INVALID ((int32_t)0x80590004)
#define FIBER_ERROR_PERMISSION ((int32_t)0x80590005)
#define FIBER_ERROR_STATE ((int32_t)0x80590006)

#define SIGNATURE_START 0xdef1649cu
#define SIGNATURE_END 0xb37592a0u
#define OPT_SIGNATURE 0xbb40e64du
#define STACK_SIGNATURE UINT64_C(0x7149f2ca7149f2ca)
#define STACK_FILL UINT64_C(0xdeadbeefdeadbeef)
#define CONTEXT_MINIMUM 512
#define FLAG_CONTEXT_SIZE_CHECK 0x10u
#define FLAG_SET_FPU 0x100u
#define SDK_350 0x03500000u

enum { STATE_RUN=1, STATE_IDLE=2, STATE_TERMINATED=3 };

typedef void (ABI *FiberEntry)(uint64_t arg_on_initialize,uint64_t arg_on_run);

/* The game reserves 256 bytes (8-byte aligned) for each fiber; the layout is ours. */
typedef struct {
    uint32_t magic_start, state;
    FiberEntry entry;
    uint64_t arg_on_initialize;
    unsigned char *addr_context;
    uint64_t size_context;
    char name[32];
    uint64_t saved_rsp;      /* suspended context, 0 when the fiber starts from its entry */
    uint32_t flags, magic_end;
} GuestFiber;
_Static_assert(sizeof(GuestFiber)<=256,"OrbisFiber is 256 bytes");

typedef struct {
    uint64_t size;
    FiberEntry entry;
    uint64_t arg_on_initialize;
    void *addr_context;
    uint64_t size_context;
    char name[32];
    uint64_t size_context_margin;
    uint8_t pad[48];
} GuestFiberInfo;
_Static_assert(sizeof(GuestFiberInfo)==128,"OrbisFiberInfo is 128 bytes");

typedef struct {
    GuestFiber *current, *previous;
    uint64_t thread_rsp;         /* the thread's context, suspended in sceFiberRun */
    uint64_t arg_on_run_to, arg_on_return;
#ifdef _WIN32
    void *stack_base, *stack_limit; /* the thread's own stack bounds in the TEB */
#endif
} FiberThread;
static _Thread_local FiberThread fiber_thread;
static uint32_t context_size_check;
static _Atomic uint64_t switches, fibers_started;

/* Not inlined: a resumed fiber may be on another thread, so the address is fetched again. */
static __attribute__((noinline)) FiberThread *this_thread(void) {
    FiberThread *t=&fiber_thread;
    __asm__ volatile("" : "+r"(t));
    return t;
}

#ifdef GUEST_CPU_NATIVE
/* fiber_swap(save, load): suspend here (*save = stack pointer) and resume the context whose
 * stack pointer is load. fiber_start begins a fresh context: rbx = GuestFiber. */
ABI void fiber_swap(uint64_t *save,uint64_t load);
ABI void fiber_start(void);
__asm__(".text\n"
        ".globl fiber_swap\nfiber_swap:\n"
        " push %rbp\n push %rbx\n push %r12\n push %r13\n push %r14\n push %r15\n"
        " sub $8,%rsp\n stmxcsr (%rsp)\n fnstcw 4(%rsp)\n"
        " mov %rsp,(%rdi)\n"
        " mov %rsi,%rsp\n"
        " ldmxcsr (%rsp)\n fldcw 4(%rsp)\n add $8,%rsp\n"
        " pop %r15\n pop %r14\n pop %r13\n pop %r12\n pop %rbx\n pop %rbp\n"
        " ret\n"
        ".globl fiber_start\nfiber_start:\n"
        " mov %rbx,%rdi\n and $-16,%rsp\n call fiber_main\n ud2\n");

#else
/* Guest-side switches (x86-64 bytes run by FEXCore): runtime_fiber_stubs.inc. */
#include "runtime_fiber_stubs.inc"
static unsigned char *stub_page;
#endif

static void fatal(const char *what,const GuestFiber *fiber) {
    fprintf(stderr,"STOP: fiber '%s': %s\n",fiber ? fiber->name : "?",what);
    runtime_report();
    fflush(NULL);
    _exit(24);
}

#ifdef GUEST_CPU_NATIVE
static void use_stack(const GuestFiber *fiber,FiberThread *t) {
#ifdef _WIN32
    NT_TIB *tib=(NT_TIB *)NtCurrentTeb();
    if (fiber && fiber->addr_context) {
        tib->StackBase=fiber->addr_context+fiber->size_context;
        tib->StackLimit=fiber->addr_context;
    } else {
        tib->StackBase=t->stack_base;
        tib->StackLimit=t->stack_limit;
    }
#else
    (void)fiber; (void)t;
#endif
}
#endif

/* The context the target fiber resumes in: its saved one, or a fresh frame at the top of its
 * stack. A fiber without a context of its own starts on the thread stack below `below`, a
 * stack pointer under which nothing live remains. */
static uint64_t context_of(GuestFiber *fiber,uint64_t below,const uint64_t *fpu_from) {
    uint64_t rsp=fiber->saved_rsp;
    if (rsp) { fiber->saved_rsp=0; return rsp; }
    uint64_t top=fiber->addr_context ? (uint64_t)(fiber->addr_context+fiber->size_context) : below-512;
    top&=~UINT64_C(15);
    uint64_t *frame=(uint64_t *)(top-64);
    uint32_t mxcsr=0x9fc0; uint16_t fpucw=0x037f;
    if (!(fiber->flags&FLAG_SET_FPU)) {
#ifdef GUEST_CPU_NATIVE
        (void)fpu_from;
        __asm__ volatile("stmxcsr %0" : "=m"(mxcsr));
        __asm__ volatile("fnstcw %0" : "=m"(fpucw));
#else
        /* The guest's current state: the first word of the context just saved by a stub. */
        mxcsr=(uint32_t)fpu_from[0]; fpucw=(uint16_t)(fpu_from[0]>>32);
#endif
    }
    frame[0]=(uint64_t)mxcsr | (uint64_t)fpucw<<32;
    frame[1]=frame[2]=frame[3]=frame[4]=0;          /* r15 r14 r13 r12 */
    frame[5]=(uint64_t)(uintptr_t)fiber;             /* rbx */
    frame[6]=0;                                      /* rbp: ends frame-pointer walks */
#ifdef GUEST_CPU_NATIVE
    frame[7]=(uint64_t)(uintptr_t)fiber_start;
#else
    frame[7]=(uint64_t)(uintptr_t)(stub_page+FIBER_STUB_START);
#endif
    ++fibers_started;
    return (uint64_t)(uintptr_t)frame;
}

static void check_stack(const GuestFiber *fiber) {
    if (fiber && fiber->addr_context && *(const uint64_t *)fiber->addr_context!=STACK_SIGNATURE)
        fatal("stack overflow (context size too small)",fiber);
}

/* Runs first in every context that gains control: the previous fiber's context is saved. */
static void arrived(FiberThread *t) {
    GuestFiber *previous=t->previous;
    t->previous=NULL;
    if (previous) __atomic_store_n(&previous->state,STATE_IDLE,__ATOMIC_RELEASE);
}

#ifdef GUEST_CPU_NATIVE
ABI __attribute__((used,noreturn)) void fiber_main(GuestFiber *fiber) {
    FiberThread *t=this_thread();
    arrived(t);
    fiber->entry(fiber->arg_on_initialize,t->arg_on_run_to);
    fatal("entry function returned",fiber);
    __builtin_unreachable();
}
#endif

static int valid(const GuestFiber *fiber) {
    return fiber->magic_start==SIGNATURE_START && fiber->magic_end==SIGNATURE_END;
}
static int32_t check_fiber(const GuestFiber *fiber) {
    if (!fiber) return FIBER_ERROR_NULL;
    if ((uintptr_t)fiber&7) return FIBER_ERROR_ALIGNMENT;
    return valid(fiber) ? 0 : FIBER_ERROR_INVALID;
}
static void prepare_context(GuestFiber *fiber) {
    if (!fiber->addr_context) return;
    uint64_t *stack=(uint64_t *)fiber->addr_context;
    stack[0]=STACK_SIGNATURE;
    if (fiber->flags&FLAG_CONTEXT_SIZE_CHECK)
        for (uint64_t i=1;i<fiber->size_context/8;++i) stack[i]=STACK_FILL;
}
static int32_t check_context(const void *addr,uint64_t size) {
    if ((uintptr_t)addr&15) return FIBER_ERROR_ALIGNMENT;
    if (size && size<CONTEXT_MINIMUM) return FIBER_ERROR_RANGE;
    if ((size&15) || (!addr)!=(!size)) return FIBER_ERROR_INVALID;
    return 0;
}
static int32_t attach_context(GuestFiber *fiber,void *addr,uint64_t size) {
    if (!addr && !size) return 0;
    int32_t e=check_context(addr,size);
    if (e) return e;
    if (!addr || fiber->addr_context) return FIBER_ERROR_INVALID;
    fiber->addr_context=addr; fiber->size_context=size;
    prepare_context(fiber);
    return 0;
}

static ABI int32_t fiber_initialize(GuestFiber *fiber,const char *name,FiberEntry entry,uint64_t arg,
                                    void *addr,uint64_t size,const uint32_t *opt,uint32_t build) {
    if (!fiber || !name || !entry) return FIBER_ERROR_NULL;
    if ((uintptr_t)fiber&7 || (opt && (uintptr_t)opt&7)) return FIBER_ERROR_ALIGNMENT;
    int32_t e=check_context(addr,size);
    if (e) return e;
    if (opt && *opt!=OPT_SIGNATURE) return FIBER_ERROR_INVALID;
    memset(fiber,0,sizeof(*fiber));
    strncpy(fiber->name,name,sizeof(fiber->name)-1);
    fiber->entry=entry; fiber->arg_on_initialize=arg;
    fiber->addr_context=addr; fiber->size_context=size;
    fiber->flags=(build>=SDK_350 ? FLAG_SET_FPU : 0) | (__atomic_load_n(&context_size_check,__ATOMIC_ACQUIRE) ? FLAG_CONTEXT_SIZE_CHECK : 0);
    prepare_context(fiber);
    fiber->magic_start=SIGNATURE_START; fiber->magic_end=SIGNATURE_END;
    __atomic_store_n(&fiber->state,STATE_IDLE,__ATOMIC_RELEASE);
    return 0;
}
static ABI int32_t fiber_opt_initialize(uint32_t *opt) {
    if (!opt) return FIBER_ERROR_NULL;
    if ((uintptr_t)opt&7) return FIBER_ERROR_ALIGNMENT;
    *opt=OPT_SIGNATURE; return 0;
}
static ABI int32_t fiber_finalize(GuestFiber *fiber) {
    int32_t e=check_fiber(fiber);
    if (e) return e;
    uint32_t expected=STATE_IDLE;
    return __atomic_compare_exchange_n(&fiber->state,&expected,STATE_TERMINATED,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)
           ? 0 : FIBER_ERROR_STATE;
}
static int claim(GuestFiber *fiber) {
    uint32_t expected=STATE_IDLE;
    return __atomic_compare_exchange_n(&fiber->state,&expected,STATE_RUN,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);
}

#ifdef GUEST_CPU_NATIVE
static ABI int32_t fiber_run_with(GuestFiber *fiber,void *addr,uint64_t size,uint64_t arg,uint64_t *ret) {
    int32_t e=check_fiber(fiber);
    if (e) return e;
    if ((uintptr_t)addr&15) return FIBER_ERROR_ALIGNMENT;
    FiberThread *t=this_thread();
    if (t->current) return FIBER_ERROR_PERMISSION;
    if ((e=attach_context(fiber,addr,size))) return e;
    if (!claim(fiber)) return FIBER_ERROR_STATE;
#ifdef _WIN32
    NT_TIB *tib=(NT_TIB *)NtCurrentTeb();
    t->stack_base=tib->StackBase; t->stack_limit=tib->StackLimit;
#endif
    t->current=fiber; t->previous=NULL; t->arg_on_run_to=arg; t->arg_on_return=0;
    uint64_t here;
    __asm__ volatile("mov %%rsp,%0" : "=r"(here));
    uint64_t target=context_of(fiber,here,NULL);
    use_stack(fiber,t);
    ++switches;
    fiber_swap(&t->thread_rsp,target);
    /* Back on this thread (it never left): a fiber returned to it. */
    t=this_thread();
    use_stack(NULL,t);
    GuestFiber *last=t->current;
    t->current=NULL;
    if (last) __atomic_store_n(&last->state,STATE_IDLE,__ATOMIC_RELEASE);
    if (ret) *ret=t->arg_on_return;
    return 0;
}
static ABI int32_t fiber_run(GuestFiber *fiber,uint64_t arg,uint64_t *ret) { return fiber_run_with(fiber,NULL,0,arg,ret); }

static ABI int32_t fiber_switch_with(GuestFiber *fiber,void *addr,uint64_t size,uint64_t arg,uint64_t *arg_on_run) {
    int32_t e=check_fiber(fiber);
    if (e) return e;
    if ((uintptr_t)addr&15) return FIBER_ERROR_ALIGNMENT;
    FiberThread *t=this_thread();
    GuestFiber *current=t->current;
    if (!current) return FIBER_ERROR_PERMISSION;
    if ((e=attach_context(fiber,addr,size))) return e;
    if (!claim(fiber)) return FIBER_ERROR_STATE;
    check_stack(current);
    t->previous=current; t->current=fiber; t->arg_on_run_to=arg;
    uint64_t target=context_of(fiber,t->thread_rsp,NULL);
    use_stack(fiber,t);
    ++switches;
    if (!current->addr_context) {
        uint64_t discarded;
        fiber_swap(&discarded,target); /* no context to come back to */
        __builtin_unreachable();
    }
    fiber_swap(&current->saved_rsp,target);
    t=this_thread();
    arrived(t);
    if (arg_on_run) *arg_on_run=t->arg_on_run_to;
    return 0;
}
static ABI int32_t fiber_switch(GuestFiber *fiber,uint64_t arg,uint64_t *arg_on_run) { return fiber_switch_with(fiber,NULL,0,arg,arg_on_run); }

static ABI int32_t fiber_return_to_thread(uint64_t arg_on_return,uint64_t *arg_on_run) {
    FiberThread *t=this_thread();
    GuestFiber *current=t->current;
    if (!current) return FIBER_ERROR_PERMISSION;
    check_stack(current);
    t->arg_on_return=arg_on_return;
    ++switches;
    if (!current->addr_context) {
        uint64_t discarded;
        fiber_swap(&discarded,t->thread_rsp);
        __builtin_unreachable();
    }
    fiber_swap(&current->saved_rsp,t->thread_rsp);
    /* Resumed later by sceFiberRun or sceFiberSwitch, possibly on another thread. */
    t=this_thread();
    arrived(t);
    if (arg_on_run) *arg_on_run=t->arg_on_run_to;
    return 0;
}

#else
/* FEXCore hosts: the bookkeeping of the guest-side stubs. A prepare_* function answers the saved
 * context (a guest stack pointer below 1 TiB) the stub loads, or a negative error code it returns
 * to the game; `saved` is the caller's context the stub has just pushed. An after_* function runs
 * in a context that gains control again (its stub's resume address) and reports the result. */
static uint64_t fail_with(int32_t error) { return (uint64_t)(int64_t)error; }
static uint64_t fiber_prepare_run(GuestFiber *fiber,void *addr,uint64_t size,uint64_t arg,uint64_t saved) {
    int32_t e=check_fiber(fiber);
    if (e) return fail_with(e);
    if ((uintptr_t)addr&15) return fail_with(FIBER_ERROR_ALIGNMENT);
    FiberThread *t=this_thread();
    if (t->current) return fail_with(FIBER_ERROR_PERMISSION);
    if ((e=attach_context(fiber,addr,size))) return fail_with(e);
    if (!claim(fiber)) return fail_with(FIBER_ERROR_STATE);
    t->current=fiber; t->previous=NULL; t->arg_on_run_to=arg; t->arg_on_return=0;
    t->thread_rsp=saved;
    ++switches;
    return context_of(fiber,saved,(const uint64_t *)(uintptr_t)saved);
}
static uint64_t fiber_after_run(uint64_t *ret) {
    /* Back on the thread that called sceFiberRun: a fiber returned to it. */
    FiberThread *t=this_thread();
    GuestFiber *last=t->current;
    t->current=NULL;
    if (last) __atomic_store_n(&last->state,STATE_IDLE,__ATOMIC_RELEASE);
    if (ret) *ret=t->arg_on_return;
    return 0;
}
static uint64_t fiber_prepare_switch(GuestFiber *fiber,void *addr,uint64_t size,uint64_t arg,uint64_t saved) {
    int32_t e=check_fiber(fiber);
    if (e) return fail_with(e);
    if ((uintptr_t)addr&15) return fail_with(FIBER_ERROR_ALIGNMENT);
    FiberThread *t=this_thread();
    GuestFiber *current=t->current;
    if (!current) return fail_with(FIBER_ERROR_PERMISSION);
    if ((e=attach_context(fiber,addr,size))) return fail_with(e);
    if (!claim(fiber)) return fail_with(FIBER_ERROR_STATE);
    check_stack(current);
    t->previous=current; t->current=fiber; t->arg_on_run_to=arg;
    const uint64_t target=context_of(fiber,t->thread_rsp,(const uint64_t *)(uintptr_t)saved);
    ++switches;
    /* No context of its own: nothing to come back to (it starts from its entry next time). */
    if (current->addr_context) current->saved_rsp=saved;
    return target;
}
static uint64_t fiber_after_switch(uint64_t *arg_on_run) {
    /* Resumed by sceFiberRun or sceFiberSwitch, possibly on another thread. */
    FiberThread *t=this_thread();
    arrived(t);
    if (arg_on_run) *arg_on_run=t->arg_on_run_to;
    return 0;
}
static uint64_t fiber_prepare_return(uint64_t arg_on_return,uint64_t saved) {
    FiberThread *t=this_thread();
    GuestFiber *current=t->current;
    if (!current) return fail_with(FIBER_ERROR_PERMISSION);
    check_stack(current);
    t->arg_on_return=arg_on_return;
    ++switches;
    if (current->addr_context) current->saved_rsp=saved;
    return t->thread_rsp;
}
static uint64_t fiber_begin(GuestFiber *fiber) {
    (void)fiber;
    FiberThread *t=this_thread();
    arrived(t);
    return t->arg_on_run_to;
}
static void fiber_entry_returned(GuestFiber *fiber) { fatal("entry function returned",fiber); }

/* The stubs in guest-executable memory below 1 TiB, their host calls bound once. */
static void init_stubs(void) {
    unsigned char *page=runtime_low_map(4096,PROT_READ|PROT_WRITE);
    if (!page) { fputs("STOP: cannot allocate fiber stubs\n",stderr); exit(21); }
    _Static_assert(sizeof(fiber_stub_code)<=4096,"fiber stubs fit one page");
    memcpy(page,fiber_stub_code,sizeof(fiber_stub_code));
    void *const host[HOST_COUNT]={
        [HOST_PREPARE_RUN]=(void *)fiber_prepare_run, [HOST_PREPARE_SWITCH]=(void *)fiber_prepare_switch,
        [HOST_PREPARE_RETURN]=(void *)fiber_prepare_return, [HOST_AFTER_RUN]=(void *)fiber_after_run,
        [HOST_AFTER_SWITCH]=(void *)fiber_after_switch, [HOST_BEGIN]=(void *)fiber_begin,
        [HOST_ENTRY_RETURNED]=(void *)fiber_entry_returned,
    };
    for (size_t i=0;i<sizeof(fiber_stub_patches)/sizeof(*fiber_stub_patches);++i) {
        const uint64_t address=guest_cpu_host_function(host[fiber_stub_patches[i].host]);
        memcpy(page+fiber_stub_patches[i].offset,&address,8);
    }
    if (mprotect(page,4096,PROT_READ|PROT_EXEC)) { perror("STOP: mprotect(fiber stubs)"); exit(21); }
    guest_cpu_code((uintptr_t)page,4096);
    stub_page=page;
}
static unsigned char *stubs(void) {
    static pthread_once_t once=PTHREAD_ONCE_INIT;
    pthread_once(&once,init_stubs);
    return stub_page;
}
int runtime_fiber_guest_code(uintptr_t address) {
    return stub_page && address-(uintptr_t)stub_page<4096;
}
#endif

static ABI int32_t fiber_get_self(GuestFiber **fiber) {
    if (!fiber) return FIBER_ERROR_NULL;
    GuestFiber *current=this_thread()->current;
    if (!current) return FIBER_ERROR_PERMISSION;
    *fiber=current; return 0;
}
static ABI int32_t fiber_get_info(GuestFiber *fiber,GuestFiberInfo *info) {
    if (!fiber || !info) return FIBER_ERROR_NULL;
    if ((uintptr_t)fiber&7 || (uintptr_t)info&7) return FIBER_ERROR_ALIGNMENT;
    if (info->size!=sizeof(*info) || !valid(fiber)) return FIBER_ERROR_INVALID;
    info->entry=fiber->entry; info->arg_on_initialize=fiber->arg_on_initialize;
    info->addr_context=fiber->addr_context; info->size_context=fiber->size_context;
    memcpy(info->name,fiber->name,sizeof(info->name));
    info->size_context_margin=(uint64_t)-1;
    if ((fiber->flags&FLAG_CONTEXT_SIZE_CHECK) && fiber->addr_context) {
        const uint64_t *stack=(const uint64_t *)fiber->addr_context;
        uint64_t i=1, words=fiber->size_context/8;
        while (i<words && stack[i]==STACK_FILL) ++i;
        info->size_context_margin=(i-1)*8;
    }
    return 0;
}
static ABI int32_t fiber_rename(GuestFiber *fiber,const char *name) {
    if (!fiber || !name) return FIBER_ERROR_NULL;
    int32_t e=check_fiber(fiber);
    if (e) return e;
    strncpy(fiber->name,name,sizeof(fiber->name)-1);
    fiber->name[sizeof(fiber->name)-1]=0;
    return 0;
}
static ABI int32_t fiber_start_size_check(uint32_t flags) {
    if (flags) return FIBER_ERROR_INVALID;
    uint32_t expected=0;
    return __atomic_compare_exchange_n(&context_size_check,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE) ? 0 : FIBER_ERROR_STATE;
}
static ABI int32_t fiber_stop_size_check(void) {
    uint32_t expected=1;
    return __atomic_compare_exchange_n(&context_size_check,&expected,0,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE) ? 0 : FIBER_ERROR_STATE;
}

static const RuntimeExport exports[]={
    {"_sceFiberInitializeImpl",fiber_initialize}, {"sceFiberOptParamInitialize",fiber_opt_initialize},
    {"sceFiberFinalize",fiber_finalize},
#ifdef GUEST_CPU_NATIVE
    {"sceFiberRun",fiber_run}, {"sceFiberSwitch",fiber_switch},
    {"_sceFiberAttachContextAndRun",fiber_run_with}, {"_sceFiberAttachContextAndSwitch",fiber_switch_with},
    {"sceFiberReturnToThread",fiber_return_to_thread},
#endif
    {"sceFiberGetSelf",fiber_get_self},
    {"sceFiberGetInfo",fiber_get_info}, {"sceFiberRename",fiber_rename},
    {"sceFiberStartContextSizeCheck",fiber_start_size_check}, {"sceFiberStopContextSizeCheck",fiber_stop_size_check},
};
#ifdef GUEST_CPU_NATIVE
uintptr_t runtime_fiber_resolve(const char *name) { return RUNTIME_LOOKUP(exports,name); }
int runtime_fiber_guest_code(uintptr_t address) { (void)address; return 0; }
#else
/* The switching functions are guest code (the stubs); the rest are host functions. */
static const struct { const char *name; unsigned offset; } stub_exports[]={
    {"sceFiberRun",FIBER_STUB_RUN}, {"sceFiberSwitch",FIBER_STUB_SWITCH},
    {"_sceFiberAttachContextAndRun",FIBER_STUB_RUN_WITH}, {"_sceFiberAttachContextAndSwitch",FIBER_STUB_SWITCH_WITH},
    {"sceFiberReturnToThread",FIBER_STUB_RETURN},
};
uintptr_t runtime_fiber_resolve(const char *name) {
    const char *symbol=runtime_symbol(name);
    for (size_t i=0;symbol && i<sizeof(stub_exports)/sizeof(*stub_exports);++i)
        if (!strcmp(symbol,stub_exports[i].name)) return (uintptr_t)(stubs()+stub_exports[i].offset);
    return RUNTIME_LOOKUP(exports,name);
}
#endif
void runtime_fiber_report(void) {
    printf("Runtime: fibers started=%llu, switches=%llu\n",(unsigned long long)fibers_started,(unsigned long long)switches);
}
