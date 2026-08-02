// RUN: llvm-mc -triple riscv64-pc-windows-msvc -filetype obj -o %t.obj %s
// RUN: llvm-objdump -dr --triple=riscv64 %t.obj | FileCheck %s

// COFF relocations have no addend field. A %pcrel_hi to a symbol with a
// non-zero byte offset stores that offset (the addend) raw in the auipc's
// 20-bit immediate, so lld can fold it into the target before computing the
// pc-relative high part. Check that the raw addend -- not the pre-rounded
// high part -- is what lands in the instruction.

.Lpcrel_hi0:
// CHECK: auipc a0, 0x1234
// CHECK-NEXT: IMAGE_REL_RISCV64_PCREL_HI20 gvar
auipc a0, %pcrel_hi(gvar+0x1234)
// CHECK-NEXT: addi a0, a0, 0x238
// CHECK-NEXT: IMAGE_REL_RISCV64_PCREL_LO12_I gvar
addi a0, a0, %pcrel_lo(.Lpcrel_hi0)

	.data
	.globl gvar
gvar:
	.zero 0x2000
