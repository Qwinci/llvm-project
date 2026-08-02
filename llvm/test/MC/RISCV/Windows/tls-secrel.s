// %tls_secrel_hi/%tls_secrel_lo materialize a symbol's link-time offset within
// .tls$, which is how Windows TLS reaches a variable instead of ELF's
// %tls_gd_pcrel_hi/__tls_get_addr scheme. Check the relocations they produce.
//
// RUN: llvm-mc -triple riscv64-pc-windows-msvc -filetype obj -o %t.obj %s
// RUN: llvm-readobj -r %t.obj | FileCheck %s

	.section	.tls$,"dw"
	.globl	tls_var
	.p2align	2
tls_var:
	.word	42

	.text
	.globl	get_tls
	.p2align	1
get_tls:
	ld	a0, 88(tp)
.Lpcrel_hi0:
	auipc	a1, %pcrel_hi(_tls_index)
	addi	a1, a1, %pcrel_lo(.Lpcrel_hi0)
	lwu	a1, 0(a1)
	slli	a1, a1, 3
	add	a0, a0, a1
	ld	a0, 0(a0)
	// IMAGE_REL_RISCV64_SECREL_HI20
	lui	a1, %tls_secrel_hi(tls_var)
	// IMAGE_REL_RISCV64_SECREL_LO12_I
	addi	a1, a1, %tls_secrel_lo(tls_var)
	add	a0, a0, a1
	lw	a0, 0(a0)
	ret

// CHECK: Format: COFF-RISCV64
// CHECK: Relocations [
// CHECK:   Section ({{.*}}) .text {
// CHECK:     IMAGE_REL_RISCV64_PCREL_HI20 _tls_index
// CHECK:     IMAGE_REL_RISCV64_PCREL_LO12_I _tls_index
// CHECK:     IMAGE_REL_RISCV64_SECREL_HI20 tls_var
// CHECK:     IMAGE_REL_RISCV64_SECREL_LO12_I tls_var
// CHECK:   }
// CHECK: ]
