// Check that lld resolves IMAGE_REL_RISCV64_SECREL_HI20/LO12_I in the Windows
// TLS access sequence.
//
// REQUIRES: riscv
// RUN: llvm-mc -filetype=obj -triple=riscv64-pc-windows-msvc %s -o %t.obj
// RUN: llvm-mc -filetype=obj -triple=riscv64-pc-windows-msvc %S/Inputs/riscv64-tls-index.s -o %t.index.obj
// RUN: lld-link -out:%t.exe -entry:get_big -subsystem:console -opt:noref %t.obj %t.index.obj
// RUN: llvm-objdump -d --triple=riscv64 --mattr=+c,+d %t.exe | FileCheck %s

	.section	.tls$,"dw"
	.globl	pad
	.p2align	2
pad:
	.zero	3000
	.globl	big_target
	.p2align	2
big_target:
	.word	777

	.text
	.globl	get_big
	.p2align	1
get_big:
	ld	a0, 88(tp)
.Lpcrel_hi0:
	auipc	a1, %pcrel_hi(_tls_index)
	addi	a1, a1, %pcrel_lo(.Lpcrel_hi0)
	lwu	a1, 0(a1)
	slli	a1, a1, 3
	add	a0, a0, a1
	ld	a0, 0(a0)
	lui	a1, %tls_secrel_hi(big_target)
	addi	a1, a1, %tls_secrel_lo(big_target)
	add	a0, a0, a1
	lw	a0, 0(a0)
	ret

// big_target's offset within .tls$ is 3000 (0xBB8) = 0x1000 - 0x448. The
// SECREL_HI20/LO12_I split biases the same way plain %hi/%lo do, so lui+addi
// must reconstruct exactly that.
// CHECK: lui a1, 0x1
// CHECK-NEXT: addi a1, a1, -0x448
