#include "PECallFrameInfo.h"

#include "ObjectFilePECOFF.h"

#include "Plugins/Process/Utility/lldb-x86-register-enums.h"
#include "Utility/RISCV_DWARF_Registers.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/Support/Win64EH.h"

using namespace lldb;
using namespace lldb_private;
using namespace llvm::Win64EH;

template <typename T>
static const T *TypedRead(const DataExtractor &data_extractor, offset_t &offset,
                          offset_t size = sizeof(T)) {
  return static_cast<const T *>(data_extractor.GetData(&offset, size));
}

struct EHInstruction {
  enum class Type {
    PUSH_REGISTER,
    ALLOCATE,
    SET_FRAME_POINTER_REGISTER,
    SAVE_REGISTER
  };

  uint8_t offset;
  Type type;
  uint32_t reg;
  uint32_t frame_offset;
};

using EHProgram = std::vector<EHInstruction>;

/// Per-architecture behavior for decoding a PE unwind info record.
///
/// The UNWIND_INFO container, the two byte unwind code slots and the chained
/// UNW_ChainInfo walk are shared by every architecture using this format, as is
/// the operand arithmetic for AllocSmall/AllocLarge/SaveNonVol. What differs is
/// the register numbering, the meaning of a few opcode numbers, how the frame
/// register is encoded in the UNWIND_INFO header, and whether the call pushed a
/// return address onto the stack.
struct EHArchPolicy {
  /// What a SAVE_REGISTER's frame_offset is measured from.
  enum SaveBase {
    /// The stack pointer as of the end of the prologue.
    PostPrologueSP,
    /// The stack pointer as of the saving instruction itself.
    SPAtSaveInstruction,
  };

  /// Register numbering used for every register in the emitted UnwindPlan.
  lldb::RegisterKind register_kind;

  /// Stack pointer, in register_kind numbering.
  uint32_t sp_regnum;

  /// The register the call left the return address in, or LLDB_INVALID_REGNUM
  /// if the call pushed it onto the stack instead. Exactly one of this and
  /// pushed_pc_regnum is valid.
  uint32_t ra_regnum;

  /// The register the call pushed the return address as, or
  /// LLDB_INVALID_REGNUM if the return address stays in ra_regnum.
  uint32_t pushed_pc_regnum;

  /// Maps an on-disk UnwindOp nibble to a llvm::Win64EH::UnwindOpcodes value,
  /// for architectures whose on-disk numbering diverges from x86_64's. Returns
  /// UINT8_MAX for values the architecture does not define, which makes the
  /// shared decoder reject the record.
  uint8_t (*effective_op)(uint8_t on_disk_op);

  /// Map a general purpose register index to register_kind numbering.
  uint32_t (*gpr_to_regnum)(uint8_t index);

  /// Map a floating point register index to register_kind numbering.
  uint32_t (*fpr_to_regnum)(uint8_t index);

  /// Scale applied to a non-big floating point register save offset.
  uint32_t fpr_offset_scale;

  /// Decode the UNWIND_INFO header's frame register field for a UOP_SetFPReg
  /// code. Returns false if the record does not establish a frame register.
  bool (*decode_frame_reg)(const UnwindInfo &unwind_info, uint32_t &reg,
                           uint32_t &offset);

  SaveBase save_base;
};

static uint8_t IdentityUnwindOp(uint8_t on_disk_op) { return on_disk_op; }

static uint32_t X86_64GprToRegnum(uint8_t index) {
  static uint32_t gpr_to_regnum[] = {
      lldb_rax_x86_64, lldb_rcx_x86_64, lldb_rdx_x86_64, lldb_rbx_x86_64,
      lldb_rsp_x86_64, lldb_rbp_x86_64, lldb_rsi_x86_64, lldb_rdi_x86_64,
      lldb_r8_x86_64,  lldb_r9_x86_64,  lldb_r10_x86_64, lldb_r11_x86_64,
      lldb_r12_x86_64, lldb_r13_x86_64, lldb_r14_x86_64, lldb_r15_x86_64};

  if (index >= std::size(gpr_to_regnum))
    return LLDB_INVALID_REGNUM;

  return gpr_to_regnum[index];
}

