# Emergency-stop startup/watchdog correction — 2026-09-06

The previous F1 emergency-stop package had two independent watchdog-model bugs
that could leave the emulated TNC permanently in HATA-SEIS:

1. MAIN's emergency-stop monoflop was artificially armed at power-on.  It
   expired before firmware had emitted a real retrigger pulse, permanently
   opening J1/8 before F1 could raise E8/J5/8.
2. CLP's switch-off time was modeled as 5 ms, but the service manual specifies
   11..26 ms.  The ROM-visible CLP retrigger cadence is about 6 ms, so the 5 ms
   model made the healthy watchdog expire between normal refresh pulses.

Corrections:

- MAIN and CLP monoflops are unarmed until their first observed firmware CRU
  retrigger pulse; an unarmed monoflop does not block the healthy-closed J1/8
  contact.
- CLP switch-off is modeled at the documented earliest limit, 11 ms.
- MAIN switch-off is modeled at the documented earliest limit, 23 ms
  (manual range 23..76 ms).
- The permanent +24 V / F1 / J1/8 / E8 wiring model is otherwise unchanged.
- Regression tests verify that waiting at power-on cannot open J1/8, that a
  normal ~6 ms CLP refresh interval remains healthy, and that an armed expired
  monoflop opens J1/8 and removes E8.
