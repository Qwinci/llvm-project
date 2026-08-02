// An unresolved SymA-SymB difference, as emitted by DWARF debug info or by a
// jump table under -ffunction-sections. ELF encodes this as an
// R_RISCV_ADD32/R_RISCV_SUB32 relocation pair, but COFF has no such pairing;
// RISCVAsmBackend::addReloc must let it fall through to getRelocType()'s
// IsCrossSection handling, which emits a single PC-relative relocation.
//
// RUN: llvm-mc -triple riscv64-pc-windows-msvc -filetype obj -o %t.obj %s
// RUN: llvm-readobj -r %t.obj | FileCheck %s

	.text
func:
	ret

	.data
table:
	.4byte func - table

// CHECK: Format: COFF-RISCV64
// CHECK: Relocations [
// CHECK:   Section {{.*}} .data {
// CHECK: 0x0 IMAGE_REL_RISCV64_REL32 func
// CHECK:   }
