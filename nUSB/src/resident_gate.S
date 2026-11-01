.syntax unified
.arm
.text
.align 2
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
.global resident_gate_words
.type resident_gate_words, %object
resident_gate_words:
    .word 0xe59ff004
    .word 0x6e555342
    .word 4
    .word 0
    .word 0
.size resident_gate_words, .-resident_gate_words

.align 2
.global resident_trampoline
.type resident_trampoline, %object
resident_trampoline:
    .word 0
    .word 0
    .word 0xe51ff004
    .word 0
.size resident_trampoline, .-resident_trampoline

.section .note.GNU-stack,"",%progbits