static uint32_t X86_64FprToRegnum(uint8_t index) {
  static uint32_t xmm_to_regnum[] = {
      lldb_xmm0_x86_64,  lldb_xmm1_x86_64,  lldb_xmm2_x86_64,
      lldb_xmm3_x86_64,  lldb_xmm4_x86_64,  lldb_xmm5_x86_64,
      lldb_xmm6_x86_64,  lldb_xmm7_x86_64,  lldb_xmm8_x86_64,
      lldb_xmm9_x86_64,  lldb_xmm10_x86_64, lldb_xmm11_x86_64,
      lldb_xmm12_x86_64, lldb_xmm13_x86_64, lldb_xmm14_x86_64,
      lldb_xmm15_x86_64};

  if (index >= std::size(xmm_to_regnum))
    return LLDB_INVALID_REGNUM;

  return xmm_to_regnum[index];
}

static bool X86_64DecodeFrameReg(const UnwindInfo &unwind_info, uint32_t &reg,
                                 uint32_t &offset) {
  // The frame register is packed into the low nibble, where zero means no
  // frame register was established.
  if (!unwind_info.getFrameRegister())
    return false;

  reg = X86_64GprToRegnum(unwind_info.getFrameRegister());
  if (reg == LLDB_INVALID_REGNUM)
    return false;

  offset = static_cast<uint32_t>(unwind_info.getFrameOffset()) * 16;
  return true;
}

static const EHArchPolicy g_x86_64_policy = {
    /*register_kind=*/lldb::eRegisterKindLLDB,
    /*sp_regnum=*/lldb_rsp_x86_64,
    /*ra_regnum=*/LLDB_INVALID_REGNUM,
    /*pushed_pc_regnum=*/lldb_rip_x86_64,
    /*effective_op=*/IdentityUnwindOp,
    /*gpr_to_regnum=*/X86_64GprToRegnum,
    /*fpr_to_regnum=*/X86_64FprToRegnum,
    /*fpr_offset_scale=*/16,
    /*decode_frame_reg=*/X86_64DecodeFrameReg,
    /*save_base=*/EHArchPolicy::PostPrologueSP,
};

/// RISCV64 reuses x86_64's UNWIND_INFO container but numbers its opcodes
/// independently, and defines only five of them. On-disk 1 through 4 happen to
/// coincide with the x86_64 ordinals for AllocLarge/AllocSmall/SetFPReg/
/// SaveNonVol, so only 5 has to be translated: x86_64 spells it
/// UOP_SaveNonVolBig, whereas RISCV64 uses it for UOP_RISCVSaveFReg, whose own
/// ordinal does not fit in the four bit field. See riscv64OnDiskOp in
/// llvm/lib/MC/MCWin64EH.cpp and llvm/docs/RISCVWinCFI.md.
static uint8_t RISCV64UnwindOp(uint8_t on_disk_op) {
  switch (on_disk_op) {
  case 1:
    return UOP_AllocLarge;
  case 2:
    return UOP_AllocSmall;
  case 3:
    return UOP_SetFPReg;
  case 4:
    return UOP_SaveNonVol;
  case 5:
    return UOP_RISCVSaveFReg;
  case 6:
    return UOP_TrapFrame;
  case 7:
    return UOP_Context;
  case 8:
    return UOP_ClearUnwoundToCall;
  default:
    // On-disk 0 and 9 through 15 are not defined for this target.
    return UINT8_MAX;
  }
}

