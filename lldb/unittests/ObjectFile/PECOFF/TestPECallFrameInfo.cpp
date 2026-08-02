//===-- TestPECallFrameInfo.cpp -------------------------------------------===//
//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "gtest/gtest.h"

#include "Plugins/ObjectFile/PECOFF/ObjectFilePECOFF.h"
#include "Plugins/Process/Utility/lldb-x86-register-enums.h"
#include "TestingSupport/SubsystemRAII.h"
#include "TestingSupport/TestUtilities.h"
#include "Utility/RISCV_DWARF_Registers.h"

#include "lldb/Core/Module.h"
#include "lldb/Symbol/CallFrameInfo.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "llvm/Testing/Support/Error.h"

using namespace lldb_private;
using namespace lldb;

class PECallFrameInfoTest : public testing::Test {
  SubsystemRAII<FileSystem, ObjectFilePECOFF> subsystems;
};

static llvm::Expected<std::unique_ptr<UnwindPlan>>
GetUnwindPlanFromYaml(llvm::StringRef yaml, addr_t file_addr) {
  llvm::Expected<TestFile> ExpectedFile = TestFile::fromYaml(yaml);
  if (!ExpectedFile)
    return ExpectedFile.takeError();

  ModuleSP module_sp = std::make_shared<Module>(ExpectedFile->moduleSpec());
  ObjectFile *object_file = module_sp->GetObjectFile();
  if (!object_file)
    return llvm::createStringError("object file is null");

  std::unique_ptr<CallFrameInfo> cfi = object_file->CreateCallFrameInfo();
  if (!cfi)
    return llvm::createStringError("call frame info is null");

  SectionList *sect_list = object_file->GetSectionList();
  if (!sect_list)
    return llvm::createStringError("section list is null");

  std::unique_ptr<UnwindPlan> plan_up =
      cfi->GetUnwindPlan(Address(file_addr, sect_list));
  if (!plan_up)
    return llvm::createStringError("unwind plan is null");
  return plan_up;
}

