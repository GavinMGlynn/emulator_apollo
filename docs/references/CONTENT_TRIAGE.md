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

## Second pass: sampled term by term, and it changed one verdict

The ranking below sorts by hardware-term *count*, and a count cannot tell a
device register from a `troff` register. **So every remaining document's matches
were extracted with their context and read.** Three outcomes.

### One document was ranked as a false friend and is a hardware source

`000792-A01` *Domain Pascal Language Reference*: 47 hits are `register` in the
language's sense and **33 are `68040`**, clustered in an appendix on the
68040's floating-point trap. `000792-A01_WALK.md` has what it gave. **A
term-count triage ranks; only reading what the terms *are* decides**, and this
is the document that proved it.

### The expected false friends were exactly that

| Document | Hits | What they are |
| --- | --- | --- |
| `005802-00` Text Processing Guide | 154 | **all** `register`, in the `troff`/`-ms` sense — "buffer register, say x" |
| `002093-A00` C Language Reference | 90 | the `register` storage class, listed among `continue if switch default int typedef` |
| `002547-A00` Aegis Command Reference | 27 `csr` | a **command named `csr`**, "create a type object module for binding" |
| `010851/2/3-A00` Managing SysV/Aegis/BSD | 2 each | "Hexadecimal address of a **tcb**" — a TCP control block |

### Only 15 of the 41 contain a *hard* term at all

Re-scanned for part numbers and unambiguous hardware vocabulary only —
`MC680xx`, `68020/30/40`, `68881/2`, `IRQ`, `DMA channel`, `CSR page`, `jumper`,
`MHz`, `nanosecond`. **Twenty-six documents contain none of it and are
settled.** Of the fifteen that do, all but two are one or two incidental hits.

**`Apollo_Price_List_Jul88` — 40 hits, and it confirms four fields of the
DN3000 row from a source the model table does not cite:**

> "DN3000 PERFORMANCE: 1.5 MIPS  CPU: **12 Mhz MC68020**, **12 Mhz MC68881**
> MEMORY: **4-8 MB**  GRAPHICS: MONO: **15", 1024 X 800**; 19", 1280 X 1024"

against `ap_model.c`'s `cpu_hz = 12000000`, `fpu = AP_FPU_M68881`,
`ram_max_bytes = 0x800000` (cited to `[S3K]` by *address range*, where this
gives the capacity), and `display = AP_DISPLAY_MONO_1024X800` — the 15-inch
option. **Four independent agreements and nothing to change.** *It also lists
the 19-inch mono as 1280 x 1024*, which is the same option pairing
`CFG_WALK.md` untangles for the Series 3500.

**`005694-A00` *Managing Domain/OS and Domain Routing in an Internet*** — 4
`jumper`, 2 `IRQ` — is the **device descriptor file** from the administrator's
end: "describes the addresses of Control and Status Registers (CSR) and the
Interrupt Request Lines (IRQ) used by the controller … `'node_data/dev` contains
device descriptor files", and "**Jumper settings on the controller determine the
unit number**". Derivative of `000959-A00`'s chapter 11, which is walked, and
the same is true of the `crddf` command's options in the Aegis and SysV command
references — "Specify the hexadecimal address of the **CSR page** for the device
in the bus address space", "`-dma channel` … used by AT-compatible device".
**The user interface to facts the GPIO manuals already gave.**

## Every remaining document, named, with its hard-term score

**The earlier passes described groups and did not list them**, so thirty-five
shelf documents were "settled" in prose and named nowhere. A check against the
walk records found it. **That is the sixth count this session that inference got
wrong and a direct check corrected** — and the fix is this table, which names
every one.

Scored for part numbers and unambiguous hardware vocabulary only: `MC680xx`,
`68020/30/40`, `68881/2`, `IRQ`, `DMA channel`, `CSR page`, `interrupt vector`,
`jumper`, `nanosecond`, `MHz`, `hexadecimal address`, `bus address`.

