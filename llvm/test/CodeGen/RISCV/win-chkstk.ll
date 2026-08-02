; Windows requires probing the guard pages between the old and new stack
; pointer whenever a frame is at least one page (4KiB) large. Unlike on other
; OSes this is not opt-in. __chkstk takes its byte count in t1 and clobbers
; t2/t3/t4 without touching sp; see compiler-rt/lib/builtins/riscv/chkstk.S.
;
; RUN: llc -mtriple=riscv64-pc-windows-msvc %s -o - | FileCheck %s

declare void @use(ptr)

define void @big_frame(i64 %a, i64 %b, i64 %c, i64 %d, i64 %e, i64 %f, i64 %g, i64 %h) {
; CHECK-LABEL: big_frame:
; CHECK:      lui t1, 2
; CHECK-NEXT: addi t1, t1, -2000
; CHECK-NEXT: call __chkstk
; CHECK-NEXT: lui a0, 2
; CHECK-NEXT: addi a0, a0, -2000
; CHECK-NEXT: sub sp, sp, a0
entry:
  %buf = alloca [8192 x i8], align 16
  call void @use(ptr %buf)
  ret void
}

define void @small_frame() {
; A frame below the probe size (4096 by default) never needs to call
; __chkstk.
; CHECK-LABEL: small_frame:
; CHECK-NOT: __chkstk
entry:
  %buf = alloca [16 x i8], align 16
  call void @use(ptr %buf)
  ret void
}

define void @big_frame_noprobe() "no-stack-arg-probe" {
; The "no-stack-arg-probe" function attribute opts a function out of the
; automatic Windows probing, same as on every other Windows target.
; CHECK-LABEL: big_frame_noprobe:
; CHECK-NOT: __chkstk
entry:
  %buf = alloca [8192 x i8], align 16
  call void @use(ptr %buf)
  ret void
}
