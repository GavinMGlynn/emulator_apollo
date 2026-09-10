# The Domain/OS software shelf — walk coverage record

**State: 1 of 100 walked whole, 1 in progress. 20,770 pages.**

- `004977-02` *Binder and Librarian Reference* — **walked whole, 118/118, 2026-09-10**,
  `004977-02_WALK.md`. It explained the DS5500's remaining symptom and changed no
  code: a zero-filled private data section is what the loader is *supposed* to
  produce.

## Why this file exists

`docs/references/` holds twenty-two other `*_WALK.md` records and **every one
of them says the same thing**: walked whole, nothing owed. `002398-04` 330/330,
`007196-01` 722/722, `008778-03` 209/209, `010005-00` 29/29, `019411-A00` 10/10,
the processor batch 2,633 pages, `[PRM]` 646/646, `[040]` 463/463, and the
peripheral datasheets besides. On the evidence of those records this project has
read everything it holds.

**It has read every *hardware* document it holds.** Not one of the twenty-two
covers a Domain/OS software manual, and the shelf holds a hundred of them —
twenty thousand seven hundred and seventy pages, four fifths of them with a text
layer. The register below is the first time they have been listed.

*The audit that found this took three attempts and the first two published wrong
numbers* — matching filenames missed documents cited by title, matching titles
missed ones cited by tag. The number here is the one that is exactly checkable:
**a shelf file whose path is not named anywhere under `docs/`**. Some of these
are cited by title or tag without their path (AEGIS Internals is, in `RING.md`),
so the claim is "no walk record covers it", not "no one has ever opened it".

## Why it matters now, and not as housekeeping

The DS5500 boots Domain/OS, pages normally, runs a user process and makes system
calls. Where it stops is a `jsr (a0)` through the `A5` global-data pointer into a
page that a completed copy loop filled with zeros — `PROJECT_STATUS.md` has the
measurement. **That is a question about how a Domain/OS program's global data
area and cross-module linkage are built**, and the documents that answer it are
the first two rows of the first two groups below: the *Binder and Librarian
Reference*, *AEGIS Internals and Data Structures*, and *Domain/OS Design
Principles*.

**This is the shape the project has been caught by before.** `fetch-the-parts-own-datasheet`,
`consult-references-before-iterating` and `check-the-route-before-the-obstacle`
are all the same lesson: the answer was on a shelf nobody looked at. The
difference here is that the shelf was *audited* as complete, because the audit
only ever counted hardware.

## Order of work

1. ~~`004977-02` Binder and Librarian Reference~~ — **done**, and it answered the
   question it was picked for. `004977-02_WALK.md`.
2. **`AEGIS_Internals_and_Data_Structures`**, 426 pages — **in progress**,
   `AEGIS_INTERNALS_WALK.md`: **all 426 pages passed over** — 7 chapters and Appendix A read in
   full, the rest in a condensed page-by-page pass. It has already confirmed six measured
   findings and explained two of them, contradicted `[MAC]` twice without
   changing anything, declined to answer `RING.md` question E, and turned out to
   have **a second 56-page document bound in at pp. 371-426** that its filename
   does not mention.
3. **`014962-A00` Domain/OS Design Principles**, 157 pages.
4. The rest, in the order the groups appear below. **Brochures, price lists and
   catalogues are listed for completeness and are not technical documents**;
   they are the one group that can be declared not worth walking, and that is a
   judgement recorded here rather than an omission.

## The register

### Operating-system internals and design — 6 document(s), 1092 pages

| Document | Pages | Layer |
| --- | --- | --- |
| `bitsavers/AEGIS_Overview_1985.pdf` | 436 | text |
| `bitsavers/AEGIS_Internals_and_Data_Structures_Jan86.pdf` | 426 | text |
| `bitsavers/014962-A00_Domain_OS_Design_Principles_Jan89.pdf` | 157 | text |
| `bitsavers/Apollo_Domain_Architecture_Preliminary_Jan81.pdf` | 32 | text |
| `bitsavers/Apollo_DOMAIN_Architecture_Feb81.pdf` | 31 | text |
| `bitsavers/019411-A00_Addendum_to_Domain_Personal_Workstations_and_Servers_Hardware_Architecture_Handbook_1991.pdf` | 10 | text |

### Linkage, assembly, system calls and drivers — 10 document(s), 2910 pages

