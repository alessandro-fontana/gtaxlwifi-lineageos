# Build LineageOS 23.2 for the Galaxy Tab A 10.1 (2016) Wi-Fi (SM-T580)

How to build the files [`INSTALL.md`](INSTALL.md) installs: the boot image,
the recovery, the install package and the package that goes back to the
factory partition table.

The sources are in three repositories:

| repository | what |
|---|---|
| [android_device_samsung_gtaxlwifi](https://github.com/alessandro-fontana/android_device_samsung_gtaxlwifi) | the device tree |
| [linux-exynos7870](https://github.com/alessandro-fontana/linux-exynos7870) | the kernel: Linux 7.2.9 and this board's commits |
| this repository | changes to other projects, release tools, instructions |

## What you need

- a 64-bit Linux computer with **32 GB of RAM** (16 GB with a large swap
  works, slowly) and **300 GB free** on a fast disk;
- Debian 13 or Ubuntu 24.04 (others work with the equivalent packages):

  ```sh
  sudo apt install bc bison build-essential ccache curl flex fontconfig git \
    git-lfs gcc-aarch64-linux-gnu libelf-dev libssl-dev libxml2-utils lz4 \
    openjdk-21-jdk-headless python-is-python3 python3 python3-mako \
    python3-ply python3-yaml rsync unzip zip zlib1g-dev zstd
  ```

- Google's `repo` tool:

  ```sh
  mkdir -p ~/bin
  curl https://storage.googleapis.com/git-repo-downloads/repo > ~/bin/repo
  chmod a+x ~/bin/repo
  export PATH=~/bin:$PATH
  ```

- a git identity (`git config --global user.name` and `user.email`): the
  changes to other projects are applied with `git am`.

In what follows, `REPO` is where this repository is and `TOP` where the
LineageOS tree goes:

```sh
export REPO=~/gtaxlwifi-lineageos TOP=~/lineage
```

## 1. Sources

```sh
mkdir -p $TOP && cd $TOP
repo init -u https://github.com/LineageOS/android.git -b lineage-23.2 --git-lfs
mkdir -p .repo/local_manifests
cp $REPO/local_manifests/gtaxlwifi.xml .repo/local_manifests/
repo sync -c --no-clone-bundle -j8
```

The local manifest adds the device tree, the kernel, the generic mainline
device and the projects it needs, and LibCamera's Android branch by TI.

## 2. Changes to other projects

Until they are merged, some projects need the commits in `patches/`, one
directory per project:

```sh
P=$REPO/patches
while read dir series; do
  git -C $TOP/$dir am -q $P/$series/*.patch || echo "FAILED: $series"
done << 'END'
device/mainline/generic            device_mainline_generic
device/mainline/common             device_mainline_common
system/core                        system_core
frameworks/base                    frameworks_base
packages/apps/Launcher3            packages_apps_Launcher3
packages/apps/LineageParts         packages_apps_LineageParts
external/drm_hwcomposer-upstream   external_drm_hwcomposer-upstream
external/minigbm-upstream          external_minigbm-upstream
external/mesa                      external_mesa
hardware/intel/sensors-iio         hardware_intel_sensors-iio
external/v4l2_codec2               external_v4l2_codec2
external/tinyhal                   external_tinyhal
external/libcamera                 external_libcamera
END
git -C $TOP/external/boringssl apply \
  $TOP/device/mainline/generic/.patches/external/boringssl/0001-*.patch
git -C $TOP/system/sepolicy apply $P/backports/system_sepolicy.patch
git -C $TOP/external/selinux apply $P/backports/external_selinux.patch
```

Nothing may print `FAILED`. The two backports bring the `memfd_file`
class from Android 17 and are not meant for submission. A `repo sync`
undoes all of this: apply it again afterwards.

## 3. Proprietary files

Seven files from Samsung and Qualcomm: the GPS daemon and its HAL library,
the Wi-Fi and Bluetooth firmware, and the video codec firmware.
`proprietary-files.txt` in the device tree lists them, with the public
firmware each one comes from and its SHA-1. With the files in a directory,
at the paths the list gives:

```sh
cd $TOP/device/samsung/gtaxlwifi
./extract-files.py <directory>
```

or, from a tablet running this build, with adb: `./extract-files.py`.
It must be run from that directory. It writes `vendor/samsung/gtaxlwifi`.

## 4. Signing keys

The release is signed as LineageOS signs its own builds, with keys of
yours (see the wiki's *Signing builds*). Create them once and keep them: a
build signed with other keys cannot update an installation of this one
without erasing its data.

```sh
cd $TOP
K=vendor/lineage-priv/keys
mkdir -p $K
subject='/C=XX/ST=State/L=City/O=Name/OU=Name/CN=Name/emailAddress=name@example.org'
for c in $(ls build/make/target/product/security/*.x509.pem | xargs -n1 basename \
           | sed 's/\.x509\.pem//' | grep -v '^testkey$') releasekey; do
  ./development/tools/make_key $K/$c "$subject"
done
cp $K/releasekey.pk8 $K/testkey.pk8
cp $K/releasekey.x509.pem $K/testkey.x509.pem
cat > $K/keys.mk << 'END'
PRODUCT_DEFAULT_DEV_CERTIFICATE := vendor/lineage-priv/keys/releasekey
PRODUCT_OTA_PUBLIC_KEYS := vendor/lineage-priv/keys/releasekey.x509.pem
PRODUCT_MAINLINE_BLUETOOTH_SEPOLICY_DEV_CERTIFICATES := vendor/lineage-priv/keys
END
```

Press Enter at the password prompts (no password). LineageOS includes
`vendor/lineage-priv/keys/keys.mk` by itself.

## 5. Build

```sh
cd $TOP
source build/envsetup.sh
lunch lineage_gtaxlwifi-bp4a-userdebug
m target-files-package otatools
$REPO/tools/build-release.sh ~/gtaxlwifi-release
```

The first build takes hours. `build-release.sh` signs the target files
(`sign_target_files_apks`), makes the install package
(`ota_from_target_files`) and writes in `~/gtaxlwifi-release`:

| file | what |
|---|---|
| `gtaxlwifi-boot.img` | the boot image, for `BOOT` |
| `gtaxlwifi-recovery.img` | LineageOS Recovery, for `RECOVERY` |
| `gtaxlwifi-los23-install.zip` | install and update |
| `gtaxlwifi-revert-to-factory.zip` | back to the factory partition table |
| `gtaxlwifi-odin-AP.tar.md5` | boot image and recovery in one file, for Odin |
| `SHA256SUMS` | checksums |

Then follow [`INSTALL.md`](INSTALL.md).
