//===-- PECallFrameInfo.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_OBJECTFILE_PECOFF_PECALLFRAMEINFO_H
#define LLDB_SOURCE_PLUGINS_OBJECTFILE_PECOFF_PECALLFRAMEINFO_H

#include "lldb/Core/AddressRange.h"
#include "lldb/Symbol/CallFrameInfo.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Utility/DataExtractor.h"

class ObjectFilePECOFF;

namespace llvm {
namespace Win64EH {

struct RuntimeFunction;

}
} // namespace llvm

class PECallFrameInfo : public virtual lldb_private::CallFrameInfo {
public:
  /// \param machine the COFF machine type of \p object_file, which selects how
  /// the unwind codes are decoded. Use IsSupportedMachine to check that a
  /// machine type can be decoded before constructing.
  explicit PECallFrameInfo(ObjectFilePECOFF &object_file,
                           uint32_t exception_dir_rva,
                           uint32_t exception_dir_size, uint16_t machine);

  /// Whether unwind info for \p machine can be decoded.
  static bool IsSupportedMachine(uint16_t machine);

  bool GetAddressRange(lldb_private::Address addr,
                       lldb_private::AddressRange &range) override;

  std::unique_ptr<lldb_private::UnwindPlan>
  GetUnwindPlan(const lldb_private::Address &addr) override {
    return GetUnwindPlan({lldb_private::AddressRange(addr, 1)}, addr);
  }

  std::unique_ptr<lldb_private::UnwindPlan>
  GetUnwindPlan(llvm::ArrayRef<lldb_private::AddressRange> ranges,
                const lldb_private::Address &addr) override;

private:
  const llvm::Win64EH::RuntimeFunction *FindRuntimeFunctionIntersectsWithRange(
      const lldb_private::AddressRange &range) const;

  ObjectFilePECOFF &m_object_file;
  lldb_private::DataExtractor m_exception_dir;
  uint16_t m_machine;
};

#endif // LLDB_SOURCE_PLUGINS_OBJECTFILE_PECOFF_PECALLFRAMEINFO_H
