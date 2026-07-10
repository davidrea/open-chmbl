# Data logger PCB datasheets

The PDF files in this directory are intentionally ignored by Git. Download the
English-language datasheets from the manufacturer links below when needed.

| Designator | Manufacturer | Part number / document | Manufacturer source |
| --- | --- | --- | --- |
| D1 | Nexperia | PTVS3V3D1BALYL | [Datasheet (PDF)](https://assets.nexperia.com/documents/data-sheet/PTVS3V3D1BAL.pdf) |
| D2 | Nexperia | PTVS3V3D1BALYL | [Datasheet (PDF)](https://assets.nexperia.com/documents/data-sheet/PTVS3V3D1BAL.pdf) |
| D3 | Littelfuse | SM24CANB-02HTG | [Datasheet (PDF)](https://www.littelfuse.com/assetdocs/littelfuse-tvs-diode-array-sm24canb-datasheet?assetguid=09dc84a6-1ff8-409f-a80f-5e92aedec8fe) |
| D4 | SMC | 20CJQ060 | [Datasheet (PDF)](https://www.smc-diodes.com/propdf/20CJQ060%20N0672%20REV.A.pdf) |
| J2 | G-Switch | GT-USB-7010ASV | Manufacturer-hosted document was not publicly available; [authorized distributor listing](https://jlcpcb.com/partdetail/gswitch-GT_USB7010ASV/C2988369) |
| J3 | JST | S5B-PH-SM4-TB(LF)(SN) | [PH connector datasheet (PDF)](https://www.jst-mfg.com/product/pdf/eng/ePH.pdf) |
| J4 | JST | S4B-PH-SM4-TB(LF)(SN) | [PH connector datasheet (PDF)](https://www.jst-mfg.com/product/pdf/eng/ePH.pdf) |
| J5 | Molex | 104031-0811 | [Product specification (PDF)](https://www.molex.com/content/dam/molex/molex-dot-com/products/automated/en-us/productspecificationpdf/104/104031/PS-104031-001-001.pdf?inline=) |
| L1 | Murata | LQH5BPN2R2NT0L | [Manufacturer product page](https://www.murata.com/en-us/products/productdetail?partno=LQH5BPN2R2NT0L) |
| Q1 | onsemi | 2N7002K | [Datasheet (PDF)](https://www.onsemi.com/pdf/datasheet/2n7002k-d.pdf) |
| U1 | Espressif | ESP32-S3-WROOM-1-N8 module | [Module datasheet (PDF)](https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf) |
| U1 | Espressif | ESP32-S3 chip | [Chip datasheet (PDF)](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf) |
| U1 | Espressif | ESP32-S3 technical reference manual | [TRM (PDF)](https://documentation.espressif.com/esp32-s3_technical_reference_manual_en.pdf) |
| U2 | Texas Instruments | TCAN330 | [Datasheet (PDF)](https://www.ti.com/lit/ds/symlink/tcan330.pdf) |
| U3 | STMicroelectronics | USBLC6-2P6 | [Datasheet (PDF)](https://www.st.com/resource/en/datasheet/usblc6-2.pdf) |
| U4 | Texas Instruments | TPS62172DSG | [TPS62170-family datasheet (PDF)](https://www.ti.com/lit/ds/symlink/tps62170.pdf) |
| SW1, SW2 | Mitsumi | R-668003 tactile switch | [Authorized distributor product and datasheet](https://www.digikey.com/en/products/detail/mitsumi/R-668003/11591279) |

Resistors and capacitors are deliberately omitted. SW1 and SW2 are intentionally
DNI on prototype builds: their 1210 pads reserve a recovery option if a board
cannot be programmed through the ESP32-S3 USB serial port.
