<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Key recording format (VELO_CSV)

The raw logger on the unit and `felucca-base/tools/velo_replay.py` use one CSV, one row per converter reading of a key:

```
ts_us,key,adc,drive
```

- `ts_us`: the time of the reading in microseconds (unsigned 32 bit, wraps after 71 minutes; only differences are used).
- `key`: the note key 0 to 26 (F3 = 0 ... G5 = 26). Keys 20 to 26 have no converter and are ignored.
- `adc`: the converter value, 0 to 1023.
- `drive`: 1 for a reading taken with the key's row driven, 0 for the row pulled up.

A header line, blank lines and lines starting with `#` are skipped. Rows must be in time order. Leave the readings in: a key that is open reads about 1023 and that is how a release is seen. Skipping stretches is fine as long as a few open readings of the key come before a contact and after a release (a gap of more than 7 ms between two readings of a key counts as a gap, and a landing straight after a gap has an unknown strength).

`velo_replay.py RECORDING.csv` runs the firmware's own sense core (`firmware/src/sense.c`) over it from a cold start (the typical key, nothing stored) and prints, per key and overall, the number of landings, the velocity quartiles, and the share at 110 or more and below 10. `--landings OUT.csv` also writes every landing (`ts_us,key,velocity,from_driven`). `--curve PCT` sets the MENU curve (0 = flat 100, 100 = the measured range).

`tests/velo_test.c --csv FILE [REF]` writes such a recording from the simulated hand (and the landings the core made of it); `host_tests.sh velo` replays it and requires the same landings.

## The capture build (DUELL_VELO_CAPTURE, developer builds only)

The unit sends these lines over the USB serial console after the console word `velocap on` (`velocap off` stops; it is OFF at power-on). Every line starts with `VC,` (tools/bench_run.py ignores them): `VC,V,ts_us,key,adc,drive` a reading (the four numbers above), `VC,N,ts_us,key,velocity,floor,ceiling` a note-on with the key's range at that moment, `VC,R,key,floor,ceiling` the learned ranges (all keys when it is switched on, then every 5 s), `VC,dropped N` rows lost because the queue was full, `VC,S,sent,dropped,peak,notes_lost` every 5 s. `tools/velo_capture.py` turns them into this CSV: readings as rows, the rest as `#` comment lines (`# note,...`, `# range,...`, `# segment,KEY,keynumber,hard|normal|soft,ts` for the guided session, `# dropped,N`). `tools/velo_fit.py` reads that file.

## The first recording from the owner's unit

`felucca-base/tests/data/velo_owner_20261026.csv` (a copy of `velo_captures/velo_20261026_091617.csv`, 105 presses on 10 keys, guided hard / normal / soft x 5 per key) is the regression recording of the key velocity: `tests/velo_owner_test.c` (host_tests.sh velo) replays it through `sense.c` on a fresh unit and holds the result to hard > normal > soft on every key. Its readings (first driven reading after the landing, lower = harder): hard 679 to 919 (middle 701), normal 770 to 1009 (middle 892), soft 937 to 1019 (middle 1004); the converter reads 1023 for an open key, so 900 to 1022 with the row driven is a pill, not a release.
