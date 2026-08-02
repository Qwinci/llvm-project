//===-- RISCVFrameLowering.h - Define frame lowering for RISC-V -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class implements RISC-V specific bits of TargetFrameLowering class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_RISCV_RISCVFRAMELOWERING_H
#define LLVM_LIB_TARGET_RISCV_RISCVFRAMELOWERING_H

#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/Support/TypeSize.h"

namespace llvm {
class RISCVSubtarget;

class RISCVFrameLowering : public TargetFrameLowering {
public:
  explicit RISCVFrameLowering(const RISCVSubtarget &STI);

  int getInitialCFAOffset(const MachineFunction &MF) const override;
  Register getInitialCFARegister(const MachineFunction &MF) const override;

  void emitPrologue(MachineFunction &MF, MachineBasicBlock &MBB) const override;
  void emitEpilogue(MachineFunction &MF, MachineBasicBlock &MBB) const override;

  uint64_t getStackSizeWithRVVPadding(const MachineFunction &MF) const;

  StackOffset getFrameIndexReference(const MachineFunction &MF, int FI,
                                     Register &FrameReg) const override;

  StackOffset getFrameIndexReferencePreferSP(const MachineFunction &MF, int FI,
                                             Register &FrameReg,
                                             bool IgnoreSPUpdates) const override;

  void determineCalleeSaves(MachineFunction &MF, BitVector &SavedRegs,
                            RegScavenger *RS) const override;

  void processFunctionBeforeFrameFinalized(MachineFunction &MF,
                                           RegScavenger *RS) const override;

  bool hasBP(const MachineFunction &MF) const;

  // Reserve and initialize the fixed stack objects (catch objects + UnwindHelp)
  // used by Windows funclet-based C++ exception handling.
  void emitWinEHFixedObjects(MachineFunction &MF, RegScavenger *RS) const;

  bool hasReservedCallFrame(const MachineFunction &MF) const override;
  MachineBasicBlock::iterator
  eliminateCallFramePseudoInstr(MachineFunction &MF, MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator MI) const override;

  bool
  assignCalleeSavedSpillSlots(MachineFunction &MF,
                              const TargetRegisterInfo *TRI,
                              std::vector<CalleeSavedInfo> &CSI) const override;
  bool spillCalleeSavedRegisters(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator MI,
                                 ArrayRef<CalleeSavedInfo> CSI,
                                 const TargetRegisterInfo *TRI) const override;
  bool
  restoreCalleeSavedRegisters(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator MI,
                              MutableArrayRef<CalleeSavedInfo> CSI,
                              const TargetRegisterInfo *TRI) const override;

  // Get the first stack adjustment amount for SplitSPAdjust.
  // Return 0 if we don't want to split the SP adjustment in prologue and
  // epilogue.
  uint64_t getFirstSPAdjustAmount(const MachineFunction &MF) const;

  bool canUseAsPrologue(const MachineBasicBlock &MBB) const override;
  bool canUseAsEpilogue(const MachineBasicBlock &MBB) const override;

  bool enableShrinkWrapping(const MachineFunction &MF) const override;

  bool isSupportedStackID(TargetStackID::Value ID) const override;
  TargetStackID::Value getStackIDForScalableVectors() const override;

  unsigned getWinEHParentFrameOffset(const MachineFunction &MF) const override;

  bool isStackIdSafeForLocalArea(unsigned StackId) const override {
    // We don't support putting RISC-V Vector objects into the pre-allocated
    // local frame block at the moment.
    return StackId != TargetStackID::ScalableVector;
  }

  void allocateStack(MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
                     MachineFunction &MF, uint64_t Offset,
                     uint64_t RealStackSize, bool EmitCFI, bool NeedProbe,
                     uint64_t ProbeSize, bool DynAllocation,
                     MachineInstr::MIFlag Flag) const;

  // Whether allocating StackSizeInBytes bytes requires a call to the
  // out-of-line __chkstk routine to probe the intervening guard pages. As on
  // every other Windows target this is unconditional, not opt-in, for frames
  // at or above the guard page size.
  bool windowsRequiresStackProbe(const MachineFunction &MF,
                                 uint64_t StackSizeInBytes) const;

protected:
  const RISCVSubtarget &STI;

  bool hasFPImpl(const MachineFunction &MF) const override;

private:
  void determineFrameLayout(MachineFunction &MF) const;
  void emitCalleeSavedRVVPrologCFI(MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator MI,
                                   bool HasFP) const;
  void emitCalleeSavedRVVEpilogCFI(MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator MI) const;
  template <typename Emitter>
  void emitCFIForCSI(MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
                     const SmallVector<CalleeSavedInfo, 8> &CSI) const;
  void deallocateStack(MachineFunction &MF, MachineBasicBlock &MBB,
                       MachineBasicBlock::iterator MBBI, const DebugLoc &DL,
                       uint64_t &StackSize, int64_t CFAOffset) const;

  std::pair<int64_t, Align>
  assignRVVStackObjectOffsets(MachineFunction &MF) const;
  // Replace a StackProbe stub (if any) with the actual probe code inline
  void inlineStackProbe(MachineFunction &MF,
                        MachineBasicBlock &PrologueMBB) const override;
  // Emit a call to the out-of-line __chkstk routine to probe Offset bytes of
  // new stack, then adjust SP down by Offset.
  void emitWindowsChkstkCall(MachineBasicBlock &MBB,
                             MachineBasicBlock::iterator MBBI,
                             const DebugLoc &DL, uint64_t Offset,
                             MachineInstr::MIFlag Flag) const;
  void allocateAndProbeStackForRVV(MachineFunction &MF, MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator MBBI,
                                   const DebugLoc &DL, int64_t Amount,
                                   MachineInstr::MIFlag Flag, bool EmitCFI,
                                   bool DynAllocation) const;
};
} // namespace llvm
#endif
