# Oracle findings

One row per **probe campaign**: a question asked of both implementations, the
number each gave, and how the disagreement was settled. This is the ledger that
makes "cycle-accurate" a checkable claim rather than an assertion.

MAME's `apollo` driver is the runnable oracle. It is **built and instrumented,
never linked** — GPL-2.0-or-later against this core's MIT, so the relationship is
arms-length: MAME runs as a separate program and its output is compared with
ours. See `ext/README.md`.

## The rule that makes this file worth keeping

**A difference is not automatically our bug.** MAME's own driver notes admit gaps
in some FPU operations and operands, Winchester bad-track formatting, and certain
video synchronisation. So every disagreement is *classified*, never silently
"fixed" by moving our number to match:

| Class | Meaning | What happens |
| --- | --- | --- |
| `ours-wrong` | The oracle is right and we are not | Fix ours; the row cites the probe that proves it |
| `oracle-wrong` | We are hardware-truer than MAME | **Keep ours.** Instrument MAME to demonstrate it, cite the manual page, and record it as a divergence |
| `sub-poll-slack` | Both are within the granularity the probe can actually resolve | Neither is wrong; record the resolution limit so it is not re-litigated |
| `open` | Not yet settled | Must name what measurement would settle it |

Resolving a discrepancy by adjusting our timing until it matches is **forbidden**
— that is trial-and-error parameter search against our own code, and it launders
the oracle's bugs into ours. Characterise the *shape* of the difference first
(proportional? fixed offset? per-instruction-type? per-operand?), because the
shape names the cause.

## Campaigns

None yet. The oracle harness is not built, so no campaign has run and there is
nothing to report. This section stays empty rather than being pre-populated with
plausible-looking rows.

When the first campaign lands, each row carries:

| Field | Meaning |
| --- | --- |
| Campaign | What was asked, and the probe that asks it |
| Ours | Our figure, in `AP_TIME_BASE_HZ` units and in the subsystem's own cycles |
| Oracle | MAME's figure, with the instrumentation that produced it |
| Class | One of the four above |
| Evidence | Manual page, patent, or ROM address — an oracle number alone never closes a row |
| Story | Why they differed, in a sentence, so a future contradiction has history |

## Instrumenting the oracle

Temporary instrumentation in `ext/mame` is **always reverted before commit**,
in the oracle's checkout as well as ours. Edit, measure, restore — never
`git checkout <file>` over uncommitted work. A campaign that needed
instrumentation records what was patched and where, so it can be reproduced
without guesswork.

The oracle build is deliberately narrow — one driver, no tools:

```sh
cd ext/mame
make SOURCES=src/mame/apollo/apollo.cpp SUBTARGET=apollo TOOLS=0 -j"$(nproc)"
```

This produces `mameapollo` rather than a full MAME. It is never built by our
CMake, never linked, and CI never compiles it.

## Where the ring is not

The Apollo Token Ring has **no runnable oracle at all**: MAME carries Domain
networking over an emulated 3c505 802.3 card instead. Ring figures therefore
never appear in this file as an oracle comparison. They live in
`docs/references/RING.md`, each citing `010005-00`, patent 4,716,575,
`008778-03`, or a ring-firmware disassembly address.

The one exception is the 3c505 itself, which *is* modelled by MAME: when that
path is implemented it gets ordinary rows here, like any other device.
