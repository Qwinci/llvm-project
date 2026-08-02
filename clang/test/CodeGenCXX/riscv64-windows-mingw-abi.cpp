// A GNU (mingw) environment must use the Itanium C++ ABI, like every other
// mingw target; only the msvc environment uses the Microsoft C++ ABI.
//
// RUN: %clang_cc1 %s -triple riscv64-w64-windows-gnu -target-feature +d -emit-llvm -o - | FileCheck %s --check-prefix=ITANIUM
// RUN: %clang_cc1 %s -triple riscv64-pc-windows-msvc -target-feature +d -emit-llvm -o - | FileCheck %s --check-prefix=MSVC

struct Foo {
  virtual void bar();
};
void Foo::bar() {}

// ITANIUM: define{{.*}} void @_ZN3Foo3barEv

// MSVC: define{{.*}} void @"?bar@Foo@@UEAAXXZ"
