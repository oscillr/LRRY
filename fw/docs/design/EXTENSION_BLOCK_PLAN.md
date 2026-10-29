# EXTENSION BLOCK PLAN: one format plan for every stored value of the decided features (2026-10-20)

Private. Research and writing only: no code changed, no branch made, no build run. INDEX.md and the other documents were not touched. I wrote or retuned no sound. Every default, range and slot number below is a proposal for you to approve; the ranges come from the design studies, and where a study gave none I say "open".

Marks, as in the other studies: **CONFIRMED** (read today in the source or the docs), **est.** (worked out, not measured), **HYPOTHESIS** (my reading, not proven). Source read: `/Users/balinth/wt/join0234` (branch wip/join0234, VERSION 0.2.25, commit 4da80f9 of 2026-10-19 23:09, the newest line; its `storage.c` and `fm1_flash.h` are byte for byte the ones in join0233).

Words (the rest are in GLOSSARY.md):
- **Patch store**: where the 128 saved patches live in flash. It is cut into **chunks**: a chunk is one 4 KiB-sector pair ("A/B pair") holding a few patch records. A save writes the changed chunk into the sector that is not current, and writes a small **commit record** (a header with length and checksum) LAST. A power cut before that leaves the old chunk in charge.
- **Record**: one saved patch inside a chunk (name, engine, values, End B, arp block).
- **Parameter id / wide id**: every stored sound value has a number, its id (0 to 126 today). A **wide id** is my name for one byte that covers all: 0 to 126 the old ids, 127 reserved, 128 to 255 the **extension slots**.
- **Extension slot (X0, X1 ...)**: a stored value that has no old id. Wide id = 128 + slot number.
- **Sparse / dense**: sparse stores only the values that differ from their default, each with its own number (a pair); dense stores a fixed list, every value, in order.
- **Section**: a labelled piece inside the extension block (a tag byte and a length byte, then data). A reader that does not know a tag skips it by the length.

---

## 1. Verdict (half a page)

