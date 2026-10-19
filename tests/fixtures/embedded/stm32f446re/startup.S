/* Authored acceptance fixture; no vendor startup/runtime dependency. */
.syntax unified
.cpu cortex-m4
.thumb
.section .vectors,"a",%progbits
.balign 512
.word _stack_top
.word Reset_Handler
.rept 110
.word Default_Handler
.endr

.section .text.Reset_Handler,"ax",%progbits
.global Reset_Handler
.type Reset_Handler,%function
.thumb_func
Reset_Handler:
    cpsid i
    ldr r0, =_data_load
    ldr r1, =_data_start
    ldr r2, =_data_end
1:  cmp r1, r2
    bcs 2f
    ldr r3, [r0], #4
    str r3, [r1], #4
    b 1b
2:  ldr r1, =_bss_start
    ldr r2, =_bss_end
    movs r3, #0
3:  cmp r1, r2
    bcs 4f
    str r3, [r1], #4
    b 3b
4:  bl main
    b Default_Handler
.size Reset_Handler, .-Reset_Handler

.section .text.Default_Handler,"ax",%progbits
.global Default_Handler
.type Default_Handler,%function
.thumb_func
Default_Handler:
    b Default_Handler
.size Default_Handler, .-Default_Handler
.section .note.GNU-stack,"",%progbits