static llvm::Expected<std::unique_ptr<UnwindPlan>>
GetUnwindPlan(addr_t file_addr) {
  return GetUnwindPlanFromYaml(
      R"(
--- !COFF
OptionalHeader:  
  AddressOfEntryPoint: 0
  ImageBase:       16777216
  SectionAlignment: 4096
  FileAlignment:   512
  MajorOperatingSystemVersion: 6
  MinorOperatingSystemVersion: 0
  MajorImageVersion: 0
  MinorImageVersion: 0
  MajorSubsystemVersion: 6
  MinorSubsystemVersion: 0
  Subsystem:       IMAGE_SUBSYSTEM_WINDOWS_CUI
  DLLCharacteristics: [ IMAGE_DLL_CHARACTERISTICS_HIGH_ENTROPY_VA, IMAGE_DLL_CHARACTERISTICS_DYNAMIC_BASE, IMAGE_DLL_CHARACTERISTICS_NX_COMPAT ]
  SizeOfStackReserve: 1048576
  SizeOfStackCommit: 4096
  SizeOfHeapReserve: 1048576
  SizeOfHeapCommit: 4096
  ExportTable:     
    RelativeVirtualAddress: 0
    Size:            0
  ImportTable:     
    RelativeVirtualAddress: 0
    Size:            0
  ResourceTable:   
    RelativeVirtualAddress: 0
    Size:            0
  ExceptionTable:  
    RelativeVirtualAddress: 12288
    Size:            60
  CertificateTable: 
    RelativeVirtualAddress: 0
    Size:            0
  BaseRelocationTable: 
    RelativeVirtualAddress: 0
    Size:            0
  Debug:           
    RelativeVirtualAddress: 0
    Size:            0
  Architecture:    
    RelativeVirtualAddress: 0
    Size:            0
  GlobalPtr:       
    RelativeVirtualAddress: 0
    Size:            0
  TlsTable:        
    RelativeVirtualAddress: 0
    Size:            0
  LoadConfigTable: 
    RelativeVirtualAddress: 0
    Size:            0
  BoundImport:     
    RelativeVirtualAddress: 0
    Size:            0
  IAT:             
    RelativeVirtualAddress: 0
    Size:            0
  DelayImportDescriptor: 
    RelativeVirtualAddress: 0
    Size:            0
  ClrRuntimeHeader: 
    RelativeVirtualAddress: 0
    Size:            0
header:          
  Machine:         IMAGE_FILE_MACHINE_AMD64
  Characteristics: [ IMAGE_FILE_EXECUTABLE_IMAGE, IMAGE_FILE_LARGE_ADDRESS_AWARE ]
sections:        
  - Name:            .text
    Characteristics: [ IMAGE_SCN_CNT_CODE, IMAGE_SCN_MEM_EXECUTE, IMAGE_SCN_MEM_READ ]
    VirtualAddress:  4096
    VirtualSize:     4096
  - Name:            .rdata
    Characteristics: [ IMAGE_SCN_CNT_INITIALIZED_DATA, IMAGE_SCN_MEM_READ ]
    VirtualAddress:  8192
    VirtualSize:     68
    SectionData:     010C06000C3208F006E00470036002302105020005540D0000100000001100000020000019400E352F74670028646600213465001A3315015E000EF00CE00AD008C00650


# Unwind info at 0x2000:
# 01 0C 06 00    No chained info, prolog size = 0xC, unwind codes size is 6 words, no frame register
# 0C 32          UOP_AllocSmall(2) 3 * 8 + 8 bytes, offset in prolog is 0xC
# 08 F0          UOP_PushNonVol(0) R15(0xF), offset in prolog is 8
# 06 E0          UOP_PushNonVol(0) R14(0xE), offset in prolog is 6
# 04 70          UOP_PushNonVol(0) RDI(7), offset in prolog is 4
# 03 60          UOP_PushNonVol(0) RSI(6), offset in prolog is 3
# 02 30          UOP_PushNonVol(0) RBX(3), offset in prolog is 2
# Corresponding prolog:
# 00    push    rbx
# 02    push    rsi
# 03    push    rdi
# 04    push    r14
# 06    push    r15
# 08    sub     rsp, 20h

# Unwind info at 0x2010:
# 21 05 02 00    Has chained info, prolog size = 5, unwind codes size is 2 words, no frame register
# 05 54 0D 00    UOP_SaveNonVol(4) RBP(5) to RSP + 0xD * 8, offset in prolog is 5
# Chained runtime function:
# 00 10 00 00    Start address is 0x1000
# 00 11 00 00    End address is 0x1100
# 00 20 00 00    Unwind info RVA is 0x2000
# Corresponding prolog:
# 00    mov     [rsp+68h], rbp

# Unwind info at 0x2024:
# 19 40 0E 35    No chained info, prolog size = 0x40, unwind codes size is 0xE words, frame register is RBP, frame register offset is RSP + 3 * 16
# 2F 74 67 00    UOP_SaveNonVol(4) RDI(7) to RSP + 0x67 * 8, offset in prolog is 0x2F
# 28 64 66 00    UOP_SaveNonVol(4) RSI(6) to RSP + 0x66 * 8, offset in prolog is 0x28
# 21 34 65 00    UOP_SaveNonVol(4) RBX(3) to RSP + 0x65 * 8, offset in prolog is 0x21
# 1A 33          UOP_SetFPReg(3), offset in prolog is 0x1A
# 15 01 5E 00    UOP_AllocLarge(1) 0x5E * 8 bytes, offset in prolog is 0x15
# 0E F0          UOP_PushNonVol(0) R15(0xF), offset in prolog is 0xE
# 0C E0          UOP_PushNonVol(0) R14(0xE), offset in prolog is 0xC
# 0A D0          UOP_PushNonVol(0) R13(0xD), offset in prolog is 0xA
# 08 C0          UOP_PushNonVol(0) R12(0xC), offset in prolog is 8
# 06 50          UOP_PushNonVol(0) RBP(5), offset in prolog is 6
# Corresponding prolog:
# 00    mov     [rsp+8], rcx
# 05    push    rbp
# 06    push    r12
# 08    push    r13
# 0A    push    r14
# 0C    push    r15
# 0E    sub     rsp, 2F0h
# 15    lea     rbp, [rsp+30h]
# 1A    mov     [rbp+2F8h], rbx
# 21    mov     [rbp+300h], rsi
# 28    mov     [rbp+308h], rdi

  - Name:            .pdata
    Characteristics: [ IMAGE_SCN_CNT_INITIALIZED_DATA, IMAGE_SCN_MEM_READ ]
    VirtualAddress:  12288
    VirtualSize:     60
    SectionData:     000000000000000000000000000000000000000000000000001000000011000000200000001100000012000010200000001200000013000024200000

# 00 00 00 00
# 00 00 00 00    Test correct processing of empty runtime functions at begin
# 00 00 00 00

# 00 00 00 00
# 00 00 00 00    Test correct processing of empty runtime functions at begin
# 00 00 00 00

# 00 10 00 00    Start address is 0x1000
# 00 11 00 00    End address is 0x1100
# 00 20 00 00    Unwind info RVA is 0x2000

# 00 11 00 00    Start address is 0x1100
# 00 12 00 00    End address is 0x1200
# 10 20 00 00    Unwind info RVA is 0x2010

# 00 12 00 00    Start address is 0x1200
# 00 13 00 00    End address is 0x1300
# 24 20 00 00    Unwind info RVA is 0x2024

symbols:         []
...
)",
      file_addr);
}

