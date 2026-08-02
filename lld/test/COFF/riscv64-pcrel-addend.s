// REQUIRES: riscv
// RUN: llvm-mc -filetype=obj -triple=riscv64-pc-windows-msvc %s -o %t.obj
// RUN: lld-link -out:%t.exe -entry:entry -subsystem:console %t.obj
// RUN: llvm-objdump -d --triple=riscv64 %t.exe | FileCheck %s

// A %pcrel_hi/%pcrel_lo pair with a non-zero symbol addend that crosses a page
// boundary. COFF has no relocation addend field, so the addend is carried in
// the auipc's immediate and must be folded back in by the linker (the +0x800
// carry needs the full target value). Regression test for the addend being
// dropped, which left the auipc one page (or more) too low.

	.text
	.globl	entry
	.p2align	1
entry:
.Lpcrel_hi0:
	auipc	a0, %pcrel_hi(gdata+0x1234)
	addi	a0, a0, %pcrel_lo(.Lpcrel_hi0)
	ret

	.data
	.globl	gdata
gdata:
	.zero	0x4000

// gdata is at RVA 0x2000 (VA 0x140002000); the reference targets
// gdata+0x1234 = 0x140003234. auipc a0,0x2 -> 0x140003000, addi 0x234.
// CHECK: Disassembly of section .text:
// CHECK: <.text>:
// CHECK-NEXT: 140001000: {{.*}} auipc a0, 0x2
// CHECK-NEXT: 140001004: {{.*}} addi a0, a0, 0x234
// CHECK-NEXT: 140001008: {{.*}} ret
