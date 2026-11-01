.syntax unified
.arm
.text
.align 2
.global explorer_gate
.type explorer_gate, %function
explorer_gate:
    b explorer_open
.size explorer_gate, .-explorer_gate

.data
.align 2
.global explorer_trampoline
.global explorer_original
.type explorer_trampoline, %object
.type explorer_original, %function
explorer_trampoline:
explorer_original:
    .space 20
.size explorer_original, .-explorer_original
.size explorer_trampoline, .-explorer_trampoline

.section .note.GNU-stack,"",%progbits