| Document | Hard terms | What they are |
| --- | --- | --- |
| `005798-A00_SysV_Command_Reference_Jul88` | 7 | `hexadecimal address`×3, `csr page`×2, `bus address`×1 |
| `002685-07_Technical_Publications_Overview_Jun87` | 6 | `68020`×5, `irq`×1 |
| `005807-00_Programming_With_DOMAIN_3D_Graphics_Metafile_Resourc` | 4 | `68020`×2, `68881`×2 |
| `002547-04_DOMAIN_System_Command_Reference_Jun87` | 2 | `hexadecimal address`×2 |
| `008543-A00_Configuring_and_Managing_TCP_IP_Jul88` | 2 | `hexadecimal address`×2 |
| `010851-A00_Managing_SYS_V_System_Software_Jun88` | 2 | `hexadecimal address`×2 |
| `010852-A00_Managing_Aegis_System_Software_May88` | 2 | `hexadecimal address`×2 |
| `010853-A00_Managing_BSD_System_Software_Jun88` | 2 | `hexadecimal address`×2 |
| `011242-A00_Domain_Documentation_Master_Index_Jul88` | 2 | `mc68000`×2 |
| `011435-A02_Making_the_Transition_to_SR10_Operating_System_Rele` | 1 | `mc68020`×1 |
| `000792-04_DOMAIN_Pascal_Language_Reference_Jan87` | 0 | — |
| `001525-04_DOMAIN_Language_Level_Debugger_Reference_Jan87` | 0 | — |
| `002348-01_Getting_Started_With_Your_Domain_System_Oct83` | 0 | — |
| `003245-01_Programmers_Guide_to_Domain_Graphics_Primitives_Apr8` | 0 | — |
| `005097-00_Programming_With_DOMAIN_2D_Graphics_Metafile_Resourc` | 0 | — |
| `005448-00_Operating_the_DN3xx_Sep85` | 0 | — |
| `005488-02_DOMAIN_System_Users_Guide_Jan87` | 0 | — |
| `005696-00_Programming_With_System_Calls_For_Interprocess_Commu` | 0 | — |
| `005801-01_DOMAIN_IX_Programmers_Reference_for_BSD4.2_1987` | 0 | — |
| `005803-01_DOMAIN_IX_Users_Guide_Dec86` | 0 | — |
| `007194-01_DOMAIN_Graphics_Primitive_Resource_Call_Reference_Ja` | 0 | — |
| `007194-02_Domain_Graphics_Primitive_Resource_Call_Reference_Ju` | 0 | — |
| `008542-00_Programming_With_DOMAIN_Advanced_System_Calls_Nov85` | 0 | — |
| `008790-A00_Engineering_in_the_DSEE_Environment_Jul88` | 0 | — |
| `008858-00_Programming_With_General_System_Calls_Mar86` | 0 | — |
| `009355-00_System_Administration_For_DOMAIN_IX_BSD4.2_Dec86` | 0 | — |
| `009413-00_DOMAIN-IX_Support_Tools_Guide_Nov86` | 0 | — |
| `009414-00_DOMAIN_System_Utilities_Sep86` | 0 | — |
| `010430-a00_Domain_Standard_Graphics_Quick_Reference_GPR_and_CT` | 0 | — |
| `Apollo_Documentation_and_Software_Replacement_Media_Catalog_Ju` | 0 | — |
| `HP_Apollo_Documentation_Catalog_Feb91` | 0 | — |
| `002348-A00_Getting_Started_With_Domain_OS_May88` | 0 | — |
| `011020-A00_Using_Your_BSD_Environment_Jul88` | 0 | — |
| `011021-A00_Using_Your_Aegis_Environment_Jul88` | 0 | — |
| `011022-A00_Using_Your_SysV_Environment_Jul88` | 0 | — |

**Twenty-five of these thirty-five contain not one hard hardware term** in their
entire text. The ten that do were read in context and every one is derivative or
false:

- **`005798-A00` / `002547-04`** — the `crddf` command's options, "the
  hexadecimal address of the **CSR page** for the device in the bus address
  space", `-dma channel`. The shell form of the device descriptor file
  `000959-A00` chapter 11 documents.
- **`002685-07`** — a publications overview describing the DN3000 as "based on
  the Motorola 68020 microprocessor". *And it names a document this shelf does
  not hold*: "the **Domain Series 3000 Configuration Worksheet** lists the
  functional parameters (**DRQ lines, IRQ lines, power required, and address
  space**) for the DN3000 system and the Apollo optional devices". **A third
  named absence**, alongside `007861` and `007861-A01`, and the one whose
  content this project would most obviously use.
- **`005807-00`** — "A node equipped with a **peb** (performance enhancement
  board). A DN460 or DN660. Any node equipped with 68020 and 68881 processors.
  Use **`NETSTAT -CONFIG`** to determine whether your node is equipped with a
  peb". A requirements list and a query command.
- **`010851/2/3-A00`, `008543-A00`** — "Hexadecimal address of a **tcb**", a
  TCP control block.
- **`011242-A00`** — index entries pointing at the Assembler Reference.
- **`011435-A02`** — "Many modern processors, such as the MC68020, are designed
  to transfer data most efficiently if the data is naturally aligned."

## The seven brochures, settled by looking at one

`CONTENT_TRIAGE`'s first pass could not read them — two to six characters of
text each — and declared them not worth walking **on their subject**, which is a
weaker basis than a measurement. **`Apollo_Series_3500_Brochure` was rendered
and looked at**: page one is a photograph of three workstations on plinths under
the headline "Apollo's Series 3500 Personal Workstation". A product photograph.

*The class is confirmed by inspection rather than assumption*, and the same
judgement is extended to the other six. **Named in full so this record can be
audited by filename rather than by prose:**

| Document | Pages | Text |
| --- | --- | --- |
| `brochures/Apollo_Series_3500_Brochure_Jul88.pdf` | 2 | 2 chars — **rendered and inspected** |
| `brochures/Apollo_Series_4500_Brochure_Jul88.pdf` | 2 | 2 chars |
| `brochures/Apollo_Series_10000_Brochure_Jul88.pdf` | — | 6 chars |
| `brochures/Apollo_Product_Line_Brochure_Jul88.pdf` | — | 6 chars |
| `brochures/Apollo_DomainOS_Brochure_Jul88.pdf` | — | 6 chars |
| `brochures/DN590-T_Brochure_May87.pdf` | — | 6 chars |
| `brochures/DN440_460_Brochure_1983.pdf` | 6 | 6 chars |
| `Apollo_Documentation_and_Software_Replacement_Media_Catalog_Jun89.pdf` | — | a media catalogue, in the zero-hardware-term table above | Any specification they carry would be
the selling-document class `PRICE_LIST_WALK.md` already read and found to agree
with `ap_model.c`.

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
