.text
.p2align 2
.globl _GBURLWriterStub
_GBURLWriterStub:
    stp x29, x30, [sp, #-0x60]!
    mov x29, sp
    stp x0, x1, [sp, #0x10]
    stp x2, x3, [sp, #0x20]
    stp x4, x5, [sp, #0x30]
    stp x6, x7, [sp, #0x40]
    str x8, [sp, #0x50]
    mov x0, #-2
    adr x1, L_name
.globl _GBWriterDlsymCall
_GBWriterDlsymCall:
    .long 0x94000000
    cbz x0, L_restore
    mov x16, x0
    ldp x0, x1, [sp, #0x10]
    blr x16
L_restore:
    ldp x0, x1, [sp, #0x10]
    ldp x2, x3, [sp, #0x20]
    ldp x4, x5, [sp, #0x30]
    ldp x6, x7, [sp, #0x40]
    ldr x8, [sp, #0x50]
    ldp x29, x30, [sp], #0x60
.globl _GBWriterOriginalJump
_GBWriterOriginalJump:
    .long 0x14000000
L_name:
    .asciz "GBDispatchURLWriteTrace"
.p2align 2
