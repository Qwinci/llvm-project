// Check the operand-less SEH opcodes used by hand-written OS runtime stubs
// (e.g. KiUserExceptionDispatcher, kernel trap entry): .seh_trap_frame,
// .seh_context and .seh_clear_unwound_to_call. These are never emitted by
// codegen. The encoding is documented in llvm/docs/RISCVWinCFI.md.
//
// Assembly round-trip (directive -> printed directive):
// RUN: llvm-mc -triple riscv64-pc-windows-msvc %s | FileCheck %s --check-prefix=ASM
//
// Object emission + decode:
// RUN: llvm-mc -triple riscv64-pc-windows-msvc -filetype obj -o %t.obj %s
// RUN: llvm-readobj -S --sd %t.obj | FileCheck %s --check-prefix=DATA
// RUN: llvm-readobj --unwind %t.obj | FileCheck %s --check-prefix=UNWIND

	.text
	.globl	dispatcher
	.seh_proc dispatcher
dispatcher:
	nop
	.seh_context
	nop
	.seh_clear_unwound_to_call
	nop
	.seh_trap_frame
	.seh_endprologue
	nop
	ret
	.seh_endproc

// ASM: .seh_context
// ASM: .seh_clear_unwound_to_call
// ASM: .seh_trap_frame

// UNWIND_INFO header: 01=version 1/no flags, 0C=PrologSize 12, 03=NumCodes 3,
// 00=FrameRegisterAndOffset. Then the codes in reverse (an unwinder walks "now"
// -> entry): trap_frame at 0x0C, then clear at 0x08, then context at 0x04. Each
// is a single 2-byte slot: CodeOffset byte then the RISCV64 on-disk nibble
// (6/7/8) with OpInfo 0. NumCodes is odd, so a 2-byte padding slot is appended.
// DATA:      Name: .xdata
// DATA:      SectionData (
// DATA-NEXT:   0000: 010C0300 0C060808 04070000
// DATA-NEXT: )

// UNWIND:      UnwindCodes [
// UNWIND-NEXT:    0x0C: TRAP_FRAME{{$}}
// UNWIND-NEXT:    0x08: CLEAR_UNWOUND_TO_CALL{{$}}
// UNWIND-NEXT:    0x04: CONTEXT{{$}}
// UNWIND-NEXT:  ]
