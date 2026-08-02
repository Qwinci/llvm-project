// Check that CodeView variable locations use the RISCV64 register ids from
// CodeViewRegisters.def, mapped by RISCV_MC::initLLVMToCVRegMapping. Without
// that mapping, MCRegisterInfo::getCodeViewRegNum aborts with "target does not
// implement codeview register mapping".
//
// RUN: %clang_cc1 %s -triple riscv64-pc-windows-msvc -target-feature +d \
// RUN:   -debug-info-kind=limited -gcodeview -O0 -S -o - | FileCheck %s --check-prefix=O0
// RUN: %clang_cc1 %s -triple riscv64-pc-windows-msvc -target-feature +d \
// RUN:   -debug-info-kind=limited -gcodeview -O2 -S -o - | FileCheck %s --check-prefix=O2

int add(int a, int b) {
  int c = a + b;
  return c;
}

// At -O0 a frame pointer is present, so locals are frame-pointer-relative. This
// still exercises the register-mapping path (getCodeViewRegNum on the declared
// frame register), which is what previously aborted with "target does not
// implement codeview register mapping".
// O0: add:
// O0: .cv_def_range {{.*}}, frame_ptr_rel,

double addd(double a, double b) {
  double c = a + b;
  return c;
}

// At -O2 the arguments stay enregistered for their whole live range: fa0/fa1
// are RISCV64_F10/F11 (CV ids 60/61).
// O2: addd:
// O2: .cv_def_range {{.*}}, reg, 60
// O2: .cv_def_range {{.*}}, reg, 61

float addf(float a, float b) {
  float c = a + b;
  return c;
}

// A single-precision argument uses the F#_F width view of the FPR, a distinct
// MCRegister from the F#_D view used above. It must map to the same CodeView id
// (fa0/fa1 = RISCV64_F10/F11 = 60/61); mapping only the _D view aborted here
// with "unknown codeview register F11_F". Anchor on the debug-section function
// name (not the .text "addf:" label) because all cv_def_range records follow
// every function's code, so a .text label can't anchor them in FileCheck order.
// O2: .asciz "addf"
// O2: .cv_def_range {{.*}}, reg, 60
// O2: .cv_def_range {{.*}}, reg, 61
