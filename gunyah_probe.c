// SPDX-License-Identifier: MIT
/*
 * gunyah_probe.c - verify whether /dev/gunyah is usable on a Qualcomm (msm) kernel.
 *
 * The Qualcomm msm-5.15 vendor gunyah stack (drivers/virt/gunyah/gh_main.c,
 * include/uapi/linux/gunyah.h) uses GH_IOCTL_TYPE = 0xB2:
 *     GH_CREATE_VM           _IO (0xB2, 0x01) = 0xB201
 *     GH_CREATE_VCPU         _IO (0xB2, 0x40) = 0xB240
 *     GH_VM_SET_FW_NAME      _IOW(0xB2, 0x41, struct gh_fw_name)
 *     GH_VM_GET_FW_NAME      _IOR(0xB2, 0x42, struct gh_fw_name)
 *     GH_VM_GET_VCPU_COUNT   _IO (0xB2, 0x43) = 0xB243
 *
 * The upstream (non-vendor) ABI instead uses GH_IOCTL_TYPE = 'G' with
 * GH_CREATE_VM = _IO('G', 0x00) = 0x4700; that number is rejected by the
 * vendor driver with "Invalid gunyah dev ioctl".  We probe both so the
 * report shows which ABI this kernel actually implements.
 *
 * The probe is non-destructive: it only creates an (empty) VM + vcpu object
 * inside the gunyah driver and closes them again.  No VMID is allocated, no
 * firmware is loaded and no hypervisor call with side effects is issued,
 * because that only happens once a firmware name is set.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#define GH_IOCTL_TYPE_VENDOR   0xB2
#define GH_IOCTL_TYPE_UPSTREAM 'G'

struct gh_fw_name {
	char name[16];
};

#define GH_CREATE_VM_V         _IO(GH_IOCTL_TYPE_VENDOR, 0x01)
#define GH_CREATE_VCPU_V       _IO(GH_IOCTL_TYPE_VENDOR, 0x40)
#define GH_VM_SET_FW_NAME_V    _IOW(GH_IOCTL_TYPE_VENDOR, 0x41, struct gh_fw_name)
#define GH_VM_GET_FW_NAME_V    _IOR(GH_IOCTL_TYPE_VENDOR, 0x42, struct gh_fw_name)
#define GH_VM_GET_VCPU_COUNT_V _IO(GH_IOCTL_TYPE_VENDOR, 0x43)
#define GH_VCPU_RUN_V          _IO(GH_IOCTL_TYPE_VENDOR, 0x80)

#define GH_CREATE_VM_U _IO(GH_IOCTL_TYPE_UPSTREAM, 0x00) /* 0x4700 */

static int n_pass, n_fail;

static void hdr(const char *s)
{
	printf("\n=== %s ===\n", s);
}

static void ok(const char *what, long ret)
{
	n_pass++;
	printf("  [PASS] %-34s ret=%ld", what, ret);
	if (ret >= 0)
		printf(" (fd=%ld)", ret);
	printf("\n");
}

static void bad(const char *what, long ret)
{
	n_fail++;
	printf("  [FAIL] %-34s ret=%ld errno=%d (%s)\n", what, ret, errno,
	       strerror(errno));
}

static void info(const char *what, long ret)
{
	printf("  [info] %-34s ret=%ld errno=%d (%s)\n", what, ret, errno,
	       strerror(errno));
}

int main(void)
{
	struct stat st;
	struct gh_fw_name fw;
	char buf[256];
	int fd = -1, vmfd = -1, vcpufd = -1;
	int have_vm = 0;
	long r;

	setvbuf(stdout, NULL, _IONBF, 0);

	hdr("environment");
	printf("  uid=%d euid=%d pid=%d\n", getuid(), geteuid(), getpid());
	{
		int kf = open("/proc/version", O_RDONLY);
		if (kf >= 0) {
			ssize_t n = read(kf, buf, sizeof(buf) - 1);
			if (n > 0) {
				buf[n] = 0;
				printf("  %s", buf);
			}
			close(kf);
		}
	}
	printf("  ioctl numbers: CREATE_VM=0x%lx CREATE_VCPU=0x%lx "
	       "GET_FW_NAME=0x%lx SET_FW_NAME=0x%lx VCPU_RUN=0x%lx "
	       "upstream_CREATE_VM=0x%lx\n",
	       (unsigned long)GH_CREATE_VM_V, (unsigned long)GH_CREATE_VCPU_V,
	       (unsigned long)GH_VM_GET_FW_NAME_V,
	       (unsigned long)GH_VM_SET_FW_NAME_V, (unsigned long)GH_VCPU_RUN_V,
	       (unsigned long)GH_CREATE_VM_U);

	hdr("node");
	if (stat("/dev/gunyah", &st) == 0) {
		n_pass++;
		printf("  [PASS] /dev/gunyah exists  mode=%04o uid=%u gid=%u "
		       "rdev=%u,%u\n",
		       st.st_mode & 07777, st.st_uid, st.st_gid,
		       (unsigned)(st.st_rdev >> 8), (unsigned)(st.st_rdev & 0xff));
	} else {
		bad("stat(/dev/gunyah)", -1);
		printf("\nRESULT: /dev/gunyah NOT PRESENT -> unusable\n");
		return 1;
	}

	hdr("open");
	fd = open("/dev/gunyah", O_RDWR);
	if (fd >= 0) {
		ok("open(O_RDWR)", fd);
	} else {
		int e = errno;
		info("open(O_RDWR)", -1);
		fd = open("/dev/gunyah", O_RDONLY);
		if (fd >= 0)
			ok("open(O_RDONLY)", fd);
		else {
			errno = e;
			bad("open(O_RDONLY)", -1);
			printf("  hint: node is root-only (0600 root:root, "
			       "label vendor_gunyah_chr_device) -> needs root\n");
			printf("\nRESULT: /dev/gunyah present but NOT OPENABLE "
			       "-> unusable for this process\n");
			return 1;
		}
	}

	hdr("vendor ABI (GH_IOCTL_TYPE = 0xB2)");
	errno = 0;
	r = ioctl(fd, GH_CREATE_VM_V, 0);
	if (r >= 0) {
		ok("GH_CREATE_VM (0xB201)", r);
		vmfd = (int)r;
		have_vm = 1;
	} else {
		/* fall back to write-open (some kernels restrict O_RDWR) */
		bad("GH_CREATE_VM (0xB201)", r);
	}

	if (have_vm) {
		memset(&fw, 0, sizeof(fw));
		errno = 0;
		r = ioctl(vmfd, GH_VM_GET_FW_NAME_V, &fw);
		if (r == 0)
			ok("GH_VM_GET_FW_NAME (0x8010B242)", r);
		else
			info("GH_VM_GET_FW_NAME (0x8010B242)", r);
		printf("  [info] fw_name='%s'\n", fw.name);

		errno = 0;
		r = ioctl(vmfd, GH_VM_GET_VCPU_COUNT_V);
		/* expected -EINVAL: only secure VMs have an allowed vcpu count */
		info("GH_VM_GET_VCPU_COUNT (0xB243)", r);

		errno = 0;
		r = ioctl(vmfd, GH_CREATE_VCPU_V, 0);
		if (r >= 0) {
			ok("GH_CREATE_VCPU id=0 (0xB240)", r);
			vcpufd = (int)r;
		} else {
			info("GH_CREATE_VCPU id=0 (0xB240)", r);
		}
	}

	hdr("control: upstream ABI number on this kernel");
	errno = 0;
	r = ioctl(fd, GH_CREATE_VM_U, 0);
	if (r >= 0) {
		printf("  [info] upstream GH_CREATE_VM (0x4700) accepted -> "
		       "kernel speaks the UPSTREAM abi\n");
		close((int)r);
	} else {
		printf("  [info] upstream GH_CREATE_VM (0x4700) rejected "
		       "errno=%d (%s) -> vendor abi only\n", errno,
		       strerror(errno));
	}

	hdr("cleanup");
	if (vcpufd >= 0) {
		close(vcpufd);
		printf("  closed vcpu fd\n");
	}
	if (vmfd >= 0) {
		close(vmfd);
		printf("  closed vm fd (driver destroys the empty VM)\n");
	}
	close(fd);

	hdr("summary");
	printf("  checks passed: %d, failed: %d\n", n_pass, n_fail);
	if (n_fail == 0 && have_vm) {
		printf("RESULT: /dev/gunyah IS USABLE "
		       "(vendor gunyah ABI 0xB2 answers and hands out VM fds)\n");
		return 0;
	}
	printf("RESULT: /dev/gunyah NOT usable (see failures above)\n");
	return 1;
}
