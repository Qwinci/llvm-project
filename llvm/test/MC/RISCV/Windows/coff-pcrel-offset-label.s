// RUN: llvm-mc -triple riscv64-pc-windows-msvc -filetype obj -o %t.obj %s
// RUN: llvm-objdump -dr --triple=riscv64 %t.obj | FileCheck %s
// RUN: llvm-readobj --symbols %t.obj | FileCheck %s --check-prefix=SYM

// COFF has no relocation addend field, so a %pcrel_hi carries its byte addend
// in the auipc's signed 20-bit immediate (see RISCVAsmBackend::applyFixup). A
// reference to a local temporary symbol is redirected to the section symbol
// plus the symbol's offset within the section, which can far exceed 20 bits.
// The WinCOFF writer emits "offset label" symbols every 2^19 bytes and points
// the relocation at the nearest one, keeping the residual addend within the
// auipc's range. Check that the relocation targets an offset label and the
// residual (not the full section offset) lands in the instruction.

.Lpcrel_hi0:
// CHECK: auipc a0, 0x10
// CHECK-NEXT: IMAGE_REL_RISCV64_PCREL_HI20 $L.data_2
auipc a0, %pcrel_hi(.Lbig+16)
// The raw residual here also folds in the 4-byte distance between the two
// instructions; lld reconstructs the true low 12 bits at link time.
// CHECK-NEXT: addi a0, a0, 0x14
// CHECK-NEXT: IMAGE_REL_RISCV64_PCREL_LO12_I $L.data_2
addi a0, a0, %pcrel_lo(.Lpcrel_hi0)

// The offset label sits at a 2^19 multiple within .data.
// SYM: Name: $L.data_2
// SYM: Value: 1048576

	.data
foo:
	.zero 0x100000
.Lbig:
	.quad 0