**New stored values for the decided features: 20.**
- TWIN FILTER 11 (TYPE, CUTOFF 2, RES 2, DRIVE, BLEND, ROUTE, LINK, KEY TRK 2, ENV 2, LFO 1, LFO 2).
- SUB LAYER 4 (LEVEL, OCTAVE, WAVE, HARMONICS).
- CYCLE 5 (MODE, RISE, FALL, CURVE and its own SYNC switch; the earlier study said 4, your SYNC decision makes 5).
- WAVE STEPS 0 (it uses WAVE's empty slot E7, no id). VELOCITY / AFTERTOUCH 0 (VELO_STUDY: on / off, curve and calibration are device settings, not patch values). EDITABLE B is not a value: it is a list of up to 32 (id, value) pairs.

**5 fit on free ids, 15 need the extension block.** The five ids are 25, 26, 42, 106 and 113, as TWIN design 2 says. One finding you should know: **only two of them are free in the code today.** Id 106 is free (params.c marks it a free id) and id 42 (ALLOC) is never read, but still has a descriptor and a morph tag. Ids 25 and 26 are still ROOT and SCALE: the global SCALE page reads their descriptors, project load resets id 26, the factory-sound loader skips it and the arp/keys block still has a retired slot on it (upreset.c `AK_KEYID`). Id 113 is ANALOG's filter TYPE and only frees when the engine filters go. So "5 on ids" needs four small cleanups first (section 5).

**Record size.** Today the biggest possible record is **434 B** against a limit of **447 B** (13 B room). With the 15 extension values (2 B each, sparse) it becomes **466 B**: it already misses 447 by 19 B, and even a dense 16-byte block would miss it by 3 B. With editable B (32 hand edits, 66 B) it is **534 B**. That fits only the bigger layout: **6 records a chunk gives a limit of 596 B** (62 B to spare, room for about 31 more values). 7 a chunk (limit 511 B) works only if editable B is capped at 20 edits.

**The 56 KiB of flash is free on the newest merged code: yes, by the flash layout in the source** (details in 3.5). I found no linker map file (the build folder has the .elf, .bin and .dis only), so I read the layout code instead. Correction to the earlier studies: **22 chunks are enough** (22 x 6 = 132 slots), not 23, so one pair (0xC6000 to 0xC7FFF, 8 KiB) stays free.

**Recommended design in plain words:**
1. One id space of 8 bits ("wide ids"): the old ids stay as they are, extension slots are 128 and up. In the unit's memory the extension values sit right after the old ones in each part's value list, so the morph, the matrix and the pages treat them like any value.
2. In the patch record, the extension block replaces the retired 2-byte "shadow" part. It has a length, a version byte and tagged sections: section 1 the extension values as pairs (only non-default ones), section 2 the editable-B edits. A reader skips what it does not know.
3. Multis and projects hold the extension values densely (a count byte, then one byte per slot), because their worst case must always fit.
4. 6 records a chunk, 22 chunks, all in the free flash. The write method (A/B pair, commit record last) does not change at all.
5. The six values without ids get no MIDI LEARN or lock in the first version, but the format reserves what they need later (3.3), so adding it will not break saves.

---

## 2. Every new stored value

Slot numbers (X0 to X14) and the id assignment are my proposal. "Bytes" are in a patch record: **0 at the default**, otherwise 2 as a pair in section 1 (an id value inside the old list costs 2 B as a pair, or 1 B in the record's bitmap form). All values belong to the sound: **per part, saved with the patch, held in multis (both parts) and in projects (the autosave and the editor backup)**. None is global. The "worst" size counts every value off its default.

### 2A. What each value is and where it lives

| # | Feature | Label on the unit | Range (default), step | Home | Bytes in a patch (worst) |
|---|---|---|---|---|---|
| 1 | TWIN | TYPE | OFF, LP, BP, HP, NOTCH, LADDER, COMB = 0..6 (OFF), 1 | id 42 | 2 (already counted in the old worst case) |
| 2 | TWIN | CUTOFF 2 (OFFSET while LINK is on; TUNE on COMB) | LINK on: -63..+63 dial steps (0); LINK off: 0..82. One stored value, two meanings; turning LINK converts it. 1 dial step = 0.11 octave | id 106 | 2 (counted) |
| 3 | TWIN | RES 2 (FEEDBACK on COMB) | 0..127 for the four TRIO-style types; -64..+63 on COMB. Default open (TRIO's own was 40). The card resets when COMB is entered or left | id 25 | 2 (counted) |
| 4 | TWIN | DRIVE | 0..127 (0 = not run), 1 | id 26 | 2 (counted) |
| 5 | TWIN | BLEND | -64..+63 (0), 1 | id 113 | 2 (counted) |
| 6 | TWIN | ROUTE | SERIES, PARALLEL = 0..1 (SERIES) | X0 (wide id 128) | 2 |
| 7 | TWIN | LINK | OFF, ON = 0..1 (**ON**) | X1 (129) | 2 |
| 8 | TWIN | KEY TRK 2 | -63..+63 (0), 1 | X2 (130) | 2 |
| 9 | TWIN | ENV 2 (to CUTOFF 2) | -64..+63 (0), 1 | X3 (131) | 2 |
| 10 | TWIN | LFO 1 (to CUTOFF 2) | -64..+63 (0), 1 | X4 (132) | 2 |
| 11 | TWIN | LFO 2 (to CUTOFF 2) | -64..+63 (0), 1 | X5 (133) | 2 |
| 12 | SUB | LEVEL | 0..127 (0 = not run), 1 | X6 (134) | 2 |
| 13 | SUB | OCTAVE | -2, -1, 0 stored 0..2 (-1), 1 | X7 (135) | 2 |
| 14 | SUB | WAVE | SINE, TRI, SQUARE, PULSE = 0..3 (SINE), append-only list | X8 (136) | 2 |
| 15 | SUB | HARMONICS | 0..127 (0 = not run), 1 | X9 (137) | 2 |
| 16 | CYCLE | MODE | LOOP, ONCE = 0..1 (default open; LOOP is the "cycling" one) | X10 (138) | 2 |
| 17 | CYCLE | RISE | 0..127, the ATK time table; note values when SYNC is on (default open) | X11 (139) | 2 |
| 18 | CYCLE | FALL | 0..127, the DEC time table; note values when SYNC is on (default open) | X12 (140) | 2 |
| 19 | CYCLE | CURVE | -64..+63 (0 = straight), 1 | X13 (141) | 2 |
| 20 | CYCLE | SYNC | OFF, ON = 0..1 (OFF); its own switch, not ENV SYNC | X14 (142) | 2 |
| (21) | spare | | | X15 (143) | |
| - | WAVE STEPS | STEPS (WAVE engine card E7) | OFF, 2..19 | the engine slot P_E6 (id 125) that every patch already stores | 0 new |
| - | EDITABLE B | B edits | up to 32 pairs (wide id, value) | section 2 of the block | 0 or 3 .. 66 |

Worst-case bytes of all fifteen extension pairs plus block header and section header: 2 + 2 + 30 = 34 B (36 B with the spare slot).

Id assignment note: 42 and 106 can be used now; 25 and 26 after cleanup; 113 only when ANALOG's own filter TYPE is gone (design 2, step 2). Whichever value sits on 113 cannot exist before that. If you ship TWIN before the engine filters, BLEND (or any one value) needs a temporary home. I list BLEND on 113 only because it is the least needed to hear TWIN at all.

### 2B. What each value can do

| # | Matrix destination (append-only list, 51 used of 128) | MORPH | MIDI LEARN / lock |
|---|---|---|---|
| 1 TYPE | no (hard switch) | never rolled in SMART; with different ends (WILD, editable B) it **switches at the middle**; both types run 8 blocks (6 ms) | yes (id) |
| 2 CUTOFF 2 | **yes**, new destination CUTOFF 2 (dest 51, per note). It adds to filter 2's cutoff sum; it does not rewrite the stored value | **interpolates**, in octaves | yes |
| 3 RES 2 | **yes**, dest 52 (per block) | **interpolates**; RES 2 above 100 never with more DRIVE | yes |
| 4 DRIVE | **yes**, dest 53 (per block, gliding) | **interpolates**; never far above the original | yes |
| 5 BLEND | **yes**, dest 54 (per block, gliding) | **interpolates** | yes |
| 6 ROUTE | no | switches at the middle, never rolled in SMART | **no** (see 3.3) |
| 7 LINK | no | switches at the middle; the cutoff glides over 8 blocks | no |
| 8 to 11 the four amounts | no (a route to CUTOFF 2 does the same job) | **interpolate**; an amount at 0 stays 0 while TYPE is OFF | no |
| 12 SUB LEVEL | **yes**, dest 55 (per block, gliding) | **interpolates** | no |
| 13 OCTAVE, 14 WAVE | no | switch at the middle; old and new sub run 4 blocks together | no |
| 15 HARMONICS | later (dest 56 if wanted) | interpolates | no |
| 16 MODE, 20 SYNC | no | switch at the middle (SYNC like ENV SYNC, `T_HARD` today); a running cycle stops after its current fall | no |
| 17 RISE, 18 FALL, 19 CURVE | not decided (ENV 2's times have destinations; CYCLE's could be appended later) | **interpolate** (like ATK 2, DEC 2) | no |

Sources: CYCLE is also a new matrix **source** (source list 15 to 16, append-only). Sources are not stored values, so they cost no extension slot.

The twin filter design says six values have no MIDI LEARN or locks: that is rows 6 to 11. Rows 12 to 20 are the same situation (nine more). 3.3 says how to give all fifteen those.

---

## 3. The extension block design

### 3.1 What goes where

**Patch record (upreset.c, CONFIRMED layout):** fixed 20 bytes (length, slot, revision, flags, engine, category, name, value count), then in this order: [shadow marker, 2 B, retired since 0.2.17], [End B, 7 B], the values (pairs, or a 16-byte bitmap plus one byte each), [pattern 32 B], [FM6 patch 112 B], [arp/keys/sequencer block, last, with its own length]. The flags byte is full (all 8 bits used, one of them, bit 16, is the retired shadow marker).

**Proposal:**
- Bit 16 becomes **PTF_XB** (extension block present). The retired 2-byte shadow part is replaced by the block, at the same place.
- Block layout: `[L: 1 byte, the number of bytes after it, 0..255] [XV: version, 1] [section]...`
- Section: `[tag] [n bytes] [data]`. Tag 1 = extension values: pairs `(wide id 128..255, value + 64)`, only non-default. Tag 2 = editable-B edits: pairs `(wide id 0..126 or 128..255, value + 64)`, at most 32, the absolute value of End B. Unknown tags are skipped by their length.
- The chunk image version (`PT_VER`, now 2) goes to 3. Before the alpha this refuses every older chunk, which is exactly "no backward compatibility before the alpha" (owner rule). It also makes re-using the retired bit 16 safe.
- Value byte stays "value + 64" (range -64 to +191), same as today.

**Why not use the old pair list for the new ids?** It would work for the plain pair form, but the other form (a 16-byte bitmap, one bit per id 0..127, used when a patch differs in more than 16 values) cannot name ids above 127. A separate block avoids touching that and keeps the old ids' code unchanged.

### 3.2 Sparse id list or fixed layout (compared)

| | Sparse pairs (id, value) | Fixed list (positions) | Bitmap + values |
|---|---|---|---|
| Typical patch (nothing used) | **0 B** | 16 B every patch | 2 B |
| Worst case (all 15 off default) | 30 B + headers | 15 to 16 B + header | 2 + 15 B |
| Add a value later | **new id, nothing moves; old records still load** | append at the end; a shorter old block means "rest default" (needs a length) | next bit; same as fixed |
| Retire a value | leave the id unused | leave the position unused | same |
| Unknown value in a record | skipped, because each pair names itself | skipped only if the block says its length | skipped |
| Fits the "every slot can always be written" rule | yes, counted at its worst | yes | yes |
| Matches the existing code | **yes, same as the old pair list** | no | partly (the BM form) |

**Recommendation: sparse pairs in the patch record.** The worst case of the fixed forms is a little smaller, but the 596 B ceiling is far away at 534 B, and sparse pairs make "unknown id: skip it" automatic, which is the property that lets later values be added after the alpha without a conversion.

**Multis and projects: dense, not sparse** (their rule is that the worst case must always fit, and they store every old value densely already, 1 byte each). Per part: after the old values, `[nx: 1 byte] [nx bytes, value + 64]`. A reader takes `min(nx, its own slot count)` and gives defaults to the rest; a writer writes all of its slots. Old shorter multi: the rest default. Newer longer multi on older firmware: the extra bytes are ignored.

### 3.3 Forward and backward behaviour, and MIDI LEARN / locks for the extension values

**Before the alpha (now):** new versions everywhere, old data not read: `PT_VER` 3 for chunks, `MU_VER` 6 to 7 for multis, `PROJ_REV` 4 to 5 for projects. The multi and project readers today accept only an exact version and `np == P_COUNT` (CONFIRMED), so they refuse old records, and that is fine before the alpha.

**After the alpha:**
- New firmware reads old records: no block means all defaults.
- Old firmware reads new records: unknown sections and unknown ids are skipped; the patch still loads; the unknown values are lost if that old firmware saves the patch again (a downgrade is not a promise). The editor's PATCH_RAW copies the record bit for bit, so a librarian trip keeps unknown values.
- Rule: a slot is **never renumbered and never reused for another meaning**. A retired value stays frozen at its default (as id 106 did). This is the rule that was broken for ids 25 and 26, which is why they are not free today.

**MIDI LEARN and parameter locks need a number in 7 bits today (CONFIRMED):**
- The learn table has 16 entries of 16 bits each: CC 7 bits, part 2 bits, code 7 bits (`ML_E0` 120). All 127 codes are taken (119 common ids, 8 engine slots). The table lives in the settings record, which is read only if its size is exactly right (a PER4 record); a larger one is no record.
- A motion record is `{place, param, value}`: `param` is 7 bits of id plus the lock bit 7.
- So the extension values cannot be learned or locked without new room. The five on-id values do not need any: they use ids, so they get learn, automation and locks for free (their learn codes are 26, 27, 43, 107, 114).

**What the format reserves now, at almost no cost:**
- **Motion id 127** is the "extension escape": a record with id 127 names an extension slot in its value byte, and the next record carries the value. (P_COUNT is at most 127, so no real id is 127; CONFIRMED asserts in project.c and core.h.) It costs two of the 128 motion records per locked step. Nothing writes it in version 1.
- **A second learn table for extension slots**, 8 entries of 16 bits (CC 7, part 2, slot 5 bits), in the settings. Because the settings record must have an exact size, adding it later would reset everyone's settings. **Recommendation: reserve a 64-byte tail in the settings record in the same format round.** It also holds velocity calibration settings if you want them there (VELO_STUDY says about 80 B, in its own small store, so that one is separate).
- The editor's MIDI LEARN map (commands 78, 79) carries a 7-bit id; extension entries would need a new tag (`43 02`) with an extra slot byte.

### 3.4 How the matrix and the morph reach the extension values

- **One rule:** the extension values live in the part's value list after the old ones: `t->p[128 + k]`, so the list is 128 + NX long instead of 127 (index 127 stays empty).
- **Matrix (CONFIRMED mod.c):** a per-block destination writes `t->p[id]` and puts it back after the block, with the id in a `uint8_t` table (`mod_param`'s `ID[MD_N]`, `mod->kid[]`). Ids up to 255 fit. So a destination for an extension value (SUB LEVEL) needs only one more table row. New destinations are appended: 51 CUTOFF 2 (per note; added to the cutoff sum in the voice path, it does not write the stored value), 52 RES 2, 53 DRIVE, 54 BLEND, 55 SUB LEVEL. The list ends at 56 of 128. New source CYCLE: 15 to 16.
- **Morph (CONFIRMED morph.c):** the tags (`MT[]`, one per id: roll kind, cap, fade) cover ids below `P_E0`; add a second table for the extension slots. The per-part ends, history of 9 rolls per end, the ramp and the blend cache are all sized `P_COUNT` today. Resize them to 128 + NX.
- **Old rolls stay the same:** a roll draws its random numbers by id in order. Extension slots come after every old id, so existing seeds still give the same ends. Exception: ids 25, 26, 42, 106 and 113 change meaning, so their draws change; TWIN design 2 already re-picks End B seeds for the 43 converted sounds, and the other 85 sounds should be checked by the sound audit (est.).
- **End B in the record:** stays "seed + AMOUNT + bits" (7 bytes) that regenerates the rolled B. Editable B adds section 2 (hand edits on top of it).

### 3.5 The store: chunk layout, flash, power cuts

**Geometry (all numbers CONFIRMED from the source, the new ones est.):**

| | Today | Proposed |
|---|---|---|
| Chunk payload | 3,584 B (ends at sector offset 0xF00, where the update loader scans) | same |
| Records a chunk (`PT_K`) | 8 | **6** |
| Chunks (`PT_NCH`) | 16 (objects 0 to 5, and 33 to 42) | **22** (objects 43 to 48 added) |
| Slots | 128 | 132 available, 128 used |
| Largest record | 434 B (limit 447) | **534 B** (limit 596) |
| Flash used | 0xA6000 to 0xB9FFF for chunks 6 to 15 | chunks 16 to 21 at 0xBA000 to 0xC5FFF |
| Left free | 0xBA000 to 0xC7FFF (56 KiB) | **0xC6000 to 0xC7FFF (8 KiB)** |

- The sector formula in `storage.c` (`st_sector`) already continues in the same stride for any object number from `OBJ_PATCHX0` up, so objects 43 to 48 land on 0xBA000 to 0xC5FFF with no change to the formula. Only `OBJ_COUNT` grows.
- `PT_MAX` (chunks x records) is asserted equal to `UP_SLOTS` today; it becomes "at least". The rule that a chunk takes a new record only under `PT_K` records keeps the guarantee "every one of the 128 slots can always be written".
- The write method does not change: erase the other sector, write the payload, write the commit record last, read back. Flash time per save is the same.
- Pool memory (est.): `pt_rbuf` (four records read ahead) grows by 400 B, `pt_enc` (the write scratch, 640 B, asserted to hold the worst pair-form record) must grow to about 768 B, the decoded record `up_rec_t` (192 B x ring of 4) grows by the extension bytes.

**Is the 56 KiB really free on the newest code? Yes (CONFIRMED by reading), with these limits on what I could check:**
- `storage.c` maps every object: wavetables 0xA0000 to 0xA5FFF, chunks 6 to 15 at 0xA6000 to 0xB9FFF, chunks 0 to 2 at 0xC8000 to 0xCDFFF, multis 0x97000 to 0x9EFFF and 0xCE000 to 0xD5FFF, pattern slots 0xD6000 to 0xDBFFF, chunks 3 and 4 at 0xDC000 to 0xDFFFF, chunk 5 at 0x9F000 and 0xFE000. Nothing maps 0xBA000 to 0xC7FFF.
- The write guard (`FL_STORE_OK`) allows 0x97000 to 0xDFFFF, so the range is writable.
- The app image is 462,684 B (build log of join0234) in a window that ends at 0x8DFBC, below the store's start at 0x97000, so program growth cannot reach it.
- `tests/erase_test.c` plants a pattern at 0xBA000 and checks that ERASE DATA leaves it alone (this test must move to the pair that stays free, 0xC6000).
- Not done: no linker map file exists to read; I did not run the linker; I did not look at a real unit's flash. Old bench worktrees (bench, bench2, core1, hotram) used 0xC6000 and 0xC7000 for a bench record (`BENCH_FL`); the newest line does not.

**Tests that must still pass, with what changes in each (all run on the computer):**
- `storage_test.c`: torn write at every byte keeps the old data. Unchanged; add a chunk of the new size.
- `patch_test.c`: power cut at every step of SAVE, RESTORE and RESTORE ALL; a move between chunks (two copies, the newer revision wins); "128 saved, any slot is still written". Needs the new worst record (all 16 extension slots off default, 32 edits, FM6, arp block), `PT_NCH == 16` assertion (line 436) and the flash range check (line 28) updated.
- `patchraw_test.c`: BEGIN, DATA, COMMIT of a whole record; the flash range check (line 25) gains the new chunks; the length limit follows `PT_WORST`.
- `backup_test.c`, `erase_test.c`: see 3.7 (backup) and the erase note above.
- `multi_test.c`, `project_test.c`, `persistence_test.c`, `fuzz_ed.c`, `fuzz_proj.c`: the new sections, version refusal, a cut or damaged block.
- New: a record with unknown tags and unknown ids loads; a block whose length does not add up is refused; extension values survive SAVE, power cycle, RESTORE, PATCH_RAW round trip.

### 3.6 Multi and project records

- **Multi (multi.c):** 1,916 bytes at most per multi, two to an object of 3,840 bytes (the sector's limit, cannot grow). Two FM6 parts with all 64 steps used take 1,775 B, leaving the automation about 46 events. Adding `1 + 16` bytes per part costs 34 B: about **35 events** left in that worst case (est.). It is a rare case (most multis have a fraction of it). Each further slot costs 2 B per multi (about 0.7 event).
- **Project (project.c, the autosave and the editor's backup):** 3,840 B, 1,950 used before the FM6 patches. +34 B fits. The main loop's local value copy (`fit[NTRK][P_COUNT]`, about 1.6 KB of stack) grows by about 140 B.
- Both get a version bump and the counted dense section from 3.2.

### 3.7 The editor, PATCH_RAW, backup and layout file

- **PATCH_RAW (cmd 84):** moves the stored record whole, so the block travels as it is. The unit side checks the length against `PT_WORST` (534). The editor's `patchrecord.js` has a hard-coded `WORST = 440` that must follow, and gains a parser for the block. The four steps (BEGIN, DATA in pieces of 112 B, COMMIT) and the CRC do not change.
- **Live values:** GET, SET, DESC use `scope` 0 (track) and 1 (global). Add **scope 2 = extension slot** (id 0 to NX-1), CHANGED push likewise. `DUMP`, `UP_GET`, `UP_PUT` stay at 127 values like End B today (UP_PUT already drops End B and the arp block); the librarian uses PATCH_RAW. INFO gets one new tag with NX.
- **Layout file:** `web/editor/layout.json` is generated by `tests/editordump.c` from the firmware, so the extension descriptors (label, format, range, default, names) come from a new table beside `TP[]` in params.c. The pages and QUICK MULTI refer to a value by scope and id, which the layout already supports (a page whose K1 id equals a global id is told apart by its scope).
- **Backup (editor_backup.c):** the archive carries the **physical chunks**: 40 ids, a chunk "with more than `PT_K` records is refused", archive "version 4". 22 chunks need six more ids and a new archive version. The code comments themselves say a logical "patch store" object (per-slot records, like PATCH_RAW) would survive a later change of geometry. **Recommendation: do it now**, while the archive format is being changed anyway.
- **Erase list (erase_data.c):** add the six objects.

### 3.8 Conversion of the sounds and multis, WAVE's SUB, factory data

**Factory data format (sounds.c, gen_duell_sounds.py, CONFIRMED):** a sound is a sparse list of `(id, value)` pairs (`ds_pair_t`, `uint8_t id`), 3,470 pairs in about 10.4 KB for 128 sounds. A wide id fits the byte, so **the generated format does not change**; only the loader (`ds_put` skips ids at or above `P_COUNT` today) routes ids of 128 and up into the extension list, and the generator needs the extension labels. Cost: about 1 to 4 pairs for each converted TRIO sound (LINK = OFF, ENV 2 amount, and an LFO amount in #53 CHIP CHAPEL, #54 STRING MACH, #108 CIRCUIT BEND), a few dozen bytes in all.

**The 43 sounds and 10 multis (design 2, section 1.5):**
- ANALOG (19) and NOISE (11) convert into filter 1's existing ids: **no extension value at all.** Nothing about the format.
- TRIO (13) converts into filter 2: TYPE, CUTOFF 2, RES 2 on ids, and the extension values LINK = OFF (every one of the 13) and the ENV 2 amount (and LFO amounts in three).
- Multis: a multi keeps its own copy of each part's values, so the converter must rewrite the records. The factory multis are built from `tools/factory_multis/M01..M16.bin` by `tools/factory_multis.py`, and they must be regenerated anyway when `MU_VER` changes. Eight of the ten are yours (M03, M04, M06, M07, M08, M09, M12, M14); design 2 question 2(d) asks for the exception, with you listening first.

**WAVE's SUB card (E5, CONFIRMED `eng_wave.c`):** retired and converted to SUB LAYER SINE -1. **Six factory sounds use it:** #61 EVOLVING PAD (SUB 25), #91 DRAWBARS (108), #22 HARM BASS (64), #21 GROWL BASS (51), #24 REESE BASS (57), #23 KICK BASS (70). Each gets SUB LEVEL (X6) at a matching level and nothing else (OCTAVE -1 and SINE are the defaults), and its WAVE slot pair is removed. The level match is by ear (the SUB LAYER's loudness curve is set in the offline test). Those sounds appear in five factory multis: M01 REESE SWAMP, M02 NIGHT ORGAN, M03 PAD SPLIT, M13 COLD CHAPEL, M14 GARGLE ACID (M01, M03, M14 are also in design 2's list, so **12 multis in all**; M02 NIGHT ORGAN is in `owner_multis.py` and M13, M14 are marked owner in `factory_multis.py`, so three of the added ones are yours too). A user's own saved WAVE patch needs no conversion before the alpha.

---

## 4. Room for later

**In the patch record (limit 596 B, worst 534 B): 62 B left**, which is any of:
- about **31 more extension values** (2 B each, all off default at once), or
- editable-B cap from 32 to 63 edits, or a mix.

**Wide-id space:** 128 slots (X0 to X127); 15 used, 1 spare.

**What is on the list for later and what it needs:**

| Later item | Needs patch-record room? | Where |
|---|---|---|
| Velocity / aftertouch sensitivity, on / off, curve, calibration | **No.** Device settings and a small store (VELO_STUDY). Only a per-patch "velocity depth" would be extension values (1 to 2 slots) | settings tail (3.3) |
| Editable B, 32 edits | In this plan (section 2) | block section 2 |
| B FROM SLOT | stores differences as edits; above 32 it must refuse with a message or the cap rises (to 63 at most) | section 2 |
| Filter 2 extras (CLIP, FM): 2 more values, later types (FORMANT and others) | 2 slots; the TYPE list is append-only | X15 and up |
| SUB HARMONICS as a matrix destination, CYCLE destinations, more TWIN destinations | none (destination list, 56 of 128) | list |
| MORPH GROUPS, BRIDGE and the other rejected helper ideas | rejected | none |
| Ring modulation between parts, FILTER FM, LISTEN | parked: 1 to 3 values or none | slots |

**What would force another format change after the alpha:**
1. A **record over 596 B** at its worst. The flash cannot give more (all seven free pairs would be used up to 22 chunks; 5 a chunk needs 26 chunks).
2. **The settings record**: it must have exactly the right size to be read. Any new setting after the alpha resets users' settings, unless the 64-byte tail is reserved now.
3. **MIDI LEARN and locks for the 15 extension values**: needs the second table in the settings and the motion escape (3.3). With the tail and escape reserved it is a firmware-only change.
4. **The backup archive**, if it stays physical: any change of records a chunk breaks every archive made before.
5. **Reusing an id or slot for a new meaning** (never do it).
6. **Matrix**: the destination list holds 128 (56 used), the source list 128 (16 used): room for a long time.
7. **Multi record at 1,916 B**: each added slot cuts the worst-case automation (2 B per multi); at about 30 slots the automation would be down to a handful of events.

---

## 5. What is cheap, what is risky, effort

**Cheap:**
- Geometry (`PT_K` 6, `PT_NCH` 22, objects 43 to 48): the sector formula already fits.
- The block in the patch record and its tests: the pair format is the one the code already has.
- Factory data format: no change to the generated file.
- WAVE STEPS: no format work.
- Wide ids for the five on-id values: no record growth.

**Risky:**
- **RAM**: the extension values add to every part's value list and to the morph's per-part arrays: about 70 B a slot (4 parts x 2 B, plus morph about 30 B for each of 2 parts, est.), so **about 1.1 KB for 16 slots**. TWIN design 2 plus SUB and CYCLE already want about 1.65 KB of the 2.1 KB spare. This does not fit as it stands. The way out is to move the morph's roll history to the pool (about 29 KB free) or to keep fewer history entries for extension slots (HYPOTHESIS; needs a look at the access cost).
- **Ids 25, 26, 42, 106, 113**: four small cleanups in several files (global SCALE page descriptors, project load, the factory-sound loader, the arp/keys block, morph tags and the engine ANALOG's TYPE code).
- **Backup**: changing archive shape touches the firmware, the editor, and the backup tests together.
- **Conversion**: the 10 (12) multis include 8 yours; you listen first.
- **Timing**: ids 113 (and 25/26) exist only after the cleanups and the engine-filter removal.
- Soft spot: all of this is "reserve before the alpha" work that changes no sound; mistakes would show as lost values, not as audio.

**Effort (rounds, est.):**

| Part | Rounds | Simulation-testable (cloud session could do it) |
|---|---|---|
| Store: geometry, the block, `PT_VER` 3, patch tests | 2 | **yes** (host tests with the simulated flash, `patch_test`, `storage_test`) |
| Multi and project sections, version bumps, tests | 1 | **yes** (`multi_test`, `project_test`, fuzz) |
| Value list to 128 + NX, morph arrays, tags, matrix table rows, ext descriptors, scope 2 | 2 | partly: the morph and matrix logic by host tests; the RAM fit needs the device build |
| Editor: `patchrecord.js`, scope 2, layout generation, mock unit | 1 to 2 | yes (editor tests against the mock unit) |
| Backup as per-slot records (archive version 5), erase list | 1 to 2 | yes (`backup_test`, `erase_test`) |
| The id cleanups (25, 26, 42, 106) | 1 | yes |
| MIDI LEARN and locks for extension values (second table, escape, editor tag) | 2 | mostly yes; a unit session for the feel |
| Converter for the 43 sounds, 6 WAVE sounds, 12 multis; test fingerprints re-made | 2 | the tool and its table test: yes; the listening is yours |
| **Total, without learn/lock** | **about 9 to 11** | the earlier "2 to 3 rounds" for stage A only counted the store. The backup, the editor, the multi and project sections and the id cleanups are what make it more. |

If you want the cheapest honest version: reserve only (geometry, block, sections, scope 2 stub, backup), 5 to 6 rounds, and let each feature fill its slots when it is built.

### Questions for you (plain words, each with my recommendation)

1. **Bigger records:** 6 a chunk (limit 596 B, up to 32 hand edits in editable B, six of the seven free flash pairs used) or 7 a chunk (limit 511 B, only about 20 hand edits)? **Recommended: 6 a chunk, 32 edits.** The 62 B left over is the room for later values.
2. **MIDI LEARN and locks for the 15 values that have no number:** build it in the first version, or only reserve the room (a 64-byte settings tail and one reserved lock code) and build it when you ask? **Recommended: reserve now, build later.** The five on-id values get learn and locks anyway.
3. **Backup of the patch store:** keep it as a copy of the physical chunks (a geometry change then breaks old backups), or store the 128 patches one by one (survives any later change)? **Recommended: one by one, now.**
4. **How many extension slots does the unit hold in memory today:** 16 (15 used plus 1 spare, about 1.1 KB of RAM) or more (about 70 B each)? The format allows 128 and later ones add without breaking saves. **Recommended: 16.**
5. **Ids 25 and 26:** clean them up so they carry TWIN's RES 2 and DRIVE (they then get MIDI LEARN and locks), or put those two in the extension block (no cleanup, no learn)? **Recommended: clean them up.**

Contradictions with the ticket, said once:
- The ticket says "no cloud talk" and also asks which parts could go to cloud sessions; I answered the second in one column of the table and nothing more.
- FILTER_PAGES_PROPOSAL recommends leaving QUICK MULTI unchanged, but FEATURE_LIST has the later decision (three layouts). I used the later one: CUT 2, RES 2 and DRIVE are all on ids, so that layer needs no extension value except that LINK picks the layout.
- HEADLINE_FEATURES says 23 chunks and "27 planned for KEY TRK": 22 chunks suffice, and id 27 is already KEY TRK in the code.

---

## Evidence

| Claim | Source | Mark |
|---|---|---|
| 20 values, 11 TWIN (5 on ids, 6 extension), 4 SUB, CYCLE with own SYNC | TWIN_FILTER_DESIGN_2.md 3.1 to 3.2, SUB_AND_CYCLE_DESIGN.md A2, B, FEATURE_LIST.md | CONFIRMED there |
| Record 434 B, limit 447 B, PT_K 8, 16 chunks, payload 3,584 | `upreset.c` `PT_W_SEQ`, `PT_K`, `PT_CAP` | CONFIRMED |
| 466 B and 534 B, 596 B and 511 B limits, 22 chunks | my arithmetic from those constants | est. |
| Flash layout and the free 0xBA000 to 0xC7FFF | `storage.c` `st_sector`, `fm1_flash.h` `FL_STORE_OK`, join0234 build log (image 462,684 B), `app.ld` window 0x8DFBC | CONFIRMED (no link map, no unit) |
| ids 25, 26 still in use; 42 and 106 free | `params.c` lines 138, 139, 154, 210; `upreset.c` `AK_KEYID`, line 1146; `project.c` line 367; `sounds.c` line 59 | CONFIRMED |
| MIDI LEARN codes full; motion param 7 bits; id 127 unused | `midi_control.c` `ML_E0`, `ML_N`; `midi_learn.c`; `core.h` `motion_event_t`; `P_COUNT <= 127` asserts | CONFIRMED |
| Settings record read only at its exact size | `settings_persist.c` header comment | CONFIRMED |
| Matrix: `kid`/ID tables are `uint8_t`; destinations 51 of 128 | `mod.c` | CONFIRMED |
| Morph arrays sized P_COUNT; roll history 9 per end | `morph.c` `morph_part_t` | CONFIRMED; the RAM figures are est. |
| Multi 1,916 B, automation 46 events at worst; project 3,840 B | `multi.c` header, `project.c` | CONFIRMED; 35 events est. |
| Factory pairs are `uint8_t` ids; 6 WAVE sounds with SUB | `sounds.c` `ds_pair_t`; `build/gen/duell_sounds.h` (parsed with a script; #NN checked against #01, #02, #54, #128) | CONFIRMED |
| Backup archive: physical chunks, 40 ids | `editor_backup.c` header | CONFIRMED |
| Velocity needs no patch values | `VELO_STUDY.md` rows 9 and the calibration notes | CONFIRMED there |

**Not verified:** every RAM figure; whether the morph history can move to the pool without a speed cost; that the 8 KiB pair at 0xC6000 is untouched on a real unit; the defaults the studies left open (RES 2, CYCLE MODE, RISE, FALL).
