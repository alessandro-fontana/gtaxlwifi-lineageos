# Installing LineageOS 23.2 on the Galaxy Tab A 10.1 (2016) Wi-Fi

For the **SM-T580** (`gtaxlwifi`) only, not the LTE model (SM-T585).

> **Installing erases everything on the tablet.** Copy what you want to keep
> first. Download mode (**Volume Down + Home + Power**, then **Volume Up**) is
> never touched: from there the stock firmware can always be restored.

## What you need

- battery above 50%, a good USB cable (data, not charge-only);
- the bootloader of the last stock firmware, **T580XXS5CTK1** (Android
  8.1): it is the only version this build is tested with. On stock,
  Settings → About tablet → Software information → Build number ends
  with it. If it is older, or you are not sure which stock firmware was
  flashed last before a custom ROM, flash T580XXS5CTK1 first (samloader-rs
  downloads and flashes Samsung firmware, see its README; or Odin);
- the release files:
  - `gtaxlwifi-boot.img`
  - `gtaxlwifi-recovery.img` (LineageOS Recovery)
  - `gtaxlwifi-repartition.zip` (once, before the first install)
  - `gtaxlwifi-los23-install.zip`
  - `gtaxlwifi-revert-to-factory.zip` (only to go back)
  - `gtaxlwifi-odin-AP.tar.md5` (only with Odin: the first two in one file)
- **samloader-rs**, the tool the LineageOS wiki uses for Samsung devices:
  the zip for your system from
  <https://github.com/topjohnwu/samloader-rs/releases/latest> (tested
  with 2.2.0). Heimdall works too.
- optional, for the Google apps: **MindTheGapps for Android 16, arm64**
  (`MindTheGapps-16.0.0-arm64-<date>.zip`), from the link on the LineageOS
  wiki page *Google apps*. Only the packages listed there are supported.

## Prepare your computer

### Linux

1. Install adb from your distribution, for example on Debian or Ubuntu:

   ```sh
   sudo apt install adb
   ```

   (Fedora: `sudo dnf install android-tools`; Arch:
   `sudo pacman -S android-tools`.)
2. Unzip samloader-rs, for example into the folder with the release files,
   and run `./samloader fix-usb` once: it adds the rules that let your user
   reach the tablet in download mode.
3. Let your user reach the tablet over USB for adb too. Most distributions
   ship the rules with the packages above (`android-udev-rules`); if
   `adb devices` later says *no permissions*, install that package and
   unplug and plug the cable again.

### Windows

1. **adb**: download Google's *SDK Platform-Tools for Windows* and unzip
   them, for example to `C:\platform-tools`. Open PowerShell in that folder
   (in Explorer: **Shift + right click → Open PowerShell window here**) and
   write the commands of this guide as `.\adb ...` instead of `adb ...`.
   Copy the release files into the same folder.
2. **Samsung USB driver**: install *Samsung Android USB Driver for Windows*
   from Samsung's developer site, then restart the computer.
3. **samloader-rs** for Windows, unzipped into the same folder (not tested
   on Windows yet), or **Odin** (Samsung's flashing tool, version 3.13 or
   later).

## Install

1. On the tablet: Settings → About → tap *Build number* seven times, then
   Settings → Developer options → **OEM unlocking** on.
2. Power off, then **Volume Down + Home + Power**, then **Volume Up**
   (download mode).
3. Flash the boot image and the recovery:

   ```sh
   samloader flash --partition BOOT gtaxlwifi-boot.img --partition RECOVERY gtaxlwifi-recovery.img
   ```

   (On Windows `.\samloader ...` in PowerShell.) Both lines must end with
   `flash successful`. Alternatives: Heimdall,
   `heimdall flash --BOOT gtaxlwifi-boot.img --RECOVERY gtaxlwifi-recovery.img`;
   or Odin on Windows, with `gtaxlwifi-odin-AP.tar.md5` in the **AP** slot
   and *Auto Reboot* on (Odin is not tested).

   The tablet reboots and, after a few seconds of black screen, opens
   LineageOS Recovery by itself.
4. Optional, recommended: keep a copy of `EFS` (Wi-Fi and Bluetooth
   addresses, unique to your tablet). In the recovery, **Advanced → Enable
   ADB** first, then on the computer:

   ```sh
   adb exec-out "dd if=/dev/block/by-name/EFS 2>/dev/null" > efs-backup.img
   ```

   It must be 20971520 bytes (Linux: `ls -l efs-backup.img`; Windows: in
   Explorer, *Properties*). On Windows run it in **Command Prompt**
   (`cmd`), not PowerShell, which corrupts binary output: `adb exec-out ...
   > efs-backup.img` from `C:\platform-tools`.
5. **Convert the partition table, once.** LineageOS needs partitions the
   factory table does not have (`super`, `vbmeta`, `metadata`, `misc`). This
   erases all data on the tablet; the Samsung partitions with the EFS and
   the bootloaders are not touched. In the recovery: **Apply update → Apply
   from ADB**. On the computer:

   ```sh
   adb sideload gtaxlwifi-repartition.zip
   ```

   It takes a few seconds and ends with **`Done!`** on the tablet; go on
   with the next step, no reboot needed. If the table was already converted,
   the zip says so and changes nothing.
