# RISCV64 Windows (COFF/PE) C++ Exception Handling ABI

**Status: not a Microsoft specification.** Microsoft has never shipped a RISCV64
Windows port and has never published a RISCV64 `__CxxFrameHandler3` contract, a
funclet calling convention, or the frame conventions C++ exception handling relies
on. Everything in this document is LLVM's own definition for the
`riscv64-*-windows-msvc` (and `-gnu`) target. Should a real specification ever be
published, it supersedes this one — do not treat any value here as ABI-stable across
that event.

This document exists so a later engineer can implement the RISCV64
`__CxxFrameHandler3` personality routine (in a CRT, in libc++abi's MSVC path, or in a
standalone runtime) **without needing to read LLVM's source code**. Every field,
register, and offset below is drawn directly from the LLVM implementation and
cross-checked against compiler-generated output; `file:line` references to the LLVM
source are given throughout so behavior can be re-verified against a specific
revision (references are to the `riscv-coff` branch at the time of writing).

## Audience and scope

This document assumes the reader already understands **x86_64 Windows C++ EH** — the
`__CxxFrameHandler3` funclet model, the `FuncInfo`/`$cppxdata` tables, two-phase
unwinding, and MSVC state numbering. If you need that background, study the layout of
`FuncInfo`, `TryBlockMapEntry`, `HandlerType`, `UnwindMapEntry`, and `IptoStateMapEntry`
as emitted for x64/ARM64; the RISCV64 tables use the **same target-independent
`WinException` emitter** (`llvm/lib/CodeGen/AsmPrinter/WinException.cpp`), so their
byte layout is identical to x64/ARM64. What is RISCV64-specific — and what this
document pins down — is the **frame contract**: what `EstablisherFrame` means, where
the catch objects and `UnwindHelp` slot live, how a funclet is entered, and what a
funclet returns.

For the orthogonal **SEH unwind info** (`.pdata`/`.xdata`, `RUNTIME_FUNCTION`,
`UNWIND_INFO`, unwind opcodes) that every function — including every EH funclet —
carries, see the companion document [RISCVWinCFI](RISCVWinCFI.md). This document
depends on that one: the personality recovers register state (including the frame
pointer `s0`) by virtually unwinding with the `.xdata` codes described there.

In scope:
- The frame-pointer / establisher-frame contract.
- The layout of the fixed EH-object area (catch objects and `UnwindHelp`).
- The funclet calling convention (entry state, return value, return mechanism).
- The register conventions for exception pointer / selector / continuation.
- The `$cppxdata` table shape as it applies to RISCV64 (which optional fields are
  present).
- A sketch of the `__CxxFrameHandler3` algorithm sufficient to implement it.

Out of scope:
- The MSVC C++ EH *semantics* themselves (state transitions, catch-type matching,
  `std::terminate` policy) — these are target-independent and already documented by
  the x64 model.
- `__CxxFrameHandler4` (the compact table format). LLVM emits `__CxxFrameHandler3`
  for this target; C4 is not implemented.

## 1. The big picture

C++ exceptions on RISCV64 Windows use the **funclet model**, identical in structure
to x64/ARM64:

- Each `catch` handler and each cleanup (destructor run during unwinding) is outlined
  by `WinEHPrepare` into a separate **funclet** — a region of the same function with
  its own `.seh_proc`/`.pdata`/`.xdata`.
- The compiler emits a `$cppxdata` (`FuncInfo`) table describing the function's try
  regions, unwind actions, and an instruction-pointer-to-state map.
- At throw time the runtime does a **two-phase** walk of the stack using
  `RtlVirtualUnwind` (or the platform equivalent) plus the language personality
  `__CxxFrameHandler3`, which reads `$cppxdata` to decide which cleanup and catch
  funclets to run.

The personality is named in the `.xdata` handler slot of each function that has EH
(emitted by the target-independent `WinException`; the RISCV64 backend does not
choose the name). The compiler references it as `__CxxFrameHandler3`.

## 2. The frame-pointer and establisher-frame contract (the crux)

