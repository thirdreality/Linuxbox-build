# BL706 Zigbee/Thread daughter-board flasher, packaged as linuxbox-bl706.
#
# The deb is built on the host from files/ and installed into the image; a copy
# is left in output/debs/ so boards in the field can be upgraded with dpkg -i.

declare -g LINUXBOX_BL706_VERSION="${LINUXBOX_BL706_VERSION:-1.0.0}"

function post_family_tweaks__linuxbox_bl706() {
	local src="${EXTENSION_DIR}/files" pkg_root deb
	pkg_root="$(mktemp -d)"
	deb="${WORKDIR}/linuxbox-bl706_${LINUXBOX_BL706_VERSION}_all.deb"

	display_alert "Building linuxbox-bl706" "${LINUXBOX_BL706_VERSION}" "info"
	run_host_command_logged mkdir -p "${pkg_root}/DEBIAN" "${pkg_root}/usr/lib/firmware/bl706"
	run_host_command_logged cp -r "${src}/partition_1m_images" "${src}/partition_2m_images" "${src}/pip-wheels" "${pkg_root}/usr/lib/firmware/bl706/"
	run_host_command_logged tar -xzf "${src}/bflb_iot.tar.gz" --no-same-owner -C "${pkg_root}/usr/lib/firmware/bl706/"
	run_host_command_logged install -m 755 "${src}/bl706_func.sh" "${pkg_root}/usr/lib/firmware/bl706/"

	cat <<- EOF > "${pkg_root}/DEBIAN/control"
		Package: linuxbox-bl706
		Version: ${LINUXBOX_BL706_VERSION}
		Architecture: all
		Maintainer: ${MAINTAINER} <${MAINTAINERMAIL}>
		Depends: python3, python3-pip, python3-setuptools, python3-wheel, python3-dev, gcc, gpiod
		Section: utils
		Priority: optional
		Description: ThirdReality BL706 Zigbee/Thread module flasher
		 bl706_func.sh {start|restart|flash|install}, bflb_iot tool and firmware images.
	EOF

	cat <<- 'EOF' > "${pkg_root}/DEBIAN/postinst"
		#!/bin/sh
		set -e
		if [ "$1" = "configure" ]; then
			/usr/lib/firmware/bl706/bl706_func.sh install
		fi
	EOF
	chmod 755 "${pkg_root}/DEBIAN/postinst"

	run_host_command_logged dpkg-deb --root-owner-group -b "${pkg_root}" "${deb}"
	run_host_command_logged rm -rf "${pkg_root}"

	install_deb_chroot "${deb}"
	# install_deb_chroot stages the deb in /root and leaves it there
	run_host_command_logged rm -f "${SDCARD}/root/$(basename "${deb}")"
	run_host_command_logged mkdir -p "${DEB_STORAGE}"
	run_host_command_logged cp -v "${deb}" "${DEB_STORAGE}/"
}