TEST_F(PECallFrameInfoTest, Basic_eh) {
  llvm::Expected<std::unique_ptr<UnwindPlan>> expected_plan =
      GetUnwindPlan(0x1001080);
  ASSERT_THAT_EXPECTED(expected_plan, llvm::Succeeded());
  UnwindPlan &plan = **expected_plan;
  EXPECT_EQ(plan.GetRowCount(), 7);

  UnwindPlan::Row row;
  row.SetOffset(0);
  row.GetCFAValue().SetIsRegisterPlusOffset(lldb_rsp_x86_64, 8);
  row.SetRegisterLocationToIsCFAPlusOffset(lldb_rsp_x86_64, 0, true);
  row.SetRegisterLocationToAtCFAPlusOffset(lldb_rip_x86_64, -8, true);
  EXPECT_EQ(*plan.GetRowAtIndex(0), row);

  row.SetOffset(2);
  row.GetCFAValue().SetIsRegisterPlusOffset(lldb_rsp_x86_64, 0x10);
  row.SetRegisterLocationToAtCFAPlusOffset(lldb_rbx_x86_64, -0x10, true);
  EXPECT_EQ(*plan.GetRowAtIndex(1), row);

  row.SetOffset(3);
  row.GetCFAValue().SetIsRegisterPlusOffset(lldb_rsp_x86_64, 0x18);
  row.SetRegisterLocationToAtCFAPlusOffset(lldb_rsi_x86_64, -0x18, true);
  EXPECT_EQ(*plan.GetRowAtIndex(2), row);

  row.SetOffset(4);
  row.GetCFAValue().SetIsRegisterPlusOffset(lldb_rsp_x86_64, 0x20);
  row.SetRegisterLocationToAtCFAPlusOffset(lldb_rdi_x86_64, -0x20, true);
  EXPECT_EQ(*plan.GetRowAtIndex(3), row);

  row.SetOffset(6);
  row.GetCFAValue().SetIsRegisterPlusOffset(lldb_rsp_x86_64, 0x28);
  row.SetRegisterLocationToAtCFAPlusOffset(lldb_r14_x86_64, -0x28, true);
  EXPECT_EQ(*plan.GetRowAtIndex(4), row);

  row.SetOffset(8);
  row.GetCFAValue().SetIsRegisterPlusOffset(lldb_rsp_x86_64, 0x30);
  row.SetRegisterLocationToAtCFAPlusOffset(lldb_r15_x86_64, -0x30, true);
  EXPECT_EQ(*plan.GetRowAtIndex(5), row);

  row.SetOffset(0xC);
  row.GetCFAValue().SetIsRegisterPlusOffset(lldb_rsp_x86_64, 0x50);
  EXPECT_EQ(*plan.GetRowAtIndex(6), row);
}

