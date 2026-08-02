//===-- RISCVWinCOFFStreamer.cpp - RISC-V Windows COFF Streamer ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Emits the Windows SEH unwind codes described in llvm/docs/RISCVWinCFI.md.
//
//===----------------------------------------------------------------------===//

#include "RISCVBaseInfo.h"
#include "RISCVMCTargetDesc.h"
#include "RISCVTargetStreamer.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCWin64EH.h"
#include "llvm/MC/MCWinCOFFStreamer.h"

using namespace llvm;

namespace {
class RISCVWinCOFFStreamer : public MCWinCOFFStreamer {
  Win64EH::RISCV64UnwindEmitter EHStreamer;

public:
  RISCVWinCOFFStreamer(MCContext &C, std::unique_ptr<MCAsmBackend> AB,
                       std::unique_ptr<MCCodeEmitter> CE,
                       std::unique_ptr<MCObjectWriter> OW)
      : MCWinCOFFStreamer(C, std::move(AB), std::move(CE), std::move(OW)) {}

  void emitWinEHHandlerData(SMLoc Loc) override;
  void emitWindowsUnwindTables() override;
  void emitWindowsUnwindTables(WinEH::FrameInfo *Frame) override;
  void finishImpl() override;
};

void RISCVWinCOFFStreamer::emitWinEHHandlerData(SMLoc Loc) {
  MCStreamer::emitWinEHHandlerData(Loc);

  // We have to emit the unwind info now, because this directive
  // actually switches to the .xdata section!
  EHStreamer.EmitUnwindInfo(*this, getCurrentWinFrameInfo(),
                            /* HandlerData = */ true);
}

void RISCVWinCOFFStreamer::emitWindowsUnwindTables(WinEH::FrameInfo *Frame) {
  EHStreamer.EmitUnwindInfo(*this, Frame, /* HandlerData = */ false);
}

void RISCVWinCOFFStreamer::emitWindowsUnwindTables() {
  if (!getNumWinFrameInfos())
    return;
  EHStreamer.Emit(*this);
}

void RISCVWinCOFFStreamer::finishImpl() {
  emitFrames();
  emitWindowsUnwindTables();

  MCWinCOFFStreamer::finishImpl();
}
} // end anonymous namespace

void RISCVTargetWinCOFFStreamer::emitRISCVWinUnwindCode(unsigned UnwindCode,
                                                        unsigned Reg,
                                                        int Offset, SMLoc Loc) {
  auto &S = getStreamer();
  WinEH::FrameInfo *CurFrame = S.EnsureValidWinFrameInfo(Loc);
  if (!CurFrame)
    return;
  // The xdata reuses x86_64's 2-byte-slot scheme, which stores each code's
  // CodeOffset explicitly as a symbol difference, so a label is needed at
  // exactly this position. (ARM64 instead recovers offsets from opcode
  // playback order and needs no label.)
  MCSymbol *Label = S.emitCFILabel();
  auto Inst = WinEH::Instruction(UnwindCode, Label, Reg, Offset);
  CurFrame->Instructions.push_back(Inst);
}

void RISCVTargetWinCOFFStreamer::emitRISCVWinCFISaveReg(unsigned Reg,
                                                        int Offset, SMLoc Loc) {
  int Index = getSEHGPRIndex(Reg);
  if (Index < 0) {
    getStreamer().getContext().reportError(
        Loc, "invalid register for .seh_savereg, expected ra or s0-s11");
    return;
  }
  if (Offset < 0 || (Offset & 7)) {
    getStreamer().getContext().reportError(
        Loc, "offset for .seh_savereg must be a non-negative multiple of 8");
    return;
  }
  emitRISCVWinUnwindCode(Win64EH::UOP_SaveNonVol, Index, Offset, Loc);
}

void RISCVTargetWinCOFFStreamer::emitRISCVWinCFISaveFReg(unsigned Reg,
                                                         int Offset,
                                                         SMLoc Loc) {
  int Index = getSEHFPRIndex(Reg);
  if (Index < 0) {
    getStreamer().getContext().reportError(
        Loc, "invalid register for .seh_savefreg, expected fs0-fs11");
    return;
  }
  if (Offset < 0 || (Offset & 7)) {
    getStreamer().getContext().reportError(
        Loc, "offset for .seh_savefreg must be a non-negative multiple of 8");
    return;
  }
  emitRISCVWinUnwindCode(Win64EH::UOP_RISCVSaveFReg, Index, Offset, Loc);
}

void RISCVTargetWinCOFFStreamer::emitRISCVWinCFISetFrame(unsigned Reg,
                                                         int Offset,
                                                         SMLoc Loc) {
  if (Reg != RISCV::X8) {
    getStreamer().getContext().reportError(
        Loc, "invalid register for .seh_setframe, expected s0");
    return;
  }
  // Since the frame register is always s0, FrameRegisterAndOffset needs no
  // register sub-field and uses the whole byte as offset/16 (see
  // RISCV64EmitUnwindInfo in MCWin64EH.cpp), giving an 8-bit (0-4080) range
  // rather than x86_64's 4-bit (0-240) one.
  if (Offset < 0 || Offset > 4080 || (Offset & 0xF)) {
    getStreamer().getContext().reportError(
        Loc, "offset for .seh_setframe must be between 0 and 4080 and a "
             "multiple of 16");
    return;
  }
  auto &S = getStreamer();
  WinEH::FrameInfo *CurFrame = S.EnsureValidWinFrameInfo(Loc);
  if (!CurFrame)
    return;
  if (CurFrame->LastFrameInst >= 0) {
    S.getContext().reportError(
        Loc, "frame register and offset can be set at most once");
    return;
  }
  MCSymbol *Label = S.emitCFILabel();
  auto Inst = WinEH::Instruction(Win64EH::UOP_SetFPReg, Label, 0, Offset);
  CurFrame->LastFrameInst = CurFrame->Instructions.size();
  CurFrame->Instructions.push_back(Inst);
}

// The following three opcodes carry no register/offset operand. They exist for
// hand-written OS runtime stubs (KiUserExceptionDispatcher, kernel trap entry,
// ...) that establish a frame via an OS-synthesized register state rather than a
// call; codegen never emits them. `context`/`trap_frame` reload PC (+SP, +regs)
// from a stack-resident structure, and `clear_unwound_to_call` tells the
// unwinder the PC so restored is an exact fault address rather than a return
// address (see llvm/docs/RISCVWinCFI.md).
void RISCVTargetWinCOFFStreamer::emitRISCVWinCFITrapFrame(SMLoc Loc) {
  emitRISCVWinUnwindCode(Win64EH::UOP_TrapFrame, 0, 0, Loc);
}

void RISCVTargetWinCOFFStreamer::emitRISCVWinCFIContext(SMLoc Loc) {
  emitRISCVWinUnwindCode(Win64EH::UOP_Context, 0, 0, Loc);
}

void RISCVTargetWinCOFFStreamer::emitRISCVWinCFIClearUnwoundToCall(SMLoc Loc) {
  emitRISCVWinUnwindCode(Win64EH::UOP_ClearUnwoundToCall, 0, 0, Loc);
}

MCStreamer *llvm::createRISCVWinCOFFStreamer(
    MCContext &C, std::unique_ptr<MCAsmBackend> &&AB,
    std::unique_ptr<MCObjectWriter> &&OW, std::unique_ptr<MCCodeEmitter> &&CE) {
  return new RISCVWinCOFFStreamer(C, std::move(AB), std::move(CE),
                                  std::move(OW));
}
