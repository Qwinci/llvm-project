// COFF has no GOT: a cross-module data reference must never use %got_pcrel_hi
// (which has no COFF relocation and is rejected by the MC layer). Under MSVC an
// external symbol is accessed directly with pc-relative addressing; under MinGW
// it may be auto-imported, so its address is loaded through a local
// .refptr.<sym> pointer instead (matching AArch64/X86). __ImageBase is a
// linker-defined symbol, so it exercises the plain (non-dllimport) path.
//
// RUN: %clang_cc1 %s -triple riscv64-pc-windows-msvc -target-feature +d -S -o - | FileCheck %s --check-prefixes=CHECK,MSVC
// RUN: %clang_cc1 %s -triple riscv64-w64-windows-gnu -target-feature +d -S -o - | FileCheck %s --check-prefixes=CHECK,GNU

extern unsigned char __ImageBase[];
int f(void) {
  return __ImageBase[0];
}

// CHECK-LABEL: f:
// MSVC:      auipc {{a[0-9]+}}, %pcrel_hi(__ImageBase)
// MSVC-NEXT: addi {{a[0-9]+}}, {{a[0-9]+}}, %pcrel_lo(
// GNU:       auipc {{a[0-9]+}}, %pcrel_hi(.refptr.__ImageBase)
// GNU-NEXT:  ld {{a[0-9]+}}, %pcrel_lo({{.*}})({{a[0-9]+}})
// CHECK-NOT: %got_pcrel_hi
