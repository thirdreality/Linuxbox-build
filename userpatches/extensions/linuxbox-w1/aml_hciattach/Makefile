# Static aarch64 aml_hciattach: bluez 5.79 hciattach + Amlogic SDK 2025r1
# hciattach_aml/rome (br-aml/package/amlogic/bluez5_utils).  Static so one
# binary runs on every Debian/Ubuntu release Armbian builds.

CROSS_COMPILE ?= aarch64-linux-gnu-
CC := $(CROSS_COMPILE)gcc
CFLAGS ?= -O2
CFLAGS += -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-result
CPPFLAGS += -I. -Iinclude -DVERSION=\"5.79\"
LDFLAGS += -static -pthread

SRCS := tools/hciattach.c tools/hciattach_st.c tools/hciattach_ti.c \
	tools/hciattach_tialt.c tools/hciattach_ath3k.c tools/hciattach_qualcomm.c \
	tools/hciattach_intel.c tools/hciattach_bcm43xx.c tools/hciattach_rome.c \
	tools/hciattach_aml.c lib/bluetooth.c lib/hci.c

OUT ?= .

$(OUT)/aml_hciattach: $(SRCS)
	mkdir -p include $(OUT)
	ln -sfn ../lib include/bluetooth
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRCS) $(LDFLAGS) -o $@

clean:
	rm -rf include $(OUT)/aml_hciattach

.PHONY: clean