static uint32_t RISCV64GprToRegnum(uint8_t index) {
  // The saveable GPRs get a dense four bit index of their own: ra, then s0
  // through s11. See getSEHGPRIndex in RISCVBaseInfo.cpp.
  static uint32_t gpr_to_regnum[] = {
      riscv_dwarf::dwarf_gpr_x1,  // ra
      riscv_dwarf::dwarf_gpr_x8,  // s0
      riscv_dwarf::dwarf_gpr_x9,  // s1
      riscv_dwarf::dwarf_gpr_x18, // s2
      riscv_dwarf::dwarf_gpr_x19, // s3
      riscv_dwarf::dwarf_gpr_x20, // s4
      riscv_dwarf::dwarf_gpr_x21, // s5
      riscv_dwarf::dwarf_gpr_x22, // s6
      riscv_dwarf::dwarf_gpr_x23, // s7
      riscv_dwarf::dwarf_gpr_x24, // s8
      riscv_dwarf::dwarf_gpr_x25, // s9
      riscv_dwarf::dwarf_gpr_x26, // s10
      riscv_dwarf::dwarf_gpr_x27, // s11
  };

  if (index >= std::size(gpr_to_regnum))
    return LLDB_INVALID_REGNUM;

  return gpr_to_regnum[index];
}

static uint32_t RISCV64FprToRegnum(uint8_t index) {
  // Likewise fs0 through fs11, in a separate index space also starting at
  // zero. See getSEHFPRIndex in RISCVBaseInfo.cpp.
  static uint32_t fpr_to_regnum[] = {
      riscv_dwarf::dwarf_fpr_f8,  // fs0
      riscv_dwarf::dwarf_fpr_f9,  // fs1
      riscv_dwarf::dwarf_fpr_f18, // fs2
      riscv_dwarf::dwarf_fpr_f19, // fs3
      riscv_dwarf::dwarf_fpr_f20, // fs4
      riscv_dwarf::dwarf_fpr_f21, // fs5
      riscv_dwarf::dwarf_fpr_f22, // fs6
      riscv_dwarf::dwarf_fpr_f23, // fs7
      riscv_dwarf::dwarf_fpr_f24, // fs8
      riscv_dwarf::dwarf_fpr_f25, // fs9
      riscv_dwarf::dwarf_fpr_f26, // fs10
      riscv_dwarf::dwarf_fpr_f27, // fs11
  };

  if (index >= std::size(fpr_to_regnum))
    return LLDB_INVALID_REGNUM;

  return fpr_to_regnum[index];
}

static bool RISCV64DecodeFrameReg(const UnwindInfo &unwind_info, uint32_t &reg,
                                  uint32_t &offset) {
  // The frame register is always s0 and is not encoded anywhere, so unlike
  // x86_64 the whole byte is the offset, scaled by 16. That also means zero is
  // an ordinary offset here rather than a "no frame register" sentinel: it is
  // the presence of this UOP_SetFPReg code that establishes the frame
  // register, so there is nothing to reject.
  reg = riscv_dwarf::dwarf_gpr_x8;
  offset = static_cast<uint32_t>(unwind_info.FrameRegisterAndOffset) * 16;
  return true;
}

static const EHArchPolicy g_riscv64_policy = {
    /*register_kind=*/lldb::eRegisterKindDWARF,
    /*sp_regnum=*/riscv_dwarf::dwarf_gpr_x2,
    /*ra_regnum=*/riscv_dwarf::dwarf_gpr_x1,
    /*pushed_pc_regnum=*/LLDB_INVALID_REGNUM,
    /*effective_op=*/RISCV64UnwindOp,
    /*gpr_to_regnum=*/RISCV64GprToRegnum,
    /*fpr_to_regnum=*/RISCV64FprToRegnum,
    /*fpr_offset_scale=*/8,
    /*decode_frame_reg=*/RISCV64DecodeFrameReg,
    /*save_base=*/EHArchPolicy::SPAtSaveInstruction,
};

