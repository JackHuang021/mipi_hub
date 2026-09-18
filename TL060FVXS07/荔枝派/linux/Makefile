# Build against the exact target kernel config and symbols.
ifneq ($(KERNELRELEASE),)
obj-m += lpi4a_tl060.o
obj-m += s6d6ft0.o
else
KDIR ?= /lib/modules/$(shell uname -r)/build
KOUT ?=
KERNEL_SRC ?= $(KDIR)
ARCH ?= riscv
DTC ?= dtc

.PHONY: all modules dtbs clean
all: modules
modules:
	$(MAKE) -C "$(KDIR)" $(if $(KOUT),O="$(KOUT)") M="$(CURDIR)" ARCH="$(ARCH)" CROSS_COMPILE="$(CROSS_COMPILE)" modules

# Provider labels match the RevyOS 6.6 source tree.
dtbs:
	$(CC) -E -nostdinc -undef -D__DTS__ -x assembler-with-cpp \
		-I "$(KERNEL_SRC)/arch/riscv/boot/dts/thead" -I "$(KERNEL_SRC)/include" \
		th1520-lichee-pi-4a-tl060.dts -o tl060.preprocessed.dts
	$(DTC) -@ -I dts -O dtb -o th1520-lichee-pi-4a-tl060.dtb tl060.preprocessed.dts
	$(CC) -E -nostdinc -undef -D__DTS__ -x assembler-with-cpp \
		'-DTL060_BASE_DTS="th1520-lichee-pi-4a-16g.dts"' \
		-I "$(KERNEL_SRC)/arch/riscv/boot/dts/thead" -I "$(KERNEL_SRC)/include" \
		th1520-lichee-pi-4a-tl060.dts -o tl060-16g.preprocessed.dts
	$(DTC) -@ -I dts -O dtb -o th1520-lichee-pi-4a-tl060-16g.dtb tl060-16g.preprocessed.dts

clean:
	$(MAKE) -C "$(KDIR)" $(if $(KOUT),O="$(KOUT)") M="$(CURDIR)" ARCH="$(ARCH)" CROSS_COMPILE="$(CROSS_COMPILE)" clean
endif
