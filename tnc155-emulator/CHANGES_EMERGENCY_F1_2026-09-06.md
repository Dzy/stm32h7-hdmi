# 2026-09-06 F1 / emergency-stop control-voltage wiring

Baseline: user-designated de facto tree `tnc155-emulator-codex-work`.

The machine-side +24 V / emergency-stop wiring now follows the TNC 151/155
interface description more literally:

- the external +24 V source is continuously present;
- F1 latches the external machine control-voltage return/relay path on/off;
- the TNC emergency-stop output at J1/8 is modeled as a healthy-closed contact
  in series with the +24 V feed;
- the TNC can open J1/8 during its CLP/MAIN monoflop self-test without changing
  either the upstream +24 V source or the F1 latch;
- E8 at J5/8 is the downstream feedback and is therefore high only when the
  source, TNC emergency-stop contact and external machine return/relay path are
  all closed/healthy;
- the diagnostic overlay moved from F1 to F10.

No direct PLC-RAM shortcut was introduced.  E8 remains a physical backplane
input read by the original MAIN firmware through the CRU input scan.