static const EHArchPolicy *GetEHArchPolicy(uint16_t machine) {
  switch (machine) {
  case llvm::COFF::IMAGE_FILE_MACHINE_AMD64:
    return &g_x86_64_policy;
  case llvm::COFF::IMAGE_FILE_MACHINE_RISCV64:
    return &g_riscv64_policy;
  default:
    return nullptr;
  }
}

class UnwindCodesIterator {
public:
  UnwindCodesIterator(ObjectFilePECOFF &object_file, uint32_t unwind_info_rva);

  bool GetNext();
  bool IsError() const { return m_error; }

  const UnwindInfo *GetUnwindInfo() const { return m_unwind_info; }
  const UnwindCode *GetUnwindCode() const { return m_unwind_code; }
  bool IsChained() const { return m_chained; }

private:
  ObjectFilePECOFF &m_object_file;

  bool m_error = false;

  uint32_t m_unwind_info_rva;
  DataExtractor m_unwind_info_data;
  const UnwindInfo *m_unwind_info = nullptr;

  DataExtractor m_unwind_code_data;
  offset_t m_unwind_code_offset;
  const UnwindCode *m_unwind_code = nullptr;

  bool m_chained = false;
};

UnwindCodesIterator::UnwindCodesIterator(ObjectFilePECOFF &object_file,
                                         uint32_t unwind_info_rva)
    : m_object_file(object_file),
      m_unwind_info_rva(unwind_info_rva), m_unwind_code_offset{} {}

bool UnwindCodesIterator::GetNext() {
  static constexpr int UNWIND_INFO_SIZE = 4;

  m_error = false;
  m_unwind_code = nullptr;
  while (!m_unwind_code) {
    if (!m_unwind_info) {
      m_unwind_info_data =
          m_object_file.ReadImageDataByRVA(m_unwind_info_rva, UNWIND_INFO_SIZE);

      offset_t offset = 0;
      m_unwind_info =
          TypedRead<UnwindInfo>(m_unwind_info_data, offset, UNWIND_INFO_SIZE);
      if (!m_unwind_info) {
        m_error = true;
        break;
      }

      m_unwind_code_data = m_object_file.ReadImageDataByRVA(
          m_unwind_info_rva + UNWIND_INFO_SIZE,
          m_unwind_info->NumCodes * sizeof(UnwindCode));
      m_unwind_code_offset = 0;
    }

    if (m_unwind_code_offset < m_unwind_code_data.GetByteSize()) {
      m_unwind_code =
          TypedRead<UnwindCode>(m_unwind_code_data, m_unwind_code_offset);
      m_error = !m_unwind_code;
      break;
    }

    if (!(m_unwind_info->getFlags() & UNW_ChainInfo))
      break;

    uint32_t runtime_function_rva =
        m_unwind_info_rva + UNWIND_INFO_SIZE +
        ((m_unwind_info->NumCodes + 1) & ~1) * sizeof(UnwindCode);
    DataExtractor runtime_function_data = m_object_file.ReadImageDataByRVA(
        runtime_function_rva, sizeof(RuntimeFunction));

    offset_t offset = 0;
    const auto *runtime_function =
        TypedRead<RuntimeFunction>(runtime_function_data, offset);
    if (!runtime_function) {
      m_error = true;
      break;
    }

    m_unwind_info_rva = runtime_function->UnwindInfoOffset;
    m_unwind_info = nullptr;
    m_chained = true;
  }

  return !!m_unwind_code;
}

class EHProgramBuilder {
public:
  EHProgramBuilder(ObjectFilePECOFF &object_file, uint32_t unwind_info_rva,
                   const EHArchPolicy &policy);

  bool Build();

  const EHProgram &GetProgram() const { return m_program; }

private:
  bool ProcessUnwindCode(UnwindCode code);
  void Finalize();

  bool ParseBigOrScaledFrameOffset(uint32_t &result, bool big, uint32_t scale);
  bool ParseBigFrameOffset(uint32_t &result);
  bool ParseFrameOffset(uint32_t &result);

