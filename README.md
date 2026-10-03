# LineageOS 23.2 for the Galaxy Tab A 10.1 (2016) Wi-Fi (SM-T580)

An unofficial LineageOS 23.2 port for the Samsung SM-T580 (`gtaxlwifi`,
Exynos 7870), on a mainline Linux kernel (7.2) with a series of patches
for this board.

<p align="center">
  <img src="docs/images/home.png" alt="Home screen" height="420">
  <img src="docs/images/about.png" alt="Settings, About tablet" height="420">
</p>

- **Install**: [`docs/INSTALL.md`](docs/INSTALL.md)
- **Build**: [`docs/SETUP.md`](docs/SETUP.md)

One branch per LineageOS release, as in LineageOS's own repositories:
`lineage-23.2` builds with a `lineage-23.2` tree.

## What works

Display, touchscreen and the capacitive keys; GPU (Panfrost); Wi-Fi,
hotspot and Wi-Fi Direct; Bluetooth; GPS; audio (speaker, headphones,
Bluetooth) and the internal microphone; both cameras, photos and video;
hardware video decoding and encoding (H.264, HEVC, VP8, MPEG-4, H.263);
accelerometer, light sensor and the magnetic cover; microSD, also as
adoptable storage; USB (MTP, adb, tethering); charging, also with the
tablet off; file-based encryption; SELinux enforcing; updates through
LineageOS Recovery.

Not working: the microphone of a wired headset.

## Layout

| path | what |
|---|---|
| `android/gtaxlwifi/` | the device tree, `device/samsung/gtaxlwifi` in the LineageOS tree |
| `android/gtaxlwifi/.patches/` | changes to other LineageOS and AOSP projects, applied as SETUP.md says |
| `android/gtaxlwifi/bootloader/loader/` | the small loader Samsung's bootloader starts: it unpacks the kernel, completes the device tree and boots Linux |
| `kernel/patches-7.2.8/` | the kernel series, for `git am` on Linux v7.2.8 |
| `docs/` | how to build and how to install |

## Licensing

The kernel patches and the loader are GPL-2.0. The rest of the device
tree is Apache-2.0 unless a file says otherwise. Proprietary files are
not in this repository: `extract-files.py` fetches them, see SETUP.md.
