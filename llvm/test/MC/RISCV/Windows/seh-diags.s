// Check the register and offset validation in RISCVWinCOFFStreamer.cpp.
//
// RUN: not llvm-mc -triple riscv64-pc-windows-msvc -filetype obj -o /dev/null %s 2>&1 | FileCheck %s --implicit-check-not=error:

	.text
	.globl	func
	.seh_proc func
func:
	addi	sp, sp, -16
	.seh_stackalloc 16
	sd	a0, 0(sp)
	.seh_savereg a0, 0
	// CHECK: [[#@LINE-1]]:2: error: invalid register for .seh_savereg, expected ra or s0-s11
	sd	ra, 8(sp)
	.seh_savereg ra, 3
	// CHECK: [[#@LINE-1]]:2: error: offset for .seh_savereg must be a non-negative multiple of 8
	.seh_setframe a0, 0
	// CHECK: [[#@LINE-1]]:2: error: invalid register for .seh_setframe, expected s0
	.seh_setframe s0, 8
	// CHECK: [[#@LINE-1]]:2: error: offset for .seh_setframe must be between 0 and 4080 and a multiple of 16
	.seh_setframe s0, 4096
	// CHECK: [[#@LINE-1]]:2: error: offset for .seh_setframe must be between 0 and 4080 and a multiple of 16
	.seh_endprologue
	addi	sp, sp, 16
	ret
	.seh_endproc