  UnwindCodesIterator m_iterator;
  const EHArchPolicy &m_policy;
  EHProgram m_program;
};

EHProgramBuilder::EHProgramBuilder(ObjectFilePECOFF &object_file,
                                   uint32_t unwind_info_rva,
                                   const EHArchPolicy &policy)
    : m_iterator(object_file, unwind_info_rva), m_policy(policy) {}

bool EHProgramBuilder::Build() {
  while (m_iterator.GetNext())
    if (!ProcessUnwindCode(*m_iterator.GetUnwindCode()))
      return false;

  if (m_iterator.IsError())
    return false;

  Finalize();

  return true;
}

bool EHProgramBuilder::ProcessUnwindCode(UnwindCode code) {
  uint8_t o = m_iterator.IsChained() ? 0 : code.u.CodeOffset;
  uint8_t unwind_operation = m_policy.effective_op(code.getUnwindOp());
  uint8_t operation_info = code.getOpInfo();

  switch (unwind_operation) {
  case UOP_PushNonVol: {
    uint32_t r = m_policy.gpr_to_regnum(operation_info);
    if (r == LLDB_INVALID_REGNUM)
      return false;

    m_program.emplace_back(
        EHInstruction{o, EHInstruction::Type::PUSH_REGISTER, r, 8});

    return true;
  }
  case UOP_AllocLarge: {
    uint32_t fo;
    if (!ParseBigOrScaledFrameOffset(fo, operation_info, 8))
      return false;

    m_program.emplace_back(EHInstruction{o, EHInstruction::Type::ALLOCATE,
                                         LLDB_INVALID_REGNUM, fo});

    return true;
  }
  case UOP_AllocSmall: {
    m_program.emplace_back(
        EHInstruction{o, EHInstruction::Type::ALLOCATE, LLDB_INVALID_REGNUM,
                      static_cast<uint32_t>(operation_info) * 8 + 8});
    return true;
  }
  case UOP_SetFPReg: {
    uint32_t fpr;
    uint32_t fpro;
    if (!m_policy.decode_frame_reg(*m_iterator.GetUnwindInfo(), fpr, fpro))
      return false;

    m_program.emplace_back(EHInstruction{
        o, EHInstruction::Type::SET_FRAME_POINTER_REGISTER, fpr, fpro});

    return true;
  }
  case UOP_SaveNonVol:
  case UOP_SaveNonVolBig: {
    uint32_t r = m_policy.gpr_to_regnum(operation_info);
    if (r == LLDB_INVALID_REGNUM)
      return false;

    uint32_t fo;
    if (!ParseBigOrScaledFrameOffset(fo, unwind_operation == UOP_SaveNonVolBig,
                                     8))
      return false;

    m_program.emplace_back(
        EHInstruction{o, EHInstruction::Type::SAVE_REGISTER, r, fo});

    return true;
  }
  case UOP_Epilog: {
    return m_iterator.GetNext();
  }
  case UOP_SpareCode: {
    // ReSharper disable once CppIdenticalOperandsInBinaryExpression
    return m_iterator.GetNext() && m_iterator.GetNext();
  }
  case UOP_SaveXMM128:
  case UOP_SaveXMM128Big:
  case UOP_RISCVSaveFReg: {
    uint32_t r = m_policy.fpr_to_regnum(operation_info);
    if (r == LLDB_INVALID_REGNUM)
      return false;

    uint32_t fo;
    if (!ParseBigOrScaledFrameOffset(fo, unwind_operation == UOP_SaveXMM128Big,
                                     m_policy.fpr_offset_scale))
      return false;

    m_program.emplace_back(
        EHInstruction{o, EHInstruction::Type::SAVE_REGISTER, r, fo});

    return true;
  }
  case UOP_TrapFrame:
  case UOP_Context:
  case UOP_ClearUnwoundToCall:
    // RISCV64's three dispatcher-only opcodes (on-disk 6/7/8), emitted only in
    // hand-written OS runtime stubs. They describe how the OS reconstructs a
    // register context at a trap/exception boundary, which is not something
    // this SP/FP-relative UnwindPlan can model. They always accompany ordinary
    // save codes that do carry the frame layout, so for a debugger backtrace it
    // is correct to consume this single slot and emit nothing. See §5.1 of
    // llvm/docs/RISCVWinCFI.md.
    return true;
  case UOP_PushMachFrame: {
    // This code, and the machine frame layout it describes, is x86_64 only.
    // Architectures whose effective_op never yields UOP_PushMachFrame cannot
    // reach this arm.
    if (operation_info)
      m_program.emplace_back(EHInstruction{o, EHInstruction::Type::ALLOCATE,
                                           LLDB_INVALID_REGNUM, 8});
    m_program.emplace_back(EHInstruction{o, EHInstruction::Type::PUSH_REGISTER,
                                         lldb_rip_x86_64, 8});
    m_program.emplace_back(EHInstruction{o, EHInstruction::Type::PUSH_REGISTER,
                                         lldb_cs_x86_64, 8});
    m_program.emplace_back(EHInstruction{o, EHInstruction::Type::PUSH_REGISTER,
                                         lldb_rflags_x86_64, 8});
    m_program.emplace_back(EHInstruction{o, EHInstruction::Type::PUSH_REGISTER,
                                         lldb_rsp_x86_64, 8});
    m_program.emplace_back(EHInstruction{o, EHInstruction::Type::PUSH_REGISTER,
                                         lldb_ss_x86_64, 8});

    return true;
  }
  default:
    return false;
  }
}

