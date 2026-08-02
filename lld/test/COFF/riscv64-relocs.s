// REQUIRES: riscv
// RUN: llvm-mc -filetype=obj -triple=riscv64-pc-windows-msvc %s -o %t.obj
// RUN: lld-link -out:%t.exe -entry:entry -subsystem:console %t.obj
// RUN: llvm-objdump -d --triple=riscv64 %t.exe | FileCheck %s
// RUN: llvm-objdump -s %t.exe | FileCheck %s --check-prefix=DATA

	.text
	.globl	entry
	.p2align	1
entry:
	call	callee
	jal	zero, tailcall
.Lpcrel_hi0:
	auipc	a0, %pcrel_hi(gdata)
	addi	a0, a0, %pcrel_lo(.Lpcrel_hi0)
	lw	a1, 0(a0)
	ret
tailcall:
	ret

	.p2align	1
	.globl	callee
callee:
	ret

	.data
	.globl	gdata
gdata:
	.word	42

// CHECK: Disassembly of section .text:
// CHECK: <.text>:
// CHECK-NEXT: 140001000: {{.*}} auipc ra, 0x0
// CHECK-NEXT: 140001004: {{.*}} jalr 0x20(ra) <.text+0x20>
// CHECK-NEXT: 140001008: {{.*}} j 0x14000101c <.text+0x1c>
// CHECK-NEXT: 14000100c: {{.*}} auipc a0, 0x1
// CHECK-NEXT: 140001010: {{.*}} addi a0, a0, -0xc
// CHECK-NEXT: 140001014: {{.*}} lw a1, 0x0(a0)
// CHECK-NEXT: 140001018: {{.*}} ret
// CHECK-NEXT: 14000101c: {{.*}} ret
// CHECK-NEXT: 140001020: {{.*}} ret

// DATA: Contents of section .data:
// DATA-NEXT: 140002000 2a000000
