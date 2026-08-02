//===-- RuntimeDyldCOFFRISCV64.h --- COFF/RISCV64 specific code ---*- C++*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// COFF RISCV64 support for MC-JIT runtime dynamic linker.
//
// The IMAGE_REL_RISCV64_* relocations are LLVM's own; see
// llvm/docs/RISCVWinCFI.md. Resolution here mirrors lld's applyRelRISCV64
// (lld/COFF/Chunks.cpp), which is the reference implementation.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_EXECUTIONENGINE_RUNTIMEDYLD_TARGETS_RUNTIMEDYLDCOFFRISCV64_H
#define LLVM_LIB_EXECUTIONENGINE_RUNTIMEDYLD_TARGETS_RUNTIMEDYLDCOFFRISCV64_H

#include "../RuntimeDyldCOFF.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/Object/COFF.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/MathExtras.h"

#define DEBUG_TYPE "dyld"

namespace llvm {

// Routes a call whose target is beyond auipc+jalr's +/-2GB reach through the
// stub built by createStubFunction(). Must not collide with any real
// IMAGE_REL_RISCV64_* value.
enum RISCV64InternalRelocationType : unsigned {
  INTERNAL_REL_RISCV64_LONG_CALL = 0x100,
};

static void addRISCV64_16(uint8_t *P, int16_t V) {
  using namespace llvm::support::endian;
  write16le(P, read16le(P) + V);
}

static void applyRISCV64Hi20(uint8_t *P, int64_t V) {
  using namespace llvm::support::endian;
  uint32_t Hi = (uint32_t)((V + 0x800) & 0xfffff000);
  write32le(P, (read32le(P) & 0xfff) | Hi);
}

static void applyRISCV64Lo12I(uint8_t *P, int64_t V) {
  using namespace llvm::support::endian;
  uint32_t Lo = V & 0xfff;
  write32le(P, (read32le(P) & 0xfffff) | (Lo << 20));
}

static void applyRISCV64Lo12S(uint8_t *P, int64_t V) {
  using namespace llvm::support::endian;
  uint32_t Lo = V & 0xfff;
  uint32_t Insn = read32le(P) & 0x1fff07f;
  Insn |= ((Lo >> 5) & 0x7f) << 25;
  Insn |= (Lo & 0x1f) << 7;
  write32le(P, Insn);
}

static void applyRISCV64Jal(uint8_t *P, int64_t V) {
  using namespace llvm::support::endian;
  assert(isInt<21>(V) && "jal target out of range");
  uint32_t Insn = read32le(P) & 0xfff;
  Insn |= ((V >> 20) & 0x1) << 31;
  Insn |= ((V >> 1) & 0x3ff) << 21;
  Insn |= ((V >> 11) & 0x1) << 20;
  Insn |= ((V >> 12) & 0xff) << 12;
  write32le(P, Insn);
}

static void applyRISCV64Branch(uint8_t *P, int64_t V) {
  using namespace llvm::support::endian;
  assert(isInt<13>(V) && "branch target out of range");
  uint32_t Insn = read32le(P) & 0x1fff07f;
  Insn |= ((V >> 12) & 0x1) << 31;
  Insn |= ((V >> 5) & 0x3f) << 25;
  Insn |= ((V >> 1) & 0xf) << 8;
  Insn |= ((V >> 11) & 0x1) << 7;
  write32le(P, Insn);
}

static void applyRISCV64RvcJump(uint8_t *P, int64_t V) {
  using namespace llvm::support::endian;
  assert(isInt<12>(V) && "c.j target out of range");
  uint16_t Insn = read16le(P) & 0xe003;
  Insn |= ((V >> 11) & 0x1) << 12;
  Insn |= ((V >> 4) & 0x1) << 11;
  Insn |= ((V >> 8) & 0x3) << 9;
  Insn |= ((V >> 10) & 0x1) << 8;
  Insn |= ((V >> 6) & 0x1) << 7;
  Insn |= ((V >> 7) & 0x1) << 6;
  Insn |= ((V >> 1) & 0x7) << 3;
  Insn |= ((V >> 5) & 0x1) << 2;
  write16le(P, Insn);
}

static void applyRISCV64RvcBranch(uint8_t *P, int64_t V) {
  using namespace llvm::support::endian;
  assert(isInt<9>(V) && "c.b target out of range");
  uint16_t Insn = read16le(P) & 0xe383;
  Insn |= ((V >> 8) & 0x1) << 12;
  Insn |= ((V >> 3) & 0x3) << 10;
  Insn |= ((V >> 6) & 0x3) << 5;
  Insn |= ((V >> 1) & 0x3) << 3;
  Insn |= ((V >> 5) & 0x1) << 2;
  write16le(P, Insn);
}

class RuntimeDyldCOFFRISCV64 : public RuntimeDyldCOFF {
private:
  uint64_t ImageBase;

