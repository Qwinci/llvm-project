; RUN: llc -mtriple=riscv64-unknown-windows-msvc < %s | FileCheck %s

; Windows funclet-based C++ exception handling with a catch clause. Exercises the
; full path: a catchpad/catchret, a catch object, and the UnwindHelp slot. The
; finalized RISC-V ABI (see llvm/docs/RISCVWinEH.md) is:
;  - s0 == the establisher frame == SP at function entry (the CFA),
;  - ParentFrameOffset == 0,
;  - catch objects and the 8-byte UnwindHelp slot live at negative offsets from
;    s0, and those offsets go into the $cppxdata handler map,
;  - the entry initializes UnwindHelp to -2,
;  - the catch funclet returns the continuation address in a0 (materialized with
;    auipc/addi) and returns to the personality with a plain `ret`.

%rtti.TypeDescriptor2 = type { ptr, ptr, [3 x i8] }
$"??_R0H@8" = comdat any
@"??_7type_info@@6B@" = external constant ptr
@"??_R0H@8" = linkonce_odr global %rtti.TypeDescriptor2 { ptr @"??_7type_info@@6B@", ptr null, [3 x i8] c".H\00" }, comdat

define dso_local i32 @test_catch() personality ptr @__CxxFrameHandler3 {
entry:
  %e = alloca i32, align 4
  invoke void @may_throw()
          to label %return unwind label %catch.dispatch
catch.dispatch:
  %0 = catchswitch within none [label %catch] unwind to caller
catch:
  %1 = catchpad within %0 [ptr @"??_R0H@8", i32 0, ptr %e]
  %2 = load i32, ptr %e, align 4
  call void @sink(i32 %2) [ "funclet"(token %1) ]
  catchret from %1 to label %return
return:
  %retval.0 = phi i32 [ 2, %catch ], [ 1, %entry ]
  ret i32 %retval.0
}

declare dso_local void @may_throw()
declare dso_local i32 @__CxxFrameHandler3(...)
declare dso_local void @sink(i32)

; Entry: establish s0 = CFA, then initialize UnwindHelp (at s0-16) to -2.
; CHECK-LABEL: test_catch:
; CHECK:         addi s0, sp, {{[0-9]+}}
; CHECK:         li [[UH:[a-z0-9]+]], -2
; CHECK:         sd [[UH]], -16(s0)

; Catch funclet: inherits s0 (no re-derivation), reads the catch object at s0-4,
; loads the continuation block address into a0, and returns with `ret`.
; CHECK-LABEL: "?catch${{[0-9]+}}@?0?test_catch@{{.*}}":
; CHECK:         sd s0, {{[0-9]+}}(sp)
; CHECK-NOT:     addi s0, sp,
; CHECK:         lw a0, -4(s0)
; CHECK:         call sink
; CHECK:         auipc a0, %pcrel_hi(.LBB0_{{[0-9]+}})
; CHECK:         ret

; The C++ EH tables carry the finalized offsets.
; CHECK: $cppxdata$test_catch:
; CHECK:         .word -16 {{.*}} UnwindHelp
; CHECK: $handlerMap$0$test_catch:
; CHECK:         .word -4 {{.*}} CatchObjOffset
; CHECK:         .word 0 {{.*}} ParentFrameOffset
