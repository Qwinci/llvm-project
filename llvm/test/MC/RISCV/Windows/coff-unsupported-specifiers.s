// COFF has no GOT, PLT, or ELF-style TLS models, so these specifiers must be
// rejected with a diagnostic rather than reaching the object writer.
//
// RUN: not llvm-mc -triple riscv64-pc-windows-msvc -filetype obj -o /dev/null %s 2>&1 | FileCheck %s --implicit-check-not=error:

	.text
.Lpcrel_hi0:
	auipc	a0, %got_pcrel_hi(foo)
	// CHECK: [[#@LINE-1]]:13: error: %got_pcrel_hi unsupported on COFF targets
	ld	a0, %pcrel_lo(.Lpcrel_hi0)(a0)

.Lpcrel_hi1:
	auipc	a1, %tls_gd_pcrel_hi(foo)
	// CHECK: [[#@LINE-1]]:13: error: %tls_gd_pcrel_hi unsupported on COFF targets
	addi	a1, a1, %pcrel_lo(.Lpcrel_hi1)

	.data
foo:
	.word	1
