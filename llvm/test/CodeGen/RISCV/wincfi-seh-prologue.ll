; Check the .seh_* directives RISCVFrameLowering emits for a Windows prologue.
; See llvm/docs/RISCVWinCFI.md.
;
; RUN: llc -mtriple=riscv64-pc-windows-msvc -mattr=+d -target-abi lp64d %s -o - | FileCheck %s

declare void @callee(i64, i64, i64, i64, double, double)
declare void @callee_buf(ptr)

define void @func(i64 %a, i64 %b, i64 %c, i64 %d, double %x, double %y) uwtable "frame-pointer"="all" {
entry:
  call void @callee(i64 %a, i64 %b, i64 %c, i64 %d, double %x, double %y)
  call void @callee(i64 %d, i64 %c, i64 %b, i64 %a, double %y, double %x)
  ret void
}

; CHECK:      .seh_proc func
; CHECK-NEXT: # %bb.0:
; CHECK-NEXT: addi sp, sp, -64
; CHECK-NEXT: .seh_stackalloc 64
; CHECK-NEXT: sd ra, 56(sp)
; CHECK-NEXT: .seh_savereg ra, 56
; CHECK-NEXT: sd s0, 48(sp)
; CHECK-NEXT: .seh_savereg s0, 48
; CHECK-NEXT: sd s1, 40(sp)
; CHECK-NEXT: .seh_savereg s1, 40
; CHECK-NEXT: sd s2, 32(sp)
; CHECK-NEXT: .seh_savereg s2, 32
; CHECK-NEXT: sd s3, 24(sp)
; CHECK-NEXT: .seh_savereg s3, 24
; CHECK-NEXT: sd s4, 16(sp)
; CHECK-NEXT: .seh_savereg s4, 16
; CHECK-NEXT: fsd fs0, 8(sp)
; CHECK-NEXT: .seh_savefreg fs0, 8
; CHECK-NEXT: fsd fs1, 0(sp)
; CHECK-NEXT: .seh_savefreg fs1, 0
; CHECK-NEXT: addi s0, sp, 64
; CHECK-NEXT: .seh_setframe s0, 64
; CHECK-NEXT: .seh_endprologue
; CHECK:      call callee
; CHECK:      call callee
; CHECK:      ld ra, 56(sp)
; CHECK-NEXT: ld s0, 48(sp)
; CHECK-NEXT: ld s1, 40(sp)
; CHECK-NEXT: ld s2, 32(sp)
; CHECK-NEXT: ld s3, 24(sp)
; CHECK-NEXT: ld s4, 16(sp)
; CHECK-NEXT: fld fs0, 8(sp)
; CHECK-NEXT: fld fs1, 0(sp)
; CHECK-NEXT: addi sp, sp, 64
; CHECK-NEXT: ret
; CHECK-NEXT: .seh_endproc

; A minimal function needs no CSR saves and no frame pointer, but still
; needs a well-formed (empty) prologue/epilogue pair so the linker can emit
; .pdata/.xdata for it.
define void @minimal() uwtable {
  ret void
}

; CHECK:      .seh_proc minimal
; CHECK-NEXT: # %bb.0:
; CHECK-NEXT: .seh_endprologue
; CHECK-NEXT: ret
; CHECK-NEXT: .seh_endproc

; A frame large enough to need a split SP adjustment (RISCVFrameLowering
; keeps the CSR-spill offsets small by allocating in two steps -- see
; getFirstSPAdjustAmount). Without a dedicated frame pointer, both steps
; need their own .seh_stackalloc, since SP is still the unwind anchor
; throughout.
define void @big_frame() uwtable {
  %buf = alloca [4096 x i8], align 16
  call void @callee_buf(ptr %buf)
  ret void
}

; CHECK:      .seh_proc big_frame
; CHECK-NEXT: # %bb.0:
; CHECK-NEXT: addi sp, sp, -2032
; CHECK-NEXT: .seh_stackalloc 2032
; CHECK-NEXT: sd ra, 2024(sp)
; CHECK-NEXT: .seh_savereg ra, 2024
; CHECK-NEXT: addi sp, sp, -2048
; CHECK-NEXT: addi sp, sp, -48
; CHECK-NEXT: .seh_stackalloc 2096
; CHECK-NEXT: .seh_endprologue

