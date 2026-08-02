# RISCV64 Windows (COFF/PE) SEH Unwind Info

**Status: not a Microsoft specification.** Microsoft has never shipped a RISCV64
Windows port and has never published `IMAGE_REL_RISCV64_*` relocation numbers or a
RISCV64 `UNWIND_INFO`/`.xdata`/`.pdata` encoding. Everything in this document is
LLVM's own definition for the `riscv64-*-windows-*` target. Should a real
specification ever be published, it supersedes this one — do not treat any value
here as ABI-stable across that event.

This document exists so a later engineer can implement RISCV64 PE stack
unwinding (e.g. in GDB, or any other non-LLVM consumer) **without needing to read
LLVM's source code** to reverse-engineer the encoding. Every field, opcode, and
threshold below is drawn directly from the LLVM implementation and cross-checked
against compiler-generated output; file:line references to the LLVM source are given
throughout so behavior can be re-verified against a specific LLVM revision.

## Audience and scope

This document assumes the reader already understands the **x86_64 Windows SEH**
unwind model (`UNWIND_INFO`, `RUNTIME_FUNCTION`, `.pdata`/`.xdata`), since the RISCV64
format below is explicitly a close variant of it, not of ARM64's format. If you need
that background first, see Microsoft's
[x64 exception handling documentation](https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64).

In scope:
- Object-file identification (COFF machine type, relocations touching `.pdata`/`.xdata`).
- The `RUNTIME_FUNCTION` (`.pdata`) entry layout.
- The `UNWIND_INFO` (`.xdata`) header and unwind-code array layout.
- Every unwind opcode RISCV64 uses, with exact bit-level encoding.
- Register numbering for the unwind codes.
- The prologue-shape invariants LLVM's code generator guarantees, which a decoder may
  rely on.
- The epilogue-detection problem: **RISCV64, like x86_64 (and unlike ARM64), has no
  unwind codes describing the epilogue at all.** A correct unwinder must detect
  epilogues by pattern-matching instruction bytes, exactly as x86_64 unwinders do.
  This document specifies that pattern.
- A minimal description of the `__C_specific_handler`-compatible scope table used for
  `__try`/`__except`, for context (not required for plain backtrace/CFI unwinding).

