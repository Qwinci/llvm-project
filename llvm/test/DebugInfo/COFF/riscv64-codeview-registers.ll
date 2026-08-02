; CodeView register ids for RISCV64 are LLVM's own (CodeViewRegisters.def), as
; is the CPUType they are keyed off. Check that a dumper decodes them back to
; RISC-V register names rather than falling through to the x86 table.
;
; RUN: llc < %s -filetype=obj | llvm-readobj --codeview - | FileCheck %s

; CHECK: Compile3Sym {
; CHECK:   Machine: RISCV64 (0xFA)
; CHECK: DefRangeRegisterSym {
; CHECK:   Register: RISCV64_X10 (0x14)

target datalayout = "e-m:w-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "riscv64-pc-windows-msvc"

define i32 @add(i32 %a, i32 %b) !dbg !7 {
entry:
  %c = add nsw i32 %a, %b, !dbg !12
    #dbg_value(i32 %c, !11, !DIExpression(), !12)
  ret i32 %c, !dbg !12
}

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!3, !4, !5}

!0 = distinct !DICompileUnit(language: DW_LANG_C99, file: !1, producer: "clang", isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug, enums: !2)
!1 = !DIFile(filename: "t.c", directory: "/")
!2 = !{}
!3 = !{i32 2, !"CodeView", i32 1}
!4 = !{i32 2, !"Debug Info Version", i32 3}
!5 = !{i32 1, !"wchar_size", i32 2}
!7 = distinct !DISubprogram(name: "add", scope: !1, file: !1, line: 1, type: !8, scopeLine: 1, spFlags: DISPFlagDefinition, unit: !0, retainedNodes: !10)
!8 = !DISubroutineType(types: !9)
!9 = !{!13, !13, !13}
!10 = !{!11}
!11 = !DILocalVariable(name: "c", scope: !7, file: !1, line: 2, type: !13)
!12 = !DILocation(line: 2, column: 3, scope: !7)
!13 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
