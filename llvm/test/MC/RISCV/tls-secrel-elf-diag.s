// %tls_secrel_hi/%tls_secrel_lo are COFF-only; ELF TLS uses
// %tls_gd_pcrel_hi/%tprel_hi/__tls_get_addr instead, so they must be rejected
// on ELF targets.
//
// RUN: not llvm-mc -triple riscv64-unknown-linux-gnu -filetype obj -o /dev/null %s 2>&1 | FileCheck %s

	.text
	lui	a1, %tls_secrel_hi(tls_var)
	// CHECK: error: unsupported relocation type
	addi	a1, a1, %tls_secrel_lo(tls_var)
	// CHECK: error: unsupported relocation type

	.data
tls_var:
	.word	42
