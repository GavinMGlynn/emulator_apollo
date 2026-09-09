# Initialising a DS5500 volume on this core

Three runs, one INVOL option each, chained through `--disk-writeback`. The
result is `media/dn5500-invol-done.awd`, the DS5500's counterpart to
`media/dn3500-invol-done.awd`.

    head -c 364904448 /dev/zero > scratch/blank.awd

    APOLLO_DISK=scratch/blank.awd tools/dn5500-md.sh \
        --boot-script tools/dn5500/invol-badspots.script \
        --disk-writeback scratch/step7.awd --boot-limit 4200000000

    APOLLO_DISK=scratch/step7.awd tools/dn5500-md.sh \
        --boot-script tools/dn5500/invol-volume.script \
        --disk-writeback scratch/step1.awd --boot-limit 4200000000

    APOLLO_DISK=scratch/step1.awd tools/dn5500-md.sh \
        --boot-script tools/dn5500/invol-paging.script \
        --disk-writeback scratch/done.awd --boot-limit 4200000000

## Why three runs and not one

Loading `/sau14/invol` off the cartridge costs **1.55 G instructions** by itself
-- measured with `--boot-stop-pc`, not estimated -- and an option costs the rest
of a 4 G run. The chaining works because **INVOL is re-entrant one option at a
time and the disk carries the state between runs**, which is not true of the
restore that follows: `FINDINGS.md` C277.

The single-run ceiling was 4,294,967,295 instructions until 2026-09-10 and is
now the caller's bound, so these could be merged; they are kept apart because
one option per run is what was verified and because a failed option costs one
run rather than three.

## Four things in the dialogue that C50's table does not have

`FINDINGS.md` C50 recorded the DN3500's INVOL dialogue from a MAME session. It
is otherwise exact, and each of these cost a run here before it was clear:

1. **`Select disk:` is asked after *every* option**, not only option 7. C50
   lists it under option 7 alone, so a script built from the table sends the
   next answer into that prompt.
2. **`Anything more to do?`** follows an option, where the table goes straight
   back to `Option:`.
3. **Option 8 has two dialogues** -- the menu says "create *or modify*". On a
   volume that already has a paging file it asks `Do you wish to change its
   size?` first, so the create-path script cannot be reused to correct a size.
4. **The DS5500's default paging size is 1000 kB**, where the DN3500's is 640.
   Take the default the machine publishes, not the one another machine did.

`1 -f` is accepted -- "don't re-format disk" -- and is what makes option 1
affordable against a file-backed medium with no defects to find.