TEST_F(PECallFrameInfoTest, Chained_eh) {
  llvm::Expected<std::unique_ptr<UnwindPlan>> expected_plan =
      GetUnwindPlan(0x1001180);
  ASSERT_THAT_EXPECTED(expected_plan, llvm::Succeeded());
  UnwindPlan &plan = **expected_plan;
  EXPECT_EQ(plan.GetRowCount(), 2);

  UnwindPlan::Row row;
  row.SetOffset(0);
  row.GetCFAValue().SetIsRegisterPlusOffset(lldb_rsp_x86_64, 0x50);
  row.SetRegisterLocationToIsCFAPlusOffset(lldb_rsp_x86_64, 0, true);
  row.SetRegisterLocationToAtCFAPlusOffset(lldb_rip_x86_64, -8, true);
  row.SetRegisterLocationToAtCFAPlusOffset(lldb_rbx_x86_64, -0x10, true);
  row.SetRegisterLocationToAtCFAPlusOffset(lldb_rsi_x86_64, -0x18, true);
  row.SetRegisterLocationToAtCFAPlusOffset(lldb_rdi_x86_64, -0x20, true);
  row.SetRegisterLocationToAtCFAPlusOffset(lldb_r14_x86_64, -0x28, true);
  row.SetRegisterLocationToAtCFAPlusOffset(lldb_r15_x86_64, -0x30, true);
  EXPECT_EQ(*plan.GetRowAtIndex(0), row);

  row.SetOffset(5);
  row.SetRegisterLocationToAtCFAPlusOffset(lldb_rbp_x86_64, 0x18, true);
  EXPECT_EQ(*plan.GetRowAtIndex(1), row);
}

TEST_F(PECallFrameInfoTest, Frame_reg_eh) {
  llvm::Expected<std::unique_ptr<UnwindPlan>> expected_plan =
      GetUnwindPlan(0x1001280);
  ASSERT_THAT_EXPECTED(expected_plan, llvm::Succeeded());
  UnwindPlan &plan = **expected_plan;
  EXPECT_EQ(plan.GetRowCount(), 11);

  UnwindPlan::Row row;
  row.SetOffset(0);
  row.GetCFAValue().SetIsRegisterPlusOffset(lldb_rsp_x86_64, 8);
  row.SetRegisterLocationToIsCFAPlusOffset(lldb_rsp_x86_64, 0, true);
  row.SetRegisterLocationToAtCFAPlusOffset(lldb_rip_x86_64, -8, true);
  EXPECT_EQ(*plan.GetRowAtIndex(0), row);

  row.SetOffset(6);
  row.GetCFAValue().SetIsRegisterPlusOffset(lldb_rsp_x86_64, 0x10);
  row.SetRegisterLocationToAtCFAPlusOffset(lldb_rbp_x86_64, -0x10, true);
  EXPECT_EQ(*plan.GetRowAtIndex(1), row);

  row.SetOffset(8);
  row.GetCFAValue().SetIsRegisterPlusOffset(lldb_rsp_x86_64, 0x18);
  row.SetRegisterLocationToAtCFAPlusOffset(lldb_r12_x86_64, -0x18, true);
  EXPECT_EQ(*plan.GetRowAtIndex(2), row);

  row.SetOffset(0xA);
  row.GetCFAValue().SetIsRegisterPlusOffset(lldb_rsp_x86_64, 0x20);
  row.SetRegisterLocationToAtCFAPlusOffset(lldb_r13_x86_64, -0x20, true);
  EXPECT_EQ(*plan.GetRowAtIndex(3), row);

  row.SetOffset(0xC);
  row.GetCFAValue().SetIsRegisterPlusOffset(lldb_rsp_x86_64, 0x28);
  row.SetRegisterLocationToAtCFAPlusOffset(lldb_r14_x86_64, -0x28, true);
  EXPECT_EQ(*plan.GetRowAtIndex(4), row);

  row.SetOffset(0xE);
  row.GetCFAValue().SetIsRegisterPlusOffset(lldb_rsp_x86_64, 0x30);
  row.SetRegisterLocationToAtCFAPlusOffset(lldb_r15_x86_64, -0x30, true);
  EXPECT_EQ(*plan.GetRowAtIndex(5), row);

  row.SetOffset(0x15);
  row.GetCFAValue().SetIsRegisterPlusOffset(lldb_rsp_x86_64, 0x320);
  EXPECT_EQ(*plan.GetRowAtIndex(6), row);

  row.SetOffset(0x1A);
  row.GetCFAValue().SetIsRegisterPlusOffset(lldb_rbp_x86_64, 0x2F0);
  EXPECT_EQ(*plan.GetRowAtIndex(7), row);

  row.SetOffset(0x21);
  row.SetRegisterLocationToAtCFAPlusOffset(lldb_rbx_x86_64, 8, true);
  EXPECT_EQ(*plan.GetRowAtIndex(8), row);

  row.SetOffset(0x28);
  row.SetRegisterLocationToAtCFAPlusOffset(lldb_rsi_x86_64, 0x10, true);
  EXPECT_EQ(*plan.GetRowAtIndex(9), row);

  row.SetOffset(0x2F);
  row.SetRegisterLocationToAtCFAPlusOffset(lldb_rdi_x86_64, 0x18, true);
  EXPECT_EQ(*plan.GetRowAtIndex(10), row);
}

