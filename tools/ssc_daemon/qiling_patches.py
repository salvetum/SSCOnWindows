#!/usr/bin/env python3
"""Qiling patches for the SSC blob on Windows (no WSL/qemu).

Unicorn 2.x does not implement ARMv8.1 LSE atomics (casa/casl/swpl/ldadda
etc.). The SSC blob (Android/bionic build) uses them heavily. Unicorn raises
an illegal-instruction interrupt (intno 1) for each one; this module emulates
the LSE family at that point and continues.

Register decode (AArch64 LSE atomic, shared by CAS/SWP/LD* family):
    Rs              bits [20:16]
    Rn (base addr)  bits [ 9: 5]
    Rt (operand)    bits [ 4: 0]
    size            bits [31:30]  (0b11 = 64-bit, else 32-bit)

Install by attaching to any Qiling instance before ql.run():
    from qiling_patches import install_lse_atomics
    install_lse_atomics(ql)
"""

import struct

from qiling import Qiling

# LSE opcode prefixes (mnemonic from capstone), mapped to an operation.
# op              = RMW : read-rmw-write; result register Rs = old value
#                   NO  : write-only (memory op), no destination register
# cas*            = compare-and-swap (Rs = expected/result, Rt = new)
# swp*            = swap (Rs = result, Rt = new)
LSE_CAS_PREFIXES = ("cas",)
LSE_SWP_PREFIXES = ("swp",)
LSE_LDADD_PREFIXES = ("ldadd",)
LSE_STADD_PREFIXES = ("stadd",)
LSE_LDCLR_PREFIXES = ("ldclr",)
LSE_STCLR_PREFIXES = ("stclr",)
LSE_LDEOR_PREFIXES = ("ldeor",)
LSE_STEOR_PREFIXES = ("steor",)
LSE_LDSET_PREFIXES = ("ldset",)
LSE_STSET_PREFIXES = ("stset",)


def _load(ql: Qiling, addr: int, size: int) -> int:
    return int.from_bytes(ql.mem.read(addr, size), "little")


def _store(ql: Qiling, addr: int, size: int, value: int) -> None:
    ql.mem.write(addr, value.to_bytes(size, "little"))


def _handle_lse_intr(ql: Qiling, intno: int):
    """Interrupt hook for ARM64 LSE atomics (unicorn: illegal instruction)."""
    from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM

    pc = ql.arch.regs.arch_pc
    insn = int.from_bytes(ql.mem.read(pc, 4), "little")

    size = 8 if (insn >> 30) & 0x3 == 0x3 else 4
    mask = (1 << (size * 8)) - 1

    rs = (insn >> 16) & 0x1F
    rn = (insn >> 5) & 0x1F
    rt = insn & 0x1F

    md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
    mnemonic = next(iter(md.disasm(insn.to_bytes(4, "little"), pc, 1)), None)
    if mnemonic is None:
        raise RuntimeError(f"cannot decode instruction at {pc:#x} ({insn:#010x})")
    name = mnemonic.mnemonic

    addr = ql.arch.regs.read(rn)
    old = _load(ql, addr, size)

    if name.startswith(LSE_CAS_PREFIXES):
        # CAS: if mem == Rs(exp) then mem = Rt(new); Rs = mem(old) always
        expected = ql.arch.regs.read(rs)
        new = ql.arch.regs.read(rt)
        if old == expected:
            _store(ql, addr, size, new)
        ql.arch.regs.write(rs, old)
    elif name.startswith(LSE_SWP_PREFIXES):
        # SWP: mem = Rt(new); Rs = mem(old)
        new = ql.arch.regs.read(rt)
        _store(ql, addr, size, new)
        ql.arch.regs.write(rs, old)
    elif name.startswith(LSE_LDADD_PREFIXES):
        _store(ql, addr, size, (old + ql.arch.regs.read(rt)) & mask)
        ql.arch.regs.write(rs, old)
    elif name.startswith(LSE_STADD_PREFIXES):
        _store(ql, addr, size, (old + ql.arch.regs.read(rt)) & mask)
    elif name.startswith(LSE_LDCLR_PREFIXES):
        _store(ql, addr, size, old & ~ql.arch.regs.read(rt) & mask)
        ql.arch.regs.write(rs, old)
    elif name.startswith(LSE_STCLR_PREFIXES):
        _store(ql, addr, size, old & ~ql.arch.regs.read(rt) & mask)
    elif name.startswith(LSE_LDEOR_PREFIXES):
        _store(ql, addr, size, old ^ (ql.arch.regs.read(rt) & mask))
        ql.arch.regs.write(rs, old)
    elif name.startswith(LSE_STEOR_PREFIXES):
        _store(ql, addr, size, old ^ (ql.arch.regs.read(rt) & mask))
    elif name.startswith(LSE_LDSET_PREFIXES):
        _store(ql, addr, size, old | (ql.arch.regs.read(rt) & mask))
        ql.arch.regs.write(rs, old)
    elif name.startswith(LSE_STSET_PREFIXES):
        _store(ql, addr, size, old | (ql.arch.regs.read(rt) & mask))
    else:
        raise RuntimeError(f"unhandled LSE mnemonic {name} at {pc:#x}")

    ql.arch.regs.arch_pc = pc + 4
    return True


def _handle_rseq(ql: Qiling, ptr: int, size: int, flags: int, sig: int) -> int:
    """rseq(2): not implemented by Qiling 1.4.x. Return ENOSYS so glibc treats
    the kernel as lacking rseq support and falls back to getcpu/tls paths
    (byte-exact with qemu-user, which also lacks rseq on this rootfs)."""
    return -38  # -ENOSYS


def install_rseq(ql: Qiling) -> None:
    """Attach an rseq stub to a Qiling instance (idempotent)."""
    ql.os.set_syscall(293, _handle_rseq)  # aarch64 rseq


def install_lse_atomics(ql: Qiling) -> None:
    """Attach LSE-atomics emulation to a Qiling instance (idempotent)."""
    for hook in ql._hook.get("intno", []):
        if getattr(hook, "callback", None) is _handle_lse_intr:
            return

    ql.log.info("qiling_patches: enabling ARM64 LSE atomics emulation")
    ql.hook_intno(_handle_lse_intr, 1)