void EHProgramBuilder::Finalize() {
  // Where the call left the return address in a register there is nothing to
  // synthesize: a spill of that register, if the function made one, is an
  // ordinary SAVE_REGISTER, and otherwise the register is still live.
  if (m_policy.pushed_pc_regnum == LLDB_INVALID_REGNUM) {
    // But every unwind code carries a non-zero CodeOffset, so without a row at
    // function offset zero UnwindPlan::GetRowForFunctionOffset returns nullptr
    // for a PC in the first few bytes of the function and unwinding silently
    // stops there. Add a zero sized allocation to anchor that row; it yields
    // exactly CFA = sp + 0 with no register rules. The program is in reverse
    // chronological order, so the earliest code is the one at the back.
    if (m_program.empty() || m_program.back().offset != 0)
      m_program.emplace_back(EHInstruction{0, EHInstruction::Type::ALLOCATE,
                                           LLDB_INVALID_REGNUM, 0});
    return;
  }

  // Otherwise model the call's implicit push of the return address, unless the
  // unwind codes already described it.
  for (const EHInstruction &i : m_program)
    if (i.reg == m_policy.pushed_pc_regnum)
      return;

  m_program.emplace_back(EHInstruction{
      0, EHInstruction::Type::PUSH_REGISTER, m_policy.pushed_pc_regnum, 8});
}

bool EHProgramBuilder::ParseBigOrScaledFrameOffset(uint32_t &result, bool big,
                                                   uint32_t scale) {
  if (big) {
    if (!ParseBigFrameOffset(result))
      return false;
  } else {
    if (!ParseFrameOffset(result))
      return false;

    result *= scale;
  }

  return true;
}

bool EHProgramBuilder::ParseBigFrameOffset(uint32_t &result) {
  if (!m_iterator.GetNext())
    return false;

  result = m_iterator.GetUnwindCode()->FrameOffset;

  if (!m_iterator.GetNext())
    return false;

  result += static_cast<uint32_t>(m_iterator.GetUnwindCode()->FrameOffset)
            << 16;

  return true;
}

bool EHProgramBuilder::ParseFrameOffset(uint32_t &result) {
  if (!m_iterator.GetNext())
    return false;

  result = m_iterator.GetUnwindCode()->FrameOffset;

  return true;
}

