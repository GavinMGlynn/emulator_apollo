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

## Comparing them

The two cores name nothing alike, so a diff needs a **field mapping**: which of
our scopes and indices correspond to which of MAME's registry names. That table
is the real deliverable — it is a written correspondence between the two models,
and building it will itself surface fields one side has and the other does not.

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