  // Fake an __ImageBase pointer by returning the section with the lowest
  // address, matching RuntimeDyldCOFFAArch64.
  uint64_t getImageBase() {
    if (!ImageBase) {
      ImageBase = std::numeric_limits<uint64_t>::max();
      for (const SectionEntry &Section : Sections)
        if (Section.getLoadAddress() != 0)
          ImageBase = std::min(ImageBase, Section.getLoadAddress());
    }
    return ImageBase;
  }

public:
  RuntimeDyldCOFFRISCV64(RuntimeDyld::MemoryManager &MM,
                         JITSymbolResolver &Resolver)
      : RuntimeDyldCOFF(MM, Resolver, 8, COFF::IMAGE_REL_RISCV64_ADDR64),
        ImageBase(0) {}

  Align getStubAlignment() override { return Align(8); }

  // auipc/ld/jr/nop plus the 8-byte literal the ld reads.
  unsigned getMaxStubSize() const override { return 24; }

  std::tuple<uint64_t, uint64_t, uint64_t>
  generateRelocationStub(unsigned SectionID, StringRef TargetName,
                         uint64_t Offset, uint64_t RelType, uint64_t Addend,
                         StubMap &Stubs) {
    uintptr_t StubOffset;
    SectionEntry &Section = Sections[SectionID];

    RelocationValueRef OriginalRelValueRef;
    OriginalRelValueRef.SectionID = SectionID;
    OriginalRelValueRef.Offset = Offset;
    OriginalRelValueRef.Addend = Addend;
    OriginalRelValueRef.SymbolName = TargetName.data();

    auto [Stub, Inserted] = Stubs.try_emplace(OriginalRelValueRef);
    if (Inserted) {
      LLVM_DEBUG(dbgs() << " Create a new stub function for "
                        << TargetName.data() << "\n");
      StubOffset = Section.getStubOffset();
      Stub->second = StubOffset;
      createStubFunction(Section.getAddressWithOffset(StubOffset));
      Section.advanceStubOffset(getMaxStubSize());
    } else {
      LLVM_DEBUG(dbgs() << " Stub function found for " << TargetName.data()
                        << "\n");
      StubOffset = Stub->second;
    }

    // Point the original call at the stub...
    const RelocationEntry RE(SectionID, Offset, RelType, Addend);
    resolveRelocation(RE, Section.getLoadAddressWithOffset(StubOffset));

    // ...and have the real target address written into the stub's literal.
    Addend = 0;
    Offset = StubOffset;
    RelType = INTERNAL_REL_RISCV64_LONG_CALL;

    return std::make_tuple(Offset, RelType, Addend);
  }

