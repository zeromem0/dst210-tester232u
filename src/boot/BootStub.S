/* The way into the tester from the FNIRSI application, for the image that
 * goes in through the FNIRSI USB update (tools/make_update.py).
 *
 * The FNIRSI Reset_Handler checks EOPB0, takes its stack, runs its
 * SystemInit (which leaves the clock on HSI, PLL off, CFGR cleared, VTOR at
 * 0x08000000) and then jumps to __main through a literal (at 0x0800733C in
 * V0.1.0). make_update.py points that literal here, and writes the address
 * it held into t232_fnirsi_main below, found through the record t232_link.
 *
 * RUN (PB15, active low) is read with the pin's pull-up on, and everything
 * touched to read it is put back. Not held: on to the FNIRSI __main,
 * registers and stack as the FNIRSI start-up left them. Held: PA3 (the power
 * hold) high, SysTick stopped and every interrupt line off and cleared (the
 * bootloader may have left some on, and the tester has no handler for them),
 * then the tester's own stack and Reset_Handler, from its vector table.
 *
 * Built with BOOT_TEST it returns instead, 0 for FNIRSI and 1 for the tester,
 * so that the key reading can be tried from RAM (src/boottest). */

    .syntax unified
    .cpu cortex-m3
    .thumb

    .section .text.t232_boot,"ax",%progbits
    .global t232_boot
    .type t232_boot, %function
t232_boot:
#ifdef BOOT_TEST
    push  {r4-r7, lr}
#endif
    ldr   r0, =0x40021018       /* RCC_APB2ENR */
    ldr   r4, [r0]
    orr   r1, r4, #0x8          /* IOPB clock */
    str   r1, [r0]
    ldr   r2, =0x40010C00       /* GPIOB */
    ldr   r5, [r2, #0x04]       /* CRH */
    ldr   r6, [r2, #0x0C]       /* ODR */
    bic   r1, r5, #0xF0000000
    orr   r1, r1, #0x80000000   /* PB15: input with pull-up/down */
    str   r1, [r2, #0x04]
    mov   r1, #0x8000
    str   r1, [r2, #0x10]       /* BSRR: the pull is up */
    movw  r3, #2000             /* ~1 ms on HSI, for the pull-up to settle */
1:  subs  r3, r3, #1
    bne   1b
    ldr   r7, [r2, #0x08]       /* IDR */
    str   r5, [r2, #0x04]       /* all three back as they were */
    str   r6, [r2, #0x0C]
    str   r4, [r0]
    tst   r7, #0x8000
    bne   fnirsi                /* high: RUN not held */

#ifdef BOOT_TEST
    movs  r0, #1
    pop   {r4-r7, pc}
fnirsi:
    movs  r0, #0
    pop   {r4-r7, pc}
#else
    /* PA3, the power hold, high at once: a start that goes wrong in the
     * tester then leaves the unit on, to be looked at, instead of off as
     * soon as the power key is let go. */
    ldr   r0, =0x40021018       /* RCC_APB2ENR: IOPA */
    ldr   r1, [r0]
    orr   r1, r1, #0x4
    str   r1, [r0]
    ldr   r0, =0x40010800       /* GPIOA */
    movs  r1, #0x8
    str   r1, [r0, #0x10]       /* BSRR: PA3 high */
    ldr   r1, [r0, #0x00]       /* CRL: PA3 push-pull output */
    bic   r1, r1, #0xF000
    orr   r1, r1, #0x3000
    str   r1, [r0, #0x00]
    ldr   r0, =0xE000E010       /* SysTick CTRL: off */
    movs  r1, #0
    str   r1, [r0]
    ldr   r0, =0xE000E180       /* NVIC ICER0..7, then ICPR0..7 */
    mvn   r1, #0
    movs  r3, #8
2:  str   r1, [r0, #0x100]      /* ICPR */
    str   r1, [r0], #4          /* ICER */
    subs  r3, r3, #1
    bne   2b
    ldr   r0, =g_pfnVectors
    ldr   r1, [r0]
    msr   msp, r1
    ldr   r1, [r0, #4]
    bx    r1
fnirsi:
    ldr   r0, t232_fnirsi_main
    bx    r0
#endif

/* For make_update.py, which finds it in the binary by its first eight
 * bytes: where the record itself was linked (and so where the tester is),
 * the way in, and the FNIRSI __main, 0xFFFFFFFF until make_update.py writes
 * it. */
    .balign 4
t232_link:
    .ascii "T232BOOT"
    .word t232_link
    .word t232_boot
t232_fnirsi_main:
    .word 0xFFFFFFFF
    .size t232_boot, . - t232_boot