class PECallFrameInfoRISCV64Test : public testing::Test {
  SubsystemRAII<FileSystem, ObjectFilePECOFF> subsystems;
};

static llvm::Expected<std::unique_ptr<UnwindPlan>>
GetRISCV64UnwindPlan(addr_t file_addr) {
  return GetUnwindPlanFromYaml(
      R"(
--- !COFF
OptionalHeader:
  AddressOfEntryPoint: 0
  ImageBase:       16777216
  SectionAlignment: 4096
  FileAlignment:   512
  MajorOperatingSystemVersion: 6
  MinorOperatingSystemVersion: 0
  MajorImageVersion: 0
  MinorImageVersion: 0
  MajorSubsystemVersion: 6
  MinorSubsystemVersion: 0
  Subsystem:       IMAGE_SUBSYSTEM_WINDOWS_CUI
  DLLCharacteristics: [ ]
  SizeOfStackReserve: 1048576
  SizeOfStackCommit: 4096
  SizeOfHeapReserve: 1048576
  SizeOfHeapCommit: 4096
  ExportTable:
    RelativeVirtualAddress: 0
    Size:            0
  ImportTable:
    RelativeVirtualAddress: 0
    Size:            0
  ResourceTable:
    RelativeVirtualAddress: 0
    Size:            0
  ExceptionTable:
    RelativeVirtualAddress: 12288
    Size:            60
  CertificateTable:
    RelativeVirtualAddress: 0
    Size:            0
  BaseRelocationTable:
    RelativeVirtualAddress: 0
    Size:            0
  Debug:
    RelativeVirtualAddress: 0
    Size:            0
  Architecture:
    RelativeVirtualAddress: 0
    Size:            0
  GlobalPtr:
    RelativeVirtualAddress: 0
    Size:            0
  TlsTable:
    RelativeVirtualAddress: 0
    Size:            0
  LoadConfigTable:
    RelativeVirtualAddress: 0
    Size:            0
  BoundImport:
    RelativeVirtualAddress: 0
    Size:            0
  IAT:
    RelativeVirtualAddress: 0
    Size:            0
  DelayImportDescriptor:
    RelativeVirtualAddress: 0
    Size:            0
  ClrRuntimeHeader:
    RelativeVirtualAddress: 0
    Size:            0
header:
  Machine:         IMAGE_FILE_MACHINE_RISCV64
  Characteristics: [ IMAGE_FILE_EXECUTABLE_IMAGE, IMAGE_FILE_LARGE_ADDRESS_AWARE ]
sections:
  - Name:            .text
    Characteristics: [ IMAGE_SCN_CNT_CODE, IMAGE_SCN_MEM_EXECUTE, IMAGE_SCN_MEM_READ ]
    VirtualAddress:  4096
    VirtualSize:     4096
  - Name:            .rdata
    Characteristics: [ IMAGE_SCN_CNT_INITIALIZED_DATA, IMAGE_SCN_MEM_READ ]
    VirtualAddress:  8192
    VirtualSize:     68
    SectionData:     011408001403100501000C14020008040300043200000000011005000C010002080403000432000001000000000000000104010004090000010C04000C07080401000412

# Unwind info at 0x2000, taken verbatim from the .xdata bytes asserted in
# llvm/test/MC/RISCV/Windows/seh-basic.s:
# 01 14 08 00    version 1, prolog size 0x14, 8 unwind codes,
#                FrameRegisterAndOffset = 0 (a whole byte holding offset / 16;
#                zero is a legal offset here, not a "no frame register" marker)
# 14 03          on-disk op 3 = UOP_SetFPReg, prolog offset 0x14
# 10 05 01 00    on-disk op 5 = UOP_RISCVSaveFReg fs0 to sp + 1 * 8, offset 0x10
# 0C 14          on-disk op 4 = UOP_SaveNonVol s0(1) ...
# 02 00              ... to sp + 2 * 8, prolog offset 0x0C
# 08 04          on-disk op 4 = UOP_SaveNonVol ra(0) ...
# 03 00              ... to sp + 3 * 8, prolog offset 8
# 04 32          on-disk op 2 = UOP_AllocSmall (3 + 1) * 8 = 32, offset 4

# Unwind info at 0x2018, a split stack adjustment with no frame pointer. The
# saves sit between the two allocations, so their offsets are relative to the
# stack pointer after the *first* one only:
# 01 10 05 00    version 1, prolog size 0x10, 5 unwind codes, no frame register
# 0C 01 00 02    UOP_AllocLarge 0x200 * 8 = 4096, prolog offset 0x0C
# 08 04 03 00    UOP_SaveNonVol ra(0) to sp + 3 * 8, prolog offset 8
# 04 32          UOP_AllocSmall (3 + 1) * 8 = 32, prolog offset 4
# 00 00          padding to an even number of slots

# Unwind info at 0x2028, a leaf function:
# 01 00 00 00    version 1, prolog size 0, no unwind codes, no frame register
# 00 00 00 00    the four byte tail pad a zero code record still carries

# Unwind info at 0x2030, malformed:
# 01 04 01 00    version 1, prolog size 4, 1 unwind code, no frame register
# 04 09          on-disk op 9, which RISCV64 does not define
# 00 00          padding

# Unwind info at 0x2038, a hand-written stub carrying a dispatcher-only opcode
# (on-disk 7 = UOP_Context) paired with an ordinary ra save. The dispatcher
# opcode describes an OS-reconstructed register context and takes no operand; a
# debugger backtrace consumes it without emitting a row, so this must decode to
# exactly the plan the ra save and allocation alone would give:
# 01 0C 04 00    version 1, prolog size 0x0C, 4 unwind codes, no frame register
# 0C 07          on-disk op 7 = UOP_Context, prolog offset 0x0C (no operand)
# 08 04 01 00    on-disk op 4 = UOP_SaveNonVol ra(0) to sp + 1 * 8, offset 8
# 04 12          on-disk op 2 = UOP_AllocSmall (1 + 1) * 8 = 16, offset 4

  - Name:            .pdata
    Characteristics: [ IMAGE_SCN_CNT_INITIALIZED_DATA, IMAGE_SCN_MEM_READ ]
    VirtualAddress:  12288
    VirtualSize:     60
    SectionData:     001000000011000000200000001100000012000018200000001200000013000028200000001300000014000030200000001400000015000038200000

# 0x1000 - 0x1100, unwind info at 0x2000
# 0x1100 - 0x1200, unwind info at 0x2018
# 0x1200 - 0x1300, unwind info at 0x2028
# 0x1300 - 0x1400, unwind info at 0x2030
# 0x1400 - 0x1500, unwind info at 0x2038

symbols:         []
...
)",
      file_addr);
}

