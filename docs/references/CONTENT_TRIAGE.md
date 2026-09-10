# Triage by content — the shelf documents a full-text scan can settle

A companion to `SOFTWARE_SHELF_WALK.md`. Where that file lists what has not been
read, this one records **what was measured about the unread documents** and what
that measurement can and cannot decide.

## The method, and its one honest limit

Every remaining shelf PDF's **entire text** was extracted and searched for the
vocabulary a hardware fact is written in:

```
register | IRQ | interrupt vector | DMA channel | hexadecimal address |
bus address | CSR | jumper | MC680xx | 68020 | 68030 | 68040 |
controller board | address space | nanosecond | megahertz | MHz
```

**A document whose full text contains none of these cannot carry a hardware fact
this core needs.** That is a claim about the text, not about the document — and
the difference is the whole content of the next section.

## The trap this triage nearly published

Twenty-three documents came back with zero hits. **Seven of them have no text
layer at all**: the brochures extract between **two and six characters** each,
because they are page images. `pdfimages -list` confirms it — `Apollo Series
3500 Brochure`, two pages, two images; `DN440/460 Brochure`, six pages, six
images.

**A scan that finds nothing in a document it cannot read has measured nothing**,
and reporting those seven alongside the sixteen would have been the same mistake
as the audit that "found" four walked documents unread. They are separated below
and the reason is stated on each row.

*This is the fifth time in one session that a check reported clean because it
could not see* — `check_docs` behind a pipe, the path audit against tag-cited
documents, the vector table behind a loose regex, and `pdftotext` on a scan,
twice.

## Settled by the scan: 16 documents, 4.11 M characters, zero hardware terms

| Document | Text |
| --- | --- |
| `005808-01` *Programming With Domain Graphics Primitives* Feb 87 | 525,209 |
| `001746-06` *Administering Your DOMAIN System* Jun 87 | 464,968 |
| `008860-A03` *Installing DOMAIN Software* Mar 92 | 452,048 |
| `005812-00` *3D Graphics Metafile Resource Call Reference* Dec 85 | 424,925 |
| `008860-A02` *Installing Domain Software…* Oct 89 | 346,868 |
| `008543-01` *Configuring and Managing TCP/IP* Jan 87 | 290,031 |
| `009793-00` *2D Graphics Metafile Resource Call Reference* Nov 86 | 284,087 |
| `008788-A01` *Getting Started With DSEE* Apr 91 | 271,397 |
| `015363-A00` *Administering the Domain/OS Registry* Sep 90 | 253,167 |
| `005808-00` *Programming With DOMAIN Graphics Primitives* Jan 87 | 218,449 |
| `011418-A00` *Display Manager Command Reference* Jul 88 | 180,677 |
| `008667-A00` *Using TCP/IP Network Applications* Jul 88 | 152,638 |
| `HP_Apollo_Documentation_Catalog` Feb 91 | 77,047 |
| `011717-A00` *Making the Transition to SR10 TCP/IP* Jul 88 | 75,087 |
| `Apollo_Documentation_and_Software_Replacement_Media_Catalog` Jun 89 | 67,318 |
| `002685-A01` *Apollo Documentation Quick Reference* Jul 89 | 30,851 |

**These are API references, administration guides, installation procedures and
documentation catalogues.** They describe software this core *runs* and
procedures a user follows; none of them describes a part. Marked **read no
further**, on the evidence above, and reopenable by anyone who finds a reason —
the scan's vocabulary is printed at the top so its blind spots can be argued
with.

## Not settled: 7 brochures, no text layer

`Apollo Series 3500`, `Apollo Series 4500`, `Apollo Series 10000`,
`Apollo Product Line`, `Apollo DomainOS`, `DN590-T`, `DN440/460`. Two to six
pages each, entirely images.

**They are marketing brochures and the shelf record already declares that class
not worth walking** — a judgement made on their subject, which is a different
and weaker basis than the scan above, and which is why they are in their own
section rather than the table. *`Apollo Series 3500` and `DN590-T` are the two
worth a render if a model-table field is ever in dispute*: a brochure prints
configured specifications, which is exactly the weak-source problem `CFG_WALK.md`
records, so they would be a last resort rather than a source.

## Ranked by hardware-term density: what to read next

| Hits | Document |
| --- | --- |
| **504** | `000959-10` *Writing Device Drivers With GPIO Calls* Jun 87 — the SR9 sibling of `000959-A00`, four times the density of anything else |
| 87 | `HP-Apollo_Products_Configuration_Guide` — `CFG_WALK.md`, in progress |
| 59 | `AEGIS_Overview_1985` |
| 31 | `Apollo_DOMAIN_Architecture` Feb 81 — 31 hits in 46 K characters, **the highest density on the shelf** |
| 28 | `008856-00` *System Call Reference Vol 2* |

The language and command references above them in raw count — Pascal, C, SysV,
Aegis — score on *false friends*: "register" in the C storage-class sense,
"address" in the pointer sense. **Density, not count, is what ranks a document
here**, and the 1981 architecture paper is the standout: it is 32 pages.