| Document | Pages | Layer |
| --- | --- | --- |
| `bitsavers/008856-00_DOMAIN_System_Call_Reference_Vol2_Feb86.pdf` | 526 | image |
| `bitsavers/008856-00_DOMAIN_System_Call_Reference_Vol1_Feb86.pdf` | 504 | image |
| `bitsavers/008858-00_Programming_With_General_System_Calls_Mar86.pdf` | 475 | text |
| `bitsavers/SR10/000959-A00_Writing_Device_Drivers_with_GPIO_Calls_Jul88.pdf` | 314 | text |
| `bitsavers/008542-00_Programming_With_DOMAIN_Advanced_System_Calls_Nov85.pdf` | 230 | text |
| `bitsavers/000959-10_Writing_Device_Drivers_With_GPIO_Calls_Jun87.pdf` | 214 | text |
| `bitsavers/008862-01_DOMAIN_Assembler_Reference_Jan87.pdf` | 201 | text |
| `bitsavers/005696-00_Programming_With_System_Calls_For_Interprocess_Communications_Jul85.pdf` | 190 | text |
| `bitsavers/001525-04_DOMAIN_Language_Level_Debugger_Reference_Jan87.pdf` | 138 | text |
| `bitsavers/004977-02_DOMAIN_Binder_and_Librarian_Reference_Jan87.pdf` | 118 | text |

### Administration, installation and networking — 17 document(s), 4023 pages

| Document | Pages | Layer |
| --- | --- | --- |
| `bitsavers/SR10/010851-A00_Managing_SYS_V_System_Software_Jun88.pdf` | 618 | text |
| `bitsavers/SR10/010853-A00_Managing_BSD_System_Software_Jun88.pdf` | 550 | text |
| `bitsavers/009355-00_System_Administration_For_DOMAIN_IX_BSD4.2_Dec86.pdf` | 388 | text |
| `bitsavers/SR10/010852-A00_Managing_Aegis_System_Software_May88.pdf` | 382 | text |
| `bitsavers/008860-A03_Installing_DOMAIN_Software_Mar92.pdf` | 295 | text |
| `bitsavers/SR10/008860-A02_Installing_Domain_Software_with_Apollos_Release_and_Installation_Tools_Oct89.pdf` | 246 | text |
| `bitsavers/001746-06_Administering_Your_DOMAIN_System_Jun87.pdf` | 214 | text |
| `bitsavers/SR10/008543-A00_Configuring_and_Managing_TCP_IP_Jul88.pdf` | 214 | text |
| `bitsavers/SR10/005694-A00_Managing_Domain_OS_and_Domain_Routing_in_an_Internet_Jul88.pdf` | 176 | text |
| `bitsavers/SR10/011435-A02_Making_the_Transition_to_SR10_Operating_System_Releases_Oct89.pdf` | 174 | text |
| `bitsavers/SR10/009916-A00_Planning_Domain_Networks_and_Internets_Aug88.pdf` | 166 | text |
| `bitsavers/008543-01_Configuring_and_Managing_TCP_IP_Jan87.pdf` | 164 | text |
| `bitsavers/015363-A00_Administering_the_Domain_OS_Registry_Sep1990.pdf` | 153 | text |
| `bitsavers/SR10/008667-A00_Using_TCP_IP_Network_Applications_Jul88.pdf` | 130 | text |
| `bitsavers/SR10/011717-A00_Making_the_Transition_to_SR10_TCP_IP_Jul88.pdf` | 74 | text |
| `bitsavers/009496-00_Update_Package_1_to_the_DOMAIN_System_Command_Reference_Jun87.pdf` | 49 | text |
| `bitsavers/009492-00_Making_the_Transition_to_SR9.5_Jan87.pdf` | 30 | text |

### User guides, commands and environments — 24 document(s), 6608 pages