This is the one part a RISCV64 personality implementer must get exactly right.

### 2.1 `s0` is the frame pointer and equals the CFA

Every function that contains EH has a frame pointer in `s0` (`x8`), forced on by
`hasFP` when `MF.hasEHFunclets()`
(`llvm/lib/Target/RISCV/RISCVFrameLowering.cpp`, `hasFPImpl`). The RISCV64 prologue
sets

```
    addi  s0, sp, <frameSize>
```

so that **`s0` points at the CFA — the value of `sp` on entry to the function**,
before the prologue allocated anything. (This differs from AArch64, whose `x29`
points partway into the frame at the saved fp/lr pair. On RISCV64 `s0` is the CFA
exactly.)

All frame objects therefore have **CFA-relative offsets**, which for locals are the
same as `MachineFrameInfo::getObjectOffset` (negative, growing down from the CFA).

### 2.2 `EstablisherFrame == CFA == entry `sp` == `s0`

The `EstablisherFrame` value the personality receives (the frame identity produced by
virtually unwinding into this function) is defined to be **the value of `sp` at
function entry — the CFA — which equals `s0`.** Consequently:

- `getWinEHParentFrameOffset()` returns **0** for RISCV64
  (`RISCVFrameLowering.cpp:541`), and this `0` is what `WinException` writes into the
  `ParentFrameOffset` field of every `HandlerType`. It means: the frame base the
  tables' offsets are relative to *is* the `EstablisherFrame`, with no adjustment.
- The catch-object and `UnwindHelp` offsets in the tables (see §3) are **negative
  byte offsets from `EstablisherFrame`**. To locate either, the runtime computes
  `EstablisherFrame + offset`.
- To run a funclet, the personality must enter it with **`s0 = EstablisherFrame`**
  (see §4). Because `s0` is callee-saved, virtually unwinding into the parent frame
  already restores the parent's `s0` into the register context, and that value equals
  `EstablisherFrame`; the personality may use either — they are the same value.

Worked example — for `int test_catch()` with a 48-byte frame, LLVM emits
`addi s0, sp, 48` (so `s0 = entry_sp`), `CatchObjOffset = -4`, `UnwindHelp = -16`,
`ParentFrameOffset = 0`. The catch object physically lives at `s0 - 4`
(`= EstablisherFrame - 4`) and the funclet reads it there; the `UnwindHelp` slot lives
at `s0 - 16`. This matches AArch64's table values field-for-field, the only
difference being that on RISCV64 `s0` itself equals `EstablisherFrame`.

## 3. The `$cppxdata` (`FuncInfo`) tables

The byte layout is emitted by the shared `WinException::emitCXXFrameHandler3Table`
(`llvm/lib/CodeGen/AsmPrinter/WinException.cpp`) and is identical to x64/ARM64. The
top-level record, in emission order:

```
FuncInfo {                         // symbol: $cppxdata$<func>
  uint32_t  MagicNumber;           // 0x19930522 (identifies __CxxFrameHandler3)
  int32_t   MaxState;              // number of state-unwind-map entries
  int32_t   UnwindMap;            // image-rel RVA -> $stateUnwindMap$<func>
  uint32_t  NumTryBlocks;
  int32_t   TryBlockMap;          // image-rel RVA -> $tryMap$<func>
  uint32_t  IPMapEntries;
  int32_t   IPToStateMap;         // image-rel RVA -> $ip2state$<func>
  uint32_t  UnwindHelp;           // *** present on RISCV64 *** (see below)
  int32_t   ESTypeList;           // 0
  int32_t   EHFlags;              // 1 = synchronous-only; 0 = async (-fasync-exceptions)
}
```

**`UnwindHelp` field presence.** Like every non-x86, non-SEH target with Windows CFI,
RISCV64 emits the `UnwindHelp` field. It is emitted whenever
`FuncInfo.UnwindHelpFrameIdx` is set (`WinException.cpp:751`). The RISCV64 backend
sets it for **every C++ (synchronous-personality) function that has funclets** —
including cleanup-only functions with no `catch` — in
`RISCVFrameLowering::emitWinEHFixedObjects` (`RISCVFrameLowering.cpp:2102`). Its value
is the CFA-relative offset of the `UnwindHelp` slot (see §3.1). A personality must
read this field to find the slot.

