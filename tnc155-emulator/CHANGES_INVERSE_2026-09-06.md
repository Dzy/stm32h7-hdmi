# 2026-09-06 inverse-video correction

Baseline: `tnc155-emulator-codex-baseline-fixedkeys-rom5-2026-09-06`.

Changes in this package:

- Reverted the rejected monoflop change that initialized the CLP monoflop at
  power-on.  CLP again starts untriggered and is triggered only by the observed
  CRU >4060 high strobe, as in the Codex source baseline.
- Reverted the rejected 3 MHz / 23 ms / 76 ms timing rewrite.  The package
  again uses the baseline 12 MHz cycle constants and 5 ms / 20 ms constants.
- Implemented character reverse video from `mode_data` D0.
- Evidence: at startup P2's character buffer >A800 contains the complete
  `MUISTIN TESTAUS` row as ASCII bytes with `mode_data = >09` for every cell,
  including trailing spaces.  `>09` and `>08` select the same P1 mode because
  P1 only sees D4..D2; therefore D0 is a post-font video attribute.  Applying
  D0 as full-cell inversion produces the expected bright row with dark text.
- `mode_data` D1 remains intentionally uninterpreted.
- Added a regression test proving that >09 is the complete-pixel inverse of
  >08 over a 16x32 character cell.
