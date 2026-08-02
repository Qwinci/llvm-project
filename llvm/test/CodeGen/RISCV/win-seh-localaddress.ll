; RUN: llc -mtriple=riscv64-unknown-windows-msvc < %s | FileCheck %s

; llvm.localaddress returns the register that llvm.localescape offsets are
; relative to, so an outlined SEH __finally helper (or a funclet) can recover the
; parent frame's escaped locals. Clang emits it for __try/__finally. Regression
; test for "fatal error: Cannot select: intrinsic %llvm.localaddress".

declare ptr @llvm.localaddress()
declare void @llvm.localescape(...)
declare ptr @llvm.localrecover(ptr, ptr, i32 immarg)
declare void @use(ptr)

; With no EH funclets and no variable-sized objects the frame is addressed off
; sp, so localaddress is sp.
define ptr @localaddr_sp() {
; CHECK-LABEL: localaddr_sp:
; CHECK:      mv a0, sp
; CHECK-NEXT: ret
  %a = call ptr @llvm.localaddress()
  ret ptr %a
}

; A function with SEH funclets is addressed off the frame pointer (s0), which is
; what localaddress must return so localrecover lands on the escaped local. The
; asynchronous (SEH) funclet must adopt the parent's establisher frame -- passed
; in a1 (PVOID EstablisherFrame, the 2nd termination-handler argument) -- as its
; frame pointer, *not* derive s0 from its own sp; otherwise localrecover reads
; the wrong frame and an unwind-driven __finally sees garbage locals.
define i32 @seh_finally() personality ptr @__C_specific_handler {
; CHECK-LABEL: seh_finally:
; Parent establishes its own frame pointer.
; CHECK:      addi s0, sp,
; CHECK:      add {{a[0-9]+}}, s0, {{a[0-9]+}}
;
; The funclet copies the establisher frame from a1 into s0 (after the prologue,
; with no .seh_setframe of its own) and recovers the local through it.
; CHECK-LABEL: "?dtor${{[0-9]+}}@?0?seh_finally@4HA":
; CHECK:      .seh_endprologue
; CHECK-NEXT: mv s0, a1
; CHECK-NOT:  .seh_setframe
; CHECK:      add {{a[0-9]+}}, s0, {{a[0-9]+}}
entry:
  %local = alloca i32, align 4
  call void (...) @llvm.localescape(ptr nonnull %local)
  store i32 7, ptr %local, align 4
  invoke void @may_throw()
          to label %cont unwind label %ehcleanup

cont:
  %fp = call ptr @llvm.localaddress()
  %l = call ptr @llvm.localrecover(ptr @seh_finally, ptr %fp, i32 0)
  %v = load i32, ptr %l, align 4
  call void @cleanup(i32 %v)
  ret i32 %v

ehcleanup:
  %pad = cleanuppad within none []
  %fp2 = call ptr @llvm.localaddress()
  %l2 = call ptr @llvm.localrecover(ptr @seh_finally, ptr %fp2, i32 0)
  %v2 = load i32, ptr %l2, align 4
  call void @cleanup(i32 %v2) [ "funclet"(token %pad) ]
  cleanupret from %pad unwind to caller
}

declare void @may_throw()
declare void @cleanup(i32)
declare i32 @__C_specific_handler(...)
