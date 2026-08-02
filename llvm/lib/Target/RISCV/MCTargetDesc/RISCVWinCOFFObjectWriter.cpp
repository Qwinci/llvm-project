//===-- RISCVWinCOFFObjectWriter.cpp - RISC-V Windows COFF Writer -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The IMAGE_REL_RISCV64_* relocation types emitted here are LLVM's own; see
// llvm/BinaryFormat/COFF.h and llvm/docs/RISCVWinCFI.md.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/RISCVFixupKinds.h"
#include "MCTargetDesc/RISCVMCAsmInfo.h"
#include "MCTargetDesc/RISCVMCTargetDesc.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCValue.h"
#include "llvm/MC/MCWinCOFFObjectWriter.h"

using namespace llvm;

namespace {

class RISCVWinCOFFObjectWriter : public MCWinCOFFObjectTargetWriter {
public:
  RISCVWinCOFFObjectWriter()
      : MCWinCOFFObjectTargetWriter(COFF::IMAGE_FILE_MACHINE_RISCV64) {}

  ~RISCVWinCOFFObjectWriter() override = default;

  unsigned getRelocType(MCContext &Ctx, const MCValue &Target,
                        const MCFixup &Fixup, bool IsCrossSection,
                        const MCAsmBackend &MAB) const override;
};

} // end anonymous namespace

unsigned RISCVWinCOFFObjectWriter::getRelocType(MCContext &Ctx,
                                                const MCValue &Target,
                                                const MCFixup &Fixup,
                                                bool IsCrossSection,
                                                const MCAsmBackend &MAB) const {
  unsigned FixupKind = Fixup.getKind();
  bool PCRel = Fixup.isPCRel();

  if (IsCrossSection) {
    // There is no IMAGE_REL_RISCV64_REL64, so treat FK_Data_8 as FK_Data_4
    // PC-relative, matching AArch64's handling of e.g. `.quad a - b` across
    // sections.
    if (PCRel || (FixupKind != FK_Data_4 && FixupKind != FK_Data_8)) {
      Ctx.reportError(Fixup.getLoc(), "Cannot represent this expression");
      return COFF::IMAGE_REL_RISCV64_ADDR32;
    }
    FixupKind = FK_Data_4;
    PCRel = true;
  }

  // Most fixups carry a Specifier holding the ELF relocation type they would
  // have produced; the relocation below is chosen from the fixup kind instead,
  // so that value is ignored here. Only the TLS and GOT/PLT specifiers must be
  // rejected: Windows TLS goes through .tls$ plus SECREL relocations, and COFF
  // has no GOT or PLT.
  auto Spec = Target.getSpecifier();
  switch (Spec) {
  default:
    break;
  case ELF::R_RISCV_TPREL_HI20:
  case ELF::R_RISCV_TLS_GOT_HI20:
  case ELF::R_RISCV_TLS_GD_HI20:
  case ELF::R_RISCV_TLSDESC_HI20:
  case ELF::R_RISCV_PLT32:
  case ELF::R_RISCV_GOT32_PCREL:
  case ELF::R_RISCV_GOT_HI20:
    Ctx.reportError(Fixup.getLoc(), "%" + RISCV::getSpecifierName(Spec) +
                                        " unsupported on COFF targets");
    return COFF::IMAGE_REL_RISCV64_ABSOLUTE;
  }

  switch (FixupKind) {
  default: {
    MCFixupKindInfo Info = MAB.getFixupKindInfo(Fixup.getKind());
    Ctx.reportError(Fixup.getLoc(), Twine("relocation type ") + Info.Name +
                                        " unsupported on COFF targets");
    return COFF::IMAGE_REL_RISCV64_ABSOLUTE;
  }

  case FK_Data_4:
    if (PCRel)
      return COFF::IMAGE_REL_RISCV64_REL32;
    if (Spec == MCSymbolRefExpr::VK_COFF_IMGREL32)
      return COFF::IMAGE_REL_RISCV64_ADDR32NB;
    return COFF::IMAGE_REL_RISCV64_ADDR32;

  case FK_Data_8:
    return COFF::IMAGE_REL_RISCV64_ADDR64;

  case FK_SecRel_2:
    return COFF::IMAGE_REL_RISCV64_SECTION;

  case FK_SecRel_4:
    return COFF::IMAGE_REL_RISCV64_SECREL;

  case RISCV::fixup_riscv_pcrel_hi20:
    return COFF::IMAGE_REL_RISCV64_PCREL_HI20;

  case RISCV::fixup_riscv_pcrel_lo12_i:
    return COFF::IMAGE_REL_RISCV64_PCREL_LO12_I;

  case RISCV::fixup_riscv_pcrel_lo12_s:
    return COFF::IMAGE_REL_RISCV64_PCREL_LO12_S;

  case RISCV::fixup_riscv_jal:
    return COFF::IMAGE_REL_RISCV64_JAL;

  case RISCV::fixup_riscv_branch:
    return COFF::IMAGE_REL_RISCV64_BRANCH;

  case RISCV::fixup_riscv_rvc_jump:
    return COFF::IMAGE_REL_RISCV64_RVC_JUMP;

  case RISCV::fixup_riscv_rvc_branch:
    return COFF::IMAGE_REL_RISCV64_RVC_BRANCH;

  case RISCV::fixup_riscv_call:
  case RISCV::fixup_riscv_call_plt:
    return COFF::IMAGE_REL_RISCV64_CALL;

  case RISCV::fixup_riscv_tls_secrel_hi20:
    return COFF::IMAGE_REL_RISCV64_SECREL_HI20;

  case RISCV::fixup_riscv_tls_secrel_lo12_i:
    return COFF::IMAGE_REL_RISCV64_SECREL_LO12_I;
  }
}

std::unique_ptr<MCObjectTargetWriter> llvm::createRISCVWinCOFFObjectWriter() {
  return std::make_unique<RISCVWinCOFFObjectWriter>();
}
