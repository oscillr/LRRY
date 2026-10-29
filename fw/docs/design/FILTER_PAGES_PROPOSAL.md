# FILTER PAGES: filter 1 and filter 2 (TWIN) together, page by page (proposal, 2026-10-19)

Private. Research and writing only: no code changed, no branch made, no picture drawn. INDEX.md and the other documents were not touched. Every layout below is a proposal for you to approve or change; nothing is decided by me. I changed no sound.

Sources read: TWIN_FILTER_DESIGN_2.md (all), TWIN_FILTER_DESIGN.md (page options), LAYOUT_PROPOSAL.md, EDITOR_NAMING_PROPOSAL.md, EDITOR_REWORK_LIST.md, STATUS_AND_QUESTIONS.md, MATRIX_LIST_PREVIEW.md, SPEC.md, the page audit, and the real code in `wt/join0233/felucca-base` (`firmware/src/params.c` PAGES, `ui_draw.c`, `ui_graph.c`, `ui_layer.c`, `ui_perform.c`, `ui.c`, `mod.c`, the three engines; `web/editor/app.js`, `layout.js`, `labels.js`). Marks: **CONFIRMED** (read in the code or docs today), **measured** (computed from the unit's own font table), **proposal** (mine).

## Words used here (the rest are in GLOSSARY.md)

- **Cutoff**: the frequency where the filter starts to close. **Resonance (RES)**: how much the filter rings at the cutoff. **Slope**: how steeply it closes (12 dB or 24 dB per octave; 24 is steeper).
- **LP / BP / HP / NOTCH**: low-pass (lets lows through), band-pass (a band), high-pass (lets highs through), notch (cuts a band). **LADDER**: a 24 dB Moog-style low-pass, fat, can sing. **COMB**: a row of evenly spaced peaks, tuned to the note.
- **Filter 1** = today's MULTI FILTER. **Filter 2** = the new TWIN filter.
- **LINK and OFFSET**: with LINK on, filter 2's cutoff follows filter 1's. Filter 2's own cutoff knob then says how far above or below filter 1 it sits (the OFFSET). With LINK off it is a normal cutoff.
- **DRIVE**: a soft overload (saturation) that adds harmonics. 0 means it is not run at all (exact bypass).
- **ROUTE**: SERIES (sound goes through filter 1, then filter 2) or PARALLEL (both filters at once, then mixed). **BLEND**: the mix of the two filters.
- **Dial step**: filter 1's cutoff knob has 82 steps, each about 0.11 octave (1.3 semitones). **Octave / semitone**: a doubling of frequency / one piano key.
- **Matrix**: the 8 slots where a source (LFO, envelope, velocity ...) drives a destination (cutoff ...).
- **Hide-inert rule**: a control that cannot do anything for the current selection is not drawn, and the cards that remain move left (T0227 rule, `ui_draw.c page_compacts`). A value of 0 is never a reason to hide.
- **Extension block**: the extra storage for the 6 filter-2 values that have no parameter id (design 2, section 3.2).

## What I found that differs from the ticket (said once, so nothing is quietly picked)

1. **Filter 1's knob order is CUTOFF, RES, MODE, SLOPE** (CONFIRMED `params.c`), not type first. You kept it on purpose in 0.2.20 (playing habit; the QUICK layer uses the same order), although the page-order audit proposed MODE, SLOPE, CUTOFF, RES. I leave it unchanged. Filter 2 starts with TYPE (it must: TYPE is its on/off).
2. **No home or EDIT overview exists** any more (removed in 0.2.14). HOME is MORPH, MORPH SET, MODE; EDIT starts at ENGINE. Section 2.4 says what that means.
3. **`docs/PAGE_ORDER_AUDIT.md` is not in the DUELL folder**; I read it in `/Users/balinth/wt/rel0214g/docs/`.
4. **The editor has no FILTER tab today.** The filter cards sit in the ENGINE tab (MULTI FILTER, then CUTOFF MOD).
5. **Design 1 hid BLEND in SERIES; design 2 shows it always** (it works in both routes). I follow design 2.
6. **The old note "hidden controls keep their knob positions" is out of date**: since T0227 the cards that show move left. I follow the code.
7. **Honesty point (label law):** with LINK on, filter 1's CUTOFF knob (and its matrix destination CUTOFF) also moves filter 2, although its label says only CUTOFF. My answer: the OFFSET label on filter 2 and a one-line caption in the graph ("FILTER 2 FOLLOWS FILTER 1") say so. If you find that not enough, the fallback is a 1 on the filter 1 cards, which renames pages you know.
8. **Six of the 11 filter-2 values have no parameter id** (design 2, 3.2): LINK, ROUTE and the four mod amounts. Cards for them cannot be MIDI-learned, step-locked or recorded as motion, while the same-looking cards of filter 1 can. Not a layout choice, but you should know before approving the pages (section 6, note).

---

## 1. Verdict

**Recommendation: Option B, five pages, two of them are filter 1's and unchanged.** Filter 2 gets three pages named for what they hold (TWIN FILTER, TWIN MOD, TWIN CHAIN), placed after filter 1 in the order the sound flows, with the mix last. LINK sits next to the cutoff it relabels; DRIVE sits with ROUTE and BLEND on the page that says how the two filters are connected. No page has a gap in the middle. With filter 2 OFF, two of its pages disappear and the third shows its TYPE card alone.

Short reasons:
- Your page-order rule: type first (filter 2's TYPE), then what changes the meaning of the next card (LINK before CUTOFF/OFFSET), then amounts, mix last (BLEND).
- Design 2's Option A had two empty knobs in the middle of TWIN ROUTE and put LINK two pages away from the cutoff it renames. Option B has no hole: the last page has 3 cards and an empty K4 at the right end, as EQ 2/2, DUCK 2/2 and INSERT 2/2 already do.
- One graph for both filters (the line you are editing bright, the other dim), so the offset between them can be seen.

**The whole filter section, in page order (Option B).** Ranges and defaults for filter 2 are mine where design 2 gives none (marked *). "Hides" = when the card is not drawn.

| # | Page (header) | K1 | K2 | K3 | K4 | Graph | Matrix destinations owned |
|---|---|---|---|---|---|---|---|
| 1 | MULTI FILTER 1/2 (today, unchanged) | **CUTOFF** 0..82 steps, 16.0 kHz (open); where filter 1 closes; with LINK on filter 2 follows. Never hides | **RES** 0..127 %, 0; ringing at the cutoff. Never hides | **MODE** LP, LP>BP, BP, BP>HP, HP (0..127), LP; Never hides | **SLOPE** 12dB / 24dB, 12dB. Never hides | filter 1's curve bright; filter 2's dim if on; a tick per cutoff; caption when linked | CUTOFF, FILTER RES, FILTER MODE |
| 2 | MULTI FILTER 2/2 (today; now the same on all 10 engines) | **KEY TRACK** -100..+100 %, +51 %; cutoff follows the played note | **FILT ENV** -64..+63, 0; how far ENV 2 opens (+) or closes (-) the cutoff | **LFO 1** -64..+63, 0; LFO 1 to cutoff | **LFO 2** -64..+63, 0 | same graph as page 1 | FILTER ENV AMOUNT (the K2 value, also on ENV 2 DEST) |
| 3 | TWIN FILTER (new) | **TYPE** OFF, LP, BP, HP, NOTCH, LADDER, COMB; OFF. Never hides (it is the switch) | **LINK** OFF / ON; ON. Hides with TYPE OFF or COMB | **OFFSET** -63..+63 steps shown as semitones; 0 (LINK on). **CUTOFF** 0..82 (LINK off). **TUNE** on COMB (semitones around the note, ±24*). Hides with TYPE OFF | **RES** 0..127 %; 0*. **FDBK** on COMB -64..+63; 0. Hides with TYPE OFF | filter 2's curve bright, filter 1's dim; ticks 1 and 2; caption by LINK. COMB: teeth at a sample note | CUTOFF 2, FILTER 2 RES |
| 4 | TWIN MOD (new; skipped while TYPE is OFF) | **KEY TRACK** -100..+100 %; 0. Hides on COMB | **FILT ENV** -64..+63; 0 (moves TUNE on COMB) | **LFO 1** -64..+63; 0 | **LFO 2** -64..+63; 0 | as page 3 | none (see section 5) |
| 5 | TWIN CHAIN (new; skipped while TYPE is OFF) | **ROUTE** SERIES / PARAL; SERIES | **DRIVE** OFF, 1..127; OFF (0 = not run) | **BLEND** F1 ONLY at -64, BOTH at 0, F2 ONLY at +63; 0 | (empty, nothing to hide: the page has three cards) | combined curve bright, both single curves dim; a one-line flow strip with the drive number | FILTER DRIVE, FILTER BLEND |

With filter 2 OFF the EDIT round has pages 1, 2 and 3 (TYPE alone). With filter 2 on, pages 1 to 5. COMB removes LINK (page 3) and KEY TRACK (page 4); the remaining cards move left.

---

## 2. The filter section as the player walks through it

### 2.1 Where it sits in EDIT's round (CONFIRMED `params.c` PAGES, after the engine filters go)

ENGINE, EDIT 1, [EDIT 2 only where an engine still has an eighth control], **MULTI FILTER 1/2, MULTI FILTER 2/2, TWIN FILTER, TWIN MOD, TWIN CHAIN**, [FM6's OP pages], VOICE 1/2, VOICE 2/2.

The three engine-filter pages (ANALOG FILTER, TRIO FILTER, NOISE FILTER) are gone (section 4). The order follows the sound: engine, filter 1, (drive), filter 2, voice.

A tap of EDIT or SELECT steps through the visible pages. With filter 2 OFF, TWIN MOD and TWIN CHAIN are skipped by the same test that skips SHIMMER unless the reverb is SHIM (`ui.c page_visible`). The footer's page number ("5/9") counts only visible pages, so it changes by itself.

### 2.2 Page by page (4-knob rows; the state shown is a typical one)

**Page 1: MULTI FILTER 1/2** (unchanged)

```
+------------+------------+------------+------------+
| CUTOFF     | RES        | MODE       | SLOPE      |
| 16.0 kHz   | 0 %        | LP         | 12dB       |
+------------+------------+------------+------------+
```
Graph: filter 1's response, 20 Hz to 20 kHz, as today (`graph_filt`). Filter 2's curve, if on, in the dim colour. Open end with MODE LP draws a flat line (the filter is bypassed), as today.

**Page 2: MULTI FILTER 2/2** (unchanged; now all four cards on every engine)

```
+------------+------------+------------+------------+
| KEY        | FILT ENV   | LFO 1      | LFO 2      |
| TRACK      | +0         | +0         | +0         |
| +51 %      |            |            |            |
+------------+------------+------------+------------+
```
KEY TRACK breaks into two lines at its space (61 px against a 50 px label), as it does today; its value takes the third line and it has no gauge. The three amounts are the same stored values as the cutoff cards on ENV 2 DEST, LFO 1 DEST and LFO 2 DEST (reachable at both places, your 2026-10-16 rule).

**Page 3: TWIN FILTER** (new). Four states:

```
TYPE OFF (the page shows its switch only; pages 4 and 5 are skipped)
+------------+------------+------------+------------+
| TYPE       |            |            |            |
| OFF        |            |            |            |
+------------+------------+------------+------------+

LP, BP, HP, NOTCH or LADDER, LINK ON
+------------+------------+------------+------------+
| TYPE       | LINK       | OFFSET     | RES        |
| LP         | ON         | +0.0 st    | 0 %        |
+------------+------------+------------+------------+

same, LINK OFF (the third card is a plain cutoff)
+------------+------------+------------+------------+
| TYPE       | LINK       | CUTOFF     | RES        |
| LP         | OFF        | 9.8 kHz    | 0 %        |
+------------+------------+------------+------------+

COMB (LINK hidden; the comb follows the key by itself)
+------------+------------+------------+------------+
| TYPE       | TUNE       | FDBK       |            |
| COMB       | +0 st      | +0         |            |
+------------+------------+------------+------------+
```
Gauges: OFFSET, TUNE and FDBK are true bipolar (they go below 0), so they fill from the centre. CUTOFF, RES fill from the left. TYPE and LINK are a list and a switch: they show their value and no fill that pretends to be an amount.

OFFSET is shown in semitones with one decimal, because one dial step is 1.32 semitones (the knob moves in jumps of 1.3; the display rounds that). Range ±63 steps = ±83.2 semitones. Plain octaves ("+0.55 oct") are 51 px wide and do not fit the 49 px value field; semitones do (measured).

Turning LINK keeps the sound: the stored value is converted (design 2, 3.1), so the card changes its label and its value, the sound does not move.

**Page 4: TWIN MOD** (new; skipped while TYPE is OFF)

```
LP / BP / HP / NOTCH / LADDER
+------------+------------+------------+------------+
| KEY        | FILT ENV   | LFO 1      | LFO 2      |
| TRACK      | +0         | +0         | +0         |
| +0 %       |            |            |            |
+------------+------------+------------+------------+

COMB (KEY TRACK hidden; the three left move to K1..K3)
+------------+------------+------------+------------+
| FILT ENV   | LFO 1      | LFO 2      |            |
| +0         | +0         | +0         |            |
+------------+------------+------------+------------+
```
Same order and same labels as page 2: one rule for both filters. These amounts are **extra**, added to filter 2 alone, on top of whatever it takes from filter 1 through LINK. The graph caption says it: with LINK on, "ADDED TO WHAT FILTER 1 SENDS"; with LINK off, "FILTER 2 ONLY".

**Page 5: TWIN CHAIN** (new; skipped while TYPE is OFF)

```
+------------+------------+------------+------------+
| ROUTE      | DRIVE      | BLEND      |            |
| SERIES     | OFF        | BOTH       |            |
+------------+------------+------------+------------+
```
BLEND is bipolar, so it fills from the centre. Its value names the ends and the middle, as MODE does today (LP, LP>BP, BP): F1 ONLY at -64, BOTH at 0, F2 ONLY at +63, a signed number between. DRIVE shows OFF at 0 because 0 really is the bypass.

The graph (section 2.3) draws the flow in one line: `F1 > DRIVE 40 > F2` for SERIES, `DRIVE 40 > F1 + F2` for PARALLEL. In PARALLEL the drive sits **before both** filters (design 1, 2.1; "between" has no place in PARALLEL), and the strip shows exactly that. In SERIES, BLEND slides filter 2's input from filter 1's output towards the unfiltered sound (design 2, 3.1).

### 2.3 The graph area (the panel under the cards, x 10..230, 122 px high)

CONFIRMED: `graph_filt` already draws 20 Hz to 20 kHz in 56 points, +18 to -42 dB, 0 dB line, a tick at the cutoff. All five pages use it, so the area is never empty.

| Page | Bright line | Dim line | Ticks | Caption row (small text, under the curves) |
|---|---|---|---|---|
| 1, 2 | filter 1 | filter 2 (if on) | 1 at filter 1's cutoff; 2 at filter 2's | filter 2 on and linked: FILTER 2 FOLLOWS FILTER 1; else none |
| 3, 4 | filter 2 | filter 1 | same | LINK on: 2 = 1 + OFFSET; LINK off: FILTER 2 ON ITS OWN. Page 4 only: ADDED TO WHAT FILTER 1 SENDS / FILTER 2 ONLY |
| 5 | the chain (what you hear) | filter 1 and filter 2 | none | flow strip with the DRIVE number |
| 3, filter 2 OFF | filter 1 | none | 1 | FILTER 2 OFF |

How each type draws (all are static pictures, as the EQ and compressor ones):
- LP, BP, HP, NOTCH: the SVF picture used now, with TRIO's softer top (Q capped near 4) so it does not show a whistle it cannot make.
- LADDER: 24 dB, resonance peak, and the thinning of the bass as RES rises.
- COMB: teeth at one sample note (caption "SHOWN AT C3"). The comb depends on the note played, so a fixed example is the honest picture. Open: your call whether to draw it (cheap).
- DRIVE is not linear, so it is not part of any curve; only its number is shown. The graph does not claim to show grit.
- The chain line for SERIES adds the two curves in dB; for PARALLEL it mixes them by BLEND. The MORPH mark (`card_mk`) works as on any card: it appears on a card the fader is moving.

### 2.4 Hold layers, PERFORM, HOME (CONFIRMED)

| Where | Shows today | After this change |
|---|---|---|
| EDIT held: QUICK MULTI | K1 CUT, K2 RES, K3 MODE (filter 1), K4 AMP REL | **Unchanged.** With LINK on, K1 and K2 take filter 2 along, which is the point of LINK. Filter 2 has no card in the layer (question 5) |
| LFO held: QUICK LFO | K2 LFO 1's cutoff amount, K4 LFO 2's | unchanged; filter 1's amounts, filter 2 follows through LINK |
| ENV held: QUICK ENV | K3 ENV 2's cutoff amount | unchanged, same |
| PERFORM, K3 KILL | the DJ filter on the whole mix: low-pass left, high-pass right, OFF in the middle | **Unchanged and not one of these filters.** KILL is a third filter on the master output. The name KILL stays so it is never confused with the part's filters |
| HOME (MORPH, MORPH SET, MODE) | no filter cards | unchanged. The MORPH fader moves filter 2's OFFSET, RES, DRIVE, BLEND and the four amounts; TYPE, ROUTE and LINK never roll in SMART (design 2, 3.5) |
| EDIT's first page ENGINE | K1 ENGINE, K2..K4 the engine's first three controls | after the shrink, ANALOG, TRIO and NOISE show no filter control here (section 4) |
| OCT - held + a knob (reset) | resets a card to its default | works on the new cards; defaults in the table above |

There is no overview page that lists the filters, so nothing else to change. (If you want a one-glance summary like SEND FX, say so; I did not propose one: "no needless pages".)

### 2.5 Options for the contested choices

**Option A: design 2's pages as written**

```
TWIN FILTER   TYPE | CUTOFF/OFFSET | RES | DRIVE
TWIN MOD      KEY TRACK | FILT ENV | LFO 1 | LFO 2
TWIN ROUTE    ROUTE | LINK | (empty) | BLEND
```
- Pros: the Virus gesture (cutoff against drive) is on one page; nothing moves from design 2.
- Cons: LINK, which changes what the cutoff card means, is on the last page; K3 of the last page is empty in the middle for a control (FM) that does not exist yet, and when FM arrives BLEND must move anyway (mix last); the title ROUTE repeats the control ROUTE.

**Option B (recommended): LINK beside the cutoff, DRIVE on the chain page**

```
TWIN FILTER   TYPE | LINK | OFFSET/CUTOFF/TUNE | RES/FDBK
TWIN MOD      KEY TRACK | FILT ENV | LFO 1 | LFO 2
TWIN CHAIN    ROUTE | DRIVE | BLEND
```
- Pros: follows your page-order rule (type, then the control that decides what the next card means, then amounts, mix last); same idea as "glide before glide type" in the editor; COMB's hide (LINK) leaves TYPE, TUNE, FDBK with no gap; the last page is one coherent idea (how the two filters connect); no empty knob in the middle.
- Cons: the cutoff and the drive are on different pages (one page turn). When CLIP (the later drive curve) arrives it goes beside DRIVE and BLEND moves from K3 to K4; that will be your decision then.

**Option C: add a mirror page "both filters" for playing**

```
FILTERS (new, first of the group)   CUTOFF 1 | RES 1 | OFFSET 2 | RES 2   (the same stored values as pages 1 and 3)
then pages 1 to 5 as Option B
```
- Pros: one page to sweep both filters, a Virus front-panel feel.
- Cons: a sixth page, four duplicated cards (the duplicate precedent is only for depths between two modules), labels need numbers (CUTOFF 1) that exist nowhere else, and QUICK MULTI plus LINK already gives one-hand control of both. I recommend against.

**Smaller choices inside B (recommendation in brackets):**
- Page titles (question 4): TWIN FILTER / TWIN MOD / TWIN CHAIN, no numbers [yes]. The page cannot say "1/3" because two of the three vanish with TYPE OFF and the title text is fixed in the page table.
- Title word for the third page: design 2 said TWIN ROUTE, which repeats the control ROUTE inside it (editor rule R1). CHAIN carries the idea without repeating [CHAIN].
- DRIVE with filter 2 OFF (question 2) [hidden, not run].

### 2.6 Every label checked against the screen (measured with the unit's font table, no kerning, plus or minus 1 px)

Rule from the code: a card's label may be 50 px wide in the small font (`LBL_W = 57 - 7`); longer labels break at a space into two lines (value on the third line, no gauge); a single word that is too long is cut with "...". A value is drawn in the medium font if it fits 49 px, else in the small font, else cut.

| Text | Small font px | Result |
|---|---|---|
| TYPE 28, LINK 25, OFFSET 42, CUTOFF 45, TUNE 31, RES 21, FDBK 29, ROUTE 38, DRIVE 32, BLEND 37 | all under 50 | one line, with gauge where it has one |
| FEEDBACK | 58 | **does not fit** as one word, would be cut; FDBK is already an accepted short form (DELAY 1/2 uses it) |
| RESONANCE | 69 | does not fit; RES is the unit's word (editor: RESONANCE) |
| KEY TRACK | 61 | two lines KEY / TRACK, as today |
| FILT ENV 48, LFO 1 31, LFO 2 31 | under 50 | one line, as today |
| CUTOFF 2 | 55 | would break into "CUTOFF" and "2": **not used as a card label** (it is a matrix destination, section 5) |
| values: SERIES 47, PARAL 46, LADDER 56, NOTCH 51, COMB 43 in the medium font | PARAL, SERIES, COMB fit medium; LADDER and NOTCH drop to the small font (44 and 41 px), as long engine names do | fine |
| value PARALLEL | 70 medium, 55 small | **does not fit even small**; use PARAL |
| values +0.0 st (47 medium), -83.2 st (53 medium, 41 small) | medium or small | fit |
| values F1 ONLY 47, F2 ONLY 47, BOTH 31 | small font | fit |
| page titles (header, medium font, about 150 px of room): TWIN FILTER 87, TWIN MOD 74, TWIN CHAIN 85, MULTI FILTER 1/2 119 | all fit | fine |

I did not run the unit's overflow test (nothing is built); the builder must run it on the new cards.

---

## 3. The same section on the web editor

**Today (CONFIRMED `app.js buildPartTab`):** there is no FILTER tab. The ENGINE tab holds: the engine card, (on ANALOG, TRIO, NOISE the CUTOFF MOD card), VOICE, MULTI FILTER (CUTOFF, RESONANCE, MODE, SLOPE), CUTOFF MOD (FILTER ENV, KEY TRACK, LFO 1, LFO 2).

**Proposal: a FILTER tab between ENGINE and MOD** (your STATUS note "the FILTER gets its own tab"). Reason: the editor groups by function, not by the unit's pages; the ENGINE tab at 1280 x 720 is already at 380 of 394 px (EDITOR_REWORK_LIST), and filter 2 adds about 3 rows. The tab strip must stay on one line (rule R5): ENGINE, FILTER, MOD, MATRIX, FX, ARP / SEQ, SEQUENCER (and OPERATORS on FM6). The builder must check the strip at 1280 wide with OPERATORS showing.

```
[ENGINE] [FILTER] MOD  MATRIX  FX  ARP/SEQ  SEQUENCER   (OPERATORS only on FM6)

.-MULTI FILTER-.--------------------------------------(r)
| CUTOFF   RESONANCE   MODE   SLOPE
.-CUTOFF MOD-.----------------------------------------(r)
| FILTER ENV   KEY TRACK   LFO 1   LFO 2
.-TWIN FILTER-.-----------------------------------[?]--(r)
| TYPE [LP v]   LINK [ON]   OFFSET   RESONANCE
.-TWIN MOD-.--------------------------------------[?]--(r)
| FILTER ENV   KEY TRACK   LFO 1   LFO 2
.-TWIN CHAIN-.-------------------------------------[?]-(r)
| ROUTE [SERIES v]   DRIVE   BLEND        (small flow line: ENGINE > FILTER 1 > DRIVE > FILTER 2 > AMP)
```
(r) is the reset button, [?] the tooltip dot of rule R4.

Rules, consistent with the unit and with EDITOR_NAMING_PROPOSAL.md:
- **Order:** top to bottom = the sound's flow and the unit's page order. Inside a card the unit's order, except CUTOFF MOD, whose FILTER ENV first order is your editor choice (T0229); TWIN MOD copies it so one rule holds for both filters.
- **Names (R2, R6):** the editor writes RESONANCE where the unit has RES (as today for filter 1), FEEDBACK where the unit has FDBK, FILTER ENV where the unit has FILT ENV. OFFSET, CUTOFF, TUNE, LINK, ROUTE, DRIVE, BLEND are the same word in both. Titles carry the context (R1): the controls do not repeat TWIN or FILTER.
- **Hidden means not drawn (R4), with the reason in the [?] tooltip:**
  - TYPE OFF: the TWIN FILTER card shows TYPE only; TWIN MOD and TWIN CHAIN are not drawn.
  - COMB: LINK and KEY TRACK are not drawn; OFFSET/CUTOFF reads TUNE; RESONANCE reads FEEDBACK. COMB is not in the TYPE list on WAVE and PHYS.
  - LINK: changes the label OFFSET / CUTOFF, hides nothing.
- **Controls:** TYPE and ROUTE are drop-downs; LINK is a switch where ON means linked (honest, unlike a mute that shows ON); OFFSET, BLEND, KEY TRACK, FILTER ENV, LFO 1, LFO 2 are sliders marked at the centre because they are true bipolar. DRIVE shows OFF at 0.
- **Flow line** in TWIN CHAIN: a small line drawing inside the card (not a deliverable picture), changing for SERIES / PARALLEL, with the DRIVE position drawn before both filters in PARALLEL. Plain text in the editor is enough if you prefer.
- **Size:** FILTER tab with filter 2 on is 5 cards of 3 to 4 controls, about 340 px (estimate, 5 rows of about 68 px); it fits 394 at 720. The ENGINE tab gets lighter (engine card and VOICE only).
- **MATRIX tab:** the new destinations in the FILTER group (section 5).
- **Not mirrored:** the unit's page splits (TWIN FILTER, TWIN MOD, TWIN CHAIN) are three cards here only because each card is one idea; the editor does not need to copy page numbers.

---

## 4. ANALOG, TRIO and NOISE: before and after (design 2, 3.7; CONFIRMED against `params.c` and the three engine tables)

Card names are as the unit shows them. "uiord" is the engine's card-order list; no stored id moves. Page counts are for filter 2 OFF.

**ANALOG** (before: 8 pages; after: 7)

| Page | Before | After |
|---|---|---|
| ENGINE | ENGINE, WAVE, DETUNE, MIX | **same** |
| EDIT 1 (titled ANALOG) | NOISE, CUTOFF, RES, DRIVE | NOISE, DRIVE (K1, K2; K3, K4 empty) |
| EDIT 2 | KEY TRACK | **gone** (no eighth control) |
| ANALOG FILTER | TYPE, CUTOFF, RES, KEY TRACK | **gone** (its character lives on as filter 1, MODE / SLOPE) |
| MULTI FILTER 2/2 | KEY TRACK only (the three amount cards hidden on this engine) | all four cards, as on the other seven |
| Everything else | | unchanged |

DRIVE stays: it drives the oscillator mix before any filter, and its meaning does not change.

**TRIO** (before: 8 pages; after: 7)

| Page | Before | After |
|---|---|---|
| ENGINE | ENGINE, WAVE, INT2, INT3 | **same** |
| EDIT 1 | DETUNE, TYPE, CUTOFF, RES | DETUNE, PW |
| EDIT 2 | PW | **gone** |
| TRIO FILTER | TYPE, CUTOFF, RES | **gone** (becomes filter 2's LP, BP, HP, NOTCH, "TRIO-style") |
| MULTI FILTER 2/2 | KEY TRACK only | all four cards |

**NOISE** (before: 8 pages; after: 7)

| Page | Before | After |
|---|---|---|
| ENGINE | ENGINE, MODE, COLOR, FREQ | ENGINE, MODE, COLOR, DENSITY |
| EDIT 1 | RES, KEY TRACK, DENSITY, DRIFT | DRIFT, CRUSH |
| EDIT 2 | CRUSH | **gone** |
| NOISE FILTER | FREQ, RES | **gone** (becomes filter 1) |
| MULTI FILTER 2/2 | KEY TRACK only | all four cards |

Common to the three:
- The "where the filter goes" hide rule for the amount cards (`card_hidden_vp`, the `ufilt != 1` test) is deleted: filter 1's four cards always show.
- Their matrix destinations for CUT, RES, KEY TRACK, MODE, FREQ, TRK disappear from the ENGINE group of the list; routes convert by design 2's table.
- Their quick knobs (`.knob`) that pointed at CUT and RES must be re-pointed (they become filter 1's, as on the other seven).
- **Editor:** card "OSCILLATORS AND FILTER" becomes OSCILLATORS on ANALOG (WAVEFORM, DETUNE, MIX, NOISE, DRIVE) and on TRIO (WAVEFORM, INTERVAL 2, INTERVAL 3, DETUNE, PULSE WIDTH); NOISE SOURCE keeps its name (MODE, COLOR, DENSITY, DRIFT, CRUSH). The ENGINE_BY entry that renamed TRIO's MODE to FILTER MODE goes. CUTOFF MOD no longer moves under the engine card on these three; it is the same everywhere (on the FILTER tab).

---

## 5. Matrix destinations and the hide-inert table

### 5.1 The FILTER group in the editor's grouped naming (MATRIX_LIST_PREVIEW.md)

| Name in the editor | Unit card shows | Status | Notes |
|---|---|---|---|
| CUTOFF | CUT (full line: MULTI FILTER CUTOFF) | kept | filter 1; **with LINK on it also moves filter 2**. The unit's second line (`more`) says "ALSO FILTER 2 WHILE LINKED" |
| FILTER RES | FILTER / RES | kept | filter 1 |
| FILTER MODE | FMODE (full line: MULTI FILTER MODE) | kept | filter 1 |
| FILTER ENV AMOUNT | E2FLT | kept | filter 1; filter 2 follows through LINK |
| **CUTOFF 2** | CUT 2 | **new, decided** | adds to filter 2's own cutoff sum (design 2, 3.1); reads **TUNE** on COMB |
| **FILTER 2 RES** | FILTER 2 / RES | new, proposed | reads **FEEDBACK** on COMB |
| **FILTER DRIVE** | FILTER / DRIVE | new, proposed | per block, gliding; VEL here gives "harder notes distort more" |
| **FILTER BLEND** | FILTER / BLEND | new, proposed | per block, gliding |

- "CUT 2" is 42 px in the medium font (fits 49). The other three use the two-line form already used for FILTER RES and the AMP ENV times; "FILTER 2" is 46 px (fits 50).
- Full DEST lines on the unit: FILTER 2 CUTOFF, FILTER 2 RESONANCE, FILTER DRIVE, FILTER BLEND (under the 32-character limit).
- Stored positions: appended as 51 to 54 (the list is append-only, 51 used of 128). Internal factory-sound names, my suggestion: CUT2, FRES2, FDRV, FBLND.
- **Not destinations:** TYPE, ROUTE, LINK (hard switches, same reason the list-type engine slots are excluded), and the four amounts (a route to CUTOFF 2 does the same job).
- **Offered only while TYPE is not OFF** (like INSERT A/B/C with INSERT OFF); a route already stored keeps playing. On COMB the names follow the type (TUNE, FEEDBACK), as INSERT A/B/C and the engine slots are named by their owner today.
- **Wording question left alone:** FILTER RES, FILTER MODE, FILTER ENV AMOUNT and CUTOFF now mean "filter 1" while a second filter exists. I did not rename them (you set those names a day ago). If you want a 1 in them, say so.
- **Removed from the ENGINE group:** the engine slots that held CUT, RES, KEY TRACK, MODE, FREQ, TRK on ANALOG, TRIO, NOISE (section 4).
- **Sources:** none added or renamed.

### 5.2 What hides, per filter-2 type (nothing hides for being at 0; only a selection hides)

Rows are cards; "shown" cards move left when others hide.

| Card | OFF | LP / BP / HP / NOTCH | LADDER | COMB |
|---|---|---|---|---|
| TYPE (page 3) | shown | shown | shown | shown |
| LINK | hidden | shown | shown | **hidden** (the comb follows the key itself) |
| OFFSET / CUTOFF / TUNE | hidden | shown | shown | shown as **TUNE** |
| RES / FDBK | hidden | shown (RES) | shown (RES; can sing) | shown as **FDBK**, -64..+63 |
| slope | none | none: fixed 12 dB, TRIO-style | none: fixed 24 dB | none |
| page 4 TWIN MOD | page skipped | all 4 cards | all 4 cards | KEY TRACK **hidden**; FILT ENV, LFO 1, LFO 2 move TUNE |
| page 5 TWIN CHAIN | page skipped | ROUTE, DRIVE, BLEND | same | same |
| matrix: CUTOFF 2 / FILTER 2 RES / FILTER DRIVE / FILTER BLEND | not offered | offered | offered | offered (named TUNE / FEEDBACK) |
| in the TYPE list | | | | not on WAVE and PHYS |

Filter 1: **nothing ever hides.** MODE, SLOPE, RES keep showing at the open end (your rule: FILTER RES is never dimmed or hidden by the cutoff). SLOPE is filter 1's only; filter 2 has no slope card in any type.

Details I leave to the builder, flagged so nobody is surprised:
- RES and FEEDBACK share one stored value with two ranges (0..127 and -64..+63). On switching to or from COMB I suggest the card resets to the new kind's default, so a RES of 0 never becomes the strongest negative feedback.
- A patch whose TYPE is COMB loaded onto WAVE or PHYS (or an engine change to them): needs a defined fallback (suggestion: LP with the same cutoff).
- The OFFSET starts at 0 with filter 1 open (the default), so a freshly chosen LP is wide open and can only be closed by turning OFFSET down or filter 1's CUTOFF. That is how the Virus behaves; the graph shows it.

---

## 6. Questions for you (only those that change the layout; none about builds or merges)

1. **Which page set: A, B or C?** B puts LINK beside the cutoff and DRIVE on the chain page; A is design 2 as written; C adds a mirror page. **Recommendation: B.**
2. **DRIVE while filter 2 is OFF:** hidden and not run (the drive exists to feed filter 2; ANALOG keeps its own oscillator DRIVE, and the FX page has the part's DRIVE), or does it still work after filter 1, which would keep TWIN CHAIN visible with DRIVE alone? **Recommendation: hidden, not run.**
3. **Editor: a new FILTER tab** (filter 1, filter 2, their mod cards, the chain card) or keep everything in the ENGINE tab? **Recommendation: new FILTER tab** (ENGINE tab is at its limit at 720 px, and your own earlier note asked for it).
4. **Page names:** TWIN FILTER, TWIN MOD, TWIN CHAIN (no numbers; CHAIN instead of design 2's ROUTE) or numbered titles like MULTI FILTER 1/2? **Recommendation: the three names.** Numbers cannot be right when two of the three pages vanish with TYPE OFF.
5. **Hold layers:** leave QUICK MULTI exactly as it is (CUT, RES, MODE, AMP REL; filter 2 comes along through LINK), or give filter 2 a card in it (for example OFFSET)? **Recommendation: leave it.** It is your performance layer, the order is your habit, and a change there rearranges a neighbour.

Note, not a question: items 7 and 8 at the top (the CUTOFF knob that moves two filters; six values without ids) are real costs of the decided design. If the missing MIDI LEARN and locks on the six cards bother you, the format would need ids for all 11, which is a build question for later, not a layout one.
