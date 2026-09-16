# Changelog

## 1.0.0 — 2026-09-16

The first release. 2,785 commits, 136 test suites, 3,358 tests, built between
2026-07-31 and 2026-09-16.

### What it does

Domain/OS **SR10.2, SR10.3 and SR10.4** boot to a login prompt, on the 68030 and
on the 68040. Two emulated nodes exchange frames over an emulated **Apollo Token
Ring** and see each other's files — which is, as far as we can establish, the
first emulation of that ring by anyone. SCSI, cartridge tape and floppy
round-trip real media: `wbak`/`rbak` to an EXABYTE EXB-8200 completes 102
commands and 102 completions on two different machines.

Twelve models from one table, DN3500-first as the reference superset.

### How it is built

One `tick()` per machine cycle, no batching and no event queues, so bus
contention is **emergent** rather than tabulated. The bus is arbitrated inside
each processor cycle rather than between instructions, which is what lets a DMA
controller take the bus between two writes of a `MOVEM` as the hardware does.

Time is counted in `AP_TIME_BASE_HZ` units — the LCM of every clock in the
machine — never in CPU cycles, because several nodes of different models share
one 12 Mbit/s ring and no CPU's cycle is a legal unit of account. A clock the
base cannot represent exactly is rejected at construction rather than rounded.

### How it is verified

- **A state-hash identity harness** on two machines, re-run after every change:
  DN3500 `A0969377600D155E` at 350 M instructions, DS5500 `DFC7700AF195DBE9`.
- **MAME as a runnable oracle**, built and instrumented, never linked — it is
  GPL-2.0-or-later and this core is MIT.
- **Bare-metal probes** that make the machine measure itself, run identically
  under this emulator and the oracle and diffed.
- **Goldens in CTest** on every platform, asserting that `-O0` and `-O3` agree,
  because emulated cycle counts are not wall-clock measurements.
- **A paper oracle for the ring**, every figure citing its manual page.

Claims about the tree are checked by the tree: `doc_claims` verifies 5,819
assertions in the living documents — test counts, cited paths and symbols,
document-walk coverage, the `PROVISIONAL` register's census, the stored-cursor
census, and that every test suite is accounted for by some status row.

### What it is not

**Not 100% of the hardware**, and it says where:

- **Six of the twelve models have no runnable oracle** (DN2500, DN3550, DN4000,
  DN4500, DSP3550, DSP4500) and are verified against documents and their own
  firmware alone. That is a weaker claim and is made as one.
- **33 figures and 29 readings are `PROVISIONAL`** — quantities chosen because a
  manual published a range, a bound, or nothing at all. Each carries its reason
  and what would close it, in the register at the end of `PROJECT_STATUS.md`.
- **`007861-A01` does not exist** on any archive. Its absence is why the DN5500's
  I/O protection map entry layout and the board cache's condition-code format
  are provisional; the search is recorded rather than the gap left unexplained.
- The **SDL frontend is deliberately not started** — the headless frontend is
  the deterministic one, and stubbing an interactive one would be pretending.

### Getting started

[`docs/REQUIREMENTS.md`](docs/REQUIREMENTS.md) is the complete manifest of
software, firmware, media and documentation — what each is for and where to get
it. A clone plus `ext/unity` builds and passes all 153 CTest entries with **no
firmware at all**; firmware and Domain/OS media are yours to supply, because they
are not ours to redistribute.
