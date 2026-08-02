; RUN: llc -mtriple=riscv64-unknown-windows-msvc < %s | FileCheck %s

; On Windows/MSVC the stack protector uses the MSVC CRT convention -- load the
; guard from the __security_cookie global and validate it by calling
; __security_check_cookie -- rather than the __stack_chk_guard/__stack_chk_fail
; pair used elsewhere. Regression test for "error in backend: unsupported
; library call operation" / "unable to lower stackguard", which fired because the
; riscv64 runtime-libcall table left both libcalls unsupported on windows-msvc.

define void @func() sspreq nounwind {
; CHECK-LABEL: func:
; CHECK:      __security_cookie
; CHECK:      call capture
; CHECK:      call __security_check_cookie
; CHECK-NOT:  __stack_chk_guard
; CHECK-NOT:  __stack_chk_fail
  %var = alloca [2 x i32], align 4
  call void @capture(ptr %var)
  ret void
}

declare void @capture(ptr)