TEST_F(PECallFrameInfoRISCV64Test, PrologueWithFramePointer) {
  llvm::Expected<std::unique_ptr<UnwindPlan>> expected_plan =
      GetRISCV64UnwindPlan(0x1001000);
  ASSERT_THAT_EXPECTED(expected_plan, llvm::Succeeded());
  UnwindPlan &plan = **expected_plan;

  // The return address lives in ra, so it is named by the plan rather than
  // modelled as a push.
  EXPECT_EQ(plan.GetReturnAddressRegister(), riscv_dwarf::dwarf_gpr_x1);
  EXPECT_EQ(plan.GetRowCount(), 6);

  UnwindPlan::Row row;

  // Row zero is synthesized. Every unwind code carries a non-zero CodeOffset,
  // so without it GetRowForFunctionOffset would return nullptr for a PC in the
  // first four bytes of the function.
  row.SetOffset(0);
  row.GetCFAValue().SetIsRegisterPlusOffset(riscv_dwarf::dwarf_gpr_x2, 0);
  row.SetRegisterLocationToIsCFAPlusOffset(riscv_dwarf::dwarf_gpr_x2, 0, true);
  EXPECT_EQ(*plan.GetRowAtIndex(0), row);

  row.SetOffset(4);
  row.GetCFAValue().SetIsRegisterPlusOffset(riscv_dwarf::dwarf_gpr_x2, 32);
  EXPECT_EQ(*plan.GetRowAtIndex(1), row);

  // ra is saved at sp + 24 with the CFA 32 bytes above sp.
  row.SetOffset(8);
  row.SetRegisterLocationToAtCFAPlusOffset(riscv_dwarf::dwarf_gpr_x1, -8, true);
  EXPECT_EQ(*plan.GetRowAtIndex(2), row);

  row.SetOffset(12);
  row.SetRegisterLocationToAtCFAPlusOffset(riscv_dwarf::dwarf_gpr_x8, -16,
                                           true);
  EXPECT_EQ(*plan.GetRowAtIndex(3), row);

  row.SetOffset(16);
  row.SetRegisterLocationToAtCFAPlusOffset(riscv_dwarf::dwarf_fpr_f8, -24,
                                           true);
  EXPECT_EQ(*plan.GetRowAtIndex(4), row);

  // Once s0 is established the CFA is anchored on it. The frame offset here is
  // zero against a 32 byte allocation, so this must come out as s0 + 32 rather
  // than the s0 + 0 that hardcoding the common case would give.
  row.SetOffset(20);
  row.GetCFAValue().SetIsRegisterPlusOffset(riscv_dwarf::dwarf_gpr_x8, 32);
  EXPECT_EQ(*plan.GetRowAtIndex(5), row);
}

