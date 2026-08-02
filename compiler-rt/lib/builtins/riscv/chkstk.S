//===-- chkstk.S - stack probing routine for riscv64-windows ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "../assembly.h"

// __chkstk routine for riscv64-windows. Called as:
//
//   li   t1, <size in bytes>
//   call __chkstk
//   sub  sp, sp, t1
//
// Input:  t1 = number of bytes of stack about to be allocated by the caller.
// Clobbers: t2, t3, t4. Does not modify sp, t1, or any other register.
//
// Touches one word in every page between the current sp and sp-t1, from high
// addresses to low, so the OS guard page is hit in order instead of being
// skipped over by a single large decrement of sp. As on AArch64, the final
// probe may fall up to one page below the caller's eventual sp.

#if defined(__riscv) && __riscv_xlen == 64

#define PAGE_SIZE 4096

  .p2align 2
DEFINE_COMPILERRT_FUNCTION(__chkstk)
  mv     t2, t1
  mv     t3, sp
  li     t4, -PAGE_SIZE
1:
  add    t3, t3, t4
  add    t2, t2, t4
  ld     zero, 0(t3)
  bgtz   t2, 1b

  ret
END_COMPILERRT_FUNCTION(__chkstk)

#endif // defined(__riscv) && __riscv_xlen == 64
