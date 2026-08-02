// Check the Windows TLS access sequence: TEB -> _tls_array -> _tls_index ->
// offset within .tls$. See RISCVISelLowering's lowerWindowsGlobalTLSAddress.
//
// RUN: %clang_cc1 %s -triple riscv64-pc-windows-msvc -target-feature +d -S -o - | FileCheck %s

__thread int tls_var = 42;

int get_tls(void) {
  return tls_var;
}

// CHECK-LABEL: get_tls:
// CHECK: ld [[TEB:a[0-9]+]], 88(tp)
// CHECK: auipc [[IDXA:a[0-9]+]], %pcrel_hi(_tls_index)
// CHECK: addi [[IDXA]], [[IDXA]], %pcrel_lo
// CHECK: lwu [[IDX:a[0-9]+]], 0([[IDXA]])
// CHECK: slli [[IDX]], [[IDX]], 3
// CHECK: add [[BASE:a[0-9]+]], [[TEB]], [[IDX]]
// CHECK: ld [[BASE]], 0([[BASE]])
// CHECK: lui [[OFF:a[0-9]+]], %tls_secrel_hi(tls_var)
// CHECK: addi [[OFF]], [[OFF]], %tls_secrel_lo(tls_var)
// CHECK: add [[BASE]], [[BASE]], [[OFF]]
// CHECK: lw {{a[0-9]+}}, 0([[BASE]])

// A TLS access with a non-zero element/field offset must keep the offset OUT of
// the %tls_secrel_hi/%tls_secrel_lo pair (materialized as a separate add), so
// the SECREL relocations reference the bare symbol with no inline addend. The
// linker's SECREL_HI20/LO12 handling (lld applyRelRISCV64,
// RuntimeDyldCOFFRISCV64) computes the value purely from the symbol's .tls$
// offset and does NOT read an inline addend -- if codegen ever started folding
// the offset into this pair, that addend would be silently dropped. Keep this
// test (and those relocation handlers) in sync if that ever changes.
__thread int tls_arr[4096];

int *get_tls_elem(void) {
  return &tls_arr[1000];
}

// CHECK-LABEL: get_tls_elem:
// CHECK: lui [[EOFF:a[0-9]+]], %tls_secrel_hi(tls_arr){{$}}
// CHECK: addi [[EOFF]], [[EOFF]], %tls_secrel_lo(tls_arr){{$}}
// The +4000 (1000*4) element offset is a separate add, not folded into SECREL.
// CHECK: addi {{a[0-9]+}}, {{a[0-9]+}}, 2047
// CHECK: addi {{a[0-9]+}}, {{a[0-9]+}}, 1953

// CHECK: .section .tls$,"dw"
// CHECK: tls_var:
