//===-- RISCVMCTargetDesc.cpp - RISC-V Target Descriptions ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// This file provides RISC-V specific target descriptions.
///
//===----------------------------------------------------------------------===//

#include "RISCVMCTargetDesc.h"
#include "RISCVELFStreamer.h"
#include "RISCVInstPrinter.h"
#include "RISCVMCAsmInfo.h"
#include "RISCVMCObjectFileInfo.h"
#include "RISCVTargetStreamer.h"
#include "TargetInfo/RISCVTargetInfo.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCInstrAnalysis.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include <bitset>

#define GET_INSTRINFO_MC_DESC
#define ENABLE_INSTR_PREDICATE_VERIFIER
#include "RISCVGenInstrInfo.inc"

#define GET_REGINFO_MC_DESC
#include "RISCVGenRegisterInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "RISCVGenSubtargetInfo.inc"

using namespace llvm;

static MCInstrInfo *createRISCVMCInstrInfo() {
  MCInstrInfo *X = new MCInstrInfo();
  InitRISCVMCInstrInfo(X);
  return X;
}

void RISCV_MC::initLLVMToCVRegMapping(MCRegisterInfo *MRI) {
  static const struct {
    codeview::RegisterId CVReg;
    MCPhysReg Reg;
  } RegMap[] = {
      {codeview::RegisterId::RISCV64_X0, RISCV::X0},
      {codeview::RegisterId::RISCV64_X1, RISCV::X1},
      {codeview::RegisterId::RISCV64_X2, RISCV::X2},
      {codeview::RegisterId::RISCV64_X3, RISCV::X3},
      {codeview::RegisterId::RISCV64_X4, RISCV::X4},
      {codeview::RegisterId::RISCV64_X5, RISCV::X5},
      {codeview::RegisterId::RISCV64_X6, RISCV::X6},
      {codeview::RegisterId::RISCV64_X7, RISCV::X7},
      {codeview::RegisterId::RISCV64_X8, RISCV::X8},
      {codeview::RegisterId::RISCV64_X9, RISCV::X9},
      {codeview::RegisterId::RISCV64_X10, RISCV::X10},
      {codeview::RegisterId::RISCV64_X11, RISCV::X11},
      {codeview::RegisterId::RISCV64_X12, RISCV::X12},
      {codeview::RegisterId::RISCV64_X13, RISCV::X13},
      {codeview::RegisterId::RISCV64_X14, RISCV::X14},
      {codeview::RegisterId::RISCV64_X15, RISCV::X15},
      {codeview::RegisterId::RISCV64_X16, RISCV::X16},
      {codeview::RegisterId::RISCV64_X17, RISCV::X17},
      {codeview::RegisterId::RISCV64_X18, RISCV::X18},
      {codeview::RegisterId::RISCV64_X19, RISCV::X19},
      {codeview::RegisterId::RISCV64_X20, RISCV::X20},
      {codeview::RegisterId::RISCV64_X21, RISCV::X21},
      {codeview::RegisterId::RISCV64_X22, RISCV::X22},
      {codeview::RegisterId::RISCV64_X23, RISCV::X23},
      {codeview::RegisterId::RISCV64_X24, RISCV::X24},
      {codeview::RegisterId::RISCV64_X25, RISCV::X25},
      {codeview::RegisterId::RISCV64_X26, RISCV::X26},
      {codeview::RegisterId::RISCV64_X27, RISCV::X27},
      {codeview::RegisterId::RISCV64_X28, RISCV::X28},
      {codeview::RegisterId::RISCV64_X29, RISCV::X29},
      {codeview::RegisterId::RISCV64_X30, RISCV::X30},
      {codeview::RegisterId::RISCV64_X31, RISCV::X31},
  };
  for (const auto &I : RegMap)
    MRI->mapLLVMRegToCVReg(I.Reg, static_cast<int>(I.CVReg));

  // Each floating-point register has several width views that are distinct
  // MCRegisters aliasing the same physical FPR: F#_H (Zfh half), F#_F (single),
  // F#_D (double) and F#_Q (quad). The CodeView register lookup is exact and
  // RISCV64 defines a single CodeView id per FPR, so every width view of F#
  // must map to RISCV64_F#. clang emits a debug location naming whichever view
  // matches the variable's type (e.g. F#_F for a `float`), so mapping only the
  // _D view crashed on any single- or half-precision code.
  static const MCPhysReg FPRegViews[32][4] = {
      {RISCV::F0_H, RISCV::F0_F, RISCV::F0_D, RISCV::F0_Q},
      {RISCV::F1_H, RISCV::F1_F, RISCV::F1_D, RISCV::F1_Q},
      {RISCV::F2_H, RISCV::F2_F, RISCV::F2_D, RISCV::F2_Q},
      {RISCV::F3_H, RISCV::F3_F, RISCV::F3_D, RISCV::F3_Q},
      {RISCV::F4_H, RISCV::F4_F, RISCV::F4_D, RISCV::F4_Q},
      {RISCV::F5_H, RISCV::F5_F, RISCV::F5_D, RISCV::F5_Q},
      {RISCV::F6_H, RISCV::F6_F, RISCV::F6_D, RISCV::F6_Q},
      {RISCV::F7_H, RISCV::F7_F, RISCV::F7_D, RISCV::F7_Q},
      {RISCV::F8_H, RISCV::F8_F, RISCV::F8_D, RISCV::F8_Q},
      {RISCV::F9_H, RISCV::F9_F, RISCV::F9_D, RISCV::F9_Q},
      {RISCV::F10_H, RISCV::F10_F, RISCV::F10_D, RISCV::F10_Q},
      {RISCV::F11_H, RISCV::F11_F, RISCV::F11_D, RISCV::F11_Q},
      {RISCV::F12_H, RISCV::F12_F, RISCV::F12_D, RISCV::F12_Q},
      {RISCV::F13_H, RISCV::F13_F, RISCV::F13_D, RISCV::F13_Q},
      {RISCV::F14_H, RISCV::F14_F, RISCV::F14_D, RISCV::F14_Q},
      {RISCV::F15_H, RISCV::F15_F, RISCV::F15_D, RISCV::F15_Q},
      {RISCV::F16_H, RISCV::F16_F, RISCV::F16_D, RISCV::F16_Q},
      {RISCV::F17_H, RISCV::F17_F, RISCV::F17_D, RISCV::F17_Q},
      {RISCV::F18_H, RISCV::F18_F, RISCV::F18_D, RISCV::F18_Q},
      {RISCV::F19_H, RISCV::F19_F, RISCV::F19_D, RISCV::F19_Q},
      {RISCV::F20_H, RISCV::F20_F, RISCV::F20_D, RISCV::F20_Q},
      {RISCV::F21_H, RISCV::F21_F, RISCV::F21_D, RISCV::F21_Q},
      {RISCV::F22_H, RISCV::F22_F, RISCV::F22_D, RISCV::F22_Q},
      {RISCV::F23_H, RISCV::F23_F, RISCV::F23_D, RISCV::F23_Q},
      {RISCV::F24_H, RISCV::F24_F, RISCV::F24_D, RISCV::F24_Q},
      {RISCV::F25_H, RISCV::F25_F, RISCV::F25_D, RISCV::F25_Q},
      {RISCV::F26_H, RISCV::F26_F, RISCV::F26_D, RISCV::F26_Q},
      {RISCV::F27_H, RISCV::F27_F, RISCV::F27_D, RISCV::F27_Q},
      {RISCV::F28_H, RISCV::F28_F, RISCV::F28_D, RISCV::F28_Q},
      {RISCV::F29_H, RISCV::F29_F, RISCV::F29_D, RISCV::F29_Q},
      {RISCV::F30_H, RISCV::F30_F, RISCV::F30_D, RISCV::F30_Q},
      {RISCV::F31_H, RISCV::F31_F, RISCV::F31_D, RISCV::F31_Q},
  };
  for (unsigned I = 0; I < 32; ++I) {
    int CVReg = static_cast<int>(codeview::RegisterId::RISCV64_F0) + I;
    for (MCPhysReg View : FPRegViews[I])
      MRI->mapLLVMRegToCVReg(View, CVReg);
  }
}

static MCRegisterInfo *createRISCVMCRegisterInfo(const Triple &TT) {
  MCRegisterInfo *X = new MCRegisterInfo();
  InitRISCVMCRegisterInfo(X, RISCV::X1);
  RISCV_MC::initLLVMToCVRegMapping(X);
  return X;
}

static MCAsmInfo *createRISCVMCAsmInfo(const MCRegisterInfo &MRI,
                                       const Triple &TT,
                                       const MCTargetOptions &Options) {
  MCAsmInfo *MAI = nullptr;
  if (TT.isOSBinFormatELF())
    MAI = new RISCVMCAsmInfo(TT);
  else if (TT.isOSBinFormatMachO())
    MAI = new RISCVMCAsmInfoDarwin();
  else if (TT.isWindowsMSVCEnvironment())
    MAI = new RISCVMCAsmInfoMicrosoftCOFF();
  else if (TT.isOSBinFormatCOFF())
    MAI = new RISCVMCAsmInfoGNUCOFF();
  else
    reportFatalUsageError("unsupported object format");

  unsigned SP = MRI.getDwarfRegNum(RISCV::X2, true);
  MCCFIInstruction Inst = MCCFIInstruction::cfiDefCfa(nullptr, SP, 0);
  MAI->addInitialFrameState(Inst);

  return MAI;
}

static MCObjectFileInfo *
createRISCVMCObjectFileInfo(MCContext &Ctx, bool PIC,
                            bool LargeCodeModel = false) {
  MCObjectFileInfo *MOFI = new RISCVMCObjectFileInfo();
  MOFI->initMCObjectFileInfo(Ctx, PIC, LargeCodeModel);
  return MOFI;
}

static MCSubtargetInfo *createRISCVMCSubtargetInfo(const Triple &TT,
                                                   StringRef CPU, StringRef FS) {
  if (CPU.empty() || CPU == "generic")
    CPU = TT.isArch64Bit() ? "generic-rv64" : "generic-rv32";

  MCSubtargetInfo *X =
      createRISCVMCSubtargetInfoImpl(TT, CPU, /*TuneCPU*/ CPU, FS);

  // If the CPU is "help" fill in 64 or 32 bit feature so we can pass
  // RISCVFeatures::validate.
  // FIXME: Why does llvm-mc still expect a source file with -mcpu=help?
  if (CPU == "help") {
    llvm::FeatureBitset Features = X->getFeatureBits();
    if (TT.isArch64Bit())
      Features.set(RISCV::Feature64Bit);
    else
      Features.set(RISCV::Feature32Bit);
    X->setFeatureBits(Features);
  }

  return X;
}

static MCInstPrinter *createRISCVMCInstPrinter(const Triple &T,
                                               unsigned SyntaxVariant,
                                               const MCAsmInfo &MAI,
                                               const MCInstrInfo &MII,
                                               const MCRegisterInfo &MRI) {
  return new RISCVInstPrinter(MAI, MII, MRI);
}

static MCTargetStreamer *
createRISCVObjectTargetStreamer(MCStreamer &S, const MCSubtargetInfo &STI) {
  const Triple &TT = STI.getTargetTriple();
  if (TT.isOSBinFormatELF())
    return new RISCVTargetELFStreamer(S, STI);
  if (TT.isOSBinFormatCOFF())
    return new RISCVTargetWinCOFFStreamer(S);
  return new RISCVTargetStreamer(S);
}

static MCStreamer *
createMachOStreamer(MCContext &Ctx, std::unique_ptr<MCAsmBackend> &&TAB,
                    std::unique_ptr<MCObjectWriter> &&OW,
                    std::unique_ptr<MCCodeEmitter> &&Emitter) {
  return createMachOStreamer(Ctx, std::move(TAB), std::move(OW),
                             std::move(Emitter),
                             /*DWARFMustBeAtTheEnd*/ false,
                             /*LabelSections*/ true);
}

static MCTargetStreamer *
createRISCVAsmTargetStreamer(MCStreamer &S, formatted_raw_ostream &OS,
                             MCInstPrinter *InstPrint) {
  return new RISCVTargetAsmStreamer(S, OS);
}

static MCTargetStreamer *createRISCVNullTargetStreamer(MCStreamer &S) {
  return new RISCVTargetStreamer(S);
}

namespace {

class RISCVMCInstrAnalysis : public MCInstrAnalysis {
  int64_t GPRState[31] = {};
  std::bitset<31> GPRValidMask;

  static bool isGPR(MCRegister Reg) {
    return Reg >= RISCV::X0 && Reg <= RISCV::X31;
  }

  static unsigned getRegIndex(MCRegister Reg) {
    assert(isGPR(Reg) && Reg != RISCV::X0 && "Invalid GPR reg");
    return Reg - RISCV::X1;
  }

  void setGPRState(MCRegister Reg, std::optional<int64_t> Value) {
    if (Reg == RISCV::X0)
      return;

    auto Index = getRegIndex(Reg);

    if (Value) {
      GPRState[Index] = *Value;
      GPRValidMask.set(Index);
    } else {
      GPRValidMask.reset(Index);
    }
  }

  std::optional<int64_t> getGPRState(MCRegister Reg) const {
    if (Reg == RISCV::X0)
      return 0;

    auto Index = getRegIndex(Reg);

    if (GPRValidMask.test(Index))
      return GPRState[Index];
    return std::nullopt;
  }

public:
  explicit RISCVMCInstrAnalysis(const MCInstrInfo *Info)
      : MCInstrAnalysis(Info) {}

  void resetState() override { GPRValidMask.reset(); }

  void updateState(const MCInst &Inst, uint64_t Addr) override {
    // Terminators mark the end of a basic block which means the sequentially
    // next instruction will be the first of another basic block and the current
    // state will typically not be valid anymore. For calls, we assume all
    // registers may be clobbered by the callee (TODO: should we take the
    // calling convention into account?).
    if (isTerminator(Inst) || isCall(Inst)) {
      resetState();
      return;
    }

    switch (Inst.getOpcode()) {
    default: {
      // Clear the state of all defined registers for instructions that we don't
      // explicitly support.
      auto NumDefs = Info->get(Inst.getOpcode()).getNumDefs();
      for (unsigned I = 0; I < NumDefs; ++I) {
        auto DefReg = Inst.getOperand(I).getReg();
        if (isGPR(DefReg))
          setGPRState(DefReg, std::nullopt);
      }
      break;
    }
    case RISCV::AUIPC:
      setGPRState(Inst.getOperand(0).getReg(),
                  Addr + SignExtend64<32>(Inst.getOperand(1).getImm() << 12));
      break;
    }
  }

  bool evaluateBranch(const MCInst &Inst, uint64_t Addr, uint64_t Size,
                      uint64_t &Target) const override {
    if (isConditionalBranch(Inst)) {
      int64_t Imm;
      if (Size == 2)
        Imm = Inst.getOperand(1).getImm();
      else
        Imm = Inst.getOperand(2).getImm();
      Target = Addr + Imm;
      return true;
    }

    switch (Inst.getOpcode()) {
    case RISCV::C_J:
    case RISCV::C_JAL:
    case RISCV::QC_E_J:
    case RISCV::QC_E_JAL:
      Target = Addr + Inst.getOperand(0).getImm();
      return true;
    case RISCV::JAL:
      Target = Addr + Inst.getOperand(1).getImm();
      return true;
    case RISCV::JALR: {
      if (auto TargetRegState = getGPRState(Inst.getOperand(1).getReg())) {
        Target = *TargetRegState + Inst.getOperand(2).getImm();
        return true;
      }
      return false;
    }
    }

    return false;
  }

  bool isTerminator(const MCInst &Inst) const override {
    if (MCInstrAnalysis::isTerminator(Inst))
      return true;

    switch (Inst.getOpcode()) {
    default:
      return false;
    case RISCV::JAL:
    case RISCV::JALR:
      return Inst.getOperand(0).getReg() == RISCV::X0;
    }
  }

  bool isCall(const MCInst &Inst) const override {
    if (MCInstrAnalysis::isCall(Inst))
      return true;

    switch (Inst.getOpcode()) {
    default:
      return false;
    case RISCV::JAL:
    case RISCV::JALR:
      return Inst.getOperand(0).getReg() != RISCV::X0;
    }
  }

  bool isReturn(const MCInst &Inst) const override {
    if (MCInstrAnalysis::isReturn(Inst))
      return true;

    switch (Inst.getOpcode()) {
    default:
      return false;
    case RISCV::JALR:
      return Inst.getOperand(0).getReg() == RISCV::X0 &&
             maybeReturnAddress(Inst.getOperand(1).getReg());
    case RISCV::C_JR:
      return maybeReturnAddress(Inst.getOperand(0).getReg());
    }
  }

  bool isBranch(const MCInst &Inst) const override {
    if (MCInstrAnalysis::isBranch(Inst))
      return true;

    return isBranchImpl(Inst);
  }

  bool isUnconditionalBranch(const MCInst &Inst) const override {
    if (MCInstrAnalysis::isUnconditionalBranch(Inst))
      return true;

    return isBranchImpl(Inst);
  }

  bool isIndirectBranch(const MCInst &Inst) const override {
    if (MCInstrAnalysis::isIndirectBranch(Inst))
      return true;

    switch (Inst.getOpcode()) {
    default:
      return false;
    case RISCV::JALR:
      return Inst.getOperand(0).getReg() == RISCV::X0 &&
             !maybeReturnAddress(Inst.getOperand(1).getReg());
    case RISCV::C_JR:
      return !maybeReturnAddress(Inst.getOperand(0).getReg());
    }
  }

  /// Returns (PLT virtual address, GOT virtual address) pairs for PLT entries.
  std::vector<std::pair<uint64_t, uint64_t>>
  findPltEntries(uint64_t PltSectionVA, ArrayRef<uint8_t> PltContents,
                 const MCSubtargetInfo &STI) const override {
    uint32_t LoadInsnOpCode;
    if (const Triple &T = STI.getTargetTriple(); T.isRISCV64())
      LoadInsnOpCode = 0x3003; // ld
    else if (T.isRISCV32())
      LoadInsnOpCode = 0x2003; // lw
    else
      return {};

    constexpr uint64_t FirstEntryAt = 32, EntrySize = 16;
    if (PltContents.size() < FirstEntryAt + EntrySize)
      return {};

    std::vector<std::pair<uint64_t, uint64_t>> Results;
    for (uint64_t EntryStart = FirstEntryAt,
                  EntryStartEnd = PltContents.size() - EntrySize;
         EntryStart <= EntryStartEnd; EntryStart += EntrySize) {
      const uint32_t AuipcInsn =
          support::endian::read32le(PltContents.data() + EntryStart);
      const bool IsAuipc = (AuipcInsn & 0x7F) == 0x17;
      if (!IsAuipc)
        continue;

      const uint32_t LoadInsn =
          support::endian::read32le(PltContents.data() + EntryStart + 4);
      const bool IsLoad = (LoadInsn & 0x707F) == LoadInsnOpCode;
      if (!IsLoad)
        continue;

      const uint64_t GotPltSlotVA = PltSectionVA + EntryStart +
                                    (AuipcInsn & 0xFFFFF000) +
                                    SignExtend64<12>(LoadInsn >> 20);
      Results.emplace_back(PltSectionVA + EntryStart, GotPltSlotVA);
    }

    return Results;
  }

private:
  static bool maybeReturnAddress(MCRegister Reg) {
    // X1 is used for normal returns, X5 for returns from outlined functions.
    return Reg == RISCV::X1 || Reg == RISCV::X5;
  }

  static bool isBranchImpl(const MCInst &Inst) {
    switch (Inst.getOpcode()) {
    default:
      return false;
    case RISCV::JAL:
      return Inst.getOperand(0).getReg() == RISCV::X0;
    case RISCV::JALR:
      return Inst.getOperand(0).getReg() == RISCV::X0 &&
             !maybeReturnAddress(Inst.getOperand(1).getReg());
    case RISCV::C_JR:
      return !maybeReturnAddress(Inst.getOperand(0).getReg());
    }
  }
};

} // end anonymous namespace

static MCInstrAnalysis *createRISCVInstrAnalysis(const MCInstrInfo *Info) {
  return new RISCVMCInstrAnalysis(Info);
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeRISCVTargetMC() {
  for (Target *T : {&getTheRISCV32Target(), &getTheRISCV64Target(),
                    &getTheRISCV32beTarget(), &getTheRISCV64beTarget()}) {
    TargetRegistry::RegisterMCAsmInfo(*T, createRISCVMCAsmInfo);
    TargetRegistry::RegisterMCObjectFileInfo(*T, createRISCVMCObjectFileInfo);
    TargetRegistry::RegisterMCInstrInfo(*T, createRISCVMCInstrInfo);
    TargetRegistry::RegisterMCRegInfo(*T, createRISCVMCRegisterInfo);
    TargetRegistry::RegisterMCAsmBackend(*T, createRISCVAsmBackend);
    TargetRegistry::RegisterMCCodeEmitter(*T, createRISCVMCCodeEmitter);
    TargetRegistry::RegisterMCInstPrinter(*T, createRISCVMCInstPrinter);
    TargetRegistry::RegisterMCSubtargetInfo(*T, createRISCVMCSubtargetInfo);
    TargetRegistry::RegisterELFStreamer(*T, createRISCVELFStreamer);
    TargetRegistry::RegisterMachOStreamer(*T, createMachOStreamer);
    TargetRegistry::RegisterCOFFStreamer(*T, createRISCVWinCOFFStreamer);
    TargetRegistry::RegisterObjectTargetStreamer(
        *T, createRISCVObjectTargetStreamer);
    TargetRegistry::RegisterMCInstrAnalysis(*T, createRISCVInstrAnalysis);

    // Register the asm target streamer.
    TargetRegistry::RegisterAsmTargetStreamer(*T, createRISCVAsmTargetStreamer);
    // Register the null target streamer.
    TargetRegistry::RegisterNullTargetStreamer(*T,
                                               createRISCVNullTargetStreamer);
  }
}
