// Check that /delayload emits the riscv64 delay-load thunk and tail-merge
// helper, plus a .pdata/UNWIND_INFO pair describing the tail-merge frame so a
// fault inside __delayLoadHelper2 can still be unwound.
//
// REQUIRES: riscv
// RUN: split-file %s %t.dir
// RUN: llvm-dlltool -m riscv64 -d %t.dir/lib.def -D library.dll -l %t.dir/lib.lib
// RUN: llvm-mc -filetype=obj -triple=riscv64-pc-windows-msvc %t.dir/main.s -o %t.dir/main.obj
// RUN: lld-link -out:%t.dir/main.exe -entry:main -subsystem:console \
// RUN:   -alternatename:__delayLoadHelper2=main %t.dir/main.obj %t.dir/lib.lib \
// RUN:   -delayload:library.dll
// RUN: llvm-objdump -d --triple=riscv64 --mattr=+d %t.dir/main.exe | FileCheck %s --check-prefix=DISASM
// RUN: llvm-readobj --coff-imports %t.dir/main.exe | FileCheck %s --check-prefix=IMPORTS
// RUN: llvm-readobj --unwind %t.dir/main.exe | FileCheck %s --check-prefix=UNWIND

//--- lib.def
LIBRARY library.dll
EXPORTS
function

//--- main.s
	.text
	.globl	main
	.p2align	1
main:
	call	function
	ret

// The thunk materializes the address of the __imp_ slot in t1 and tail-jumps
// (auipc+jalr, so no +/-1MB range limit) to the shared tail-merge chunk.
// DISASM:      auipc t1, 0x2
// DISASM-NEXT: addi  t1, t1, {{-?0x[0-9a-f]+}}
// DISASM-NEXT: auipc t2, 0x0
// DISASM-NEXT: jr    {{-?0x[0-9a-f]+}}(t2)

// The tail-merge spills every register that can carry an outgoing argument of
// the delayed call, calls the helper, then jumps to the resolved address.
// DISASM:      addi  sp, sp, -0x90
// DISASM-NEXT: sd    ra, 0x80(sp)
// DISASM-NEXT: sd    a0, 0x0(sp)
// DISASM-NEXT: sd    a1, 0x8(sp)
// DISASM-NEXT: sd    a2, 0x10(sp)
// DISASM-NEXT: sd    a3, 0x18(sp)
// DISASM-NEXT: sd    a4, 0x20(sp)
// DISASM-NEXT: sd    a5, 0x28(sp)
// DISASM-NEXT: sd    a6, 0x30(sp)
// DISASM-NEXT: sd    a7, 0x38(sp)
// DISASM-NEXT: fsd   fa0, 0x40(sp)
// DISASM-NEXT: fsd   fa1, 0x48(sp)
// DISASM-NEXT: fsd   fa2, 0x50(sp)
// DISASM-NEXT: fsd   fa3, 0x58(sp)
// DISASM-NEXT: fsd   fa4, 0x60(sp)
// DISASM-NEXT: fsd   fa5, 0x68(sp)
// DISASM-NEXT: fsd   fa6, 0x70(sp)
// DISASM-NEXT: fsd   fa7, 0x78(sp)
// DISASM-NEXT: mv    a1, t1
// DISASM-NEXT: auipc a0, 0x1
// DISASM-NEXT: addi  a0, a0, {{-?0x[0-9a-f]+}}
// DISASM-NEXT: auipc t2, 0x0
// DISASM-NEXT: jalr  {{-?0x[0-9a-f]+}}(t2)
// DISASM-NEXT: mv    t3, a0
// DISASM-NEXT: ld    ra, 0x80(sp)
// DISASM-NEXT: ld    a0, 0x0(sp)
// DISASM-NEXT: ld    a1, 0x8(sp)
// DISASM-NEXT: ld    a2, 0x10(sp)
// DISASM-NEXT: ld    a3, 0x18(sp)
// DISASM-NEXT: ld    a4, 0x20(sp)
// DISASM-NEXT: ld    a5, 0x28(sp)
// DISASM-NEXT: ld    a6, 0x30(sp)
// DISASM-NEXT: ld    a7, 0x38(sp)
// DISASM-NEXT: fld   fa0, 0x40(sp)
// DISASM-NEXT: fld   fa1, 0x48(sp)
// DISASM-NEXT: fld   fa2, 0x50(sp)
// DISASM-NEXT: fld   fa3, 0x58(sp)
// DISASM-NEXT: fld   fa4, 0x60(sp)
// DISASM-NEXT: fld   fa5, 0x68(sp)
// DISASM-NEXT: fld   fa6, 0x70(sp)
// DISASM-NEXT: fld   fa7, 0x78(sp)
// DISASM-NEXT: addi  sp, sp, 0x90
// DISASM-NEXT: jr    t3

// IMPORTS:      DelayImport {
// IMPORTS-NEXT:   Name: library.dll
// IMPORTS:        Symbol: function

// Only the stack allocation and the ra spill are recorded; a0-a7/fa0-fa7 are
// volatile and need no unwind codes.
// UNWIND:      UnwindInfo {
// UNWIND-NEXT:   Version: 1
// UNWIND-NEXT:   Flags [ (0x0)
// UNWIND-NEXT:   ]
// UNWIND-NEXT:   PrologSize: 72
// UNWIND-NEXT:   FrameRegister: -
// UNWIND-NEXT:   FrameOffset: -
// UNWIND-NEXT:   UnwindCodeCount: 4
// UNWIND-NEXT:   UnwindCodes [
// UNWIND-NEXT:     0x08: SAVE_NONVOL reg=ra, offset=0x80
// UNWIND-NEXT:     0x04: ALLOC_LARGE size=144
// UNWIND-NEXT:   ]
// UNWIND-NEXT: }
