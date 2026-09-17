# gunyah-probe

Static aarch64 probe that answers one question:

> Is `/dev/gunyah` on a Qualcomm (msm) Android device actually usable?

## Background

The Qualcomm `msm-5.15` vendor gunyah stack (`drivers/virt/gunyah/gh_main.c`,
`include/uapi/linux/gunyah.h`) implements a **different** ioctl ABI than the
upstream Gunyah driver:

| ioctl | vendor ABI (msm) | upstream ABI |
|---|---|---|
| `GH_IOCTL_TYPE` | `0xB2` | `'G'` (0x47) |
| `GH_CREATE_VM` | `_IO(0xB2, 0x01)` = `0xB201` | `_IO('G', 0x00)` = `0x4700` |
| `GH_CREATE_VCPU` | `_IO(0xB2, 0x40)` = `0xB240` | `_IO('G', 0x40)` |
| `GH_VM_SET_FW_NAME` | `_IOW(0xB2, 0x41, struct gh_fw_name)` | — |
| `GH_VM_GET_FW_NAME` | `_IOR(0xB2, 0x42, struct gh_fw_name)` | — |
| `GH_VM_GET_VCPU_COUNT` | `_IO(0xB2, 0x43)` = `0xB243` | `_IO('G', 0x41)` |
| `GH_VCPU_RUN` | `_IO(0xB2, 0x80)` = `0xB280` | `_IO('G', 0x80)` |

`/dev/gunyah` in the vendor driver only implements `GH_CREATE_VM` on the device
fd; everything else lives on the VM fd returned by it.

## What the probe does (non-destructive)

1. `stat` + `open("/dev/gunyah", O_RDWR)` — needs root (node is `0600 root:root`,
   SELinux label `vendor_gunyah_chr_device`).
2. `GH_CREATE_VM` (vendor `0xB201`) — must return a fresh VM fd.
3. On that VM fd: `GH_VM_GET_FW_NAME`, `GH_VM_GET_VCPU_COUNT`, `GH_CREATE_VCPU`.
4. Control case: the upstream number `0x4700`, to show which ABI the kernel speaks.
5. Closes everything (an empty VM is destroyed by the driver on last close — no
   VMID is allocated, no firmware is loaded, no side-effecting hypercall is made).

## Build

CI (`.github/workflows/build.yml`) cross-compiles a statically linked aarch64
binary with `gcc-aarch64-linux-gnu` and uploads it as the `gunyah_probe-aarch64`
artifact. Locally the same command works:

```sh
aarch64-linux-gnu-gcc -O2 -Wall -Wextra -static -o gunyah_probe gunyah_probe.c
```

## Run

```sh
adb push gunyah_probe /data/local/tmp/
adb shell su -c /data/local/tmp/gunyah_probe
```