| Document | Pages | Layer |
| --- | --- | --- |
| `bitsavers/SR10/005798-A00_SysV_Command_Reference_Jul88.pdf` | 826 | text |
| `bitsavers/SR10/002547-A00_Aegis_Command_Reference_Jul88.pdf` | 524 | text |
| `bitsavers/002547-04_DOMAIN_System_Command_Reference_Jun87.pdf` | 522 | text |
| `bitsavers/005802-00_DOMAIN_IX_Text_Processing_Guide_1985.pdf` | 440 | text |
| `bitsavers/005801-01_DOMAIN_IX_Programmers_Reference_for_BSD4.2_1987.pdf` | 416 | text |
| `bitsavers/SR10/011020-A00_Using_Your_BSD_Environment_Jul88.pdf` | 386 | text |
| `bitsavers/SR10/011022-A00_Using_Your_SysV_Environment_Jul88.pdf` | 382 | text |
| `bitsavers/002398-04_Domain_Engineering_Handbook_Rev4_Jan87.pdf` | 330 | text |
| `bitsavers/SR10/011021-A00_Using_Your_Aegis_Environment_Jul88.pdf` | 330 | text |
| `bitsavers/005488-02_DOMAIN_System_Users_Guide_Jan87.pdf` | 304 | text |
| `bitsavers/008790-A00_Engineering_in_the_DSEE_Environment_Jul88.pdf` | 265 | text |
| `bitsavers/002398-03_Domain_Engineering_Handbook_Rev3_Feb85.pdf` | 260 | text |
| `bitsavers/008788-A01_Getting_Started_With_DSEE_Apr91.pdf` | 227 | text |
| `bitsavers/009413-00_DOMAIN-IX_Support_Tools_Guide_Nov86.pdf` | 221 | text |
| `bitsavers/002398-01_Domain_Engineering_Handbook_Rev1_Apr83.pdf` | 215 | text |
| `bitsavers/008778-03_DOMAIN_Series_3000_4000_Technical_Reference_Aug87.pdf` | 209 | text |
| `bitsavers/SR10/002348-A00_Getting_Started_With_Domain_OS_May88.pdf` | 179 | text |
| `bitsavers/005803-01_DOMAIN_IX_Users_Guide_Dec86.pdf` | 148 | text |
| `bitsavers/SR10/011418-A00_Domain_OS_Display_Manager_Command_Reference_Jul88.pdf` | 146 | text |
| `bitsavers/002348-01_Getting_Started_With_Your_Domain_System_Oct83.pdf` | 93 | text |
| `bitsavers/009414-00_DOMAIN_System_Utilities_Sep86.pdf` | 80 | text |
| `bitsavers/002685-07_Technical_Publications_Overview_Jun87.pdf` | 38 | text |
| `bitsavers/005448-00_Operating_the_DN3xx_Sep85.pdf` | 38 | text |
| `bitsavers/010005-00_Apollo_Token_Ring_Media_Access_Control_Layer_and_Physical_Layer_Protocols_Oct87.pdf` | 29 | text |

### Language references — 3 document(s), 1571 pages

| Document | Pages | Layer |
| --- | --- | --- |
| `bitsavers/000792-A01_Domain_Pascal_Language_Reference_Dec90.pdf` | 611 | text |
| `bitsavers/SR10/002093-A00_Domain_C_Language_Reference_Jul88.pdf` | 554 | text |
| `bitsavers/000792-04_DOMAIN_Pascal_Language_Reference_Jan87.pdf` | 406 | text |

### Graphics — 9 document(s), 2472 pages

| Document | Pages | Layer |
| --- | --- | --- |
| `bitsavers/005812-00_DOMAIN_3D_Graphics_Metafile_Resource_Call_Reference_Dec85.pdf` | 378 | text |
| `bitsavers/003245-01_Programmers_Guide_to_Domain_Graphics_Primitives_Apr84.pdf` | 347 | text |
| `bitsavers/005097-00_Programming_With_DOMAIN_2D_Graphics_Metafile_Resource_Jul85.pdf` | 316 | text |
| `bitsavers/005808-01_Programming_With_Domain_Graphics_Primitives_Feb87.pdf` | 311 | text |
| `bitsavers/009793-00_DOMAIN_2D_Graphics_Metafile_Resource_Call_Reference_Nov86.pdf` | 287 | text |
| `bitsavers/005807-00_Programming_With_DOMAIN_3D_Graphics_Metafile_Resource_Nov85.pdf` | 272 | text |
| `bitsavers/007194-02_Domain_Graphics_Primitive_Resource_Call_Reference_Jun87.pdf` | 203 | text |
| `bitsavers/007194-01_DOMAIN_Graphics_Primitive_Resource_Call_Reference_Jan87.pdf` | 196 | text |
| `bitsavers/005808-00_Programming_With_DOMAIN_Graphics_Primitives_Jan87.pdf` | 162 | text |