Out of scope / not attempted by this design: SEH funclet range extension thunks
(RISCV64 currently never needs a thunk mechanism at all — see the "Known
limitations" section), and any consideration of ARM64EC-style hybrid images.

## 1. Target and object-file identification

- COFF machine type: `IMAGE_FILE_MACHINE_RISCV64 = 0x5064`
  (`llvm/include/llvm/BinaryFormat/COFF.h:115`). (`IMAGE_FILE_MACHINE_RISCV32 =
  0x5032` and `IMAGE_FILE_MACHINE_RISCV128 = 0x5128` also exist for the other RISC-V
  XLENs but are not populated by this target.)
- Triple: `riscv64-*-windows-msvc` (or `-windows-gnu`; the object-file/unwind format
  is identical either way — only the C++ ABI/name-mangling personality differs).
- Pointer size: 8 bytes. `RUNTIME_FUNCTION`/`UNWIND_INFO` addresses are all 32-bit
  image-relative (RVA) values, same as every other Windows unwind format — there is
  no 64-bit address anywhere in `.pdata`/`.xdata` itself.
- CodeView CPU type: `CPUType::RISCV64 = 0xfa`
  (`llvm/include/llvm/DebugInfo/CodeView/CodeView.h`). Microsoft has assigned no
  `CV_CPU_TYPE_e` value for RISC-V; this one continues the sequence after
  `ARM64X = 0xf9`. It appears in the `S_COMPILE3` record of every object and of
  the linker record in the PDB, and selects the RISCV64 register-name table when
  decoding variable locations. The register ids themselves are the `RISCV64_*`
  block of `CodeViewRegisters.def`.

### Relocations touching `.pdata`/`.xdata`

The full `IMAGE_REL_RISCV64_*` relocation enum
(`llvm/include/llvm/BinaryFormat/COFF.h:427-441`) has 15 entries for general code/data
relocation, but **only one relocation type is ever used inside `.pdata` or
`.xdata`**:

| Value | Name | Used for |
|---|---|---|
| `0x0003` | `IMAGE_REL_RISCV64_ADDR32NB` | Every cross-reference in `.pdata`/`.xdata`: function start/end RVAs, the `.xdata` pointer from `.pdata`, and (inside `.xdata`'s handler-data area) the personality-function RVA and scope-table label RVAs. |

`ADDR32NB` is "address, no base" — a 32-bit RVA relative to the image base, exactly
like x86_64/ARM64 use for the same purpose. There is no addend field on COFF
relocations (unlike ELF's Rela), which is why every `.pdata`/`.xdata` cross-reference
is expressed as a relocation against a real symbol (a temporary label), never as a
symbol-plus-constant computed at assembly time and then dropped.

> **Addends outside `.pdata`/`.xdata`.** The general code/data relocations do need
> to express a symbol-plus-constant (e.g. `&global[n]`), and with no Rela addend the
> only place to keep the constant is the instruction itself. `IMAGE_REL_RISCV64_PCREL_HI20`
> therefore uses an ARM64-style convention: the referenced `auipc`'s 20-bit immediate
> carries the **raw byte addend** (the offset from the symbol), and the linker folds it
> into the target (`s + addend - p`) before taking the pc-relative high part. This is
> required because the `%pcrel_hi`/`%pcrel_lo` split rounds with `+0x800`, a carry that
> can only be computed from the *full* target value — dropping the addend would leave the
> `auipc` one page (or more) too low. The paired `IMAGE_REL_RISCV64_PCREL_LO12_I/S`
> already round-trips the low 12 bits, so it needs no addend of its own. Addends that do
> not fit in a signed 20-bit field are not folded into the pair by codegen (they stay as a
> separate add), so the `auipc` immediate is always a valid signed-20-bit addend.

## 2. `RUNTIME_FUNCTION` (`.pdata`) entry format

One 12-byte entry per function (or per funclet — see "Funclets" below), 4-byte
aligned. Implementation: `EmitRuntimeFunction` in `llvm/lib/MC/MCWin64EH.cpp:200-210`
(shared, target-independent code — RISCV64 does not override this).

```
struct RUNTIME_FUNCTION {
    uint32_t BeginAddress;      // RVA, ADDR32NB reloc to function entry symbol
    uint32_t EndAddress;        // RVA, ADDR32NB reloc to function end symbol
    uint32_t UnwindInfoAddress; // RVA, ADDR32NB reloc to the .xdata UNWIND_INFO
};
```

Multiple `RUNTIME_FUNCTION` entries are simply concatenated in `.pdata`, one per
function/funclet, in whatever order the assembler emitted them (no ordering
requirement; a real unwinder does a binary search over `.pdata` sorted by
`BeginAddress`, which the linker is responsible for guaranteeing at link time — this
is identical to every other Windows architecture and not RISCV64-specific).

## 3. `UNWIND_INFO` (`.xdata`) format

One variable-length record per function, 4-byte aligned, immediately followed
in-place by handler data if present. Implementation: `RISCV64EmitUnwindInfo` in
`llvm/lib/MC/MCWin64EH.cpp:542-613`.

```
struct UNWIND_INFO {
    uint8_t  VersionAndFlags;    // bits 0-2: version (always 1); bits 3-7: flags
    uint8_t  SizeOfPrologInBytes;
    uint8_t  CountOfUnwindCodes;
    uint8_t  FrameRegisterAndOffset; // the whole byte is offset/16 on this target
                                     // (see note below) -- unlike x86_64/ARM64,
                                     // there is no separate register sub-field
    UnwindCode UnwindCodes[CountOfUnwindCodes]; // padded to even count, see below
    // Followed by (mutually exclusive, selected by flags):
    //   - if UNW_ChainInfo:      a RUNTIME_FUNCTION for the chained parent, OR
    //   - if UNW_ExceptionHandler|UNW_TerminateHandler:
    //         uint32_t ExceptionHandlerRVA (ADDR32NB), then handler-specific data
    //   - if none of the above and CountOfUnwindCodes == 0: uint32_t padding (0)
};
```

This layout, the flags bit assignment, and the chained/handler-data tail are **byte-
for-byte identical to x86_64's `UNWIND_INFO`** (`llvm/include/llvm/Support/
Win64EH.h:176-208`) — RISCV64 reuses the container unchanged and only introduces new
values inside `UnwindCode` entries. **This is not shared with ARM64**, which uses an
entirely different packed/optionally-compressed encoding; do not consult ARM64's
`.xdata` format for anything beyond high-level inspiration.

Field notes:
- `VersionAndFlags` bits 0-2 (`getVersion()`): always `1`
  (`RISCV64EmitUnwindInfo`, `flags = info->Version`, and `WinEH::FrameInfo::Version`
  defaults to 1 and is never changed by RISCV64 codegen). Bits 3-7 (`getFlags()`):
  `UNW_ExceptionHandler = 0x01`, `UNW_TerminateHandler = 0x02`,
  `UNW_ChainInfo = 0x04` — same three flag bits, same meaning, as x86_64/ARM64
  (`llvm/include/llvm/Support/Win64EH.h:157-165`).
- `SizeOfPrologInBytes`: an **8-bit byte count** — the byte offset (not instruction
  count) from function entry to the end of the prologue (the point where
  `.seh_endprologue`/`SEH_PrologEnd` was emitted). Because it is byte-granular, not
  instruction-count-granular, it is unaffected by the RVC (compressed instruction)
  extension mixing 2-byte and 4-byte instructions in the prologue — this was a
  deliberate design choice (see `MCWin64EH.cpp:447-453`) precisely to avoid needing a
  separate encoding path for compressed vs. uncompressed prologues. A prologue longer
  than 255 bytes cannot be represented; LLVM does not currently diagnose this
  overflow case (treat it as an unlikely-but-real encoder gap, not a spec feature).
- `CountOfUnwindCodes`: count of 2-byte `UnwindCode` slots that follow (see §5 for how
  multi-slot opcodes are counted). If this count is odd, one extra padding
  `UnwindCode` slot (value 0) follows to keep the array 4-byte aligned overall
  (`RISCV64EmitUnwindInfo`, `MCWin64EH.cpp:596-598`) — this padding slot is **not**
  included in `CountOfUnwindCodes` itself.
- `FrameRegisterAndOffset`: **the entire byte is `offset / 16`** if a `UOP_SetFPReg`
  code exists in this function, else `0` (`RISCV64EmitUnwindInfo`,
  `MCWin64EH.cpp:576-591`, on-disk pack `frame = (frameInst.Offset >> 4) & 0xFF`).
  This gives an 8-bit, ×16-scaled field — offsets `0` to `4080` in steps of `16` —
  rather than x86_64/ARM64's 4-bit field (offsets `0`-`240`). **There is no register
  sub-field at all on this target**, unlike x86_64/ARM64: `emitRISCVWinCFISetFrame`
  (`RISCVWinCOFFStreamer.cpp:121-152`) rejects any register other than `s0` at
  assembly time, so there is nothing left to disambiguate once only one register is
  ever legal, and the full byte is repurposed for a wider offset range instead of
  being split 4/4 with an always-`0` register nibble. **A decoder must simply
  hardcode the assumption that the frame pointer, if one exists (i.e. if any
  `UOP_SetFPReg` code is present in the unwind-code array), is always `s0`** — it
  is never named anywhere in `.xdata`. If no frame pointer is used, this whole byte
  is `0`.

### Trailing padding-to-8-bytes rule

If a function has `CountOfUnwindCodes == 0` **and** no chained-unwind-info flag
**and** no exception/terminate-handler flag, a 4-byte zero padding word follows the
(empty, even-padded-to-zero) unwind code array, so the whole `UNWIND_INFO` is never
smaller than 8 bytes (`RISCV64EmitUnwindInfo`, `MCWin64EH.cpp:607-612`). This matches
x86_64's identical minimum-size rule.

## 4. Register numbering for unwind codes

RISCV64's unwind codes only ever need to name the 13 saveable GPRs (`ra`, `s0`-`s11`)
and the 12 saveable FPRs (`fs0`-`fs11`, D-extension/64-bit registers only — there is
no separate encoding for F-extension-only 32-bit register saves, since the LP64D ABI
always uses the D-extension register file for callee-saved FP values). Each set gets
its own independent, densely-packed 4-bit index space (not the raw RISC-V physical
register encoding). Implementation: `getSEHGPRIndex`/`getSEHFPRIndex`,
`llvm/lib/Target/RISCV/MCTargetDesc/RISCVBaseInfo.cpp:128-186`.

**GPR index (used by `UOP_SaveNonVol` and `UOP_SetFPReg`):**

| Index | Register | Index | Register | Index | Register |
|---|---|---|---|---|---|
| 0 | `ra` (x1) | 5 | `s4` (x20) | 10 | `s9` (x25) |
| 1 | `s0` (x8) | 6 | `s5` (x21) | 11 | `s10` (x26) |
| 2 | `s1` (x9) | 7 | `s6` (x22) | 12 | `s11` (x27) |
| 3 | `s2` (x18) | 8 | `s7` (x23) | | |
| 4 | `s3` (x19) | 9 | `s8` (x24) | | |

**FPR index (used only by `UOP_RISCVSaveFReg`):**

| Index | Register | Index | Register | Index | Register |
|---|---|---|---|---|---|
| 0 | `fs0` (f8) | 4 | `fs4` (f20) | 8 | `fs8` (f24) |
| 1 | `fs1` (f9) | 5 | `fs5` (f21) | 9 | `fs9` (f25) |
| 2 | `fs2` (f18) | 6 | `fs6` (f22) | 10 | `fs10` (f26) |
| 3 | `fs3` (f19) | 7 | `fs7` (f23) | 11 | `fs11` (f27) |

Note that GPR index and FPR index are **separate namespaces that both start at 0** —
the opcode itself (`UOP_SaveNonVol` vs. `UOP_RISCVSaveFReg`) disambiguates which table
applies, exactly as x86_64 disambiguates `UOP_SaveNonVol` (GPR) from `UOP_SaveXMM128`
(XMM) by opcode. There is no register number that is invalid to see in a
`UOP_SaveNonVol`/`UOP_RISCVSaveFReg` code, since the assembler-level
`.seh_savereg`/`.seh_savefreg` directives reject any register outside these two
tables at assembly time (`RISCVWinCOFFStreamer.cpp`,
`emitRISCVWinCFISaveReg`/`SaveFReg`).

`UOP_SetFPReg` does not use the GPR table above at all: there is no register field
anywhere for it to occupy (see §3's `FrameRegisterAndOffset` note). Since RISC-V's
calling convention designates `s0`/`x8` as the only legal frame-pointer register, and
`emitRISCVWinCFISetFrame` rejects every other register at assembly time
(`RISCVWinCOFFStreamer.cpp:124-128`), there is nothing a register field could
usefully disambiguate, so the format simply omits one. **A conforming decoder must
treat "a frame pointer exists" (any `UOP_SetFPReg` code present) as meaning "the
frame pointer is `s0`", full stop — there is no register index to read anywhere for
this opcode.**

## 5. Unwind codes

Each `UnwindCode` slot is 2 bytes: `{ uint8_t CodeOffset; uint8_t
UnwindOp:4, OpInfo:4; }` for single-slot opcodes, with additional 2-byte slots
following for opcodes that need more operand data — this slot-count/layout mechanism
is **identical to x86_64's** (`llvm/include/llvm/Support/Win64EH.h:142-153`).

`CodeOffset` is the **byte offset** (not instruction count) from function entry to
the **first byte of the instruction immediately following** the one this code
describes (i.e., it is the point in the prologue *after* which this code's effect has
taken place) — same convention as x86_64. Because RVC (compressed 2-byte
instructions) may be freely mixed with 4-byte instructions in the prologue, this
offset must be computed by literally measuring bytes, never by counting instructions.

RISCV64 uses **5 opcodes for compiler-generated code**, reusing 4 of x86_64's
unmodified and adding exactly 1 new one, plus **3 operand-less opcodes that only
appear in hand-written OS runtime stubs** (§5.1), never in codegen output. The
on-disk 4-bit `UnwindOp` nibble does **not** match the enum ordinal of
`Win64EH::UnwindOpcodes` (that shared enum has grown far past 15 entries across
x86_64+ARM64+ARM's blocks) — RISCV64 remaps to small values explicitly. This
remapping table is the single most important piece of information in this document
for building a correct decoder:

| On-disk `UnwindOp` | Enum name | Slot count | Meaning |
|---|---|---|---|
| 1 | `UOP_AllocLarge` | 2 or 3 | Large stack allocation |
| 2 | `UOP_AllocSmall` | 1 | Small stack allocation |
| 3 | `UOP_SetFPReg` | 1 | Establish frame pointer |
| 4 | `UOP_SaveNonVol` | 2 | Save a GPR |
| 5 | `UOP_RISCVSaveFReg` | 2 | Save an FPR (**new opcode**, only one not shared with x86_64) |
| 6 | `UOP_TrapFrame` | 1 | Restore PC/SP/regs from a kernel trap frame (§5.1) |
| 7 | `UOP_Context` | 1 | Restore full `CONTEXT` from the stack (§5.1) |
| 8 | `UOP_ClearUnwoundToCall` | 1 | Clear `CONTEXT_UNWOUND_TO_CALL` (§5.1) |

(Source: `riscv64OnDiskOp`, `llvm/lib/MC/MCWin64EH.cpp:460-481`; slot counts from
`RISCV64CountOfUnwindCodes`, same file.) On-disk values 6-8 reuse the ARM64
`Win64EH::UnwindOpcodes` enum entries `UOP_TrapFrame`/`UOP_Context`/
`UOP_ClearUnwoundToCall` (whose real ordinals are far past 15), remapped into
RISCV64's dense nibble range like every other opcode here. On-disk value `0` and
values `9`-`15` are unused/reserved by this format; a decoder should treat them as
a malformed/unsupported record rather than silently ignoring them.

Detailed per-opcode encoding (all multi-byte fields little-endian, matching every
other Windows unwind format):

### `UOP_SetFPReg` (on-disk 3) — 1 slot

```
Slot 0: CodeOffset (1 byte) | OpInfo=<unused, 0> UnwindOp=3 (1 byte)
```

`OpInfo` is always `0` here (there is no register to encode — see the note in §4;
the frame pointer is implicitly always `s0`). The offset is **not** stored in this
slot at all either — it lives in the `UNWIND_INFO` header's `FrameRegisterAndOffset`
byte (§3), since there can only be one frame-pointer-establishing instruction per
function. This slot exists purely to record *where in the prologue* (via
`CodeOffset`) the frame pointer became valid; a decoder needs this to know whether,
at a given PC within the prologue, it should compute the CFA/frame from SP or from FP
yet. Validated legal register/offset range (enforced by the assembler, not encoded in
the bitstream itself): register must be `s0`; offset must be a multiple of 16 in
`[0, 4080]` (`RISCVWinCOFFStreamer.cpp`, `emitRISCVWinCFISetFrame`) — wider than
x86_64/ARM64's `SetFPReg` (`[0, 240]`), since RISC-V uses the whole
`FrameRegisterAndOffset` byte for the offset rather than splitting it with a register
sub-field (§3).

### `UOP_AllocSmall` (on-disk 2) — 1 slot

```
Slot 0: CodeOffset (1 byte) | OpInfo (1 byte) UnwindOp=2 (1 byte)
```

Allocation size in bytes = `(OpInfo * 8) + 8` (i.e. `OpInfo` encodes `(size-8)/8`,
so representable sizes are 8, 16, 24, ..., 128 — `MCWin64EH.cpp:522-526`:
`b2 |= (((inst.Offset - 8) >> 3) & 0x0F) << 4`). Used when the generic
`Win64EH::Instruction::Alloc()` factory (target-independent,
`llvm/include/llvm/MC/MCWin64EH.h:29-32`) is given a size `<= 128`.

### `UOP_AllocLarge` (on-disk 1) — 2 or 3 slots

```
2-slot form (allocation < 512KiB - 8, i.e. fits in 16 bits after >>3):
  Slot 0: CodeOffset (1 byte) | OpInfo=0 (1 byte) UnwindOp=1 (1 byte)
  Slot 1: Size >> 3 (2 bytes)

3-slot form (allocation >= 512KiB - 8):
  Slot 0: CodeOffset (1 byte) | OpInfo=1 (1 byte) UnwindOp=1 (1 byte, i.e. byte 2 = 0x11)
  Slot 1: Size & 0xFFF8 (2 bytes, low 16 bits, 8-byte aligned)
  Slot 2: Size >> 16   (2 bytes, high bits)
```

(`MCWin64EH.cpp:508-521`.) Used for allocations `> 128` bytes (the generic `Alloc()`
factory threshold). In practice, LLVM's RISCV64 codegen keeps every individual
allocation `<= 2048`ish bytes by construction (see the "split SP adjustment"
invariant in §6), so the 3-slot huge form is reachable only via extremely large
single-shot stack frames or hand-written `.s` input, not from ordinary compiled code.

### `UOP_SaveNonVol` (on-disk 4) — 2 slots

```
Slot 0: CodeOffset (1 byte) | GPRIndex (1 byte, low nibble = 0, i.e. byte 2 =
        (GPRIndex << 4) | 4)
Slot 1: Offset >> 3 (2 bytes) -- offset from SP at the point of this save
```

(`MCWin64EH.cpp:531-537`.) `GPRIndex` per the table in §4. Offset must be a
non-negative multiple of 8 (enforced by the assembler,
`RISCVWinCOFFStreamer.cpp::emitRISCVWinCFISaveReg`). Note there is **no
`UOP_SaveNonVolBig` counterpart** on this target (unlike x86_64, which has one for
offsets `> 512KiB - 8`) — the generic `SaveNonVol()` factory
(`llvm/include/llvm/MC/MCWin64EH.h:37-41`) would pick that opcode for a
large-enough offset, but `RISCV64EmitUnwindCode`'s `switch` has no case for it and
would hit `llvm_unreachable` if ever reached. This is unreachable from any real
compiled function (frame sizes anywhere near 512KiB are implausible), but a decoder
does not need to support it either, since LLVM's own encoder cannot produce it.

### `UOP_RISCVSaveFReg` (on-disk 5) — 2 slots — **the one genuinely new opcode**

```
Slot 0: CodeOffset (1 byte) | FPRIndex (1 byte, low nibble = 0, i.e. byte 2 =
        (FPRIndex << 4) | 5)
Slot 1: Offset >> 3 (2 bytes) -- offset from SP at the point of this save
```

Bit-identical layout to `UOP_SaveNonVol` above, just a different `UnwindOp` value and
a different (FPR, §4) register table. This opcode exists because x86_64's
`UOP_SaveXMM128` assumes a 16-byte-wide register save slot (rounds/scales offsets for
128-bit XMM registers), which is wrong for RISC-V's 8-byte D-extension FPR saves; a
plain reuse of `UOP_SaveXMM128` would have silently mis-scaled every FPR save offset.
Same offset constraints as `UOP_SaveNonVol` (non-negative multiple of 8).

## 5.1 Dispatcher-only opcodes and `CONTEXT_UNWOUND_TO_CALL`

The three opcodes on-disk `6`/`7`/`8` are **never emitted by codegen**. They exist
solely so that hand-written OS runtime stubs — `KiUserExceptionDispatcher`, the
APC/callback dispatchers, and the kernel's trap/interrupt entry — can be described,
because those routines establish a frame via an OS-synthesized register state on the
stack rather than via an ordinary call. LLVM exposes them only as assembler
directives (`.seh_trap_frame`, `.seh_context`, `.seh_clear_unwound_to_call`); there
is no `SEH_*` machine-instruction pseudo and no code path in the RISCV backend that
produces them. Each is a single 2-byte slot with **no register/offset operand**:

```
Slot 0: CodeOffset (1 byte) | OpInfo=<unused, 0> UnwindOp=<6|7|8> (1 byte)
```

`CodeOffset` still records the byte offset (from function/stub entry) of the point at
which the operation takes effect, exactly as for every other opcode. `OpInfo` is
always `0`. (`MCWin64EH.cpp`, `RISCV64EmitUnwindCode`.)

- **`UOP_TrapFrame` (on-disk 6) — `.seh_trap_frame`.** Marks that the frame at this
  point is a kernel trap/interrupt frame; the unwinder reloads PC/SP (and the
  volatile register state) from the OS-defined trap-frame layout at the current SP.
- **`UOP_Context` (on-disk 7) — `.seh_context`.** Marks that a full `CONTEXT` record
  lives on the stack at the current SP; the unwinder restores the entire register
  file from it. This is what `KiUserExceptionDispatcher` uses to unwind back into the
  faulting frame.
- **`UOP_ClearUnwoundToCall` (on-disk 8) — `.seh_clear_unwound_to_call`.** A pure
  flag mutation — it touches no register. It clears the `CONTEXT_UNWOUND_TO_CALL`
  flag (see below). It is emitted **alongside** a `UOP_Context`/`UOP_TrapFrame`
  restore in the same stub, because the PC that restore just loaded is an *exact*
  fault address, not a return address.

### Why the flag exists: the `-1` lookup adjustment

`CONTEXT_UNWOUND_TO_CALL` (`0x20000000` in `ContextFlags`) is the unwinder's own
bookkeeping, not something stored in `.xdata`. It records whether the PC in the
working context is a **precise fault address** or a **return address**, and its only
effect is a small adjustment when looking up the next frame's function entry:

```
lookupPc = ctx.Pc;
if (ctx.ContextFlags & CONTEXT_UNWOUND_TO_CALL)
    lookupPc -= 1;   // land inside the call instruction, not at the return address
funcEntry = RtlLookupFunctionEntry(lookupPc);
```

The `-1` is required because a `noreturn` call as the last instruction of a function
leaves a return address that points at the *first byte of the next function*; without
the decrement, the lookup resolves the wrong `RUNTIME_FUNCTION`, and scope-table
matching in a language handler would attribute a trailing call to the region *after*
the `__try` body rather than inside it. `-1` (one byte, not one instruction) is the
conservative choice: it always lands in the last byte of the call regardless of
whether the call was a 4-byte `jalr` or a 2-byte compressed `c.jalr`, so it is
correct for RVC-mixed code. For a *precise fault* PC (flag clear), the fault happened
*at* that instruction, so the exact PC must be looked up with no decrement — this
matches libunwind's `setInfoBasedOnIPRegister(isReturnAddress)` logic
(`libunwind/src/UnwindCursor.hpp`), where `--pc` is applied only for return
addresses and suppressed for signal/exception frames.

### How the flag is set and cleared

The flag is **sticky**, maintained entirely by the unwinder across `RtlVirtualUnwind`
steps; it is not encoded per-frame in `.xdata`:

- Every ordinary unwind step **sets** the flag in the resulting context, because
  after following any return address the new PC is, by definition, a return address.
  So the default at the end of a normal frame is "set".
- **`UOP_ClearUnwoundToCall` clears it** for the one frame where it appears. Because
  the flag is sticky, that cleared state then persists — the *next* function-entry
  lookup uses the exact PC (no `-1`) — until the following `RtlVirtualUnwind` step
  runs and, being an ordinary step, sets the flag again. In other words, a `context`/
  `trap_frame` boundary re-establishes a precise-PC context, and one plain frame of
  unwinding past it returns to return-address bookkeeping.

Ordinary nested C++ exception handling (a throw inside a catch/cleanup) needs **none**
of these opcodes: that is pure call-frame unwinding, and each fresh exception dispatch
re-enters the dispatcher from a new precise-fault context. The dispatcher-only opcodes
matter only at the boundary between a dispatcher/trap frame and the call frames on
either side of it.

## 6. Prologue-shape invariants (guaranteed by LLVM's code generator)

A decoder can rely on these properties of any prologue emitted by LLVM's RISCV64
backend (`llvm/lib/Target/RISCV/RISCVFrameLowering.cpp`); they are not encoded
explicitly anywhere but follow from how codegen always constructs a prologue:

1. **Order of unwind-code-generating events in the prologue is always:** an optional
   first stack allocation (`UOP_AllocSmall`/`UOP_AllocLarge`), then zero or more GPR
   saves (`UOP_SaveNonVol`) and FPR saves (`UOP_RISCVSaveFReg`) in any relative order
   with each other, then an optional frame-pointer establishment (`UOP_SetFPReg`),
   then an optional **second** stack allocation with **no unwind code of its own**
   (see point 3). The unwind-code array itself is emitted in **reverse chronological
   order** on disk (`RISCV64EmitUnwindInfo`, `MCWin64EH.cpp:585-590`: `info-
   >Instructions.back()` popped in a loop) — this is the standard Windows convention
   (codes are naturally consumed by an unwinder walking backward from "now" to
   "function entry"), identical to x86_64/ARM64.
2. **A function can have at most one `UOP_SetFPReg` code.** RISC-V only ever
   establishes one frame pointer, in `s0`, at one point in the prologue.
3. **"Split SP adjustment" — a function may perform two separate stack-pointer
   decrements in its prologue, but only the first one gets an unwind code if a frame
   pointer is subsequently established.** RISC-V's load/store/`addi` immediate
   encoding is limited to 12 bits, so for a total frame size that would push
   callee-saved-register spill offsets out of that range, codegen splits the
   allocation: allocate enough to keep spill offsets small first, spill CSRs, then
   (optionally establish FP, then) allocate the remainder
   (`getFirstSPAdjustAmount`, `RISCVFrameLowering.cpp:2064` and callers). If no
   frame pointer is established, **both** allocations get their own `UOP_AllocSmall`/
   `UOP_AllocLarge` code (since SP remains the only unwind anchor throughout). If a
   frame pointer *is* established between the two allocations, only the **first**
   gets a code — once FP is fixed, it alone determines the original entry-SP value
   (`CFA = FP + FrameRegisterAndOffset`), so further SP-only changes are invisible to
   the unwind mechanism by construction, and do not need (and must not have) their
   own codes. This exact reasoning is shared with why x86_64 never needs anything
   past its own single `UOP_SetFPReg`-then-done pattern; RISC-V just has an extra
   possible allocation *before* that point that x86_64 (with its unrestricted 32-bit
   immediate displacements) never needs.
4. **A large frame combined with an explicit/forced frame pointer is representable
   using the full `[0, 4080]` `FrameRegisterAndOffset` range (§3).** Because of
   point 3, whenever a split adjustment happens, the first allocation is always
   chosen close to 2048 bytes (`getFirstSPAdjustAmount`, `RISCVFrameLowering.cpp:2064`,
   `2048 - StackAlign`), since RISC-V's ABI mandates the frame pointer point at the
   frame's top boundary (the incoming SP). A decoder should expect
   `UOP_SetFPReg`/`FrameRegisterAndOffset` values up to `4080`, not just up to `240`
   as x86_64/ARM64 producers would ever emit.
5. **RVV (vector extension) frames, Qualcomm Xqci interrupt frames, Zcmp/Xqccmp
   push-pop-compressed frames, save/restore-libcall frames,
   and inline-stack-probed frames are all diagnosed as unsupported for SEH and never
   reach the encoder** — LLVM disqualifies Windows CFI generation entirely for these
   shapes (`RISCVFrameLowering.cpp`, several `DiagnosticInfoUnsupported` sites keyed
   on `RVFI->getRVVStackSize()`, `useQCIInterrupt`, `isPushable`,
   `getSpillLibCallName`, `hasInlineStackProbe`). A decoder
   never needs to handle unwind codes describing any of these; if it ever
   encounters `.xdata` claiming to, that indicates either a bug or hand-written
   assembly outside what the compiler can produce. None of these gates are hit by
   ordinary default-flags C/C++ compilation — they all require explicit, non-default
   compiler flags or attributes. (An earlier revision of this document listed a large
   local buffer plus an explicitly forced frame pointer, `-fno-omit-frame-pointer`
   style, as a reachable exception to this rule; that combination is now fully
   representable — see point 4's revision note. Stack realignment was likewise once
   listed here and is now supported — see point 7.)
6. **Funclets (`__except`/`__finally` outlined bodies) get their own, independent
   `RUNTIME_FUNCTION`/`UNWIND_INFO` pair**, exactly like a small ordinary function
   (this is generic/shared `WinException.cpp` behavior, not RISCV64-specific code —
   RISCV64 deliberately does *not* opt into ARM64's alternate funclet-marking
   scheme, `isAArch64`-gated code in `llvm/lib/CodeGen/AsmPrinter/WinException.cpp`).
   A decoder walking `.pdata` will see one entry per funclet in addition to one for
   the parent function; there is no explicit "this pdata entry is a funclet of that
   other one" marker — association is inferred purely by an unwinder consulting the
   parent function's C-specific-handler scope table (§8) at exception-dispatch time,
   not by anything in `.pdata`/`.xdata` structure itself.
7. **Stack realignment (an over-aligned local) is representable, and it produces
   no unwind code of its own.** When a function has a local whose alignment exceeds
   the ABI stack alignment, codegen realigns SP with an `andi sp, sp, -MaxAlign`
   (or, for `MaxAlign > 2048`, an `srli`/`slli` pair through a scratch register).
   This always happens *after* the frame pointer is established (`UOP_SetFPReg`),
   never before — so the realigning instruction, like the split-SP second
   allocation in point 3, is an SP-only change the frame pointer already accounts
   for and it gets **no unwind code at all**. The `FrameRegisterAndOffset` value is
   still `s0`'s offset from the *entry* SP (the frame's top boundary), which is a
   fixed quantity computed before the `andi` and therefore unaffected by however
   many bytes the realignment discards; a decoder recovers the CFA and every
   callee-save location from `s0` exactly as in the non-realigned case. This is the
   direct analog of x86_64's handling: x86_64 likewise emits its realigning
   `and rsp, -N` with no unwind code and relies solely on `UWOP_SET_FPREG`
   (`X86FrameLowering.cpp`, `BuildStackAlignAND` with no accompanying `SEH_`
   pseudo). If the function *also* has a variable-sized object (a VLA or dynamic
   `alloca`), codegen additionally captures the realigned SP into a base pointer
   with `mv s1, sp` (`RISCVABI::getBPReg()` is `s1`); this too is an SP-derived,
   post-`UOP_SetFPReg` move with no unwind code. In every realigned function the
   epilogue's first step is a "restore SP from FP" (`addi sp, s0, -N`, §7 step 1),
   which is what lets the frame be torn down without the realignment ever having
   been recorded. A decoder therefore needs no realignment-specific machinery: the
   `UOP_SetFPReg` it already handles covers this case completely.

## 7. Epilogue detection — no unwind codes describe the epilogue at all

**This is the most important section for a from-scratch unwinder implementation
(e.g. GDB), and the one place this format is genuinely x86_64-like rather than
self-explanatory from `.xdata` alone.**

Unlike ARM64 (which has explicit "epilogue scope" records describing exactly which
instructions undo the prologue and in what order), RISCV64 — like x86_64 — has **zero
unwind-code representation of the epilogue**. `UNWIND_INFO`'s codes describe only the
prologue. The x86_64-style optional "packed epilogue" opcode
(`Win64EH::UOP_Epilog`, used by some x86_64 producers as an optimization) is
**not used or supported by RISCV64 at all** — `RISCV64EmitUnwindCode`'s switch has no
case for it, and RISCV64 codegen never calls `emitWinCFIStartEpilogue`/
`emitWinCFIEndEpilogue` (`RISCVFrameLowering.cpp` contains no such calls, unlike
ARM64's frame lowering). This means:

> **A correct unwinder must recognize when the current PC is inside a function's
> epilogue by pattern-matching the actual machine instructions, exactly the way an
> x86_64 unwinder recognizes a `pop`/`add rsp`/`ret` epilogue sequence. There is no
> shortcut via `.xdata`.**

### Why this is safe / how x86_64 does it (background)

The x86_64 unwinder's approach (which this design deliberately mirrors) is:
starting from the current PC, scan forward through the instruction stream. If the
scan encounters a return (or an unconditional tail-jump out of the function) after
seeing *only* a recognized, bounded set of "epilogue-shaped" instructions (stack
deallocation, non-volatile register restores, frame-pointer-based SP recovery), the
unwinder concludes it is inside an epilogue and can simulate those instructions
forward to recover the register state at the point of return, **without consulting
`UNWIND_INFO` at all** for that part of the walk. If the scan does not match this
shape, the PC is treated as being inside the function body/prologue and the normal
`UNWIND_INFO`-code-driven walk is used instead.

This works because LLVM (like MSVC for x86_64) guarantees epilogues are a
mechanical, limited-vocabulary mirror image of the prologue — see
`RISCVFrameLowering::emitEpilogue` (`RISCVFrameLowering.cpp:1380-1520`).

### The exact RISCV64 epilogue grammar

An epilogue, in program order, consists of (each step optional, but always in this
relative order when present):

1. **At most one** "restore SP from FP" step: `addi sp, s0, -N` (or the compressed
   form, see below), present only when the function used
   `RestoreSPFromFP` (dynamic allocas, realigned stack, or no reserved call-frame —
   `RISCVFrameLowering.cpp:1432` region, `RestoreSPFromFP`). If present, this is
   always the **first** epilogue instruction.
2. **Zero or more** callee-saved-register reloads, each one of:
   - `ld <gpr>, <offset>(sp)` — restore a GPR (`ra` or `s0`-`s11`)
   - `fld <fpr>, <offset>(sp)` — restore an FPR (`fs0`-`fs11`)

   These appear in the **same relative order as they were saved in the prologue**
   (verified against compiled output — not reversed), and always address `sp`
   (never `s0`), even in functions that also have a frame pointer, since by this
   point in the epilogue SP has already been restored to the value it had
   immediately after the CSR-spill-area was allocated in the prologue.
3. **Zero, one, or two** stack-deallocation steps: `addi sp, sp, +N` (positive
   immediate; the mirror image of the prologue's `addi sp, sp, -N`). Two occur only
   in the "split SP adjustment" case (§6 point 3) without a frame pointer — first the
   mirror of the *second* prologue allocation, then the mirror of the *first*. Watch
   for the ADDI 12-bit immediate limit: a single logical deallocation can itself be
   split into **two consecutive** `addi sp, sp, +N1` / `addi sp, sp, +N2` instructions
   purely because the total doesn't fit in one 12-bit immediate (compare the worked
   example in §9) — this is unrelated to, and can co-occur with, the "split SP
   adjustment" case; a robust matcher should simply accumulate consecutive `addi sp,
   sp, +N` instructions rather than assuming exactly one or two.
4. **Exactly one** terminating construct, which is one of:
   - **A plain return**: `ret` (which RISC-V defines as an alias for `jalr x0, 0(ra)`),
     or `jr ra`.
   - **A tail call.** RISCV64 *does* emit tail calls, and does so routinely — the
     mid-level and codegen tail-call optimization is on at `-O1` and above, so any
     optimized build will contain them wherever a call is in tail position. A
     tail-call terminator takes one of these shapes (all with a **zero link register**,
     `x0`, which is what makes them a tail branch rather than an ordinary call):
     - **Indirect tail call** (`PseudoTAILIndirect`): a single `jalr x0, <rN>, 0`,
       where `<rN>` is a tail-call-clobbered GPR — `t1`/`t2`/`a0`-`a7`/`t3`-`t6`
       (the `GPRTC` class), i.e. **any GPR except `ra`**. The only thing that
       distinguishes this from `ret` is the source register (`ra` ⇒ return,
       anything else ⇒ tail call); either way it terminates the function and ends
       the epilogue.
     - **Direct tail call** (`PseudoTAIL`): a **two-instruction** sequence
       `auipc <rT>, %pcrel_hi(target)` immediately followed by
       `jalr x0, <rT>, %pcrel_lo(target)`, where `<rT>` is `t1` (`x6`) — or `t2`
       (`x7`) when the Zicfilp landing-pad extension is enabled
       (`getTailExpandUseRegNo`, `RISCVMCCodeEmitter.cpp`). This is the same
       `auipc`+`jalr` "call" pair used for ordinary calls (see the *Known
       limitations* section), but with `x0` as the
       link register. Note the `auipc` is a legitimate **epilogue** instruction here —
       a matcher that rejects `auipc` outright (see the algorithm below) will fail to
       recognize direct-tail-call epilogues.
     - **Relaxed direct tail call**: when call relaxation is enabled, the
       `auipc`+`jalr` pair above can collapse to a single `jal x0, target`
       (spelled `j target`), which RVC may in turn compress to `c.j target`. Relaxation
       is off by default for this target, so the `auipc`+`jalr` form is the one you will
       normally see, but a robust matcher should accept the relaxed forms too.

   In every tail-call case the full frame teardown (steps 1-3) still runs *before* the
   tail branch, exactly as it does before a `ret`, because the callee reuses this
   function's stack — so the grammar above is unchanged up to the terminator.

**Compressed (RVC) instruction forms**: since the C extension may be enabled (and
commonly is), every instruction class above has a compressed encoding a matcher must
also recognize, distinguished by their 2-byte vs. 4-byte length (RVC instructions
always have `bits[1:0] != 0b11`; standard instructions always have `bits[1:0] ==
0b11` — this is how a disassembler distinguishes them without any other context):

| Uncompressed | Compressed form | Constraint |
|---|---|---|
| `ld rd, off(sp)` | `c.ldsp rd, off` | `rd != x0`; `off` is a multiple of 8, `0..504` |
| `fld rd, off(sp)` | `c.fldsp rd, off` | `off` is a multiple of 8, `0..504` |
| `addi sp, sp, +N` (small) | `c.addi16sp +N` (**sp-only** form) | `N` is a nonzero multiple of 16, `-512..496` magnitude range applies only to the *negative* prologue form; the epilogue's positive `+N` form is likewise restricted to that magnitude |
| `addi sp, sp, -N` (prologue) | `c.addi16sp -N` | as above |
| `jalr x0, 0(ra)` / `ret` | `c.jr ra` | — |
| `jalr x0, 0(rN)` (indirect tail) | `c.jr rN` | `rN != x0`; `rN != ra` is what makes it a tail call rather than a return |
| `jal x0, target` / `j target` (relaxed direct tail) | `c.j target` | J-type ±1 MiB range; only appears when call relaxation is enabled |

A matcher that only special-cases the four-byte forms will silently fail to detect
epilogues in the (very common, RVC is on by default in most real-world RISCV64
configurations) case where the compiler emits compressed forms. Since `SizeOfProlog`
and unwind-code `CodeOffset` are already byte-granular specifically to accommodate
this (see §3), the epilogue matcher is the only place this needs *separate*,
explicit handling, since it has no equivalent "the format already handles it for you"
escape hatch — it must decode instruction lengths itself while scanning.

### Practical detection algorithm

```
is_in_epilogue(pc, function_end):
    ip = pc
    seen_reload_or_dealloc = false   // true once any reload or dealloc step has matched
    while ip < function_end:
        insn, len = decode_one_instruction(ip)   // handle both 2- and 4-byte forms
        if insn is (ld|c.ldsp) restoring {ra, s0-s11} from sp-relative offset:
            seen_reload_or_dealloc = true
        elif insn is (fld|c.fldsp) restoring {fs0-fs11} from sp-relative offset:
            seen_reload_or_dealloc = true
        elif insn is (addi sp, sp, +N) or (c.addi16sp +N):
            seen_reload_or_dealloc = true
        elif insn is (addi sp, s0, -N) and ip == pc:
            seen_reload_or_dealloc = true   // "restore SP from FP" only legal as the very first instruction seen

        // ---- terminating instructions ----
        elif insn is (jalr x0, rN, 0) or (c.jr rN):
            // rN == ra  -> ordinary return; rN != ra -> indirect tail call.
            // Both terminate the function and end the epilogue.
            return true
        elif insn is (jal x0, target) or (j target) or (c.j target):
            return true             // relaxed direct tail call (call relaxation enabled)
        elif insn is (auipc rT, imm):
            // Only legal in an epilogue as the first half of a direct tail call:
            // it must be immediately followed by (jalr x0, rT, imm) with a matching rT.
            next, _ = decode_one_instruction(ip + len)
            if next is (jalr x0, rT, imm2):   // same rT as the auipc
                return true         // direct tail call: auipc+jalr terminator
            return false            // a lone auipc is not epilogue-shaped

        else:
            return false            // not an epilogue shape; fall back to UNWIND_INFO-driven walk
        ip += len
    return false   // ran off the end of the function without hitting a return
```

Note the terminator is no longer just `ret`: an ordinary return and an *indirect* tail
call share the same `jalr x0, rN, 0` / `c.jr rN` encoding and differ only in whether
`rN` is `ra`, and a *direct* tail call terminates the epilogue with an `auipc`+`jalr`
pair (or a single relaxed `j`/`c.j`). A matcher that only accepts `ret`/`jr ra` — or
that rejects `auipc` unconditionally — will misclassify the very common optimized-build
case of a function whose last action is a tail call, treating its epilogue as function
body and producing a wrong unwind at exactly the PCs where a backtrace is most likely to
be taken.

This is a direct RISCV64 analog of the algorithm x86_64 unwinders already implement;
no new conceptual machinery is needed beyond substituting the RISC-V instruction
vocabulary above for x86_64's `pop reg`/`add rsp, N`/`ret` vocabulary.

## 8. `__try`/`__except` scope table (for context; not required for plain backtrace)

For functions using `MSVC_TableSEH` personality (`__C_specific_handler`), the handler
data immediately following the unwind-code array in `.xdata` (see §3's tail) is:

```
int32_t ParentFrameOffsetPlaceholder;  // see below — NOT part of the wire format
uint32_t NumCallSiteEntries;
struct { // one per __try scope
    uint32_t LabelStart;       // ADDR32NB into .text
    uint32_t LabelEnd;         // ADDR32NB into .text
    uint32_t FilterOrCatchAll; // 1 for a compile-time-constant "catch all" filter,
                               // or an ADDR32NB pointer to an outlined filter function
    uint32_t ExceptionHandler; // ADDR32NB into .text (the __except block)
} CallSiteEntries[NumCallSiteEntries];
```

This table format is entirely target-independent and not invented for this project —
it is Windows' standard `_CSpecificHandler`-consumed table, identical across
x86_64/ARM64/RISCV64 (`WinException::emitCSpecificHandlerTable`, `llvm/lib/CodeGen/
AsmPrinter/WinException.cpp`). The `ParentFrameOffsetPlaceholder` line above is
**not part of the on-disk table** — it refers to a separate `.text`-adjacent local
symbol assignment (`<mangled-name>$parent_frame_offset = N`, an assembler `.set`, not
a relocation) used only when an outlined filter/handler function needs
`llvm.eh.recoverfp`/`llvm.localrecover` to reach back into the parent's locals; it
has no fixed location in `.xdata` and a plain backtrace unwinder never needs to
consult it. See `RISCVFrameLowering.cpp`'s `SEHSetFrameOffset` assignment
(`WinEHFuncInfo::SEHSetFrameOffset`) if implementing exception dispatch (not just
backtrace) support.

## 9. Worked example

**This example is not illustrative — every byte below was extracted from actual
compiler output** (`llc -mtriple=riscv64-pc-windows-msvc -mattr=+d -filetype=obj` on
`llvm/test/CodeGen/RISCV/wincfi-seh-prologue.ll`'s `@func`, inspected with
`llvm-readobj -S --sd`), so it can be trusted as ground truth rather than a
hand-derived guess. `@func` has 6 GPR callee-saves (`ra`, `s0`-`s4`), 2 FPR
callee-saves (`fs0`, `fs1`), and a frame pointer:

```asm
func:
.seh_proc func
# %bb.0:
        addi    sp, sp, -64        # bytes [0,4)
        .seh_stackalloc 64
        sd      ra, 56(sp)         # bytes [4,8)
        .seh_savereg ra, 56
        sd      s0, 48(sp)         # bytes [8,12)
        .seh_savereg s0, 48
        sd      s1, 40(sp)         # bytes [12,16)
        .seh_savereg s1, 40
        sd      s2, 32(sp)         # bytes [16,20)
        .seh_savereg s2, 32
        sd      s3, 24(sp)         # bytes [20,24)
        .seh_savereg s3, 24
        sd      s4, 16(sp)         # bytes [24,28)
        .seh_savereg s4, 16
        fsd     fs0, 8(sp)         # bytes [28,32)
        .seh_savefreg fs0, 8
        fsd     fs1, 0(sp)         # bytes [32,36)
        .seh_savefreg fs1, 0
        addi    s0, sp, 64         # bytes [36,40)
        .seh_setframe s0, 64
        .seh_endprologue
        ...
        addi    sp, sp, 64
        ret
        .seh_endproc
```

Every instruction here happens to be 4 bytes (no RVC compression occurred in this
particular build), so `SizeOfPrologInBytes` = 40 exactly. The `.xdata` bytes for this
function's `UNWIND_INFO`, as they actually appear on disk (offsets relative to the
start of this `UNWIND_INFO` record):

```
offset  bytes        field
0x00    01 28 12 04  header: VersionAndFlags=01 PrologSize=0x28(40) NumCodes=0x12(18) FrameRegAndOffset=0x04
0x04    28 03        slot: CodeOffset=0x28(40) op-byte=0x03
0x06    24 15        slot: CodeOffset=0x24(36) op-byte=0x15
0x08    00 00        slot: value=0x0000
0x0A    20 05        slot: CodeOffset=0x20(32) op-byte=0x05
0x0C    01 00        slot: value=0x0001
0x0E    1C 54        slot: CodeOffset=0x1C(28) op-byte=0x54
0x10    02 00        slot: value=0x0002
0x12    18 44        slot: CodeOffset=0x18(24) op-byte=0x44
0x14    03 00        slot: value=0x0003
0x16    14 34        slot: CodeOffset=0x14(20) op-byte=0x34
0x18    04 00        slot: value=0x0004
0x1A    10 24        slot: CodeOffset=0x10(16) op-byte=0x24
0x1C    05 00        slot: value=0x0005
0x1E    0C 14        slot: CodeOffset=0x0C(12) op-byte=0x14
0x20    06 00        slot: value=0x0006
0x22    08 04        slot: CodeOffset=0x08(8) op-byte=0x04
0x24    07 00        slot: value=0x0007
0x26    04 72        slot: CodeOffset=0x04(4) op-byte=0x72
```

Decoded (op-byte's low nibble = `UnwindOp`, high nibble = `OpInfo`; codes are stored
on disk in reverse-chronological/prologue order, so this table reads "last prologue
step first," matching the "unwind by walking backward from now" consumption model):

| CodeOffset | op-byte | UnwindOp | OpInfo | 2nd slot | Meaning |
|---|---|---|---|---|---|
| 40 | 0x03 | 3 (`SetFPReg`) | 0 (unused, see §4) | — | FP established; real offset (64) is in the header, not here |
| 36 | 0x15 | 5 (`RISCVSaveFReg`) | 1 (`fs1`) | `0` (`0>>3`) | save `fs1` at `[sp+0]` |
| 32 | 0x05 | 5 (`RISCVSaveFReg`) | 0 (`fs0`) | `1` (`8>>3`) | save `fs0` at `[sp+8]` |
| 28 | 0x54 | 4 (`SaveNonVol`) | 5 (`s4`) | `2` (`16>>3`) | save `s4` at `[sp+16]` |
| 24 | 0x44 | 4 (`SaveNonVol`) | 4 (`s3`) | `3` (`24>>3`) | save `s3` at `[sp+24]` |
| 20 | 0x34 | 4 (`SaveNonVol`) | 3 (`s2`) | `4` (`32>>3`) | save `s2` at `[sp+32]` |
| 16 | 0x24 | 4 (`SaveNonVol`) | 2 (`s1`) | `5` (`40>>3`) | save `s1` at `[sp+40]` |
| 12 | 0x14 | 4 (`SaveNonVol`) | 1 (`s0`) | `6` (`48>>3`) | save `s0` at `[sp+48]` |
| 8  | 0x04 | 4 (`SaveNonVol`) | 0 (`ra`) | `7` (`56>>3`) | save `ra` at `[sp+56]` |
| 4  | 0x72 | 2 (`AllocSmall`) | 7 (`(64-8)/8`) | — | allocate 64 bytes |

Every `CodeOffset` matches the byte offset immediately after the instruction it
describes (e.g. `sd ra, 56(sp)` occupies bytes `[4,8)`, and its code's `CodeOffset` is
`8`) — confirming the "offset after the instruction" convention from §5. 18 codes
total (`NumCodes = 0x12 = 18`), even count, no padding slot needed, so the array ends
at offset `0x28` (= 4-byte header + 18×2 bytes = 4+36 = 40 = `0x28`), and (having no
handler/chain flags and a nonzero code count) the `UNWIND_INFO` record ends there too.

`FrameRegisterAndOffset = 0x04`: the whole byte is `offset / 16`, so `4 × 16 = 64`,
matching `.seh_setframe s0, 64` — confirming there is no separate register sub-field
to decode on this target, per the note in §3/§4 (this value was re-verified after
the format was widened from an earlier 4-bit/4-bit split, where the equivalent byte
would have read `0x40`; see §3's revision note for why).

### Widened-range example (split-SP adjustment + forced frame pointer)

To confirm the widened `[0, 4080]` range (§3's revision note) actually round-trips
through real object emission, not just the assembler-level directive check: compiling
a function with a large (4096-byte) local buffer, `uwtable`, and an explicitly forced
frame pointer produces `.seh_setframe s0, 2032` (the split-SP-adjustment's first
allocation, per §6 point 3/4), and the corresponding `UNWIND_INFO` header byte,
confirmed via `llvm-readobj -S --sd` on the compiled object, is:

```
FrameRegisterAndOffset = 0x7F
0x7F = 127; 127 * 16 = 2032  -- matches ".seh_setframe s0, 2032" exactly
```

`0x7F` would have been unrepresentable in the old 4-bit-offset shape (max `0xF0`,
i.e. offset `240`); this is exactly the case that motivated widening the format (§3's
revision note).

## Known limitations / things a decoder does not need to (and cannot) handle

These are limitations of what LLVM's *encoder* can currently produce, not
limitations of the wire format's theoretical expressiveness — a decoder never needs
to handle them because no compiler output will ever contain them:

- Frames with RVV, Qualcomm Xqci interrupts, Zcmp/Xqccmp push-pop, save/restore
  libcalls, or inline stack probing never reach `.xdata` at all
  (§6 point 5) — LLVM diagnoses and omits SEH info instead. (Stack realignment,
  once in this list, is now supported — §6 point 7.)
- `UOP_SaveNonVolBig` (offset > 512KiB−8) can theoretically be requested by the
  generic, target-independent `SaveNonVol()` factory but has no RISCV64 encoder
  support and would crash LLVM itself before ever reaching an object file — not
  reachable from any realistic compiled function.
- `UOP_RISCVSaveFReg` has no analogous "big" fallback at all (unlike `UOP_SaveNonVol`
  on x86_64, which has `UOP_SaveNonVolBig`); an FPR-save offset large enough to
  overflow the 16-bit `Offset >> 3` field would silently truncate rather than use an
  alternate encoding. This is a real (if exceedingly unlikely to trigger) latent gap
  in the encoder, noted here for completeness — not something a decoder can work
  around, since the bits actually on disk would already be wrong.
- Range-extension thunks for `JAL`/`BRANCH` do not exist for this target and are
  believed unnecessary: real-world RISC-V codegen (confirmed by inspecting LLD's
  mature RISC-V ELF port, which has never needed thunks either) essentially never
  produces an out-of-range direct `JAL`/conditional branch, since compilers use the
  effectively-unlimited-range `AUIPC`+`JALR` "call" pseudo-instruction for anything
  not provably nearby.