6. In the recovery: **Apply update → Apply from ADB**. On the computer:

   ```sh
   adb sideload gtaxlwifi-los23-install.zip
   ```

   About five minutes. Ignore the percentage on the computer: it stops
   around 47% even when everything works (adb assumes the package is read
   twice, and the recovery keeps it in memory after the first read). The
   bar on the tablet is the one to watch. The tablet must show
   **`Install completed with status 0.`**

7. Optional, **Google apps**. They go in now, **before the first start**, as
   on every LineageOS device; installed after the first start they need a
   factory reset first.
   1. Still in the recovery, right after the install (no need to reboot):
      **Apply update → Apply from ADB**, then on the computer:

      ```sh
      adb sideload MindTheGapps-16.0.0-arm64-<date>.zip
      ```

   2. The recovery says the package is not signed by this build and asks
      whether to install anyway: answer **Yes**. This is expected: only the
      LineageOS zips are signed with the key the recovery knows, on every
      LineageOS device. The tablet must show
      **`Install completed with status 0.`**
8. **Reboot system now**. The first start takes one to two minutes and ends
   in the setup wizard (Google's, with the Google apps).

**From a microSD card** instead of adb: copy the zips to the card, then in
the recovery **Apply update** and choose them on the card. For the first
install never put the zip in the tablet's internal storage: it is erased.

**To remove the Google apps**, a factory reset is not enough: the installer
keeps the add-ons it finds on `system`. In the recovery, **Factory reset →
Format system partition** (if it answers `Failed to open /dev/block/dm-0`,
choose it again: the second time it works), then **Format data / factory
reset**, then install LineageOS again (step 6).

## Update

Your data is kept, and so are the Google apps if you installed them: the
installer backs them up and puts them back, as LineageOS updates do, so
they are **not** installed again after an update. With
**Local update** the tablet restarts into the recovery, installs, and comes
back to Android by itself.

The Updater app looks for new versions on LineageOS's own server, which
lists official builds only: this build installs from the zip file.

- **From the zip file**: copy `gtaxlwifi-los23-install.zip` to the tablet
  (for example to Download). Then Settings → System → **Updater** → menu
  **⋮** → **Local update**, choose the zip, **Install**.
If the tablet was installed before p23 was named `vbmeta`, the update stops
with `the partition table is not migrated` and nothing is written: install
`gtaxlwifi-repartition.zip` once (it only renames p23, data is kept), then
the update.

- **With adb**, from the computer:

  ```sh
  adb reboot recovery
  ```

  In the recovery: **Apply update → Apply from ADB**, then
  `adb sideload gtaxlwifi-los23-install.zip`. When the tablet shows
  `Install completed with status 0.`, **Reboot system now**.

## Go back to stock or to LineageOS 21

This erases everything on the tablet.

```sh
adb reboot recovery
```

In the recovery: **Apply update → Apply from ADB**, then:

```sh
adb sideload gtaxlwifi-revert-to-factory.zip
```

The tablet shows `Install completed with status 0.` Back on the factory
table there is no `misc` partition: from now on, to sideload, choose
**Apply update → Apply from ADB** in the recovery menu (`adb reboot
sideload` does not get there). Then:

- **stock**: download mode, and flash the full stock firmware with
  samloader-rs (see its README) or Odin;
- **LineageOS 21**: download mode, flash the recovery LineageOS 21 is
  installed with (TWRP), and follow its instructions. In TWRP, **wipe the
  cache and format data** before installing LineageOS 21: the cache this
  build leaves behind cannot be read by LineageOS 21's kernel, and its first
  start would only go back to the recovery.

Until then the tablet starts only the recovery and download mode.

## If something goes wrong

If the recovery shows **`Installation aborted.`**, do not reboot to system:
read why, either in the recovery (**View recovery logs**) or on the
computer, after **Advanced → Enable ADB**:

```sh
adb pull /tmp/recovery.log
adb pull /tmp/gtaxl-ota.log
adb pull /tmp/gtaxl/repartition.log
```

During a first install the recovery also prints `Could not read partition
table`: that is expected, the partitions it looks for are created by the
install itself.

| in the log | what to do |
|---|---|
| `This package is for device: gtaxlwifi` | wrong tablet or wrong recovery; nothing was written |
| `the partition table is not migrated` | install `gtaxlwifi-repartition.zip` first (step 5); nothing was written. On a tablet installed before vendor verification (p23 still named `boot`) the repartition zip only renames p23 to `vbmeta` and **keeps your data**; then install LineageOS again |
| `partition table not recognised, nothing written` | flash the stock firmware with Odin, then start again |
| `older layout` | install `gtaxlwifi-revert-to-factory.zip`, then `gtaxlwifi-repartition.zip` |
| `the kernel did not reread the table` | the new table is written: **Advanced → Reboot to recovery**, then go on with step 6 |
| `the kernel still sees the old partitions` | the same |
| `E1001: Failed to update system image` | install again; if it repeats, keep the logs |

| on the tablet | what to do |
|---|---|
| the recovery asks whether to install anyway | the zip and the recovery come from different releases: use files from the same one |
| after installing, it always comes back to the recovery | install the zip again and read the log |
| stuck on the Samsung logo | open the recovery (**Volume Up + Home + Power**) and install the zip again |
| download mode says *An error has occurred while updating the device software* | flash again: `samloader flash --partition BOOT gtaxlwifi-boot.img` |
| boot animation for more than 5 minutes | open the recovery and get the log |

If nothing else works: download mode, and flash the stock firmware with
samloader-rs or Odin.

Building the files yourself is described in [`SETUP.md`](SETUP.md).