TEST_F(PECallFrameInfoRISCV64Test, SplitStackAdjustmentWithoutFramePointer) {
  llvm::Expected<std::unique_ptr<UnwindPlan>> expected_plan =
      GetRISCV64UnwindPlan(0x1001100);
  ASSERT_THAT_EXPECTED(expected_plan, llvm::Succeeded());
  UnwindPlan &plan = **expected_plan;
  EXPECT_EQ(plan.GetRowCount(), 4);

  UnwindPlan::Row row;

  row.SetOffset(0);
  row.GetCFAValue().SetIsRegisterPlusOffset(riscv_dwarf::dwarf_gpr_x2, 0);
  row.SetRegisterLocationToIsCFAPlusOffset(riscv_dwarf::dwarf_gpr_x2, 0, true);
  EXPECT_EQ(*plan.GetRowAtIndex(0), row);

  row.SetOffset(4);
  row.GetCFAValue().SetIsRegisterPlusOffset(riscv_dwarf::dwarf_gpr_x2, 32);
  EXPECT_EQ(*plan.GetRowAtIndex(1), row);

  row.SetOffset(8);
  row.SetRegisterLocationToAtCFAPlusOffset(riscv_dwarf::dwarf_gpr_x1, -8, true);
  EXPECT_EQ(*plan.GetRowAtIndex(2), row);

  // The second allocation moves the CFA but not the save, which was made
  // against the stack pointer as it stood before that allocation. Computing the
  // save's location against the running CFA instead would give 24 - 4128.
  row.SetOffset(12);
  row.GetCFAValue().SetIsRegisterPlusOffset(riscv_dwarf::dwarf_gpr_x2, 4128);
  EXPECT_EQ(*plan.GetRowAtIndex(3), row);
}

