# Amlogic W155S1 (W1) WiFi/BT user space.
#
# The kernel side (aml_sdio / vlsicomm / sdio_bt and the SDIO glue) is in the
# linuxbox-6.6 kernel patch set.  This installs what the driver and bring-up
# scripts expect at runtime, at the paths LinuxBox has always used:
#   /usr/lib/firmware/w1/   rf/cfg txt, BT firmware, w1_start.sh, w1_bt_start.sh
#   /usr/bin/aml_hciattach  built here from source (static aarch64)
#   amlogicw1.service, amlogicw1-bt.service
#
# Installed straight into the rootfs (not the cached bsp-cli deb) so edits to
# the files below always reach the next image.

function add_host_dependencies__linuxbox_w1() {
	if [[ "$(declare -p EXTRA_BUILD_DEPS 2> /dev/null)" == "declare -a"* ]]; then
		EXTRA_BUILD_DEPS+=("gcc-aarch64-linux-gnu" "libc6-dev-arm64-cross" "make")
	else
		declare -g EXTRA_BUILD_DEPS="${EXTRA_BUILD_DEPS} gcc-aarch64-linux-gnu libc6-dev-arm64-cross make"
	fi
}

function post_family_tweaks__linuxbox_w1() {
	local w1_src="${EXTENSION_DIR}" fw_dir="${SDCARD}/usr/lib/firmware/w1" build_dir

	display_alert "Installing W1 WiFi/BT user space" "${BOARD}" "info"
	run_host_command_logged mkdir -p "${fw_dir}" "${SDCARD}/usr/lib/systemd/system" "${SDCARD}/usr/bin"

	run_host_command_logged install -m 644 \
		"${w1_src}"/files/aml_wifi_drv_cfg_0.conf "${w1_src}"/files/aml_wifi_rf*.txt \
		"${w1_src}"/files/aml_bt_rf.txt "${w1_src}"/files/a2dp_mode_cfg.txt "${fw_dir}/"
	# LINUXBOX_W1_BT_FW=w1_bt_fw_uart.bin.linuxbox selects the pre-SDK-2025r1 firmware
	run_host_command_logged install -m 644 "${w1_src}/files/${LINUXBOX_W1_BT_FW:-w1_bt_fw_uart.bin}" "${fw_dir}/w1_bt_fw_uart.bin"
	run_host_command_logged install -m 755 "${w1_src}"/files/w1_start.sh "${w1_src}"/files/w1_bt_start.sh "${fw_dir}/"
	run_host_command_logged install -m 644 "${w1_src}"/systemd/*.service "${SDCARD}/usr/lib/systemd/system/"

	build_dir="$(mktemp -d)"
	run_host_command_logged cp -r "${w1_src}/aml_hciattach/." "${build_dir}/"
	run_host_command_logged make -C "${build_dir}" CROSS_COMPILE=aarch64-linux-gnu- OUT="${build_dir}/out"
	run_host_command_logged install -m 755 "${build_dir}/out/aml_hciattach" "${SDCARD}/usr/bin/aml_hciattach"
	run_host_command_logged rm -rf "${build_dir}"

	chroot_sdcard systemctl --no-reload enable amlogicw1.service amlogicw1-bt.service

	# p2p0 is the driver's always-present P2P vif; NM would otherwise pick it for STA connections
	if [[ -d "${SDCARD}/etc/NetworkManager/conf.d" ]]; then
		cat <<- EOF > "${SDCARD}/etc/NetworkManager/conf.d/10-linuxbox-w1-p2p0-unmanaged.conf"
			[keyfile]
			unmanaged-devices=interface-name:p2p0
		EOF
	fi

	linuxbox_w1_select_upstream_regdb
}

# Debian's wireless-regdb defaults to the Debian-signed regulatory.db, which a
# mainline kernel rejects (CONFIG_CFG80211_REQUIRE_SIGNED_REGDB) and then falls
# back to world roaming.  Both copies carry identical rules; pick the upstream
# signature.  Undo on a device with: update-alternatives --auto regulatory.db
function linuxbox_w1_select_upstream_regdb() {
	local db_upstream="/lib/firmware/regulatory.db-upstream"

	if [[ ! -f "${SDCARD}${db_upstream}" ]]; then
		display_alert "No upstream-signed regulatory.db in this release" "${RELEASE}" "wrn"
		return 0
	fi
	chroot_sdcard update-alternatives --set regulatory.db "${db_upstream}"
}
