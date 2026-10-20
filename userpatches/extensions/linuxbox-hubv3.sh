# HubV3 board integration that is not WiFi/BT, BL706 or burn-image specific.

# The boot button is multiplexed onto eMMC DAT5, so pressing it while running
# can corrupt a writable /boot; the boot partition is mounted read-only.
function format_partitions__linuxbox_boot_ro() {
	local fstab="${SDCARD}/etc/fstab"
	[[ -f "${fstab}" ]] || return 0
	if grep -qE '^\S+\s+/boot\s+ext4\s+' "${fstab}"; then
		display_alert "Mounting /boot read-only" "${BOARD}" "info"
		sed -i -E 's#^(\S+\s+/boot\s+ext4\s+)\S+#\1ro,noatime#' "${fstab}"
	fi
}

# cgroup v1 for Docker / HA Supervised. armbian-firstrun used to append this at
# first boot, but /boot is read-only at runtime, so it must be written here.
function post_family_tweaks__linuxbox_cgroup_v1() {
	local env="${SDCARD}/boot/armbianEnv.txt"
	[[ -f "${env}" ]] || exit_with_error "armbianEnv.txt not found" "${env}"
	if ! grep -q '^extraargs=.*systemd.unified_cgroup_hierarchy=false' "${env}"; then
		display_alert "Enabling cgroup v1" "extraargs in armbianEnv.txt" "info"
		echo "extraargs=systemd.unified_cgroup_hierarchy=false" >> "${env}"
	fi
}

# Armbian's bsp-cli cache key omits BOARD_NAME; a hook body is part of it, so
# keep the name here to force a rebuild whenever it changes.
function post_family_tweaks_bsp__linuxbox_board_name() {
	[[ "${BOARD_NAME}" == "LinuxBox-HubV3" ]] || display_alert "BOARD_NAME changed, update linuxbox-hubv3.sh" "${BOARD_NAME}" "wrn"
}