The referenced sub-tables (all target-independent):

```
UnwindMapEntry {                   // $stateUnwindMap$<func>, MaxState entries
  int32_t  ToState;                // state to transition to when this action runs
  int32_t  Action;                 // image-rel RVA of the cleanup funclet, or 0
}

TryBlockMapEntry {                 // $tryMap$<func>, NumTryBlocks entries
  int32_t  TryLow;
  int32_t  TryHigh;
  int32_t  CatchHigh;
  int32_t  NumCatches;
  int32_t  HandlerArray;           // image-rel RVA -> $handlerMap$<n>$<func>
}

HandlerType {                      // $handlerMap$<n>$<func>, NumCatches entries
  int32_t  Adjectives;             // catch qualifiers (const/volatile/reference/...)
  int32_t  Type;                   // image-rel RVA of the RTTI TypeDescriptor, or 0 for catch(...)
  int32_t  CatchObjOffset;         // *** CFA-relative *** offset of the catch object, or 0 if none
  int32_t  Handler;                // image-rel RVA of the catch funclet
  int32_t  ParentFrameOffset;      // *** always 0 on RISCV64 *** (non-x86 field)
}

IpToStateMapEntry {                // $ip2state$<func>, IPMapEntries entries
  int32_t  Ip;                     // image-rel RVA of an instruction boundary
  int32_t  State;                  // the EH state in effect from Ip onward (-1 = none)
}
```

Two fields are RISCV64-frame-specific in *value* (their layout is generic):

- **`CatchObjOffset`** — a negative CFA-relative byte offset. The runtime copies the
  caught exception object to `EstablisherFrame + CatchObjOffset` before entering the
  catch funclet. `0` means the catch has no named object (`catch(T)` without a
  parameter, or `catch(...)`), and no copy is performed.
- **`ParentFrameOffset`** — always `0` (see §2.2).

### 3.1 The `UnwindHelp` slot

`UnwindHelp` is an 8-byte slot at `EstablisherFrame + <UnwindHelp offset>`
(the offset is the value in the `FuncInfo.UnwindHelp` field; e.g. `-16`). The
compiler:

- Reserves it as a fixed stack object in `emitWinEHFixedObjects`
  (`RISCVFrameLowering.cpp:2102`), 16-byte aligned, immediately below the catch-object
  area (which itself sits just below any incoming vararg save area).
- **Initializes it to `-2` in the entry prologue**, after `s0` is established:

  ```
      li   <tmp>, -2
      sd   <tmp>, <UnwindHelp offset>(s0)
  ```

  (`-2` is the "no active try state" sentinel `__CxxFrameHandler3` expects before any
  try region is entered.)

The runtime uses this slot exactly as on x64/ARM64: it records the current EH state
there so that if an exception is thrown *during* a cleanup or catch (a nested throw),
the second unwind can resume from the correct state rather than re-running cleanups.
A conforming personality must read and update `*(int32_t*)(EstablisherFrame +
UnwindHelpOffset)` as it drives the state machine, identically to the x64
implementation. The slot is 8 bytes but only the low 32-bit state value is
meaningful.

### 3.2 Fixed EH-object area layout

`emitWinEHFixedObjects` lays the fixed area out downward from the CFA, below the
vararg save area (`VarArgsSaveSize`, normally 0):

```
   CFA (= s0 = EstablisherFrame) ── highest address
   [ incoming vararg save area          ]   (size VarArgsSaveSize, if any)
   [ catch object 0 ]  at  -(VarArgsSaveSize + align + size0)
   [ catch object 1 ]  ...
   [ UnwindHelp (8B) ]  at  -align16(catchObjectsEnd + 8)
   [ callee-saved ra, s0, ...            ]   (assigned by PEI, below the fixed area)
   [ local variables, spills             ]
   ── lowest address (sp after prologue)
```