### Release notes — 6 document(s), 805 pages

| Document | Pages | Layer |
| --- | --- | --- |
| `bitsavers/release_notes/005809-A03_10.1_Release_Notes_Dec88.pdf` | 256 | text |
| `bitsavers/release_notes/018901-A00_10.4_Release_Notes_Mar92.pdf` | 202 | image |
| `bitsavers/release_notes/005809-A05_10.2_Release_Notes_Nov89.pdf` | 166 | text |
| `bitsavers/release_notes/005809-A01_10.0_Beta_2_Release_Notes_Apr88.pdf` | 114 | text |
| `bitsavers/release_notes/005809-A00_Domain_System_Software_Release_Notes_SR9.7_Nov87.pdf` | 62 | text |
| `bitsavers/release_notes/019534-A00_10.4_Addendum.pdf` | 5 | image |

### Patents — 10 document(s), 316 pages

| Document | Pages | Layer |
| --- | --- | --- |
| `bitsavers/patents/4979099_Quasi_fair_arbitration_scheme_with_defau.pdf` | 168 | image |
| `bitsavers/patents/5022004_Method_and_apparatus_for_DRAM_memory_per.pdf` | 35 | image |
| `bitsavers/patents/4746773_Connector_for_automatically_maintaining_.pdf` | 27 | image |
| `bitsavers/patents/4809170_Computer_device_for_aiding_in_the_develo.pdf` | 20 | image |
| `bitsavers/patents/4951192_Device_for_managing_software_configurati.pdf` | 18 | image |
| `bitsavers/patents/4857901_Display_controller_utilizing_attribute_b.pdf` | 16 | image |
| `bitsavers/patents/4716575_Adaptively_synchronized_ring_network_for.pdf` | 10 | text |
| `bitsavers/patents/4751446_Lookup_table_initialization.pdf` | 9 | image |
| `bitsavers/patents/5023907_Network_license_server.pdf` | 7 | image |
| `bitsavers/patents/4994962_Variable_length_cache_fill.pdf` | 6 | image |

### Brochures, catalogues and price lists — 15 document(s), 973 pages

| Document | Pages | Layer |
| --- | --- | --- |
| `bitsavers/HP-Apollo_Products_Configuration_Guide_Dec89.pdf` | 383 | text |
| `bitsavers/SR10/011242-A00_Domain_Documentation_Master_Index_Jul88.pdf` | 180 | text |
| `bitsavers/010430-a00_Domain_Standard_Graphics_Quick_Reference_GPR_and_CTM_Dec89.pdf` | 90 | text |
| `bitsavers/5952-2149_Apollo_Quick-Reference_Configuration_Guide_Jul90.pdf` | 88 | text |
| `bitsavers/Apollo_Price_List_Jul88.pdf` | 81 | text |
| `bitsavers/HP_Apollo_Documentation_Catalog_Feb91.pdf` | 44 | text |
| `bitsavers/SR10/002685-A01_Apollo_Documentation_Quick_Reference_Jul89.pdf` | 38 | text |
| `bitsavers/Apollo_Documentation_and_Software_Replacement_Media_Catalog_Jun89.pdf` | 35 | text |
| `bitsavers/brochures/Apollo_DomainOS_Brochure_Jul88.pdf` | 6 | image |
| `bitsavers/brochures/Apollo_Product_Line_Brochure_Jul88.pdf` | 6 | image |
| `bitsavers/brochures/Apollo_Series_10000_Brochure_Jul88.pdf` | 6 | image |
| `bitsavers/brochures/DN440_460_Brochure_1983.pdf` | 6 | image |
| `bitsavers/brochures/DN590-T_Brochure_May87.pdf` | 6 | image |
| `bitsavers/brochures/Apollo_Series_3500_Brochure_Jul88.pdf` | 2 | image |
| `bitsavers/brochures/Apollo_Series_4500_Brochure_Jul88.pdf` | 2 | image |

## Rule this record is kept under

The same one as every other walk: a document that yields one fact this core does
not have is a document to be read **whole**, page by page, and the row above is
updated with what each section gave. A row that still says `NOT WALKED` is a
claim that nobody has looked, not that there is nothing there.