class EHProgramRange {
public:
  EHProgramRange(EHProgram::const_iterator begin, EHProgram::const_iterator end,
                 const EHArchPolicy &policy);

  UnwindPlan::Row BuildUnwindPlanRow() const;

private:
  int32_t GetCFAFrameOffset() const;

  EHProgram::const_iterator m_begin;
  EHProgram::const_iterator m_end;
  const EHArchPolicy &m_policy;
};

EHProgramRange::EHProgramRange(EHProgram::const_iterator begin,
                               EHProgram::const_iterator end,
                               const EHArchPolicy &policy)
    : m_begin(begin), m_end(end), m_policy(policy) {}

UnwindPlan::Row EHProgramRange::BuildUnwindPlanRow() const {
  UnwindPlan::Row row;

  if (m_begin != m_end)
    row.SetOffset(m_begin->offset);

  int32_t cfa_frame_offset = GetCFAFrameOffset();

  bool frame_pointer_found = false;
  for (EHProgram::const_iterator it = m_begin; it != m_end; ++it) {
    switch (it->type) {
    case EHInstruction::Type::SET_FRAME_POINTER_REGISTER:
      row.GetCFAValue().SetIsRegisterPlusOffset(it->reg, cfa_frame_offset -
                                                             it->frame_offset);
      frame_pointer_found = true;
      break;
    default:
      break;
    }
    if (frame_pointer_found)
      break;
  }
  if (!frame_pointer_found)
    row.GetCFAValue().SetIsRegisterPlusOffset(m_policy.sp_regnum,
                                              cfa_frame_offset);

  if (m_policy.save_base == EHArchPolicy::SPAtSaveInstruction) {
    // Saves are relative to the stack pointer as of the saving instruction, so
    // the running allocation total has to be the one in effect at that point.
    // Walk chronologically, which is backwards through the program. This is
    // what makes a split stack adjustment come out right: the saves happen
    // between the two allocations, so only the first may be counted.
    int32_t sp_below_cfa = 0;
    for (auto it = std::make_reverse_iterator(m_end);
         it != std::make_reverse_iterator(m_begin); ++it) {
      switch (it->type) {
      case EHInstruction::Type::ALLOCATE:
        sp_below_cfa += it->frame_offset;
        break;
      case EHInstruction::Type::SAVE_REGISTER:
        row.SetRegisterLocationToAtCFAPlusOffset(
            it->reg, static_cast<int32_t>(it->frame_offset) - sp_below_cfa,
            false);
        break;
      default:
        break;
      }
    }
  } else {
    int32_t sp_frame_offset = 0;
    for (EHProgram::const_iterator it = m_begin; it != m_end; ++it) {
      switch (it->type) {
      case EHInstruction::Type::PUSH_REGISTER:
        row.SetRegisterLocationToAtCFAPlusOffset(
            it->reg, sp_frame_offset - cfa_frame_offset, false);
        sp_frame_offset += it->frame_offset;
        break;
      case EHInstruction::Type::ALLOCATE:
        sp_frame_offset += it->frame_offset;
        break;
      case EHInstruction::Type::SAVE_REGISTER:
        row.SetRegisterLocationToAtCFAPlusOffset(
            it->reg, it->frame_offset - cfa_frame_offset, false);
        break;
      default:
        break;
      }
    }
  }

  row.SetRegisterLocationToIsCFAPlusOffset(m_policy.sp_regnum, 0, false);

  return row;
}

int32_t EHProgramRange::GetCFAFrameOffset() const {
  int32_t result = 0;

  for (EHProgram::const_iterator it = m_begin; it != m_end; ++it) {
    switch (it->type) {
    case EHInstruction::Type::PUSH_REGISTER:
    case EHInstruction::Type::ALLOCATE:
      result += it->frame_offset;
      break;
    default:
      break;
    }
  }

  return result;
}

