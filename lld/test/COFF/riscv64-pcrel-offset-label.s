// REQUIRES: riscv
// RUN: llvm-mc -filetype=obj -triple=riscv64-pc-windows-msvc %s -o %t.obj
// RUN: lld-link -out:%t.exe -entry:entry -subsystem:console %t.obj
// RUN: llvm-objdump -d --triple=riscv64 %t.exe | FileCheck %s

// A %pcrel_hi/%pcrel_lo pair targeting a local temporary symbol more than 2^19
// bytes into its section. COFF has no relocation addend field and a temporary
// symbol is not in the symbol table, so the relocation is redirected to a
// synthetic "offset label" symbol placed every 2^19 bytes; the small residual
// addend is carried in the auipc's immediate. Regression test for the addend
// (the symbol's section offset) overflowing the auipc's signed 20-bit field.

	.text
	.globl	entry
	.p2align	1
entry:
.Lpcrel_hi0:
	auipc	a0, %pcrel_hi(.Lbig+16)
	addi	a0, a0, %pcrel_lo(.Lpcrel_hi0)
	ret

	.data
foo:
	.zero	0x100000
.Lbig:
	.quad	0

// .data is at RVA 0x2000 (VA 0x140002000); .Lbig is at .data+0x100000, so the
// reference targets .Lbig+16 = 0x140102010. auipc a0,0x101 -> 0x140102000,
// addi 0x10 -> 0x140102010.
// CHECK: Disassembly of section .text:
// CHECK: <.text>:
// CHECK-NEXT: 140001000: {{.*}} auipc a0, 0x101
// CHECK-NEXT: 140001004: {{.*}} addi a0, a0, 0x10
// CHECK-NEXT: 140001008: {{.*}} ret
