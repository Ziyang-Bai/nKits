.syntax unified
.arm
.text
.align 2
.global resident_lock
.type resident_lock, %function
resident_lock:
    mrs r0, cpsr
    orr r1, r0, #0xc0
    msr cpsr_c, r1
    bx lr
.size resident_lock, .-resident_lock

.global resident_unlock
.type resident_unlock, %function
resident_unlock:
    msr cpsr_c, r0
    bx lr
.size resident_unlock, .-resident_unlock

.global resident_publish
.type resident_publish, %function
resident_publish:
    push {r4, lr}
    mrs r4, cpsr
    orr ip, r4, #0xc0
    msr cpsr_c, ip
    ldr ip, [r0]
    cmp ip, r2
    ldreq ip, [r0, #4]
    cmpeq ip, r3
    bne 1f
    ldr r2, =0xe51ff004
    str r1, [r0, #4]
    str r2, [r0]
    bl clear_cache
    mov r0, #1
    b 2f
1:
    mov r0, #0
2:
    msr cpsr_c, r4
    pop {r4, pc}
.size resident_publish, .-resident_publish

.data
.align 2
.global resident_display_gate
.type resident_display_gate, %object
resident_display_gate:
    .word 0xe59ff004
    .word 0x6e4b5253
    .word 8
    .word 0
    .word 0
.size resident_display_gate, .-resident_display_gate

.global resident_time_gate
.type resident_time_gate, %object
resident_time_gate:
    .word 0xe59ff004
    .word 0x6e4b5253
    .word 8
    .word 0
    .word 0
.size resident_time_gate, .-resident_time_gate

.global resident_event_gate
.type resident_event_gate, %object
resident_event_gate:
    .word 0xe59ff004
    .word 0x6e4b5253
    .word 8
    .word 0
    .word 0
.size resident_event_gate, .-resident_event_gate

.global resident_event_trampoline
.type resident_event_trampoline, %object
resident_event_trampoline:
    .space 20
.size resident_event_trampoline, .-resident_event_trampoline

.global resident_activity_gate
.type resident_activity_gate, %object
resident_activity_gate:
    .word 0xe59ff004
    .word 0x6e4b5253
    .word 8
    .word 0
    .word 0
.size resident_activity_gate, .-resident_activity_gate

.global resident_activity_trampoline
.type resident_activity_trampoline, %object
resident_activity_trampoline:
    .space 20
.size resident_activity_trampoline, .-resident_activity_trampoline

.section .note.GNU-stack,"",%progbits
