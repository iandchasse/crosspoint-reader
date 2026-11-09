# Experimental 7.5-inch CrossPoint build

`silkscreen-750-experimental` targets the **GDEY075T7 family**, 800×480, UC8179,
on the usual Silkscreen J2 connector and S3 N16R8 board. The model is inferred
from the owner's panel description; check the label when testing. It is not a
generic UC8179 build for every panel using that controller.

Build with `scripts/silkscreen/pio_locked.sh silkscreen-750 pio run -e silkscreen-750-experimental`.
The image and full flash files go in `flash/silkscreen-750-experimental/` after
`python -I -X utf8 scripts/silkscreen/freeze_images.py silkscreen-750-experimental`.
The firmware tag is `silkscreen-750`, so SD/OTA updates reject the
regular 4.26-inch and 3.68-inch images in either direction. Use the full USB flash
bundle for the initial switch of panel family. Disconnect cell and USB before
swapping panels, as the panel supply remains live during deep sleep.

The SDK derives the profile from Silkscreen: keys, battery estimator/cutoff,
RTC, SDMMC, pin parking, radio limits and sleep policy remain the board's.
The controller is fixed, with no probe or retained probe verdict. SPI is 10 MHz
and BUSY is active-low. This environment compiles out touch and frontlight;
use keys. It retains the existing 800×480 layout and font settings.

The panel path adapts `Gdey075t7Driver` from
[Free-Ink PR #80](https://github.com/Free-Ink/freeink-sdk/pull/80), pinned at
`5941b44f8e3555ad2798627f61c72562dcc9c789`. Only its panel driver is imported;
the PR's reTerminal input, battery and global grayscale changes are excluded.
The small adaptations use the current bus/capability API, keep reset/wake
resynchronization and honor power-off after legacy grayscale-mask calls.

The driver uses native 800×480 addressing and factory black-and-white OTP
waveforms. Full and Half both use the factory fast-full selector (`0x5A`);
Fast uses factory partial (`0x6E`) after the first completed paint. Reset/wake
starts with fast-full. Full writes the same image to both controller planes;
partial uses the supplied previous frame when present, otherwise retained
controller RAM. The PR enables the controller's NEW-to-OLD copy. Plane bytes
are sent in the PR's native order, without the earlier prototype's Y flip.
The X4/X4 Pro UC8179 driver is retained separately: its 600-row scan and custom
grayscale waveforms are not used. Grayscale is unavailable in this first build.
Refresh calls block until completion, matching the PR. This gives up CPU work
overlapping a refresh but requires no async shadow buffer. There is no new
driver allocation or task; its framebuffer remains 48,000 bytes.

SDK source changes are mirrored in both SDK checkouts. Only CrossPoint has an
experimental build target; the regular CrossInk and CrossPoint targets keep
their previous controller selection.

Panel references: [Good Display GDEY075T7](https://www.good-display.com/blank7.html?productId=396)
and [GxEPD2's GDEY075T7 implementation](https://github.com/ZinggJM/GxEPD2/blob/master/src/gdey/GxEPD2_750_GDEY075T7.cpp).
No GxEPD2 dependency is added.

## Device check

This is an unverified panel build. After flashing, confirm a complete first
paint, then page through alternating light/dark content. Full refresh should
clear old content; subsequent Fast updates should erase previous text rather
than accumulate it. Check all four orientations, key navigation, sleep/wake,
RTC timekeeping and USB/battery status. Wake is expected to use fast-full.
In particular, check the top/bottom scan orientation and that several
successive partial updates erase old text. Controller RAM copying, waveform
compatibility and refresh appearance have not been verified on this panel.
Stop the test if BUSY times out, the image is garbled or refresh never completes;
capture the serial log and exact panel label. Runtime heap, refresh appearance,
and sleep current require hardware measurements.