  Expected<object::relocation_iterator>
  processRelocationRef(unsigned SectionID, object::relocation_iterator RelI,
                       const object::ObjectFile &Obj,
                       ObjSectionToIDMap &ObjSectionToID,
                       StubMap &Stubs) override {
    using namespace llvm::support::endian;

    auto Symbol = RelI->getSymbol();
    if (Symbol == Obj.symbol_end())
      report_fatal_error("Unknown symbol in relocation");

    Expected<StringRef> TargetNameOrErr = Symbol->getName();
    if (!TargetNameOrErr)
      return TargetNameOrErr.takeError();
    StringRef TargetName = *TargetNameOrErr;

    auto SectionOrErr = Symbol->getSection();
    if (!SectionOrErr)
      return SectionOrErr.takeError();
    auto Section = *SectionOrErr;

    uint64_t RelType = RelI->getType();
    uint64_t Offset = RelI->getOffset();

    // If there is no section, this must be an external reference.
    bool IsExtern = Section == Obj.section_end();

    uint64_t Addend = 0;
    SectionEntry &AddendSection = Sections[SectionID];
    uintptr_t ObjTarget = AddendSection.getObjAddress() + Offset;
    uint8_t *Displacement = (uint8_t *)ObjTarget;

    unsigned TargetSectionID = -1;
    uint64_t TargetOffset = -1;

    if (TargetName.starts_with(getImportSymbolPrefix())) {
      TargetSectionID = SectionID;
      TargetOffset = getDLLImportOffset(SectionID, Stubs, TargetName);
      TargetName = StringRef();
      IsExtern = false;
    } else if (!IsExtern) {
      if (auto TargetSectionIDOrErr = findOrEmitSection(
              Obj, *Section, Section->isText(), ObjSectionToID))
        TargetSectionID = *TargetSectionIDOrErr;
      else
        return TargetSectionIDOrErr.takeError();

      TargetOffset = getSymbolOffset(*Symbol);
    }

    // Extract the inline addend. The data relocations carry it in place, and
    // both halves of a pc-relative pair do too: the %pcrel_lo immediate holds
    // the symbol addend plus the distance back to the auipc, and (because COFF
    // has no relocation addend field) the %pcrel_hi auipc immediate holds the
    // raw byte addend. The assembler leaves jal/branch immediates at zero and
    // expects them to be overwritten; see the matching behavior in lld's
    // applyRelRISCV64.
    switch (RelType) {
    case COFF::IMAGE_REL_RISCV64_ADDR32:
    case COFF::IMAGE_REL_RISCV64_ADDR32NB:
    case COFF::IMAGE_REL_RISCV64_REL32:
    case COFF::IMAGE_REL_RISCV64_SECREL:
      Addend = read32le(Displacement);
      break;
    case COFF::IMAGE_REL_RISCV64_ADDR64:
      Addend = read64le(Displacement);
      break;
    case COFF::IMAGE_REL_RISCV64_PCREL_HI20:
      // COFF has no relocation addend field, so the auipc's 20-bit immediate
      // carries the raw byte addend (the offset from the referenced symbol);
      // capture it so resolveRelocation folds it into the target before taking
      // the pc-relative high part. See RISCVAsmBackend::applyFixup and lld's
      // applyRelRISCV64.
      Addend = SignExtend64<20>(read32le(Displacement) >> 12);
      break;
    case COFF::IMAGE_REL_RISCV64_PCREL_LO12_I:
      // Holds the symbol addend plus the distance back to the paired auipc.
      Addend = SignExtend64<12>(read32le(Displacement) >> 20);
      break;
    case COFF::IMAGE_REL_RISCV64_PCREL_LO12_S: {
      uint32_t Insn = read32le(Displacement);
      Addend =
          SignExtend64<12>(((Insn >> 25) & 0x7f) << 5 | ((Insn >> 7) & 0x1f));
      break;
    }
    case COFF::IMAGE_REL_RISCV64_CALL:
    case COFF::IMAGE_REL_RISCV64_JAL:
      // These are the only relocations that can reach an arbitrary external
      // address, so they are the only ones that can need a stub.
      if (IsExtern)
        std::tie(Offset, RelType, Addend) = generateRelocationStub(
            SectionID, TargetName, Offset, RelType, Addend, Stubs);
      break;
    default:
      break;
    }

#if !defined(NDEBUG)
    SmallString<32> RelTypeName;
    RelI->getTypeName(RelTypeName);

    LLVM_DEBUG(dbgs() << "\t\tIn Section " << SectionID << " Offset " << Offset
                      << " RelType: " << RelTypeName << " TargetName: "
                      << TargetName << " Addend " << Addend << "\n");
#endif

    if (IsExtern) {
      RelocationEntry RE(SectionID, Offset, RelType, Addend);
      addRelocationForSymbol(RE, TargetName);
    } else {
      RelocationEntry RE(SectionID, Offset, RelType, TargetOffset + Addend);
      addRelocationForSection(RE, TargetSectionID);
    }
    return ++RelI;
  }

