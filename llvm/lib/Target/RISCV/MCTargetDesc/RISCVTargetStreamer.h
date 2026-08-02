//===-- RISCVTargetStreamer.h - RISC-V Target Streamer ---------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_RISCV_MCTARGETDESC_RISCVTARGETSTREAMER_H
#define LLVM_LIB_TARGET_RISCV_MCTARGETDESC_RISCVTARGETSTREAMER_H

#include "RISCV.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"

namespace llvm {

class formatted_raw_ostream;

enum class RISCVOptionArchArgType {
  Full,
  Plus,
  Minus,
};

struct RISCVOptionArchArg {
  RISCVOptionArchArgType Type;
  std::string Value;

  RISCVOptionArchArg(RISCVOptionArchArgType Type, std::string Value)
      : Type(Type), Value(Value) {}
};

class RISCVTargetStreamer : public MCTargetStreamer {
  RISCVABI::ABI TargetABI = RISCVABI::ABI_Unknown;
  bool HasRVC = false;
  bool HasTSO = false;

public:
  RISCVTargetStreamer(MCStreamer &S);
  void finish() override;
  virtual void reset();

  virtual void emitDirectiveOptionArch(ArrayRef<RISCVOptionArchArg> Args);
  virtual void emitDirectiveOptionExact();
  virtual void emitDirectiveOptionNoExact();
  virtual void emitDirectiveOptionPIC();
  virtual void emitDirectiveOptionNoPIC();
  virtual void emitDirectiveOptionPop();
  virtual void emitDirectiveOptionPush();
  virtual void emitDirectiveOptionRelax();
  virtual void emitDirectiveOptionNoRelax();
  virtual void emitDirectiveOptionRVC();
  virtual void emitDirectiveOptionNoRVC();
  virtual void emitDirectiveVariantCC(MCSymbol &Symbol);
  virtual void emitAttribute(unsigned Attribute, unsigned Value);
  virtual void finishAttributeSection();
  virtual void emitTextAttribute(unsigned Attribute, StringRef String);
  virtual void emitIntTextAttribute(unsigned Attribute, unsigned IntValue,
                                    StringRef StringValue);

  void emitTargetAttributes(const MCSubtargetInfo &STI, bool EmitStackAlign);
  void setTargetABI(RISCVABI::ABI ABI);
  RISCVABI::ABI getTargetABI() const { return TargetABI; }
  void setFlagsFromFeatures(const MCSubtargetInfo &STI);
  bool hasRVC() const { return HasRVC; }
  bool hasTSO() const { return HasTSO; }

  // Windows SEH directives that need RISC-V's own register numbering.
  // `.seh_stackalloc`, `.seh_endprologue`, `.seh_startepilogue` and
  // `.seh_endepilogue` carry no register operand and are parsed generically by
  // COFFAsmParser.cpp.
  virtual void emitRISCVWinCFISaveReg(unsigned Reg, int Offset, SMLoc Loc) {}
  virtual void emitRISCVWinCFISaveFReg(unsigned Reg, int Offset, SMLoc Loc) {}
  virtual void emitRISCVWinCFISetFrame(unsigned Reg, int Offset, SMLoc Loc) {}
  // Operand-less SEH opcodes for hand-written OS runtime stubs (never emitted
  // by codegen): `.seh_trap_frame`, `.seh_context`, `.seh_clear_unwound_to_call`.
  // See llvm/docs/RISCVWinCFI.md.
  virtual void emitRISCVWinCFITrapFrame(SMLoc Loc) {}
  virtual void emitRISCVWinCFIContext(SMLoc Loc) {}
  virtual void emitRISCVWinCFIClearUnwoundToCall(SMLoc Loc) {}
};

// This part is for ascii assembly output
class RISCVTargetAsmStreamer : public RISCVTargetStreamer {
  formatted_raw_ostream &OS;

  void finishAttributeSection() override;
  void emitAttribute(unsigned Attribute, unsigned Value) override;
  void emitTextAttribute(unsigned Attribute, StringRef String) override;
  void emitIntTextAttribute(unsigned Attribute, unsigned IntValue,
                            StringRef StringValue) override;

public:
  RISCVTargetAsmStreamer(MCStreamer &S, formatted_raw_ostream &OS);

  void emitDirectiveOptionArch(ArrayRef<RISCVOptionArchArg> Args) override;
  void emitDirectiveOptionExact() override;
  void emitDirectiveOptionNoExact() override;
  void emitDirectiveOptionPIC() override;
  void emitDirectiveOptionNoPIC() override;
  void emitDirectiveOptionPop() override;
  void emitDirectiveOptionPush() override;
  void emitDirectiveOptionRelax() override;
  void emitDirectiveOptionNoRelax() override;
  void emitDirectiveOptionRVC() override;
  void emitDirectiveOptionNoRVC() override;
  void emitDirectiveVariantCC(MCSymbol &Symbol) override;

  void emitRISCVWinCFISaveReg(unsigned Reg, int Offset, SMLoc Loc) override;
  void emitRISCVWinCFISaveFReg(unsigned Reg, int Offset, SMLoc Loc) override;
  void emitRISCVWinCFISetFrame(unsigned Reg, int Offset, SMLoc Loc) override;
  void emitRISCVWinCFITrapFrame(SMLoc Loc) override;
  void emitRISCVWinCFIContext(SMLoc Loc) override;
  void emitRISCVWinCFIClearUnwoundToCall(SMLoc Loc) override;
};

// This part is for Windows COFF output.
class RISCVTargetWinCOFFStreamer : public llvm::RISCVTargetStreamer {
public:
  RISCVTargetWinCOFFStreamer(llvm::MCStreamer &S) : RISCVTargetStreamer(S) {}

  void emitRISCVWinCFISaveReg(unsigned Reg, int Offset, SMLoc Loc) override;
  void emitRISCVWinCFISaveFReg(unsigned Reg, int Offset, SMLoc Loc) override;
  void emitRISCVWinCFISetFrame(unsigned Reg, int Offset, SMLoc Loc) override;
  void emitRISCVWinCFITrapFrame(SMLoc Loc) override;
  void emitRISCVWinCFIContext(SMLoc Loc) override;
  void emitRISCVWinCFIClearUnwoundToCall(SMLoc Loc) override;

private:
  void emitRISCVWinUnwindCode(unsigned UnwindCode, unsigned Reg, int Offset,
                              SMLoc Loc);
};
}
#endif
