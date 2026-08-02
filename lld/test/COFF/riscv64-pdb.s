// Check that /debug records the riscv64 CodeView CPU type in the linker's
// S_COMPILE3 record. Before RISCV64 was mapped, toCodeViewMachine() reached
// llvm_unreachable and the PDB claimed the image was ARMNT.
//
// REQUIRES: riscv
// RUN: llvm-mc -filetype=obj -triple=riscv64-pc-windows-msvc %s -o %t.obj
// RUN: rm -f %t.pdb
// RUN: lld-link -out:%t.exe -pdb:%t.pdb -entry:entry -subsystem:console -debug %t.obj
// RUN: llvm-pdbutil dump --symbols %t.pdb | FileCheck %s

	.text
	.globl	entry
	.p2align	1
entry:
	ret

// CHECK: S_COMPILE3
// CHECK-NEXT: machine = riscv64, Ver = LLVM Linker, language = link
