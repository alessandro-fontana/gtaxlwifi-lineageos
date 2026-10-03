# Boot chain of gtaxlwifi

S-BOOT, Samsung's signed bootloader, stays on the tablet: it starts BOOT (p9)
or RECOVERY (p10) as Samsung boot images. It ignores the command line in the
image, refuses ramdisks over 14 MiB and expects a DT table next to the
kernel. So the "kernel" of both images is `loader/`, a small loader built by
`bootimg/mkbootimg.mk` with the kernel's clang, followed by a payload: the
real kernel (LZ4), the DTB, the ramdisk and the command line (docs §73).

The loader, before starting Linux:

- sets INFORM3 back to "normal boot" (S-BOOT remembers the last mode);
- takes `androidboot.serialno` from S-BOOT's command line;
- adds `androidboot.mode=charger` when the cable switched the tablet on
  (cold start and PMIC power-on source, read over HSI2C);
- reads the Wi-Fi and Bluetooth addresses of the tablet from Samsung's EFS,
  read-only, and puts them in the DTB;
- passes an entropy seed (the one Android keeps in the first sector of `OTA`,
  plus timing jitter, hashed);
- goes to the recovery when the partition table has no `super`;
- leaves its timings in INFORM4 and any error in `/chosen/gtaxl,loader-log`.

| file | what |
|---|---|
| `loader/` | the loader: entry, MMU, LZ4, PMIC, eMMC (IDMAC), ext4, SHA-256, libfdt from `external/dtc` |
| `stub-dt.img` | the DT table S-BOOT requires next to the kernel, from `stub.dts` |

Until §73 this place was held by U-Boot (upstream `v2026.01` with the patches
in `uboot/` of the gtaxlwifi repository).
