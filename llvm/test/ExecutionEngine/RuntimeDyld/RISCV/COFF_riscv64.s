// RUN: llvm-mc -triple riscv64-windows -filetype obj -o %t.obj %s
// RUN: llvm-rtdyld -triple riscv64-windows -dummy-extern dummy=0x1234567890 \
// RUN:   -target-addr-start=40960000000000 -verify -check %s %t.obj

// The callee lives in its own section so that calls to it survive as
// relocations rather than being resolved by the assembler.
	.section	.text$callee,"xr"
	.globl	target
	.p2align	2
target:
	ret

	.text

// IMAGE_REL_RISCV64_CALL: the auipc/jalr pair share the auipc's PC baseline,
// so both halves are computed from the same displacement.
	.globl	test_call
	.p2align	2
# rtdyld-check: decode_operand(call_hi, 1) = (target - call_hi + 0x800)[31:12]
# rtdyld-check: (*{4}(call_hi+4))[31:20] = (target - call_hi)[11:0]
test_call:
call_hi:
	call	target
	ret

// IMAGE_REL_RISCV64_CALL to an external symbol, which can sit anywhere in the
// 64-bit address space and so must be routed through a stub. The stub loads
// its target from the literal following its four instructions.
	.globl	test_call_extern
	.p2align	2
# rtdyld-check: *{8}(stub_addr(COFF_riscv64.s.tmp.obj/.text, dummy) + 16) = dummy
# rtdyld-check: decode_operand(call_extern, 1) = (stub_addr(COFF_riscv64.s.tmp.obj/.text, dummy) - call_extern + 0x800)[31:12]
test_call_extern:
call_extern:
	call	dummy
	ret

// IMAGE_REL_RISCV64_JAL.
	.globl	test_jal
	.p2align	2
# rtdyld-check: decode_operand(jal, 1) = (target - jal)
test_jal:
jal:
	jal	zero, target

// IMAGE_REL_RISCV64_PCREL_HI20 / IMAGE_REL_RISCV64_PCREL_LO12_I. The low half
// reconstructs the same address as the high half despite sitting one
// instruction later, because its inline addend carries the distance back.
	.globl	test_pcrel
	.p2align	2
# rtdyld-check: decode_operand(pcrel_hi, 1) = (gvar - pcrel_hi + 0x800)[31:12]
# rtdyld-check: decode_operand(pcrel_lo, 2)[11:0] = (gvar - pcrel_hi)[11:0]
test_pcrel:
pcrel_hi:
	auipc	a0, %pcrel_hi(gvar)
pcrel_lo:
	addi	a0, a0, %pcrel_lo(test_pcrel)
	ret

// Same, but with a non-zero (page-crossing) symbol addend. COFF has no
// relocation addend field, so the byte offset rides in the auipc's immediate
// and must be folded into the target before the +0x800 high-part carry --
// otherwise the auipc lands a page (or more) too low. Regression test for the
// PCREL_HI20 addend being dropped.
	.globl	test_pcrel_addend
	.p2align	2
# rtdyld-check: decode_operand(pcrel_hi_a, 1) = (gvar - pcrel_hi_a + 0x1a34)[31:12]
# rtdyld-check: decode_operand(pcrel_lo_a, 2)[11:0] = (gvar - pcrel_hi_a + 0x1234)[11:0]
test_pcrel_addend:
pcrel_hi_a:
	auipc	a0, %pcrel_hi(gvar+0x1234)
pcrel_lo_a:
	addi	a0, a0, %pcrel_lo(test_pcrel_addend)
	ret

	.data

// IMAGE_REL_RISCV64_ADDR64, whose addend is held inline.
	.globl	gvar
	.p2align	3
gvar:
# rtdyld-check: *{8}gvar = target + 8
	.quad	target+8

// IMAGE_REL_RISCV64_ADDR32.
	.globl	gvar32
	.p2align	2
gvar32:
# rtdyld-check: *{4}gvar32 = target[31:0]
	.word	target
