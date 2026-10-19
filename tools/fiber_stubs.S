# Guest-side (x86-64) fiber context switches for hosts where the game's code runs in FEXCore
# (src/runtime_fiber.c, !GUEST_CPU_NATIVE). tools/gen_fiber_stubs.py assembles this file and
# writes src/runtime_fiber_stubs.inc; the host functions behind the HOST_* constants are patched
# in at run time.
#
# The host never switches stacks: each stub saves the caller's context on the guest stack in the
# layout of fiber_swap (MXCSR/x87 control word, r15..r12, rbx, rbp, then a resume address), calls
# a short host function that does the bookkeeping and answers the context to resume (or a negative
# error code), and loads that context itself. A resumed context continues at the resume address
# of the stub that suspended it ("after"), which reports the operation's result through a second
# host call. A fresh fiber starts at fiber_start with rbx = its GuestFiber.
#
# Host calls go through FEXCore's host stubs (syscall), which clobber rcx and r11.
        .intel_syntax noprefix
        .text

        .set HOST_PREPARE_RUN,    0x1111111111111101
        .set HOST_PREPARE_SWITCH, 0x1111111111111102
        .set HOST_PREPARE_RETURN, 0x1111111111111103
        .set HOST_AFTER_RUN,      0x1111111111111104
        .set HOST_AFTER_SWITCH,   0x1111111111111105
        .set HOST_BEGIN,          0x1111111111111106
        .set HOST_ENTRY_RETURNED, 0x1111111111111107

# Saves the context below the resume address and out pointer already pushed.
.macro SAVE
        push rbp
        push rbx
        push r12
        push r13
        push r14
        push r15
        sub rsp, 8
        stmxcsr [rsp]
        fnstcw [rsp+4]
.endm
# Loads the context at rsp and continues at its resume address.
.macro LOAD
        ldmxcsr [rsp]
        fldcw [rsp+4]
        add rsp, 8
        pop r15
        pop r14
        pop r13
        pop r12
        pop rbx
        pop rbp
        ret
.endm
# rax: a context to load, or a sign-extended error code (bit 63 set) to return to the caller.
.macro SWITCH_OR_FAIL
        bt rax, 63
        jc 1f
        mov rsp, rax
        LOAD
1:      add rsp, 8
        pop r15
        pop r14
        pop r13
        pop r12
        pop rbx
        pop rbp
        add rsp, 16
        ret
.endm
# Resume address: [rsp] is the operation's out pointer.
.macro AFTER host
        pop rdi
        sub rsp, 8
        movabs rax, \host
        call rax
        add rsp, 8
        ret
.endm

# sceFiberRun(fiber, arg_on_run_to, *arg_on_return)
        .globl stub_run
stub_run:
        push rdx
        lea rax, [rip+after_run]
        push rax
        SAVE
        mov rcx, rsi
        xor esi, esi
        xor edx, edx
        mov r8, rsp
        movabs rax, HOST_PREPARE_RUN
        call rax
        SWITCH_OR_FAIL
after_run:
        AFTER HOST_AFTER_RUN
        .globl stub_run_end
stub_run_end:

# _sceFiberAttachContextAndRun(fiber, addr_context, size_context, arg_on_run_to, *arg_on_return)
        .globl stub_run_with
stub_run_with:
        push r8
        lea rax, [rip+after_run_with]
        push rax
        SAVE
        mov r8, rsp
        movabs rax, HOST_PREPARE_RUN
        call rax
        SWITCH_OR_FAIL
after_run_with:
        AFTER HOST_AFTER_RUN
        .globl stub_run_with_end
stub_run_with_end:

# sceFiberSwitch(fiber, arg_on_run_to, *arg_on_run)
        .globl stub_switch
stub_switch:
        push rdx
        lea rax, [rip+after_switch]
        push rax
        SAVE
        mov rcx, rsi
        xor esi, esi
        xor edx, edx
        mov r8, rsp
        movabs rax, HOST_PREPARE_SWITCH
        call rax
        SWITCH_OR_FAIL
after_switch:
        AFTER HOST_AFTER_SWITCH
        .globl stub_switch_end
stub_switch_end:

# _sceFiberAttachContextAndSwitch(fiber, addr_context, size_context, arg_on_run_to, *arg_on_run)
        .globl stub_switch_with
stub_switch_with:
        push r8
        lea rax, [rip+after_switch_with]
        push rax
        SAVE
        mov r8, rsp
        movabs rax, HOST_PREPARE_SWITCH
        call rax
        SWITCH_OR_FAIL
after_switch_with:
        AFTER HOST_AFTER_SWITCH
        .globl stub_switch_with_end
stub_switch_with_end:

# sceFiberReturnToThread(arg_on_return, *arg_on_run)
        .globl stub_return
stub_return:
        push rsi
        lea rax, [rip+after_return]
        push rax
        SAVE
        mov rsi, rsp
        movabs rax, HOST_PREPARE_RETURN
        call rax
        SWITCH_OR_FAIL
after_return:
        AFTER HOST_AFTER_SWITCH
        .globl stub_return_end
stub_return_end:

# A fresh fiber: rbx = GuestFiber (entry at +8, arg_on_initialize at +16); rsp is 16-byte aligned.
        .globl stub_start
stub_start:
        and rsp, -16
        mov rdi, rbx
        movabs rax, HOST_BEGIN
        call rax
        mov rsi, rax
        mov rdi, [rbx+16]
        mov rax, [rbx+8]
        call rax
        mov rdi, rbx
        movabs rax, HOST_ENTRY_RETURNED
        call rax
        ud2
        .globl stub_start_end
stub_start_end:
