# `.awdmeta`, the sidecar that carries what a raw sector image cannot

An `.awd` is sector data and nothing else. A real ESDI surface carries two more
things per sector, and `[OMTI]` §5 has commands that read and write both:

- the **ID field**, whose flags §5.4.7 sets for a bad track and §5.4.16 for an
  alternate — so `07 FORMAT BAD TRACK` differs from `06 FORMAT TRACK` only in a
  bit this image has nowhere to put;
- the **ECC field**, six bytes for an ESDI drive, which §5.4.27 READ LONG
  returns and §5.4.28 WRITE LONG is given.

Both were carried as deliberate approximations — zeros out, six bytes dropped —
until the decision was taken to extend the format so nothing is approximated.

## Why a sidecar and not the image

Three shapes were possible. Widening the sector to 1066 bytes breaks every
existing image's offset arithmetic. Appending a metadata trailer keeps one file.
A companion file keeps two.

`docs/references/DOMAINOS_IMAGE.md` decides it. That document **pins the image's
SHA-256** as the identity of an artefact that cost a full install to produce and
that it states "is not expected to be bit-identical" on rebuild. A trailer
changes the hash and invalidates the pin — the one record that lets anyone else
confirm their media match ours. A sidecar leaves the image byte-for-byte what the
document describes.

So the raw image is never written by this feature, and a reader that knows
nothing about `.awdmeta` sees exactly the file it saw before.

## The file

`<image>.awdmeta`, beside `<image>.awd`. **Optional.** Absent, the drive is a
defect-free surface with no recorded ECC — which is what a raw image *is*, so the
default is a description rather than a fallback, and every image in `media/`
today keeps working unchanged.

    offset  size  field
    0       8     magic, "AWDMETA1"
    8       4     header bytes, little-endian (16 for this version)
    12      4     per-sector bytes, little-endian (7 for this version)
    16      ...   per-sector records, sector 0 first, contiguous

The per-sector record is seven bytes:

    0       1     flags
    1       6     ECC, as recorded

    flags bit 0   bad track          -- §5.4.7, sense 19 on access
    flags bit 1   alternate assigned -- §5.4.16
    flags bit 2   is an alternate    -- §5.4.16, direct access is sense 1C
    flags 3-7     reserved, written zero

Both length fields are in the header so a reader can skip a record it does not
understand and a later version can grow one without a second magic. A file whose
per-sector size exceeds this version's is read for the fields it does know and
the remainder ignored; a file shorter than the sector count it is paired with
describes the sectors it covers and no more, which is the same rule `ap_awd`
already applies to a short image.

## What it is not

**Not a bad-block list.** The flags are per *sector* because that is where an ID
field lives; a bad *track* is every sector of that track carrying the flag, which
is what `06`/`07` write and what makes `07` differ from `06` at all.

**Not ECC this core computes.** The six bytes are storage, not a polynomial —
`[OMTI]` does not publish one, and a value this core invented would be
indistinguishable from a recorded one. WRITE LONG keeps what it is given and
READ LONG returns it; a sector never written by WRITE LONG has zeros, which is
"none recorded" and not a claim.

**Not gitignored by accident.** `media/` stays gitignored and so does this: it
describes a disk we are not entitled to redistribute, and a defect list is part
of that disk.
