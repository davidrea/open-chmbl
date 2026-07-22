# LED driver eval board datasheets

The PDF files in this directory are intentionally ignored by Git. Download the
English-language datasheets from the manufacturer links below when needed.

| Designator | Manufacturer | Part number / document | Manufacturer source |
| --- | --- | --- | --- |
| C2 | Taiyo Yuden | LMK107B7105KA-T (1µF, 0603) | [Datasheet (PDF), via LCSC](https://www.lcsc.com/product-detail/multilayer-ceramic-capacitors-mlcc-smd-smt_taiyo-yuden-lmk107b7105ka-t_C92806.html) |
| C4 | YAGEO | CC0603KRX7R9BB224 (0.22µF, 0603) | [Datasheet (PDF), via LCSC](https://www.lcsc.com/product-detail/Multilayer-Ceramic-Capacitors-MLCC-SMD-SMT_YAGEO_C107083.html) |
| L1 | Sunlord | SWPA5020S220MT (22µH, 1.1A) | [SWPA series datasheet (PDF)](https://www.sunlordinc.com/uploads/files/20221122/SWPA%20series%20of%20SMD%20Power%20Inductor.pdf) |
| R4 | YAGEO | PE1206FRF470R56L (560 mΩ, 1%, current-sense) | [Datasheet (PDF), via LCSC](https://www.lcsc.com/product-detail/C5844413.html) |
| U3 | Diodes Incorporated | AP3019AKTR | [Datasheet (PDF)](https://www.diodes.com/assets/Datasheets/AP3019A.pdf) |

`U3` and its surrounding components (`C2`, `C4`, `L1`, `R4`) implement the AP3019A
buck-boost LED driver reference design — see [`led_drv_eval.kicad_prl.kicad_sch`](../../led_drv_eval.kicad_prl.kicad_sch).

`TP1`–`TP6` (test points) are intentionally DNI and omitted here — they're bare
probe pads, not sourced parts.
