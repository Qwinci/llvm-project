//==- BuiltinsRISCV.td - RISC-V Builtin function database ---*- tablegen -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the RISC-V-specific builtin function database.
//
//===----------------------------------------------------------------------===//

include "clang/Basic/BuiltinsBase.td"

class RISCVBuiltin<string prototype, string features = ""> : TargetBuiltin {
  let Spellings = ["__builtin_riscv_" # NAME];
  let Prototype = prototype;
  let Features = features;
}

class RISCVLibBuiltin<string prototype> : TargetLibBuiltin {
  let Spellings = [NAME];
  let Prototype = prototype;
}

let Attributes = [NoThrow, Const] in {
//===----------------------------------------------------------------------===//
// Zbb extension.
//===----------------------------------------------------------------------===//
def orc_b_32 : RISCVBuiltin<"unsigned int(unsigned int)", "zbb">;
def orc_b_64 : RISCVBuiltin<"uint64_t(uint64_t)", "zbb,64bit">;
def clz_32 : RISCVBuiltin<"unsigned int(unsigned int)", "zbb|xtheadbb">;
def clz_64 : RISCVBuiltin<"unsigned int(uint64_t)", "zbb|xtheadbb,64bit">;
def ctz_32 : RISCVBuiltin<"unsigned int(unsigned int)", "zbb">;
def ctz_64 : RISCVBuiltin<"unsigned int(uint64_t)", "zbb,64bit">;

//===----------------------------------------------------------------------===//
// Zbc or Zbkc extension.
//===----------------------------------------------------------------------===//
def clmul_32 : RISCVBuiltin<"unsigned int(unsigned int, unsigned int)", "zbc|zbkc">;
def clmul_64 : RISCVBuiltin<"uint64_t(uint64_t, uint64_t)", "zbc|zbkc,64bit">;
def clmulh_32 : RISCVBuiltin<"unsigned int(unsigned int, unsigned int)", "zbc|zbkc,32bit">;
def clmulh_64 : RISCVBuiltin<"uint64_t(uint64_t, uint64_t)", "zbc|zbkc,64bit">;
def clmulr_32 : RISCVBuiltin<"unsigned int(unsigned int, unsigned int)", "zbc,32bit">;
def clmulr_64 : RISCVBuiltin<"uint64_t(uint64_t, uint64_t)", "zbc,64bit">;

//===----------------------------------------------------------------------===//
// Zbkx extension.
//===----------------------------------------------------------------------===//
let Features = "zbkx,32bit" in {
def xperm4_32 : RISCVBuiltin<"unsigned int(unsigned int, unsigned int)">;
def xperm8_32 : RISCVBuiltin<"unsigned int(unsigned int, unsigned int)">;
} // Features = "zbkx,32bit"

let Features = "zbkx,64bit" in {
def xperm4_64 : RISCVBuiltin<"uint64_t(uint64_t, uint64_t)">;
def xperm8_64 : RISCVBuiltin<"uint64_t(uint64_t, uint64_t)">;
} // Features = "zbkx,64bit"

//===----------------------------------------------------------------------===//
// Zbkb extension.
//===----------------------------------------------------------------------===//
def brev8_32 : RISCVBuiltin<"unsigned int(unsigned int)", "zbkb">;
def brev8_64 : RISCVBuiltin<"uint64_t(uint64_t)", "zbkb,64bit">;
def zip_32 : RISCVBuiltin<"unsigned int(unsigned int)", "zbkb,32bit">;
def unzip_32 : RISCVBuiltin<"unsigned int(unsigned int)", "zbkb,32bit">;

//===----------------------------------------------------------------------===//
// Zknd extension.
//===----------------------------------------------------------------------===//
let Features = "zknd,32bit" in {
def aes32dsi : RISCVBuiltin<"unsigned int(unsigned int, unsigned int, _Constant unsigned int)">;
def aes32dsmi : RISCVBuiltin<"unsigned int(unsigned int, unsigned int, _Constant unsigned int)">;
} // Features = "zknd,32bit"

let Features = "zknd,64bit" in {
def aes64ds : RISCVBuiltin<"uint64_t(uint64_t, uint64_t)">;
def aes64dsm : RISCVBuiltin<"uint64_t(uint64_t, uint64_t)">;
def aes64im : RISCVBuiltin<"uint64_t(uint64_t)">;
} // Features = "zknd,64bit"

//===----------------------------------------------------------------------===//
// Zknd & Zkne extension.
//===----------------------------------------------------------------------===//
let Features = "zknd|zkne,64bit" in {
def aes64ks1i : RISCVBuiltin<"uint64_t(uint64_t, _Constant unsigned int)">;
def aes64ks2 : RISCVBuiltin<"uint64_t(uint64_t, uint64_t)">;
} // Features = "zknd|zkne,64bit"

//===----------------------------------------------------------------------===//
// Zkne extension.
//===----------------------------------------------------------------------===//
let Features = "zkne,32bit" in {
def aes32esi : RISCVBuiltin<"unsigned int(unsigned int, unsigned int, _Constant unsigned int)">;
def aes32esmi : RISCVBuiltin<"unsigned int(unsigned int, unsigned int, _Constant unsigned int)">;
} // Features = "zkne,32bit"

let Features = "zkne,64bit" in {
def aes64es : RISCVBuiltin<"uint64_t(uint64_t, uint64_t)">;
def aes64esm : RISCVBuiltin<"uint64_t(uint64_t, uint64_t)">;
} // Features = "zkne,64bit"

//===----------------------------------------------------------------------===//
// Zknh extension.
//===----------------------------------------------------------------------===//
let Features = "zknh" in {
def sha256sig0 : RISCVBuiltin<"unsigned int(unsigned int)">;
def sha256sig1 : RISCVBuiltin<"unsigned int(unsigned int)">;
def sha256sum0 : RISCVBuiltin<"unsigned int(unsigned int)">;
def sha256sum1 : RISCVBuiltin<"unsigned int(unsigned int)">;
} // Features = "zknh"

let Features = "zknh,32bit" in {
def sha512sig0h : RISCVBuiltin<"unsigned int(unsigned int, unsigned int)">;
def sha512sig0l : RISCVBuiltin<"unsigned int(unsigned int, unsigned int)">;
def sha512sig1h : RISCVBuiltin<"unsigned int(unsigned int, unsigned int)">;
def sha512sig1l : RISCVBuiltin<"unsigned int(unsigned int, unsigned int)">;
def sha512sum0r : RISCVBuiltin<"unsigned int(unsigned int, unsigned int)">;
def sha512sum1r : RISCVBuiltin<"unsigned int(unsigned int, unsigned int)">;
} // Features = "zknh,32bit"

let Features = "zknh,64bit" in {
def sha512sig0 : RISCVBuiltin<"uint64_t(uint64_t)">;
def sha512sig1 : RISCVBuiltin<"uint64_t(uint64_t)">;
def sha512sum0 : RISCVBuiltin<"uint64_t(uint64_t)">;
def sha512sum1 : RISCVBuiltin<"uint64_t(uint64_t)">;
} // Features = "zknh,64bit"

//===----------------------------------------------------------------------===//
// Zksed extension.
//===----------------------------------------------------------------------===//
let Features = "zksed" in {
def sm4ed : RISCVBuiltin<"unsigned int(unsigned int, unsigned int, _Constant unsigned int )">;
def sm4ks : RISCVBuiltin<"unsigned int(unsigned int, unsigned int, _Constant unsigned int)">;
} // Features = "zksed"

//===----------------------------------------------------------------------===//
// Zksh extension.
//===----------------------------------------------------------------------===//
let Features = "zksh" in {
def sm3p0 : RISCVBuiltin<"unsigned int(unsigned int)">;
def sm3p1 : RISCVBuiltin<"unsigned int(unsigned int)">;
} // Features = "zksh"

} // Attributes = [Const, NoThrow]

//===----------------------------------------------------------------------===//
// Zihintntl extension.
//===----------------------------------------------------------------------===//
let Features = "zihintntl", Attributes = [CustomTypeChecking] in {
def ntl_load : RISCVBuiltin<"void(...)">;
def ntl_store : RISCVBuiltin<"void(...)">;
} // Features = "zihintntl", Attributes = [CustomTypeChecking]

//===----------------------------------------------------------------------===//
// Zihintpause extension.
//===----------------------------------------------------------------------===//
let Features = "zihintpause", Attributes = [NoThrow] in
def pause : RISCVBuiltin<"void()">;

//===----------------------------------------------------------------------===//
// MSVC-compatible intrinsics available through <intrin.h>, matching the set
// provided on other Windows targets (e.g. AArch64, X86).
//===----------------------------------------------------------------------===//
let Header = "intrin.h", Languages = "ALL_MS_LANGUAGES",
    Attributes = [NoThrow, RequireDeclaration] in {
  def _BitScanForward : RISCVLibBuiltin<"unsigned char(msuint32_t *, msuint32_t)">;
  def _BitScanReverse : RISCVLibBuiltin<"unsigned char(msuint32_t *, msuint32_t)">;
  def _BitScanForward64 : RISCVLibBuiltin<"unsigned char(msuint32_t *, unsigned long long int)">;
  def _BitScanReverse64 : RISCVLibBuiltin<"unsigned char(msuint32_t *, unsigned long long int)">;

  def _InterlockedAnd64 : RISCVLibBuiltin<"long long int(long long int volatile *, long long int)">;
  def _InterlockedOr64 : RISCVLibBuiltin<"long long int(long long int volatile *, long long int)">;
  def _InterlockedXor64 : RISCVLibBuiltin<"long long int(long long int volatile *, long long int)">;
  def _InterlockedExchange64 : RISCVLibBuiltin<"long long int(long long int volatile *, long long int)">;
  def _InterlockedExchangeAdd64 : RISCVLibBuiltin<"long long int(long long int volatile *, long long int)">;
  def _InterlockedExchangeSub64 : RISCVLibBuiltin<"long long int(long long int volatile *, long long int)">;
  def _InterlockedIncrement64 : RISCVLibBuiltin<"long long int(long long int volatile *)">;
  def _InterlockedDecrement64 : RISCVLibBuiltin<"long long int(long long int volatile *)">;

  def _InterlockedCompareExchange128 : RISCVLibBuiltin<"unsigned char(long long int volatile *, long long int, long long int, long long int *)">;

  def _ReadWriteBarrier : RISCVLibBuiltin<"void()">;
  // _InterlockedAnd ordered variants
  def _InterlockedAnd8_acq : RISCVLibBuiltin<"char(char volatile *, char)">;
  def _InterlockedAnd8_nf : RISCVLibBuiltin<"char(char volatile *, char)">;
  def _InterlockedAnd8_rel : RISCVLibBuiltin<"char(char volatile *, char)">;
  def _InterlockedAnd16_acq : RISCVLibBuiltin<"short(short volatile *, short)">;
  def _InterlockedAnd16_nf : RISCVLibBuiltin<"short(short volatile *, short)">;
  def _InterlockedAnd16_rel : RISCVLibBuiltin<"short(short volatile *, short)">;
  def _InterlockedAnd_acq : RISCVLibBuiltin<"msint32_t(msint32_t volatile *, msint32_t)">;
  def _InterlockedAnd_nf : RISCVLibBuiltin<"msint32_t(msint32_t volatile *, msint32_t)">;
  def _InterlockedAnd_rel : RISCVLibBuiltin<"msint32_t(msint32_t volatile *, msint32_t)">;
  def _InterlockedAnd64_acq : RISCVLibBuiltin<"long long int(long long int volatile *, long long int)">;
  def _InterlockedAnd64_nf : RISCVLibBuiltin<"long long int(long long int volatile *, long long int)">;
  def _InterlockedAnd64_rel : RISCVLibBuiltin<"long long int(long long int volatile *, long long int)">;
  // _InterlockedOr ordered variants
  def _InterlockedOr8_acq : RISCVLibBuiltin<"char(char volatile *, char)">;
  def _InterlockedOr8_nf : RISCVLibBuiltin<"char(char volatile *, char)">;
  def _InterlockedOr8_rel : RISCVLibBuiltin<"char(char volatile *, char)">;
  def _InterlockedOr16_acq : RISCVLibBuiltin<"short(short volatile *, short)">;
  def _InterlockedOr16_nf : RISCVLibBuiltin<"short(short volatile *, short)">;
  def _InterlockedOr16_rel : RISCVLibBuiltin<"short(short volatile *, short)">;
  def _InterlockedOr_acq : RISCVLibBuiltin<"msint32_t(msint32_t volatile *, msint32_t)">;
  def _InterlockedOr_nf : RISCVLibBuiltin<"msint32_t(msint32_t volatile *, msint32_t)">;
  def _InterlockedOr_rel : RISCVLibBuiltin<"msint32_t(msint32_t volatile *, msint32_t)">;
  def _InterlockedOr64_acq : RISCVLibBuiltin<"long long int(long long int volatile *, long long int)">;
  def _InterlockedOr64_nf : RISCVLibBuiltin<"long long int(long long int volatile *, long long int)">;
  def _InterlockedOr64_rel : RISCVLibBuiltin<"long long int(long long int volatile *, long long int)">;
  // _InterlockedXor ordered variants
  def _InterlockedXor8_acq : RISCVLibBuiltin<"char(char volatile *, char)">;
  def _InterlockedXor8_nf : RISCVLibBuiltin<"char(char volatile *, char)">;
  def _InterlockedXor8_rel : RISCVLibBuiltin<"char(char volatile *, char)">;
  def _InterlockedXor16_acq : RISCVLibBuiltin<"short(short volatile *, short)">;
  def _InterlockedXor16_nf : RISCVLibBuiltin<"short(short volatile *, short)">;
  def _InterlockedXor16_rel : RISCVLibBuiltin<"short(short volatile *, short)">;
  def _InterlockedXor_acq : RISCVLibBuiltin<"msint32_t(msint32_t volatile *, msint32_t)">;
  def _InterlockedXor_nf : RISCVLibBuiltin<"msint32_t(msint32_t volatile *, msint32_t)">;
  def _InterlockedXor_rel : RISCVLibBuiltin<"msint32_t(msint32_t volatile *, msint32_t)">;
  def _InterlockedXor64_acq : RISCVLibBuiltin<"long long int(long long int volatile *, long long int)">;
  def _InterlockedXor64_nf : RISCVLibBuiltin<"long long int(long long int volatile *, long long int)">;
  def _InterlockedXor64_rel : RISCVLibBuiltin<"long long int(long long int volatile *, long long int)">;
  // _InterlockedExchange ordered variants
  def _InterlockedExchange8_acq : RISCVLibBuiltin<"char(char volatile *, char)">;
  def _InterlockedExchange8_nf : RISCVLibBuiltin<"char(char volatile *, char)">;
  def _InterlockedExchange8_rel : RISCVLibBuiltin<"char(char volatile *, char)">;
  def _InterlockedExchange16_acq : RISCVLibBuiltin<"short(short volatile *, short)">;
  def _InterlockedExchange16_nf : RISCVLibBuiltin<"short(short volatile *, short)">;
  def _InterlockedExchange16_rel : RISCVLibBuiltin<"short(short volatile *, short)">;
  def _InterlockedExchange_acq : RISCVLibBuiltin<"msint32_t(msint32_t volatile *, msint32_t)">;
  def _InterlockedExchange_nf : RISCVLibBuiltin<"msint32_t(msint32_t volatile *, msint32_t)">;
  def _InterlockedExchange_rel : RISCVLibBuiltin<"msint32_t(msint32_t volatile *, msint32_t)">;
  def _InterlockedExchange64_acq : RISCVLibBuiltin<"long long int(long long int volatile *, long long int)">;
  def _InterlockedExchange64_nf : RISCVLibBuiltin<"long long int(long long int volatile *, long long int)">;
  def _InterlockedExchange64_rel : RISCVLibBuiltin<"long long int(long long int volatile *, long long int)">;
  // _InterlockedExchangeAdd ordered variants
  def _InterlockedExchangeAdd8_acq : RISCVLibBuiltin<"char(char volatile *, char)">;
  def _InterlockedExchangeAdd8_nf : RISCVLibBuiltin<"char(char volatile *, char)">;
  def _InterlockedExchangeAdd8_rel : RISCVLibBuiltin<"char(char volatile *, char)">;
  def _InterlockedExchangeAdd16_acq : RISCVLibBuiltin<"short(short volatile *, short)">;
  def _InterlockedExchangeAdd16_nf : RISCVLibBuiltin<"short(short volatile *, short)">;
  def _InterlockedExchangeAdd16_rel : RISCVLibBuiltin<"short(short volatile *, short)">;
  def _InterlockedExchangeAdd_acq : RISCVLibBuiltin<"msint32_t(msint32_t volatile *, msint32_t)">;
  def _InterlockedExchangeAdd_nf : RISCVLibBuiltin<"msint32_t(msint32_t volatile *, msint32_t)">;
  def _InterlockedExchangeAdd_rel : RISCVLibBuiltin<"msint32_t(msint32_t volatile *, msint32_t)">;
  def _InterlockedExchangeAdd64_acq : RISCVLibBuiltin<"long long int(long long int volatile *, long long int)">;
  def _InterlockedExchangeAdd64_nf : RISCVLibBuiltin<"long long int(long long int volatile *, long long int)">;
  def _InterlockedExchangeAdd64_rel : RISCVLibBuiltin<"long long int(long long int volatile *, long long int)">;
  // _InterlockedAdd (returns new value)
  def _InterlockedAdd : RISCVLibBuiltin<"msint32_t(msint32_t volatile *, msint32_t)">;
  def _InterlockedAdd_acq : RISCVLibBuiltin<"msint32_t(msint32_t volatile *, msint32_t)">;
  def _InterlockedAdd_nf : RISCVLibBuiltin<"msint32_t(msint32_t volatile *, msint32_t)">;
  def _InterlockedAdd_rel : RISCVLibBuiltin<"msint32_t(msint32_t volatile *, msint32_t)">;
  def _InterlockedAdd64 : RISCVLibBuiltin<"long long int(long long int volatile *, long long int)">;
  def _InterlockedAdd64_acq : RISCVLibBuiltin<"long long int(long long int volatile *, long long int)">;
  def _InterlockedAdd64_nf : RISCVLibBuiltin<"long long int(long long int volatile *, long long int)">;
  def _InterlockedAdd64_rel : RISCVLibBuiltin<"long long int(long long int volatile *, long long int)">;
  // _InterlockedIncrement ordered variants
  def _InterlockedIncrement16_acq : RISCVLibBuiltin<"short(short volatile *)">;
  def _InterlockedIncrement16_nf : RISCVLibBuiltin<"short(short volatile *)">;
  def _InterlockedIncrement16_rel : RISCVLibBuiltin<"short(short volatile *)">;
  def _InterlockedIncrement_acq : RISCVLibBuiltin<"msint32_t(msint32_t volatile *)">;
  def _InterlockedIncrement_nf : RISCVLibBuiltin<"msint32_t(msint32_t volatile *)">;
  def _InterlockedIncrement_rel : RISCVLibBuiltin<"msint32_t(msint32_t volatile *)">;
  def _InterlockedIncrement64_acq : RISCVLibBuiltin<"long long int(long long int volatile *)">;
  def _InterlockedIncrement64_nf : RISCVLibBuiltin<"long long int(long long int volatile *)">;
  def _InterlockedIncrement64_rel : RISCVLibBuiltin<"long long int(long long int volatile *)">;
  // _InterlockedDecrement ordered variants
  def _InterlockedDecrement16_acq : RISCVLibBuiltin<"short(short volatile *)">;
  def _InterlockedDecrement16_nf : RISCVLibBuiltin<"short(short volatile *)">;
  def _InterlockedDecrement16_rel : RISCVLibBuiltin<"short(short volatile *)">;
  def _InterlockedDecrement_acq : RISCVLibBuiltin<"msint32_t(msint32_t volatile *)">;
  def _InterlockedDecrement_nf : RISCVLibBuiltin<"msint32_t(msint32_t volatile *)">;
  def _InterlockedDecrement_rel : RISCVLibBuiltin<"msint32_t(msint32_t volatile *)">;
  def _InterlockedDecrement64_acq : RISCVLibBuiltin<"long long int(long long int volatile *)">;
  def _InterlockedDecrement64_nf : RISCVLibBuiltin<"long long int(long long int volatile *)">;
  def _InterlockedDecrement64_rel : RISCVLibBuiltin<"long long int(long long int volatile *)">;
  // _InterlockedCompareExchange ordered variants
  def _InterlockedCompareExchange8_acq : RISCVLibBuiltin<"char(char volatile *, char, char)">;
  def _InterlockedCompareExchange8_nf : RISCVLibBuiltin<"char(char volatile *, char, char)">;
  def _InterlockedCompareExchange8_rel : RISCVLibBuiltin<"char(char volatile *, char, char)">;
  def _InterlockedCompareExchange16_acq : RISCVLibBuiltin<"short(short volatile *, short, short)">;
  def _InterlockedCompareExchange16_nf : RISCVLibBuiltin<"short(short volatile *, short, short)">;
  def _InterlockedCompareExchange16_rel : RISCVLibBuiltin<"short(short volatile *, short, short)">;
  def _InterlockedCompareExchange_acq : RISCVLibBuiltin<"msint32_t(msint32_t volatile *, msint32_t, msint32_t)">;
  def _InterlockedCompareExchange_nf : RISCVLibBuiltin<"msint32_t(msint32_t volatile *, msint32_t, msint32_t)">;
  def _InterlockedCompareExchange_rel : RISCVLibBuiltin<"msint32_t(msint32_t volatile *, msint32_t, msint32_t)">;
  def _InterlockedCompareExchange64_acq : RISCVLibBuiltin<"long long int(long long int volatile *, long long int, long long int)">;
  def _InterlockedCompareExchange64_nf : RISCVLibBuiltin<"long long int(long long int volatile *, long long int, long long int)">;
  def _InterlockedCompareExchange64_rel : RISCVLibBuiltin<"long long int(long long int volatile *, long long int, long long int)">;
  // pointer exchange/compare-exchange ordered variants
  def _InterlockedExchangePointer_acq : RISCVLibBuiltin<"void *(void *volatile *, void *)">;
  def _InterlockedExchangePointer_nf : RISCVLibBuiltin<"void *(void *volatile *, void *)">;
  def _InterlockedExchangePointer_rel : RISCVLibBuiltin<"void *(void *volatile *, void *)">;
  def _InterlockedCompareExchangePointer_acq : RISCVLibBuiltin<"void *(void *volatile *, void *, void *)">;
  def _InterlockedCompareExchangePointer_rel : RISCVLibBuiltin<"void *(void *volatile *, void *, void *)">;
  // _InterlockedCompareExchange128 ordered variants
  def _InterlockedCompareExchange128_acq : RISCVLibBuiltin<"unsigned char(long long int volatile *, long long int, long long int, long long int *)">;
  def _InterlockedCompareExchange128_nf : RISCVLibBuiltin<"unsigned char(long long int volatile *, long long int, long long int, long long int *)">;
  def _InterlockedCompareExchange128_rel : RISCVLibBuiltin<"unsigned char(long long int volatile *, long long int, long long int, long long int *)">;
  // High multiply, bit counting, bit copy, return address
  def __mulh : RISCVLibBuiltin<"long long int(long long int, long long int)">;
  def __umulh : RISCVLibBuiltin<"unsigned long long int(unsigned long long int, unsigned long long int)">;
  def _AddressOfReturnAddress : RISCVLibBuiltin<"void *()">;
  def _CopyFloatFromInt32 : RISCVLibBuiltin<"float(int)">;
  def _CopyInt32FromFloat : RISCVLibBuiltin<"int(float)">;
  def _CopyDoubleFromInt64 : RISCVLibBuiltin<"double(long long int)">;
  def _CopyInt64FromDouble : RISCVLibBuiltin<"long long int(double)">;
  def _CountLeadingZeros : RISCVLibBuiltin<"unsigned int(msuint32_t)">;
  def _CountLeadingZeros64 : RISCVLibBuiltin<"unsigned int(unsigned long long int)">;
  def _CountLeadingOnes : RISCVLibBuiltin<"unsigned int(msuint32_t)">;
  def _CountLeadingOnes64 : RISCVLibBuiltin<"unsigned int(unsigned long long int)">;
  def _CountLeadingSigns : RISCVLibBuiltin<"unsigned int(msint32_t)">;
  def _CountLeadingSigns64 : RISCVLibBuiltin<"unsigned int(long long int)">;
  def _CountOneBits : RISCVLibBuiltin<"unsigned int(msuint32_t)">;
  def _CountOneBits64 : RISCVLibBuiltin<"unsigned int(unsigned long long int)">;
}

//===----------------------------------------------------------------------===//
// XCV extensions.
//===----------------------------------------------------------------------===//
include "clang/Basic/BuiltinsRISCVXCV.td"

//===----------------------------------------------------------------------===//
// XAndes extensions.
//===----------------------------------------------------------------------===//
include "clang/Basic/BuiltinsRISCVXAndes.td"

//===----------------------------------------------------------------------===//
// MIPS extensions.
//===----------------------------------------------------------------------===//
include "clang/Basic/BuiltinsRISCVXMIPS.td"
