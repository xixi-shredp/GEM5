TOOLCHAIN_DIR ?= /home/xixi/mybin/riscv/bin
CROSS_COMPILE ?= $(TOOLCHAIN_DIR)/riscv64-unknown-linux-gnu-
CC := $(CROSS_COMPILE)gcc

CFLAGS = -O2 -static -nostdlib -nostartfiles -ffreestanding \
	-fno-stack-protector -msmall-data-limit=0 -march=rv64gc -mabi=lp64d
OUT = quadrilatero-store-pack

.PHONY: all clean

all: $(OUT)

$(OUT): quadrilatero-store-pack.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(OUT)
