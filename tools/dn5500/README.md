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

**It ends with `shut`, which `008860-A03` and the SR10.3 route both require** --
Domain/OS is a single-level store and the root directory's updated links sit in
the node's cache until the *guest* shuts down.

**What `shut` is NOT, corrected 2026-09-11:** it is not what decided whether this
volume booted, and this file said it was. The restore was re-run *with* it, the
guest printed `Shutdown successful`, and the DS5500 gave the identical
`boot error: SAU14 not found in root_dir`. The DN3500 control then restored the
same cartridge with **no `shut` at all** and took its root directory from 3
entries to 17. So links are written without it here, and the DS5500's failure was
elsewhere: its root record (blocks **165750-165753**, four sectors to a record)
came out **byte-identical to the virgin INVOL input** while 50,916 other blocks
changed. One variable separated the two runs: the CPU. The 68040's ATC was never
writing `M` back to the page descriptor, so a single-level store had no dirty
directory page to page out -- a defect established from the manual and fixed on
2026-09-11, *and* the mechanism that fits this failure, though the run that
proves it is the cause is the restore re-run on the fixed core. See
`PROJECT_STATUS.md`.
`FINDINGS.md` C192's DN3500 measurement of 3 root entries against 17 stands for
what it measured, an *unclean end* against a clean one, and does not generalise
to a run that ends inside its bound.

`shut` belongs to the `)` prompt of the Phase II environment, which is where
RBAK leaves the machine, so no EOT is needed -- unlike the MINST route, where
`shut` has to escape a shell first (`sr10-3-install-route`).

**No CALENDAR step**, and that is checked rather than assumed: the 14-day gate
compares the RTC against the volume's own timestamps, `dn5500-md.sh` starts the
clock at 2002-11-28, and INVOL stamped this volume "Nov. 28, 2002". The DN3500
route needs CALENDAR because it restores onto a volume whose timestamps are
years from its clock.

## Booting the volume

    APOLLO_DISK=scratch/restored.awd tools/dn5500-boot.sh

`tools/dn5500-boot.sh` is `dn5500-md.sh` **without `--service-mode`**, which
`008860-A03` p. 1-12 step 1 says not to use for an install and which turns out to
be unnecessary anyway: a node with no bootable volume falls back to MD on its
own. Its console is the discriminator, and each line means something different:

| the machine prints | what it means |
| --- | --- |
| `error: sysboot not found` | the boot area is empty or mis-shaped |
| `error: incorrect sysboot installed` | the records are there and the tag is wrong |
| `boot error: SAU14 not found in root_dir` | the SYSBOOT **ran** and mounted the volume; a directory is missing |
| `Loaded: SELF_TEST Revision: …` | the boot path is whole |

## What comes after the restore, from `008860-A03` rather than from guesswork

The restore leaves a volume that mounts and boots as far as its root directory.
What is still missing is `/sau14` **as a root-directory entry**, which is
Chapter 1's **Step 4**, and the manual's route is:

1. **`GO`** at the `)` prompt. This is the step this project never had: it
   "starts the DM and the login prompt appears. If you are at a DSP, the SPM is
   started" (p. 1-17).
2. Log in as **`user`**. `minst` **starts automatically** on that login after a
   boot from distribution media (pp. 1-18, 10-44) — no command is typed for it.
3. Answer `minst`, **or quit it and use the tools directly**. Chapter 10 says
   both work: "You can do everything `minst` does by invoking other commands
   directly", and Chapter 5 gives the command lines, every one of which has a
   known completion line and needs no interactive answer:

       install/tools/rbak_sr10 -dev ct0 -ms -sacl -pdt -force -du -f 1 -all
       install/tools/distaa -f -m c AA
       install/tools/install -vx -s AA -c AA/install/templates/apollo/os.v.10.4/cf.<product> //node_<id>

   `config` is skippable because "the default configuration file that ships with
   every product" can be handed straight to `install` (p. 5-12).

**No SAU selection is needed.** p. 10-46: "novice mode installs only the `/sau`
directory for the machine type of the target node". The DN3500 run got `/sau7`
because its target was a DN3500; a DS5500 run gets `/sau14` for the same reason.
The **template** number is the aegis/bsd/sys5 size, a different question, and the
Authorized Area gets *every* SAU regardless (p. 1-24).

**The template names are known without running anything.** `019594-001` carries
them as file names, and the three prefixes are `008860-A03`'s: `aa.` a selection
file, `ov.` its override, `cf.` a configuration file.

    aa.aegis_small      aa.aegis_medium        aa.aegis_large    aa.aegis_large3
    aa.aegis_smallm     aa.aegis_small_prog
    aa.bsd4.3_medium    aa.bsd4.3_large        aa.bsd4.3_mediumm
    aa.sys5.3_medium    aa.sys5.3_large        aa.sys5.3_mediumm
    aa.aegis_bsd4.3_medium   aa.aegis_bsd4.3_large
    aa.aegis_sys5.3_medium   aa.aegis_sys5.3_large   aa.aegis_sys5.3_large7
    aa.large   aa.large3   aa.hlp   aa.template

    cf.os   cf.pas   cf.named   cf.dpss_only   cf.tcp_only   cf.tcp_and_dpss

**The product is `os` version `10.4`** — `008860-A03` p. 5-18's sample `config`
session lists it that way — so the default configuration file a scripted install
hands to `install -c` is

    AA/install/templates/apollo/os.v.10.4/cf.os

and a subset load hands `distaa` one of the `aa.` paths in the same directory.

**And `select all` is safe on this media.** p. 5-14 warns that `config` prefers
an **a88k** build (a `.p` version suffix) over the m68k one, and Chapter 12 says
an ISP mismatch "may prevent the target from booting" — but `019594-001`,
`019594-002` and `019593-001` carry **zero** `ri.apollo.os.v.10.4.p` against
97,495, 122,650 and 800 of the plain form. These cartridges are m68k only.

**What is interactive is the media and only the media**: "If the set of
distribution media contains more than one tape volume, `minst` prompts you in
sequence to insert each tape into the drive" (p. 10-45). That is what
`--boot-script`'s `swap PATH` exists for. The first product volume is
**`019594-001`, not the boot volume** (p. 5-4), and all four are in
`media/domainos/`.

*Not scripted here yet, deliberately.* The prompt **texts** are what a script
needs and they have not been read off a running DS5500;
`tools/mame-oracle/install-sau14.cmds` drives the DN3500's MINST on time rather
than on text, which does not translate to this frontend's `expect`. The
tool-level route above is the one to script, because every step of it ends in a
line the manual prints.

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
