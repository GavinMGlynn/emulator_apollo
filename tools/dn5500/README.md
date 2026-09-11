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

## The SR10.4 restore, which is the step after INVOL

`restore.script`, with `tools/dn5500-md.sh` and the boot cartridge:

    cp media/dn5500-invol-done.awd scratch/vol.awd
    APOLLO_DISK=scratch/vol.awd tools/dn5500-md.sh \
        --boot-script tools/dn5500/restore.script \
        --disk-writeback scratch/restored.awd --boot-limit 40000000000

**It ends with `shut`, and that line is the whole difference between a volume
that boots and one that does not.** Domain/OS is a single-level store: the root
directory's updated links sit in the node's cache until the *guest* shuts down.
A run of 2026-09-11 without it restored 396 entries including `(dir) "sau14"`,
wrote a ` M68K_4K ` SYSBOOT into the boot area, and produced a volume the DS5500
mounted and then rejected with `boot error: SAU14 not found in root_dir` --
objects on the disk, links never written. `FINDINGS.md` C192 measured the same
thing on a DN3500 as 3 root entries against 17.

`shut` belongs to the `)` prompt of the Phase II environment, which is where
RBAK leaves the machine, so no EOT is needed -- unlike the MINST route, where
`shut` has to escape a shell first (`sr10-3-install-route`).

**No CALENDAR step**, and that is checked rather than assumed: the 14-day gate
compares the RTC against the volume's own timestamps, `dn5500-md.sh` starts the
clock at 2002-11-28, and INVOL stamped this volume "Nov. 28, 2002". The DN3500
route needs CALENDAR because it restores onto a volume whose timestamps are
years from its clock.

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