PECallFrameInfo::PECallFrameInfo(ObjectFilePECOFF &object_file,
                                 uint32_t exception_dir_rva,
                                 uint32_t exception_dir_size, uint16_t machine)
    : m_object_file(object_file),
      m_exception_dir(object_file.ReadImageDataByRVA(exception_dir_rva,
                                                     exception_dir_size)),
      m_machine(machine) {}

bool PECallFrameInfo::IsSupportedMachine(uint16_t machine) {
  return GetEHArchPolicy(machine) != nullptr;
}

bool PECallFrameInfo::GetAddressRange(Address addr, AddressRange &range) {
  range.Clear();

  const RuntimeFunction *runtime_function =
      FindRuntimeFunctionIntersectsWithRange(AddressRange(addr, 1));
  if (!runtime_function)
    return false;

  range.GetBaseAddress() =
      m_object_file.GetAddress(runtime_function->StartAddress);
  range.SetByteSize(runtime_function->EndAddress -
                    runtime_function->StartAddress);

  return true;
}

std::unique_ptr<UnwindPlan> PECallFrameInfo::GetUnwindPlan(
    llvm::ArrayRef<lldb_private::AddressRange> ranges,
    const lldb_private::Address &addr) {
  // Only continuous functions are supported.
  if (ranges.size() != 1)
    return nullptr;
  const AddressRange &range = ranges[0];

  const RuntimeFunction *runtime_function =
      FindRuntimeFunctionIntersectsWithRange(range);
  if (!runtime_function)
    return nullptr;

  const EHArchPolicy *policy = GetEHArchPolicy(m_machine);
  if (!policy)
    return nullptr;

  auto plan_up = std::make_unique<UnwindPlan>(policy->register_kind);
  plan_up->SetSourceName("PE EH info");
  plan_up->SetSourcedFromCompiler(eLazyBoolYes);
  if (policy->ra_regnum != LLDB_INVALID_REGNUM)
    plan_up->SetReturnAddressRegister(policy->ra_regnum);

  EHProgramBuilder builder(m_object_file, runtime_function->UnwindInfoOffset,
                           *policy);
  if (!builder.Build())
    return nullptr;

  std::vector<UnwindPlan::Row> rows;

  uint32_t last_offset = UINT32_MAX;
  for (auto it = builder.GetProgram().begin(); it != builder.GetProgram().end();
       ++it) {
    if (it->offset == last_offset)
      continue;

    EHProgramRange program_range =
        EHProgramRange(it, builder.GetProgram().end(), *policy);
    rows.push_back(program_range.BuildUnwindPlanRow());

    last_offset = it->offset;
  }

  for (auto it = rows.rbegin(); it != rows.rend(); ++it)
    plan_up->AppendRow(std::move(*it));

  plan_up->SetPlanValidAddressRanges({AddressRange(
      m_object_file.GetAddress(runtime_function->StartAddress),
      runtime_function->EndAddress - runtime_function->StartAddress)});
  plan_up->SetUnwindPlanValidAtAllInstructions(eLazyBoolNo);

  return plan_up;
}

const RuntimeFunction *PECallFrameInfo::FindRuntimeFunctionIntersectsWithRange(
    const AddressRange &range) const {
  uint32_t rva = m_object_file.GetRVA(range.GetBaseAddress());
  addr_t size = range.GetByteSize();

  uint32_t begin = 0;
  uint32_t end = m_exception_dir.GetByteSize() / sizeof(RuntimeFunction);
  while (begin < end) {
    uint32_t curr = (begin + end) / 2;

    offset_t offset = curr * sizeof(RuntimeFunction);
    const auto *runtime_function =
        TypedRead<RuntimeFunction>(m_exception_dir, offset);
    if (!runtime_function)
      break;

    if (runtime_function->StartAddress < rva + size &&
        runtime_function->EndAddress > rva)
      return runtime_function;

    if (runtime_function->StartAddress >= rva + size)
      end = curr;

    if (runtime_function->EndAddress <= rva)
      begin = curr + 1;
  }

  return nullptr;
}
