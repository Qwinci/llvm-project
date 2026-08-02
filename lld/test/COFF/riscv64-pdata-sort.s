// PE/COFF spec 5.5 requires .pdata to be sorted ascending by BeginAddress so an
// unwinder can binary-search it. func1 and func2 emit their .pdata entries in
// declaration order, but /order: reverses their placement in .text;
// Writer::sortExceptionTables() must sort by the final addresses.
//
// REQUIRES: riscv
// RUN: llvm-mc -filetype=obj -triple=riscv64-pc-windows-msvc %s -o %t.obj
// RUN: echo func2 > %t.ord
// RUN: echo func1 >> %t.ord
// RUN: lld-link -out:%t.exe -entry:func1 -subsystem:console -opt:noref -order:@%t.ord %t.obj
// RUN: llvm-objdump -s --section=.pdata %t.exe | FileCheck %s
//
// llvm-objdump does not decode riscv64 .pdata, so check the raw EntryX64-shaped
// (begin, end, unwind) bytes: each 12-byte record's first word must be
// ascending. func2 is placed first in .text (RVA 0x1000) and func1 second (RVA
// 0x1010), so the records appear in the reverse of their emission order above.

	.section	.text,"xr",one_only,func1
	.globl	func1
	.p2align	1
	.seh_proc func1
func1:
	addi	sp, sp, -16
	.seh_stackalloc 16
	.seh_endprologue
	addi	a0, a0, 1
	addi	sp, sp, 16
	ret
	.seh_endproc

	.section	.text,"xr",one_only,func2
	.globl	func2
	.p2align	1
	.seh_proc func2
func2:
	addi	sp, sp, -16
	.seh_stackalloc 16
	.seh_endprologue
	addi	a0, a0, 2
	addi	sp, sp, 16
	ret
	.seh_endproc

// CHECK: Contents of section .pdata:
// CHECK-NEXT: 140003000 00100000 10100000 08200000 10100000
// CHECK-NEXT: 140003010 20100000 00200000
