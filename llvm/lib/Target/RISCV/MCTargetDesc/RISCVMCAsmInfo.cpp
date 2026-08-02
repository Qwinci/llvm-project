//===-- RISCVMCAsmInfo.cpp - RISC-V Asm properties ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declarations of the RISCVMCAsmInfo properties.
//
//===----------------------------------------------------------------------===//

#include "RISCVMCAsmInfo.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/TargetParser/Triple.h"
using namespace llvm;

void RISCVMCAsmInfo::anchor() {}

RISCVMCAsmInfo::RISCVMCAsmInfo(const Triple &TT) {
  IsLittleEndian = TT.isLittleEndian();
  CodePointerSize = CalleeSaveStackSlotSize = TT.isArch64Bit() ? 8 : 4;
  CommentString = "#";
  AlignmentIsInBytes = false;
  SupportsDebugInformation = true;
  ExceptionsType = ExceptionHandling::DwarfCFI;
  UseAtForSpecifier = false;
  Data16bitsDirective = "\t.half\t";
  Data32bitsDirective = "\t.word\t";
}

const MCExpr *RISCVMCAsmInfo::getExprForFDESymbol(const MCSymbol *Sym,
                                                  unsigned Encoding,
                                                  MCStreamer &Streamer) const {
  if (!(Encoding & dwarf::DW_EH_PE_pcrel))
    return MCAsmInfo::getExprForFDESymbol(Sym, Encoding, Streamer);

  // The default symbol subtraction results in an ADD/SUB relocation pair.
  // Processing this relocation pair is problematic when linker relaxation is
  // enabled, so we follow binutils in using the R_RISCV_32_PCREL relocation
  // for the FDE initial location.
  MCContext &Ctx = Streamer.getContext();
  const MCExpr *ME = MCSymbolRefExpr::create(Sym, Ctx);
  assert(Encoding & dwarf::DW_EH_PE_sdata4 && "Unexpected encoding");
  return MCSpecifierExpr::create(ME, ELF::R_RISCV_32_PCREL, Ctx);
}

// Shared by every RISCVMCAsmInfo* variant (ELF, Microsoft COFF, GNU COFF):
// the specifier space (RISCV::Specifier) and its `%name(...)` print syntax
// are object-format-independent, unlike the AtSpecifier ("@IMGREL"-style)
// mechanism used elsewhere for genuinely COFF-specific syntax.
static void printRISCVSpecifierExpr(const MCAsmInfo &MAI, raw_ostream &OS,
                                    const MCSpecifierExpr &Expr) {
  auto S = Expr.getSpecifier();
  bool HasSpecifier = S != 0 && S != ELF::R_RISCV_CALL_PLT;
  if (HasSpecifier)
    OS << '%' << RISCV::getSpecifierName(S) << '(';
  MAI.printExpr(OS, *Expr.getSubExpr());
  if (HasSpecifier)
    OS << ')';
}

void RISCVMCAsmInfo::printSpecifierExpr(raw_ostream &OS,
                                        const MCSpecifierExpr &Expr) const {
  printRISCVSpecifierExpr(*this, OS, Expr);
}

RISCVMCAsmInfoDarwin::RISCVMCAsmInfoDarwin() {
  CodePointerSize = 4;
  PrivateGlobalPrefix = "L";
  PrivateLabelPrefix = "L";
  SeparatorString = "%%";
  CommentString = ";";
  AlignmentIsInBytes = false;
  SupportsDebugInformation = true;
  ExceptionsType = ExceptionHandling::DwarfCFI;
  Data16bitsDirective = "\t.half\t";
  Data32bitsDirective = "\t.word\t";
}

// This provides `@IMGREL` parsing/printing support for COFF data directives
// (e.g. `.long sym@IMGREL`), independent of the `%pcrel_hi(...)`-style
// specifier syntax used elsewhere.
static const MCAsmInfo::AtSpecifier RISCVCOFFAtSpecifiers[] = {
    {MCSymbolRefExpr::VK_COFF_IMGREL32, "IMGREL"},
};

RISCVMCAsmInfoMicrosoftCOFF::RISCVMCAsmInfoMicrosoftCOFF() {
  CodePointerSize = CalleeSaveStackSlotSize = 8;
  PrivateGlobalPrefix = ".L";
  PrivateLabelPrefix = ".L";
  CommentString = "#";
  AlignmentIsInBytes = false;
  SupportsDebugInformation = true;
  Data16bitsDirective = "\t.half\t";
  Data32bitsDirective = "\t.word\t";
  ExceptionsType = ExceptionHandling::WinEH;
  WinEHEncodingType = WinEH::EncodingType::Itanium;
  initializeAtSpecifiers(RISCVCOFFAtSpecifiers);
}

void RISCVMCAsmInfoMicrosoftCOFF::printSpecifierExpr(
    raw_ostream &OS, const MCSpecifierExpr &Expr) const {
  printRISCVSpecifierExpr(*this, OS, Expr);
}

RISCVMCAsmInfoGNUCOFF::RISCVMCAsmInfoGNUCOFF() {
  CodePointerSize = CalleeSaveStackSlotSize = 8;
  PrivateGlobalPrefix = ".L";
  PrivateLabelPrefix = ".L";
  CommentString = "#";
  AlignmentIsInBytes = false;
  SupportsDebugInformation = true;
  Data16bitsDirective = "\t.half\t";
  Data32bitsDirective = "\t.word\t";
  ExceptionsType = ExceptionHandling::WinEH;
  WinEHEncodingType = WinEH::EncodingType::Itanium;
  initializeAtSpecifiers(RISCVCOFFAtSpecifiers);
}

void RISCVMCAsmInfoGNUCOFF::printSpecifierExpr(
    raw_ostream &OS, const MCSpecifierExpr &Expr) const {
  printRISCVSpecifierExpr(*this, OS, Expr);
}
