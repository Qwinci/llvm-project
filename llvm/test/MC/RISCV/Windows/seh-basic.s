// Check the .xdata/.pdata bytes emitted for a basic SEH prologue, and how
// llvm-readobj decodes them. The encoding is documented in
// llvm/docs/RISCVWinCFI.md.
//
// RUN: llvm-mc -triple riscv64-pc-windows-msvc -mattr=+d -filetype obj -o %t.obj %s
// RUN: llvm-readobj -S --sd -r %t.obj | FileCheck %s
// RUN: llvm-readobj --unwind %t.obj | FileCheck %s --check-prefix=UNWIND

	.text
	.globl	func
	.p2align	1
	.seh_proc func
func:
	addi	sp, sp, -32
	.seh_stackalloc 32
	sd	ra, 24(sp)
	.seh_savereg ra, 24
	sd	s0, 16(sp)
	.seh_savereg s0, 16
	fsd	fs0, 8(sp)
	.seh_savefreg fs0, 8
	addi	s0, sp, 32
	.seh_setframe s0, 0
	.seh_endprologue
	nop
	fld	fs0, 8(sp)
	ld	s0, 16(sp)
	ld	ra, 24(sp)
	addi	sp, sp, 32
	ret
	.seh_endproc

// CHECK: Name: .xdata
// CHECK: RawDataSize: 20
// CHECK:      SectionData (
// CHECK-NEXT:   0000: 01140800 14031005 01000C14 02000804
// This slot sequence, decoded (prologue is undone in reverse order):
//   offset=0x14(20) op=3 (SetFPReg)                          -- .seh_setframe s0, 0
//   offset=0x10(16) op=5 reg=0 (RISCVSaveFReg fs0) off=8     -- .seh_savefreg fs0, 8
//   offset=0x0C(12) op=4 reg=1 (SaveNonVol s0)     off=16    -- .seh_savereg s0, 16
//   offset=0x08(8)  op=4 reg=0 (SaveNonVol ra)     off=24    -- .seh_savereg ra, 24
// CHECK-NEXT:   0010: 03000432
//   offset=0x04(4)  op=2 alloc-nibble=3 (AllocSmall 32)      -- .seh_stackalloc 32
// CHECK-NEXT: )

// CHECK: Name: .pdata
// CHECK: RawDataSize: 12

// CHECK: Relocations [
// CHECK:   Section ({{.*}}) .pdata {
// CHECK-NEXT: 0x0 IMAGE_REL_RISCV64_ADDR32NB .text
// CHECK-NEXT: 0x4 IMAGE_REL_RISCV64_ADDR32NB .text
// CHECK-NEXT: 0x8 IMAGE_REL_RISCV64_ADDR32NB .xdata
// CHECK-NEXT:   }
// CHECK-NEXT: ]

// The `.seh_setframe s0, 0` above deliberately uses offset 0. x86_64/ARM64 pack
// a register field into this header byte, so a zero byte safely means "no frame
// pointer"; RISCV64 uses the whole byte as offset/16, making 0 an ordinary
// value. A decoder must therefore not confuse "frame pointer at offset 0" with
// "no frame pointer" -- FrameRegister/FrameOffset must print s0/0 here, not the
// "-"/"<invalid>" a naive `FrameRegisterAndOffset == 0` test would give.
// UNWIND:      UnwindInfo {
// UNWIND-NEXT:   Version: 1
// UNWIND-NEXT:   Flags [ (0x0)
// UNWIND-NEXT:   ]
// UNWIND-NEXT:   PrologSize: 20
// UNWIND-NEXT:   FrameRegister: s0
// UNWIND-NEXT:   FrameOffset: 0x0
// UNWIND-NEXT:   UnwindCodeCount: 8
// UNWIND-NEXT:   UnwindCodes [
// UNWIND-NEXT:     0x14: SET_FPREG reg=s0, offset=0x0
// UNWIND-NEXT:     0x10: SAVE_FREG reg=fs0, offset=0x8
// UNWIND-NEXT:     0x0C: SAVE_NONVOL reg=s0, offset=0x10
// UNWIND-NEXT:     0x08: SAVE_NONVOL reg=ra, offset=0x18
// UNWIND-NEXT:     0x04: ALLOC_SMALL size=32
// UNWIND-NEXT:   ]
// UNWIND-NEXT: }
