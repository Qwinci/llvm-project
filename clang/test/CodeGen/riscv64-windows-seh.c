// Check the .seh_* prologue directives and the __C_specific_handler scope
// table emitted for __try/__except.
//
// RUN: %clang_cc1 %s -triple riscv64-pc-windows-msvc -target-feature +d \
// RUN:   -fms-extensions -S -o - | FileCheck %s

void may_throw(void);

// A compile-time-constant filter needs no outlined filter function and no
// frame-pointer-recovery machinery, so it stays the simplest, most common
// shape (matching MSVC's usual __except(EXCEPTION_EXECUTE_HANDLER) idiom).
int catch_all(void) {
  __try {
    may_throw();
  } __except (1) {
    return -1;
  }
  return 0;
}

// CHECK-LABEL: catch_all:
// CHECK: .seh_proc catch_all
// CHECK: .seh_handler __C_specific_handler, @unwind, @except
// CHECK: .seh_stackalloc
// CHECK: .seh_savereg ra,
// CHECK: .seh_savereg s0,
// CHECK: .seh_setframe s0,
// CHECK: .seh_endprologue
// CHECK: .seh_handlerdata
// CHECK-NEXT: [[FRAME_OFFSET:\.L[a-zA-Z0-9_$]+parent_frame_offset]] = [[#]]
// CHECK: .word (.Llsda_end{{[0-9]+}}-.Llsda_begin{{[0-9]+}})/16
// CHECK-NEXT: .Llsda_begin{{[0-9]+}}:
// CHECK-NEXT: .word {{.*}}@IMGREL
// CHECK-NEXT: .word {{.*}}@IMGREL
// CHECK-NEXT: .word 1
// CHECK-NEXT: .word {{.*}}@IMGREL
// CHECK: .seh_endproc

// A filter *expression* capturing a local forces the filter into its own
// outlined function (WinEHPrepare), which recovers the parent frame via
// llvm.eh.recoverfp/llvm.localrecover -- exercising RISCVISelLowering's
// eh_recoverfp/LOCAL_RECOVER lowering and RISCVRegisterInfo's LOCAL_ESCAPE
// frame-index handling.
int filter_capture(void) {
  int code = 0;
  __try {
    may_throw();
  } __except (code = 1) {
    return code;
  }
  return 0;
}

// CHECK-LABEL: filter_capture:
// CHECK: .seh_proc filter_capture
// CHECK: .seh_setframe s0,
// CHECK: .seh_endprologue
// CHECK: [[ESCAPE_SYM:\.Lfilter_capture\$frame_escape_[0-9]+]] = -[[#]]
// CHECK: .seh_handlerdata
// CHECK: .word "?filt$0@0@filter_capture@@"@IMGREL
// CHECK: .seh_endproc

// CHECK-LABEL: "?filt$0@0@filter_capture@@":
// CHECK: lui a{{[0-9]+}}, %hi([[ESCAPE_SYM]])
// CHECK: addi a{{[0-9]+}}, a{{[0-9]+}}, %lo([[ESCAPE_SYM]])
