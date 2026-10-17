# Z1 driver test batches — 2026-07-17

## KPD capture (after the next safe boot)

`mt6779-keypad` emits `EV_MSC/MSC_SCAN`, then `EV_KEY`, then `EV_SYN/SYN_REPORT`. MT6572 single-keypad hardware slots are decoded as `row=slot/9`, `col=slot%9`.

The ARM32 `struct input_event` is 16 bytes: two 32-bit timeval words, two 16-bit fields, one signed 32-bit value. BusyBox-only raw decoder:

```sh
hexdump -v -e '2/4 "%u " 2/2 "%u " 1/4 "%d\n"' /dev/input/event0
```

Interpret `(type,code)` as: `(4,4)=MSC_SCAN`, `(1,keycode)=EV_KEY`, `(0,0)=SYN_REPORT`. Record press and release for each physical key; do not change DTS for unknown slots until captured.

## GPU/display gates

1. **simplefb v2**: require successful registration, `/dev/fb0`, 240x320, stride 512 and a reversible small framebuffer pattern.
2. **Lima-only**: rebuild from the simplefb-passed baseline. Verify Mali-400MP1 MMIO, IRQs 114-117, MFG clocks/reset/power-domain and devfreq. No native display changes and no 3D success claim without userspace tests.
3. **MMSYS/SMI read-only**: validate clocks, DIS power domain, SMI/IOMMU probe ordering and register accessibility; do not replace LK scanout.
4. **OVL/RDMA**: add MT6572 DDP data and one stage at a time, retaining a rollback image and UART capture.
5. **DSI/ST7785M**: only after OVL/RDMA, add the 240x320 video-mode graph and exact vendor panel init. Backlight/panel power remain separately reversible.

## Other isolated batches

- MT6323 MFD/IRQ/regulators before poweroff or CONNSYS rails.
- I2C controller only, with all speculative touch children disabled.
- AUXADC, IOMMU/SMI and power-domain tests as separate images.
- Never combine first-time CONNSYS, native display, Lima, poweroff or touch changes in one image.