  void resolveRelocation(const RelocationEntry &RE, uint64_t Value) override {
    using namespace llvm::support::endian;

    const auto Section = Sections[RE.SectionID];
    uint8_t *Target = Section.getAddressWithOffset(RE.Offset);
    uint64_t FinalAddress = Section.getLoadAddressWithOffset(RE.Offset);

    switch (RE.RelType) {
    default:
      llvm_unreachable("unsupported relocation type");

    case COFF::IMAGE_REL_RISCV64_ABSOLUTE:
      // This relocation is ignored.
      break;

    case COFF::IMAGE_REL_RISCV64_ADDR32:
      // The 32-bit VA of the target.
      write32le(Target, Value + RE.Addend);
      break;

    case COFF::IMAGE_REL_RISCV64_ADDR64:
      // The 64-bit VA of the target.
      write64le(Target, Value + RE.Addend);
      break;

    case COFF::IMAGE_REL_RISCV64_ADDR32NB:
      // The target's 32-bit RVA.
      write32le(Target, Value + RE.Addend - getImageBase());
      break;

    case COFF::IMAGE_REL_RISCV64_SECTION:
      // 16-bit section index of the section containing the target.
      assert(static_cast<uint32_t>(RE.SectionID) <= UINT16_MAX &&
             "relocation overflow");
      addRISCV64_16(Target, RE.SectionID);
      break;

    case COFF::IMAGE_REL_RISCV64_SECREL:
      // 32-bit offset of the target from the start of its section.
      assert(static_cast<int64_t>(RE.Addend) <= INT32_MAX &&
             "Relocation overflow");
      assert(static_cast<int64_t>(RE.Addend) >= INT32_MIN &&
             "Relocation underflow");
      write32le(Target, RE.Addend);
      break;

    case COFF::IMAGE_REL_RISCV64_REL32:
      // The 32-bit relative address from the byte following the relocation.
      write32le(Target, Value + RE.Addend - FinalAddress - 4);
      break;

    case COFF::IMAGE_REL_RISCV64_PCREL_HI20:
      applyRISCV64Hi20(Target, Value + RE.Addend - FinalAddress);
      break;

    case COFF::IMAGE_REL_RISCV64_PCREL_LO12_I:
      // RE.Addend already carries the distance back to the paired auipc, so
      // this reconstructs the same value PCREL_HI20 computed.
      applyRISCV64Lo12I(Target, Value + RE.Addend - FinalAddress);
      break;

    case COFF::IMAGE_REL_RISCV64_PCREL_LO12_S:
      applyRISCV64Lo12S(Target, Value + RE.Addend - FinalAddress);
      break;

    case COFF::IMAGE_REL_RISCV64_CALL: {
      // Spans the auipc+jalr pair; both share the same PC baseline.
      int64_t Delta = Value + RE.Addend - FinalAddress;
      assert(isInt<32>(Delta) && "call target out of range");
      applyRISCV64Hi20(Target, Delta);
      applyRISCV64Lo12I(Target + 4, Delta);
      break;
    }

    case COFF::IMAGE_REL_RISCV64_JAL:
      applyRISCV64Jal(Target, Value + RE.Addend - FinalAddress);
      break;

    case COFF::IMAGE_REL_RISCV64_BRANCH:
      applyRISCV64Branch(Target, Value + RE.Addend - FinalAddress);
      break;

    case COFF::IMAGE_REL_RISCV64_RVC_JUMP:
      applyRISCV64RvcJump(Target, Value + RE.Addend - FinalAddress);
      break;

    case COFF::IMAGE_REL_RISCV64_RVC_BRANCH:
      applyRISCV64RvcBranch(Target, Value + RE.Addend - FinalAddress);
      break;

    case COFF::IMAGE_REL_RISCV64_SECREL_HI20:
      // Section-relative, so no PC baseline is subtracted. RE.Addend is just the
      // symbol's .tls$ offset: processRelocationRef reads no inline addend for
      // SECREL because codegen materializes any element/field offset as a
      // separate add, never folding it into this pair (see lld's
      // applyRelRISCV64 and clang/test/CodeGen/riscv64-windows-tls.c).
      applyRISCV64Hi20(Target, RE.Addend);
      break;

    case COFF::IMAGE_REL_RISCV64_SECREL_LO12_I:
      applyRISCV64Lo12I(Target, RE.Addend);
      break;

    case INTERNAL_REL_RISCV64_LONG_CALL:
      // Fill in the literal the stub's `ld` reads. Target points at the start
      // of the stub; the literal follows the four instructions.
      write64le(Target + 16, Value + RE.Addend);
      break;
    }
  }

  void registerEHFrames() override {}
};

} // End namespace llvm

#undef DEBUG_TYPE

#endif // LLVM_LIB_EXECUTIONENGINE_RUNTIMEDYLD_TARGETS_RUNTIMEDYLDCOFFRISCV64_H