; The same large frame with an explicitly forced frame pointer. The first
; split-SP-adjustment allocation becomes the .seh_setframe offset, which is why
; that field needs the full 8-bit (0-4080) range rather than x86_64's 4-bit
; (0-240) one.
define void @big_frame_fp() uwtable "frame-pointer"="all" {
  %buf = alloca [4096 x i8], align 16
  call void @callee_buf(ptr %buf)
  ret void
}

; CHECK:      .seh_proc big_frame_fp
; CHECK-NEXT: # %bb.0:
; CHECK-NEXT: addi sp, sp, -2032
; CHECK-NEXT: .seh_stackalloc 2032
; CHECK-NEXT: sd ra, 2024(sp)
; CHECK-NEXT: .seh_savereg ra, 2024
; CHECK-NEXT: sd s0, 2016(sp)
; CHECK-NEXT: .seh_savereg s0, 2016
; CHECK-NEXT: addi s0, sp, 2032
; CHECK-NEXT: .seh_setframe s0, 2032
; CHECK-NEXT: addi sp, sp, -2048
; CHECK-NEXT: addi sp, sp, -48
; CHECK-NEXT: .seh_endprologue

; An over-aligned local forces stack realignment. The frame pointer is
; established (.seh_setframe) *before* the realigning `andi sp, sp, -32`, so it
; anchors the CFA and every callee-save offset regardless of how many bytes the
; `andi` discards -- exactly like x86-64's UWOP_SET_FPREG. The `andi` itself is
; SP-only and gets no unwind code: there is no .seh_* directive between the
; .seh_setframe and the .seh_endprologue for it. The epilogue restores SP from
; the frame pointer (addi sp, s0, -32) as its first step, which the byte-pattern
; epilogue matcher recognizes (see RISCVWinCFI.md section 7).
define void @realign() uwtable {
  %buf = alloca [16 x i8], align 32
  call void @callee_buf(ptr %buf)
  ret void
}

; CHECK:      .seh_proc realign
; CHECK-NEXT: # %bb.0:
; CHECK-NEXT: addi sp, sp, -32
; CHECK-NEXT: .seh_stackalloc 32
; CHECK-NEXT: sd ra, 24(sp)
; CHECK-NEXT: .seh_savereg ra, 24
; CHECK-NEXT: sd s0, 16(sp)
; CHECK-NEXT: .seh_savereg s0, 16
; CHECK-NEXT: addi s0, sp, 32
; CHECK-NEXT: .seh_setframe s0, 32
; CHECK-NEXT: andi sp, sp, -32
; CHECK-NEXT: .seh_endprologue
; CHECK:      addi sp, s0, -32
; CHECK-NEXT: ld ra, 24(sp)
; CHECK-NEXT: ld s0, 16(sp)
; CHECK-NEXT: addi sp, sp, 32
; CHECK-NEXT: ret
; CHECK-NEXT: .seh_endproc

; An over-aligned local *and* a variable-sized object: realignment plus a base
; pointer (s1). Both the realigning `andi sp, sp, -32` and the `mv s1, sp` that
; captures the realigned SP are SP-only and carry no unwind code -- once the
; frame pointer is established neither is visible to the unwinder. As above, the
; only .seh_* directives after .seh_setframe is the .seh_endprologue.
define void @realign_vla(i64 %n) uwtable {
  %buf = alloca [16 x i8], align 32
  %p = alloca i8, i64 %n, align 16
  call void @callee_buf(ptr %buf)
  call void @callee_buf(ptr %p)
  ret void
}

; CHECK:      .seh_proc realign_vla
; CHECK-NEXT: # %bb.0:
; CHECK-NEXT: addi sp, sp, -64
; CHECK-NEXT: .seh_stackalloc 64
; CHECK-NEXT: sd ra, 56(sp)
; CHECK-NEXT: .seh_savereg ra, 56
; CHECK-NEXT: sd s0, 48(sp)
; CHECK-NEXT: .seh_savereg s0, 48
; CHECK-NEXT: sd s1, 40(sp)
; CHECK-NEXT: .seh_savereg s1, 40
; CHECK-NEXT: sd s2, 32(sp)
; CHECK-NEXT: .seh_savereg s2, 32
; CHECK-NEXT: addi s0, sp, 64
; CHECK-NEXT: .seh_setframe s0, 64
; CHECK-NEXT: andi sp, sp, -32
; CHECK-NEXT: mv s1, sp
; CHECK-NEXT: .seh_endprologue
