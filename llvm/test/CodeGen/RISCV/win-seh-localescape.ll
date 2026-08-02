; RUN: llc -mtriple=riscv64-unknown-windows-msvc < %s | FileCheck %s

; SEH (__try/__except) local capture: an __except filter reads and writes locals
; of the enclosing function through llvm.localescape / llvm.localrecover. This
; verifies the frame-recovery contract for RISC-V:
;
;  - the parent establishes s0 as its frame pointer (s0 == CFA == the value the
;    SEH runtime passes as the EstablisherFrame),
;  - the escaped-local offsets are relative to s0 (== EstablisherFrame),
;  - llvm.eh.recoverfp is the identity (the incoming frame pointer already *is*
;    the parent's s0; no ParentFrameOffset adjustment is applied -- matches
;    AArch64, not x86_64), so the filter addresses a local as
;    EstablisherFrame + escape_offset, exactly the slot the parent uses.

define dso_local signext i32 @seh_read() personality ptr @__C_specific_handler {
entry:
  %x = alloca i32, align 4
  call void (...) @llvm.localescape(ptr %x)
  store i32 42, ptr %x, align 4
  invoke void @may_throw()
          to label %cleanup unwind label %catch.dispatch
catch.dispatch:
  %0 = catchswitch within none [label %except.ret] unwind to caller
except.ret:
  %1 = catchpad within %0 [ptr @"?filt$0@0@seh_read@@"]
  catchret from %1 to label %except
except:
  %2 = load i32, ptr %x, align 4
  br label %cleanup
cleanup:
  %retval.0 = phi i32 [ %2, %except ], [ 0, %entry ]
  ret i32 %retval.0
}

define internal signext i32 @"?filt$0@0@seh_read@@"(ptr %exception_pointers, ptr %frame_pointer) {
entry:
  %0 = tail call ptr @llvm.eh.recoverfp(ptr @seh_read, ptr %frame_pointer)
  %x = tail call ptr @llvm.localrecover(ptr @seh_read, ptr %0, i32 0)
  %call = tail call signext i32 @filter_helper(ptr %x)
  ret i32 %call
}

declare dso_local signext i32 @filter_helper(ptr)
declare dso_local void @may_throw()
declare dso_local i32 @__C_specific_handler(...)
declare void @llvm.localescape(...)
declare ptr @llvm.eh.recoverfp(ptr, ptr)
declare ptr @llvm.localrecover(ptr, ptr, i32 immarg)

; The parent establishes s0 as the frame pointer, records it via .seh_setframe,
; and the escaped local's offset (defined by the $frame_escape symbol) is exactly
; the s0-relative slot the local is stored to.
; CHECK-LABEL: seh_read:
; CHECK:         addi s0, sp, [[FS:[0-9]+]]
; CHECK:         .seh_setframe s0, [[FS]]
; CHECK:         .Lseh_read$frame_escape_0 = [[OFF:-?[0-9]+]]
; CHECK:         sw {{[a-z0-9]+}}, [[OFF]](s0)

; The filter recovers the local as EstablisherFrame (2nd arg, a1) + escape offset.
; recoverfp is the identity, so there is no ParentFrameOffset add: a single
; `add ..., a1` reaches the same slot the parent used.
; CHECK-LABEL: "?filt$0@0@seh_read@@":
; CHECK:         lui [[T:[a-z0-9]+]], %hi(.Lseh_read$frame_escape_0)
; CHECK:         addi [[T]], [[T]], %lo(.Lseh_read$frame_escape_0)
; CHECK:         add a0, a1, [[T]]
; CHECK:         tail filter_helper