Each catch object keeps its natural size/alignment; `UnwindHelp` is 8 bytes,
16-byte-aligned. For a single `int` catch object this yields `CatchObjOffset = -4`
and `UnwindHelp = -16`, matching x64/ARM64.

## 4. The funclet calling convention

A funclet (catch handler or cleanup) is a separate `.seh_proc` with its own SEH
prologue/epilogue. The compiler never re-derives `s0` inside a synchronous-EH funclet
(`RISCVFrameLowering.cpp:1240` gates the FP setup on `!IsSyncEHFunclet`); the funclet
saves and restores `s0` as a callee-saved register but treats the **incoming `s0` as
the parent's frame pointer** and addresses all parent locals through it.

### 4.1 Entry contract — what the personality must set up

Before transferring control to any funclet, the personality must:

1. Set `s0 = EstablisherFrame` (the parent's frame pointer; see §2.2). Equivalently,
   ensure the register context handed to the funclet has the parent's callee-saved
   `s0` restored — same value.
2. Set `sp` to a valid stack below the parent frame for the funclet's own frame (the
   funclet's SEH prologue then allocates its small frame: saved `ra`, `s0`, and space
   for any outgoing call arguments).
3. For a **catch** funclet whose `HandlerType.CatchObjOffset != 0`, copy the caught
   object to `EstablisherFrame + CatchObjOffset` first.

No arguments are passed in the integer argument registers to the funclet in the C++
(synchronous) case; the funclet reaches everything it needs through `s0`. (This is
the synchronous contract, matching AArch64's C++ path. The asynchronous SEH path —
`__try`/`__except`, a different personality — instead passes the establisher frame in
a register and uses `llvm.localrecover`; it is out of scope here.)

### 4.2 Cleanup funclets and `cleanupret`

A cleanup funclet runs a destructor (or several) and then executes `cleanupret`,
which the compiler lowers to a plain return:

```
    ret                     # jalr x0, 0(ra)  — lowered from CLEANUPRET
```

(`RISCVAsmPrinter.cpp:339`). The funclet returns to the personality, which continues
unwinding to the next state per the `$stateUnwindMap`. A cleanup funclet returns no
value.

### 4.3 Catch funclets and `catchret`

A catch funclet runs the handler body and then executes `catchret`, which resumes
execution in the parent at a **continuation block** (the code after the `try`/`catch`).
On RISCV64 the funclet **returns the continuation address in `a0` (`x10`)**:

```
    auipc a0, %pcrel_hi(<continuation label>)
    addi  a0, a0, %pcrel_lo(<continuation label>)   # (may be folded)
    ...
    ret                     # jalr x0, 0(ra)  — lowered from CATCHRET
```

The address is materialized just before the funclet epilogue by
`RISCVInstrInfo::expandPostRAPseudo` (`RISCVInstrInfo.cpp:2077`), and the `CATCHRET`
pseudo is lowered to `ret` by the asm printer (`RISCVAsmPrinter.cpp:340`). So:

- **The catch funclet returns to the personality with `a0` = the address to resume
  execution at in the parent frame.**
