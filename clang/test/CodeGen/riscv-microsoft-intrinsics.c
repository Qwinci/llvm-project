// RUN: %clang_cc1 -triple riscv64-windows -Wno-implicit-function-declaration -fms-compatibility -emit-llvm -o - %s \
// RUN:    | FileCheck %s --check-prefix=CHECK-MSVC

// RUN: not %clang_cc1 -triple riscv64-linux -Werror -S -o /dev/null %s 2>&1 \
// RUN:    | FileCheck %s -check-prefix CHECK-LINUX

unsigned char test_BitScanForward(unsigned long *Index, unsigned long Mask) {
  return _BitScanForward(Index, Mask);
}

// CHECK-MSVC-LABEL: define {{.*}} i8 @test_BitScanForward(ptr {{.*}}, i32 {{.*}})
// CHECK-MSVC: @llvm.cttz.i32
// CHECK-LINUX: error: call to undeclared function '_BitScanForward'

unsigned char test_BitScanReverse64(unsigned long *Index, unsigned long long Mask) {
  return _BitScanReverse64(Index, Mask);
}

// CHECK-MSVC-LABEL: define {{.*}} i8 @test_BitScanReverse64(ptr {{.*}}, i64 {{.*}})
// CHECK-MSVC: @llvm.ctlz.i64
// CHECK-LINUX: error: call to undeclared function '_BitScanReverse64'

long long test_InterlockedExchangeAdd64(long long volatile *Addend, long long Value) {
  return _InterlockedExchangeAdd64(Addend, Value);
}

// CHECK-MSVC-LABEL: define {{.*}} i64 @test_InterlockedExchangeAdd64(ptr {{.*}}, i64 {{.*}})
// CHECK-MSVC: atomicrmw add ptr {{.*}}, i64 {{.*}} seq_cst
// CHECK-LINUX: error: call to undeclared function '_InterlockedExchangeAdd64'

long long test_InterlockedOr64(long long volatile *Value, long long Mask) {
  return _InterlockedOr64(Value, Mask);
}

// CHECK-MSVC-LABEL: define {{.*}} i64 @test_InterlockedOr64(ptr {{.*}}, i64 {{.*}})
// CHECK-MSVC: atomicrmw or ptr {{.*}}, i64 {{.*}} seq_cst
// CHECK-LINUX: error: call to undeclared function '_InterlockedOr64'

long long test_InterlockedIncrement64(long long volatile *Addend) {
  return _InterlockedIncrement64(Addend);
}

// CHECK-MSVC-LABEL: define {{.*}} i64 @test_InterlockedIncrement64(ptr {{.*}})
// CHECK-MSVC: atomicrmw add ptr {{.*}}, i64 1 seq_cst
// CHECK-LINUX: error: call to undeclared function '_InterlockedIncrement64'

unsigned char test_InterlockedCompareExchange128(long long volatile *Destination,
                                                 long long ExchangeHigh,
                                                 long long ExchangeLow,
                                                 long long *ComparandResult) {
  return _InterlockedCompareExchange128(Destination, ExchangeHigh, ExchangeLow,
                                        ComparandResult);
}

// CHECK-MSVC-LABEL: define {{.*}} i8 @test_InterlockedCompareExchange128(ptr {{.*}})
// CHECK-MSVC: cmpxchg volatile ptr {{.*}}, i128 {{.*}}, i128 {{.*}} seq_cst seq_cst
// CHECK-LINUX: error: call to undeclared function '_InterlockedCompareExchange128'

void test_ReadWriteBarrier(void) {
  _ReadWriteBarrier();
}

// CHECK-MSVC-LABEL: define {{.*}} void @test_ReadWriteBarrier()
// CHECK-MSVC: fence syncscope("singlethread") seq_cst
// CHECK-LINUX: error: call to undeclared function '_ReadWriteBarrier'

void test_fastfail(void) {
  __fastfail(4);
}

// CHECK-MSVC-LABEL: define {{.*}} void @test_fastfail()
// CHECK-MSVC: call void asm sideeffect "ebreak", "{a0}"(i32 4)

long long test_InterlockedExchangeAdd64_acq(long long volatile *Addend, long long Value) {
  return _InterlockedExchangeAdd64_acq(Addend, Value);
}

// CHECK-MSVC-LABEL: define {{.*}} i64 @test_InterlockedExchangeAdd64_acq(ptr {{.*}}, i64 {{.*}})
// CHECK-MSVC: atomicrmw add ptr {{.*}}, i64 {{.*}} acquire
// CHECK-LINUX: error: call to undeclared function '_InterlockedExchangeAdd64_acq'

long test_InterlockedAdd(long volatile *Addend, long Value) {
  return _InterlockedAdd(Addend, Value);
}

// CHECK-MSVC-LABEL: define {{.*}} i32 @test_InterlockedAdd(ptr {{.*}}, i32 {{.*}})
// CHECK-MSVC: %[[OLD:.*]] = atomicrmw add ptr {{.*}}, i32 {{.*}} seq_cst
// CHECK-MSVC: add i32 %[[OLD]],
// CHECK-LINUX: error: call to undeclared function '_InterlockedAdd'

long long test_mulh(long long a, long long b) {
  return __mulh(a, b);
}

// CHECK-MSVC-LABEL: define {{.*}} i64 @test_mulh(i64 {{.*}}, i64 {{.*}})
// CHECK-MSVC: mul nsw i128
// CHECK-LINUX: error: call to undeclared function '__mulh'

void *test_AddressOfReturnAddress(void) {
  return _AddressOfReturnAddress();
}

// CHECK-MSVC-LABEL: define {{.*}} ptr @test_AddressOfReturnAddress()
// CHECK-MSVC: call ptr @llvm.addressofreturnaddress.p0()
// CHECK-LINUX: error: call to undeclared function '_AddressOfReturnAddress'

float test_CopyFloatFromInt32(long i) {
  return _CopyFloatFromInt32(i);
}

// CHECK-MSVC-LABEL: define {{.*}} float @test_CopyFloatFromInt32(i32 {{.*}})
// CHECK-MSVC: bitcast i32 {{.*}} to float
// CHECK-LINUX: error: call to undeclared function '_CopyFloatFromInt32'

unsigned test_CountLeadingZeros64(unsigned long long x) {
  return _CountLeadingZeros64(x);
}

// CHECK-MSVC-LABEL: define {{.*}} i32 @test_CountLeadingZeros64(i64 {{.*}})
// CHECK-MSVC: call i64 @llvm.ctlz.i64(i64 {{.*}}, i1 false)
// CHECK-LINUX: error: call to undeclared function '_CountLeadingZeros64'

unsigned test_CountLeadingSigns(long x) {
  return _CountLeadingSigns(x);
}

// CHECK-MSVC-LABEL: define {{.*}} i32 @test_CountLeadingSigns(i32 {{.*}})
// CHECK-MSVC: call i32 @llvm.ctlz.i32(i32 {{.*}}, i1 false)
// CHECK-LINUX: error: call to undeclared function '_CountLeadingSigns'

unsigned test_CountOneBits(unsigned long x) {
  return _CountOneBits(x);
}

// CHECK-MSVC-LABEL: define {{.*}} i32 @test_CountOneBits(i32 {{.*}})
// CHECK-MSVC: call i32 @llvm.ctpop.i32(i32 {{.*}})
// CHECK-LINUX: error: call to undeclared function '_CountOneBits'