TEST_F(PECallFrameInfoRISCV64Test, LeafFunction) {
  llvm::Expected<std::unique_ptr<UnwindPlan>> expected_plan =
      GetRISCV64UnwindPlan(0x1001200);
  ASSERT_THAT_EXPECTED(expected_plan, llvm::Succeeded());
  UnwindPlan &plan = **expected_plan;

  // A leaf that spills nothing still needs its synthesized row, and must leave
  // ra without a location so that the unwinder reads the live register.
  EXPECT_EQ(plan.GetReturnAddressRegister(), riscv_dwarf::dwarf_gpr_x1);
  EXPECT_EQ(plan.GetRowCount(), 1);

  UnwindPlan::Row row;
  row.SetOffset(0);
  row.GetCFAValue().SetIsRegisterPlusOffset(riscv_dwarf::dwarf_gpr_x2, 0);
  row.SetRegisterLocationToIsCFAPlusOffset(riscv_dwarf::dwarf_gpr_x2, 0, true);
  EXPECT_EQ(*plan.GetRowAtIndex(0), row);
}

TEST_F(PECallFrameInfoRISCV64Test, MalformedOpcodeIsRejected) {
  // On-disk opcode 9 is undefined for RISCV64 (only 1 through 8 are defined).
  // An undefined opcode has an unknown slot count, so the record cannot be
  // decoded safely and has to be rejected outright rather than yielding a
  // plausible looking plan.
  llvm::Expected<std::unique_ptr<UnwindPlan>> expected_plan =
      GetRISCV64UnwindPlan(0x1001300);
  EXPECT_THAT_EXPECTED(expected_plan, llvm::Failed());
}

TEST_F(PECallFrameInfoRISCV64Test, DispatcherOnlyOpcodeIsIgnored) {
  // The dispatcher-only opcodes (on-disk 6/7/8: UOP_TrapFrame, UOP_Context,
  // UOP_ClearUnwoundToCall) appear only in hand-written OS runtime stubs and
  // take no operand. They describe an OS-reconstructed register context that
  // this SP/FP-relative UnwindPlan cannot model, so the decoder consumes the
  // slot and emits nothing. The stub here carries a UOP_Context alongside an
  // ordinary ra save, and must decode to exactly the plan the ra save and
  // allocation alone would give -- the UOP_Context must not add a row.
  llvm::Expected<std::unique_ptr<UnwindPlan>> expected_plan =
      GetRISCV64UnwindPlan(0x1001400);
  ASSERT_THAT_EXPECTED(expected_plan, llvm::Succeeded());
  UnwindPlan &plan = **expected_plan;

  EXPECT_EQ(plan.GetReturnAddressRegister(), riscv_dwarf::dwarf_gpr_x1);
  EXPECT_EQ(plan.GetRowCount(), 3);

  UnwindPlan::Row row;

  // The synthesized anchor row at function offset zero.
  row.SetOffset(0);
  row.GetCFAValue().SetIsRegisterPlusOffset(riscv_dwarf::dwarf_gpr_x2, 0);
  row.SetRegisterLocationToIsCFAPlusOffset(riscv_dwarf::dwarf_gpr_x2, 0, true);
  EXPECT_EQ(*plan.GetRowAtIndex(0), row);

  // After the 16 byte allocation the CFA sits 16 bytes above sp.
  row.SetOffset(4);
  row.GetCFAValue().SetIsRegisterPlusOffset(riscv_dwarf::dwarf_gpr_x2, 16);
  EXPECT_EQ(*plan.GetRowAtIndex(1), row);

  // ra is saved at sp + 8, i.e. CFA - 8. The UOP_Context at prolog offset 0x0C
  // adds no further row, so the plan ends here.
  row.SetOffset(8);
  row.SetRegisterLocationToAtCFAPlusOffset(riscv_dwarf::dwarf_gpr_x1, -8, true);
  EXPECT_EQ(*plan.GetRowAtIndex(2), row);
}
