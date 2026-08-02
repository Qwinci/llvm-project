// RUN: llvm-mc -triple riscv64-pc-windows-msvc -filetype obj -o %t.obj %s
// RUN: llvm-readobj -r %t.obj | FileCheck %s

// IMAGE_REL_RISCV64_ADDR32
.long foo

// IMAGE_REL_RISCV64_ADDR32NB
.long func@IMGREL

// IMAGE_REL_RISCV64_ADDR64
.globl struc
struc:
  .quad arr

// IMAGE_REL_RISCV64_CALL (spans the auipc+jalr pair produced by `call`)
call func

// IMAGE_REL_RISCV64_JAL
jal zero, func

.Lpcrel_hi0:
// IMAGE_REL_RISCV64_PCREL_HI20
auipc a0, %pcrel_hi(gvar)
// IMAGE_REL_RISCV64_PCREL_LO12_I
addi a0, a0, %pcrel_lo(.Lpcrel_hi0)

.Lpcrel_hi1:
// IMAGE_REL_RISCV64_PCREL_HI20
auipc a1, %pcrel_hi(gvar)
// IMAGE_REL_RISCV64_PCREL_LO12_S
sd a1, %pcrel_lo(.Lpcrel_hi1)(a1)

// IMAGE_REL_RISCV64_SECREL
.secrel32 .Linfo
.Linfo:

// IMAGE_REL_RISCV64_SECTION
.secidx func

.data
.globl gvar
gvar:
  .word 1

// CHECK: Format: COFF-RISCV64
// CHECK: Arch: riscv64
// CHECK: AddressSize: 64bit
// CHECK: Relocations [
// CHECK:   Section (1) .text {
// CHECK: 0x0 IMAGE_REL_RISCV64_ADDR32 foo
// CHECK: 0x4 IMAGE_REL_RISCV64_ADDR32NB func
// CHECK: 0x8 IMAGE_REL_RISCV64_ADDR64 arr
// CHECK: 0x10 IMAGE_REL_RISCV64_CALL func
// CHECK: 0x18 IMAGE_REL_RISCV64_JAL func
// CHECK: 0x1C IMAGE_REL_RISCV64_PCREL_HI20 gvar
// CHECK: 0x20 IMAGE_REL_RISCV64_PCREL_LO12_I gvar
// CHECK: 0x24 IMAGE_REL_RISCV64_PCREL_HI20 gvar
// CHECK: 0x28 IMAGE_REL_RISCV64_PCREL_LO12_S gvar
// CHECK: 0x2C IMAGE_REL_RISCV64_SECREL .text
// CHECK: 0x30 IMAGE_REL_RISCV64_SECTION func
// CHECK:   }
