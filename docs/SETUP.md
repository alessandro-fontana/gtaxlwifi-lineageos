# Build LineageOS 23.2 for the Galaxy Tab A 10.1 (2016) Wi-Fi (SM-T580)

How to build, from this repository, the files [`INSTALL.md`](INSTALL.md)
installs: the boot image, the recovery, the install zip and the zip that
goes back to the factory partition table.

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
  kernel patches are applied with `git am`.

In what follows, `REPO` is where this repository is and `TOP` where the
LineageOS tree goes:

```sh
export REPO=~/gtaxlwifi TOP=~/lineage
```

## 1. LineageOS sources

```sh
mkdir -p $TOP && cd $TOP
repo init -u https://github.com/LineageOS/android.git -b lineage-23.2 --git-lfs
mkdir -p .repo/local_manifests
cp $REPO/android/gtaxlwifi/mainline.xml .repo/local_manifests/
repo sync -c --no-clone-bundle -j8
```

`mainline.xml` adds the generic mainline device of LineageOS and the
projects it needs, the sensor and thermal HALs, and LibCamera's Android
branch by TI.

## 2. Patches to the LineageOS tree

```sh
P=$REPO/android/gtaxlwifi/.patches
git -C $TOP/external/boringssl apply \
  $TOP/device/mainline/generic/.patches/external/boringssl/0001-*.patch
while read dir patch; do
  git -C $TOP/$dir apply $P/$patch.patch || echo "FAILED: $patch"
done << 'END'
external/minigbm-upstream          external_minigbm-upstream
packages/apps/Launcher3            packages_apps_Launcher3
frameworks/base                    frameworks_base
frameworks/base                    frameworks_base-bootanimation
external/mesa                      external_mesa
device/mainline/generic            device_mainline_generic
device/mainline/common             device_mainline_common
system/sepolicy                    system_sepolicy
external/selinux                   external_selinux
system/core                        system_core
hardware/intel/sensors-iio         hardware_intel_sensors-iio
external/drm_hwcomposer-upstream   external_drm_hwcomposer-upstream
packages/apps/LineageParts         packages_apps_LineageParts
END
for d in libcamera v4l2_codec2 tinyhal; do
  git -C $TOP/external/$d am $P/external_$d/*.patch || echo "FAILED: $d"
done
```

Nothing may print `FAILED`. A `repo sync` undoes these changes: after one,
apply them again (`git -C <project> checkout -- .` first if a project had
them already).

## 3. Device tree and kernel

```sh
mkdir -p $TOP/device/samsung
rsync -a --exclude=.patches --exclude=tools --exclude=keys --exclude=proprietary \
  --exclude=firmware --exclude=boot-cmdline.txt \
  $REPO/android/gtaxlwifi/ $TOP/device/samsung/gtaxlwifi/

git clone --depth 1 -b v7.2.8 \
  https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git \
  $TOP/kernel/samsung/exynos7870
git -C $TOP/kernel/samsung/exynos7870 am $REPO/kernel/patches-7.2.8/*.patch
```

All the patches must apply. After a change to the device tree in the
repository, run the `rsync` again.

## 4. Proprietary files

Seven files from Samsung and Qualcomm: the GPS daemon and its HAL library,
the Wi-Fi and Bluetooth firmware, and the video codec firmware. `proprietary-files.txt` lists them, with the
public firmware each one comes from and its SHA-1. With the files in a
directory, at the paths the list gives:

```sh
cd $TOP/device/samsung/gtaxlwifi
./extract-files.py <directory>
```

or, from a tablet running this build, with adb: `./extract-files.py`.
It must be run from that directory. It writes `vendor/samsung/gtaxlwifi`.

## 5. Signing keys

The release is signed as LineageOS signs its own builds, with keys of
yours. Create them once and keep them: a build signed with other keys
cannot update an installation of this one without erasing its data.

```sh
cd $TOP
mkdir -p vendor/gtaxl-keys
subject='/C=XX/ST=State/L=City/O=Name/OU=Name/CN=Name/emailAddress=name@example.org'
for c in $(ls build/make/target/product/security/*.x509.pem | xargs -n1 basename \
           | sed 's/\.x509\.pem//' | grep -v '^testkey$') releasekey; do
  ./development/tools/make_key vendor/gtaxl-keys/$c "$subject"
done
cp vendor/gtaxl-keys/releasekey.pk8 vendor/gtaxl-keys/testkey.pk8
cp vendor/gtaxl-keys/releasekey.x509.pem vendor/gtaxl-keys/testkey.x509.pem
cp $REPO/android/gtaxlwifi/keys/keys.mk vendor/gtaxl-keys/
```

Press Enter at the password prompts (no password).

## 6. Build

```sh
cd $TOP
source build/envsetup.sh
lunch lineage_gtaxlwifi-bp4a-userdebug
m target-files-package otatools
$REPO/android/gtaxlwifi/tools/build-release.sh ~/gtaxlwifi-release
```

The first build takes hours. `build-release.sh` signs the target files
(`sign_target_files_apks`), makes the install zip (`ota_from_target_files`)
and writes in `~/gtaxlwifi-release`:

| file | what |
|---|---|
| `gtaxlwifi-boot.img` | the boot image, for `BOOT` |
| `gtaxlwifi-recovery.img` | LineageOS Recovery, for `RECOVERY` |
| `gtaxlwifi-los23-install.zip` | install and update |
| `gtaxlwifi-revert-to-factory.zip` | back to the factory partition table |
| `gtaxlwifi-odin-AP.tar.md5` | boot image and recovery in one file, for Odin |

Then follow [`INSTALL.md`](INSTALL.md).
