# The emulator/oracle full-state differential

Both machines dump **every** field they model, run to the same instant, and the
dumps are compared field by field. This replaces hypothesis-driven probing,
which in one session produced ten failed candidates and four comparisons that
turned out to be measuring differently-configured machines.

A hash says two states differ. A dump says *where*.

## Our side

    apollo-headless … --dump-state FILE

`--dump-state` is the identity harness's own traversal with an output attached —
`ap_hash_t` gained an optional stream, so every `ap_hash_*` call in the walk
emits a line as it absorbs. It is **not** a second walker, because two walkers
drift and the drift is silent in the worst direction: the dump shows two
machines agreeing on every field it visits while their hashes differ, because
the differing field is one it does not visit.

`ap_machine_dump_state` returns the hash of the walk it wrote, and the report
prints it beside the ordinary `state hash`. **Check they match before trusting a
dump** — the report flags it if they do not.

Format: `scope.index type value`, e.g. `timer.007 u8 0000000000000034`. Blobs
(main memory, frame buffers) emit the running hash *after* absorbing rather than
their contents, so a difference shows without burying the dump in megabytes.

## The oracle's side

Requires the temporary instrumentation in `apollo-state-dump.patch`:

    git -C ext/mame apply ../../tools/mame-oracle/apollo-state-dump.patch
    # build SUBTARGET=apollo as FINDINGS.md describes

It adds `save_manager::apollo_dump_text` and binds it as
`machine:apollo_dump_state(path)`. That walks `m_entry_list` — **the same
registry the serialiser uses**, so a field MAME saves is a field it prints, with
the name MAME registered it under. Then:

    APOLLO_STATE_DUMP=out.txt APOLLO_STATE_DUMP_AT=<emulated seconds> \
      mdsession.py --stage watch --disk <copy>

Format: `module.tag.name.block.index uN value`.

**`ext/mame` is never committed with this applied.** The patch is the artifact;
the working tree is reverted.

## Where the two machines are stopped, which is not "at the same time"

`tools/state-compare.sh` runs both halves. The point it stops them at is a
**program counter and a visit count**, not an instant.

An earlier version synchronised on emulated seconds, reasoning that both
machines measure time the same way while instruction counts are not comparable
across two cores. The second half is true; the conclusion does not follow. Two
cores whose timing differs at all are at different points in the program at the
same instant, so a diff taken there compares two unrelated machine states — the
same error as comparing snapshots of two machines with no shared clock, which
has already cost this project five withdrawn conclusions. "The Nth time this
instruction is about to run" means the same thing on both machines however fast
either got there, and it is the only kind of point at which a difference can be
*attributed*, because both machines executed the same instructions to reach it.

### The oracle stops with a breakpoint, not a tap

Five earlier attempts used memory taps and all five failed the same way:
`install_read_tap` sees bus reads and cannot tell an instruction fetch from a
data read of the same address; gating on the PC then fails the other way,
because by the time the tap fires the PC has moved. A tap is the wrong
instrument for "stop here".

`statesync.lua` uses `-debug -debugger none`, which gives a working
`device_debug` with no window to want, and `bpset` stops exactly at an
instruction boundary. There is no UI to notice the stop, so the script polls
`execution_state` and drives it.

### And it steps once, because the two stops are on opposite sides

MAME's breakpoint stops **before** executing the instruction at the address.
This core's `--boot-stop-pc` stops **after** it: `--boot-stop-pc 653A` ends with
`PC = 6542`. Diffed directly, every register that instruction touched differs —
a difference in the harness that reads exactly like a difference in the
emulator. So the oracle steps one instruction after the hit and lands where we
do. `step` is asynchronous and the first poll after it can still show the old
PC, so the script waits for the PC to *move* rather than for the debugger to be
stopped.

## Comparing them

The two cores name nothing alike, so a diff needs a **field mapping**: which of
our scopes and indices correspond to which of MAME's registry names. That table
is the real deliverable — it is a written correspondence between the two models,
and building it will itself surface fields one side has and the other does not.

## The comparison tool

    tools/state-diff.py OURS THEIRS --map FILE [--show-unmapped]

Built around the mapping being **incomplete for a long time**, which is the
condition that makes a differential lie if it is not designed for. It separates
three populations, and conflating them is how this produces confident nonsense:

  * **matched** — mapped on both sides; the only values that mean anything.
  * **unmapped** — present with no counterpart declared. **Not a difference.**
  * **missing** — mapped, but the named field is absent. A real finding: one
    core models something the other does not, or a name moved.

It never guesses a correspondence from a similar-looking name. A wrong mapping
shows two unrelated fields agreeing — a silent false negative exactly where a
differential is supposed to be trustworthy.

The map is `ours <TAB> theirs`. A trailing `*` on both sides maps a whole scope
**positionally**, which maps a device in one line once its field order is known
to agree — and the tool refuses to do it when the two scopes have different
lengths, because then the orders cannot be assumed to correspond.

## The protocol at each difference

1. **This core is wrong** → fix it, against the reference documentation, with
   the citation in the commit.
2. **The oracle is wrong** → do *not* match it. Add a named quirk
   (`model/ap_quirk.h`): documented behaviour by default, the oracle's under
   `--oracle-quirk NAME`, so the run can be carried past the difference instead
   of drowning in its consequences.
3. **Undecided by the documents** → record it as open, in the findings file.

At the end, the accumulated quirks are revisited: which of them factually
support the crash, and which are differences that can be ignored.

## The trap this exists to avoid

Confirm each run's configuration **from its own output**, never from a machine
or file name. Four comparisons in one session were invalid because of that, the
last in a run set up specifically to be matched.
