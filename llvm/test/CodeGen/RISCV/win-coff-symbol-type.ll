; Functions must be marked IMAGE_SYM_DTYPE_FUNCTION in the COFF symbol table.
; Without it a function is indistinguishable from a global variable, and
; consumers that resolve a function by name off the symbol table (LLDB's
; DW_OP_entry_value call edge resolution, for one) fail to find it.

; RUN: llc -mtriple=riscv64-pc-windows-msvc -target-abi lp64d %s -o - \
; RUN:   | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=riscv64-pc-windows-msvc -target-abi lp64d -filetype=obj %s -o %t.o
; RUN: llvm-readobj --symbols %t.o | FileCheck %s --check-prefix=OBJ

@global_data = global i32 0

define i32 @external_func(i32 %a) {
  ret i32 %a
}

define internal i32 @internal_func(i32 %a) {
  ret i32 %a
}

; ASM:      .def    external_func
; ASM-NEXT:   .scl    2
; ASM-NEXT:   .type   32
; ASM-NEXT: .endef

; ASM:      .def    internal_func
; ASM-NEXT:   .scl    3
; ASM-NEXT:   .type   32
; ASM-NEXT: .endef

; OBJ:        Name: external_func
; OBJ:        ComplexType: Function (0x2)
; OBJ-NEXT:   StorageClass: External (0x2)

; OBJ:        Name: internal_func
; OBJ:        ComplexType: Function (0x2)
; OBJ-NEXT:   StorageClass: Static (0x3)

; Data must keep the null complex type so the two stay distinguishable.
; OBJ:        Name: global_data
; OBJ:        ComplexType: Null (0x0)