- The personality is then responsible for setting up the parent's register context
  (in particular restoring the parent's callee-saved registers and `sp`/`s0`) and
  transferring control to `a0`. This is the RISCV64 analogue of x64 returning the
  continuation in `rax` and ARM64 in `x0`.

`a0` is the natural choice: it is also the exception-pointer register (§5), and
returning the resume address there mirrors the integer return-value register
convention.

## 5. Register conventions

| Purpose | Register | Source |
|---|---|---|
| Exception pointer on EH-pad entry | `a0` (`x10`) | `RISCVISelLowering.cpp:25408` (`getExceptionPointerRegister`) |
| Exception selector on EH-pad entry | `a1` (`x11`) | `RISCVISelLowering.cpp:25413` (`getExceptionSelectorRegister`) |
| `catchret` continuation address (funclet → personality) | `a0` (`x10`) | §4.3 |
| Frame pointer / establisher frame inside a funclet | `s0` (`x8`) | §2 |

These are the values the code generator assumes; a personality that transfers control
into or out of compiler-generated EH code must honor them.

## 6. Sketch of a conforming `__CxxFrameHandler3`

Given the above, a RISCV64 `__CxxFrameHandler3(ExceptionRecord, EstablisherFrame,
ContextRecord, DispatcherContext)` follows the same algorithm as the x64
implementation, with these RISCV64 bindings:

1. Recover `FuncInfo` from the `DispatcherContext` (the `.xdata` handler-data pointer
   is `$cppxdata$<func>`; parse per §3).
2. Compute the **current state** by binary-searching `$ip2state$` for the faulting IP
   (the control PC within this frame), exactly as on x64.
3. **`UnwindHelp`:** treat `*(int32_t*)(EstablisherFrame + UnwindHelpOffset)` as the
   authoritative current state during a nested unwind; it was initialized to `-2`
   (§3.1).
4. **Unwind phase:** for each state from the current state up to the target, consult
   `$stateUnwindMap[state]`; if `Action != 0`, call the cleanup funclet. Enter it per
   §4.1 (set `s0 = EstablisherFrame`, give it a scratch stack); it returns via `ret`
   (§4.2). Update the `UnwindHelp` state as you go.
5. **Catch phase:** for each `TryBlockMapEntry` whose `[TryLow, TryHigh]` covers the
   current state, walk its `HandlerArray`; for each `HandlerType` whose `Type`
   RTTI matches the thrown object (respecting `Adjectives`), copy the object to
   `EstablisherFrame + CatchObjOffset` if that field is non-zero (§3), then enter the
   catch funclet per §4.1. It returns (via `ret`) with **the continuation address in
   `a0`** (§4.3).
6. On catch, transfer control to the address returned in `a0` after restoring the
   parent frame's context (callee-saved registers, `sp`, `s0`) using the SEH unwind
   codes (see [RISCVWinCFI](RISCVWinCFI.md)). Execution resumes after the `try`.

The only RISCV64-specific inputs to this algorithm are: `EstablisherFrame` = CFA =
`s0` (§2), `ParentFrameOffset` = 0, the CFA-relative `CatchObjOffset`/`UnwindHelp`
offsets, the `s0` funclet-entry contract (§4.1), and the `a0` catch-continuation
convention (§4.3). Everything else is the standard MSVC C++ EH state machine.

## 7. Compiler-side reference (for re-verification)

| Concern | Location |
|---|---|
| `CLEANUPRET` / `CATCHRET` pseudos + patterns | `RISCVInstrInfo.td:2357` |
| `CATCHRET`/`CLEANUPRET` → `ret` lowering | `RISCVAsmPrinter.cpp:339` |
| `catchret` continuation materialized into `a0` | `RISCVInstrInfo.cpp:2077` (`expandPostRAPseudo`) |
| `CatchRetOpcode` (funclet scope coloring) | `RISCVInstrInfo.cpp:87` (ctor arg) |
| Funclet inherits `s0` (no FP re-derivation) | `RISCVFrameLowering.cpp:1240` (prologue), `:1469` (epilogue) |
| `getWinEHParentFrameOffset` == 0 | `RISCVFrameLowering.cpp:541` |
| CFA-relative table offsets | `RISCVFrameLowering.cpp:1599` (`getFrameIndexReferencePreferSP`) |
| Catch-object offsets + `UnwindHelp` slot + `-2` init | `RISCVFrameLowering.cpp:2102` (`emitWinEHFixedObjects`) |
| `needsFixedCatchObjects` / exception registers | `RISCVISelLowering.cpp:25408` |
| `$cppxdata` table emission (target-independent) | `WinException.cpp` (`emitCXXFrameHandler3Table`) |

See [RISCVWinCFI](RISCVWinCFI.md) for the per-function/per-funclet SEH unwind info
this document builds on.
