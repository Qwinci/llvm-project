; RUN: llc -mtriple=riscv64-w64-windows-gnu -relocation-model=pic < %s | FileCheck %s

; On COFF/Windows a reference to a non-dso-local global goes through an
; indirection a loader or the mingw auto-import runtime-pseudo-relocation
; mechanism can patch, because COFF has no GOT and the direct
; %pcrel_hi/%pcrel_lo pair cannot be relocated at load time:
;   - a plain external global -> a local .refptr.<sym> pointer
;   - a dllimport global      -> __imp_<sym> in the import address table
; A dso_local global keeps the direct %pcrel_hi/%pcrel_lo addressing.

@localvar = dso_local global i32 7, align 4
@extvar = external global i32, align 4
@dllvar = external dllimport global i32, align 4

; CHECK-LABEL: a_ext:
; CHECK:      .Lpcrel_hi0:
; CHECK-NEXT:   auipc a0, %pcrel_hi(.refptr.extvar)
; CHECK-NEXT:   ld a0, %pcrel_lo(.Lpcrel_hi0)(a0)
; CHECK-NEXT:   ret
define dso_local ptr @a_ext() {
  ret ptr @extvar
}

; CHECK-LABEL: a_dll:
; CHECK:      .Lpcrel_hi1:
; CHECK-NEXT:   auipc a0, %pcrel_hi(__imp_dllvar)
; CHECK-NEXT:   ld a0, %pcrel_lo(.Lpcrel_hi1)(a0)
; CHECK-NEXT:   ret
define dso_local ptr @a_dll() {
  ret ptr @dllvar
}

; CHECK-LABEL: a_loc:
; CHECK:      .Lpcrel_hi2:
; CHECK-NEXT:   auipc a0, %pcrel_hi(localvar)
; CHECK-NEXT:   addi a0, a0, %pcrel_lo(.Lpcrel_hi2)
; CHECK-NEXT:   ret
define dso_local ptr @a_loc() {
  ret ptr @localvar
}

; The .refptr.extvar stub is emitted as a discardable COMDAT pointer holding the
; symbol's address (an ADDR64 the runtime pseudo-relocation mechanism patches).
; __imp_dllvar needs no stub -- it lives in the import address table.
; CHECK:      .section .rdata$.refptr.extvar,"dr",discard,.refptr.extvar
; CHECK:      .globl .refptr.extvar
; CHECK:    .refptr.extvar:
; CHECK-NEXT:   .quad extvar
