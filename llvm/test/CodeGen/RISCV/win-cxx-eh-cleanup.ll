; RUN: llc -mtriple=riscv64-unknown-windows-msvc < %s | FileCheck %s

; Windows funclet-based C++ exception handling: a cleanuppad/cleanupret pair
; (a destructor run during unwinding) must select without crashing and lower to
; a real cleanup funclet. The funclet inherits the parent's frame pointer (s0)
; from the personality routine, so it must NOT re-derive s0 from its own SP --
; it accesses the parent's local through the inherited s0.

%struct.Dtor = type { i8 }

define dso_local void @test_cleanup() personality ptr @__CxxFrameHandler3 {
entry:
  %d = alloca %struct.Dtor, align 1
  invoke void @may_throw()
          to label %invoke.cont unwind label %ehcleanup

invoke.cont:
  call void @dtor(ptr %d)
  ret void

ehcleanup:
  %0 = cleanuppad within none []
  call void @dtor(ptr %d) [ "funclet"(token %0) ]
  cleanupret from %0 unwind to caller
}

declare dso_local void @may_throw()
declare dso_local i32 @__CxxFrameHandler3(...)
declare dso_local void @dtor(ptr)

; The parent function establishes s0 as its frame pointer, then initializes the
; UnwindHelp slot to -2 relative to it (the establisher frame).
; CHECK-LABEL: test_cleanup:
; CHECK:         .seh_proc test_cleanup
; CHECK:         addi s0, sp, {{[0-9]+}}
; CHECK:         li [[REG:[a-z0-9]+]], -2
; CHECK:         sd [[REG]], -16(s0)
; CHECK:         .seh_handlerdata

; The cleanup funclet is emitted as its own procedure. It saves and restores s0
; as a callee-saved register but never recomputes it (no "addi s0, sp, ..."),
; and returns to the personality with a plain `ret` lowered from cleanupret.
; CHECK-LABEL: "?dtor$2@?0?test_cleanup@4HA":
; CHECK:         .seh_proc "?dtor$2@?0?test_cleanup@4HA"
; CHECK:         sd s0, {{[0-9]+}}(sp)
; CHECK-NOT:     addi s0, sp,
; CHECK:         addi a0, s0, {{-?[0-9]+}}
; CHECK:         ld s0, {{[0-9]+}}(sp)
; CHECK:         ret

; The C++ EH tables are emitted for the parent.
; CHECK: $cppxdata$test_cleanup:
; CHECK: $stateUnwindMap$test_cleanup:
; CHECK: $ip2state$test_cleanup:
