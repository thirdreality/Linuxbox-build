# Produce the Amlogic USB burn image (.burn.img.zip) next to every built .img.
#
# Board config provides LINUXBOX_BURN_VARIANT (v3, v3b, ...); the converter
# comes from amlogic-boot-tools, fetched by the linuxbox family.

function add_host_dependencies__linuxbox_burn() {
	# convert.sh: fdisk, cpp + cc (dtbTool), dtc, zip
	if [[ "$(declare -p EXTRA_BUILD_DEPS 2> /dev/null)" == "declare -a"* ]]; then
		EXTRA_BUILD_DEPS+=("fdisk" "cpp" "gcc" "device-tree-compiler" "zip")
	else
		declare -g EXTRA_BUILD_DEPS="${EXTRA_BUILD_DEPS} fdisk cpp gcc device-tree-compiler zip"
	fi
}

# Take u-boot from the image itself so the burn package matches what was installed.
function pre_umount_final_image__linuxbox_grab_uboot() {
	local uboot_dir="${MOUNT}/usr/lib/linux-u-boot-${BRANCH}-${BOARD}" uboot_bin
	uboot_bin="$(find "${uboot_dir}" -name u-boot.nosd.bin 2> /dev/null | head -n 1)"
	[[ -f "${uboot_bin}" ]] || exit_with_error "u-boot.nosd.bin not found in image" "${uboot_dir}"
	run_host_command_logged mkdir -p "${WORKDIR}/linuxbox-burn"
	run_host_command_logged cp -v "${uboot_bin}" "${WORKDIR}/linuxbox-burn/u-boot.nosd.bin"
}

function post_build_image__linuxbox_burn_image() {
	: "${LINUXBOX_BURN_VARIANT:?board must set LINUXBOX_BURN_VARIANT}"
	local burn_dir="${SRC}/cache/sources/amlogic-boot-tools/burn"
	local work_dir="${WORKDIR}/linuxbox-burn"

	[[ -x "${burn_dir}/convert.sh" ]] || exit_with_error "amlogic-boot-tools not fetched" "${burn_dir}"
	[[ -f "${FINAL_IMAGE_FILE}" ]] || exit_with_error "image not found" "${FINAL_IMAGE_FILE}"

	display_alert "Converting to Amlogic burn image" "${LINUXBOX_BURN_VARIANT}" "info"
	run_host_command_logged mkdir -p "${work_dir}/out"
	OUTPUT_DIR="${work_dir}/out" run_host_command_logged "${burn_dir}/convert.sh" \
		"${FINAL_IMAGE_FILE}" "${LINUXBOX_BURN_VARIANT}" armbian compress "${work_dir}/u-boot.nosd.bin"

	# Straight to FINALDEST: anything left in DESTIMG would be recompressed as an image.
	run_host_command_logged mkdir -p "${FINALDEST}"
	run_host_command_logged mv -v "${work_dir}"/out/*.burn.img.zip "${FINALDEST}/"
	run_host_command_logged rm -rf "${work_dir}"
}
