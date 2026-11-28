# OnionHEN PS5 PRX Example Makefile
#
# Uses clang + ld.lld + python3 (no proprietary SDK required).
# Defaults to PS5 SDK 7.00 metadata (0x10590001 / 0x07000038); override
# SDK_VER / PPR_SDK / SDK_UNK if targeting a game built with a different SDK.

MODULE_NAME ?= OnionExample
TARGET      ?= onion_example.prx
BUILD_DIR   ?= build

SDK_VER     ?= 0x10590001
PPR_SDK     ?= 0x07000038
SDK_UNK     ?= 0x00000411

# Prefer Homebrew LLVM or PS5_PAYLOAD_SDK clang/lld, falling back to PATH.
LLVM_BIN := $(firstword $(wildcard /opt/homebrew/opt/llvm@18/bin /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin))
ifneq ($(LLVM_BIN),)
  DEFAULT_CC := $(LLVM_BIN)/clang
  DEFAULT_LD := $(LLVM_BIN)/ld.lld
else ifdef PS5_PAYLOAD_SDK
  DEFAULT_CC := $(PS5_PAYLOAD_SDK)/bin/clang
  DEFAULT_LD := $(PS5_PAYLOAD_SDK)/bin/ld.lld
else
  DEFAULT_CC := clang
  DEFAULT_LD := ld.lld
endif
ifeq ($(origin CC),default)
  CC := $(DEFAULT_CC)
endif
ifeq ($(origin LD),default)
  LD := $(DEFAULT_LD)
endif
PYTHON ?= python3

CFLAGS := \
	--target=x86_64-pc-freebsd12-elf \
	-fPIC \
	-fvisibility=hidden \
	-ffreestanding \
	-fno-builtin \
	-fno-stack-protector \
	-fno-exceptions \
	-fno-asynchronous-unwind-tables \
	-O2 \
	-Wall -Wextra

STUB_SO := $(BUILD_DIR)/libkernel.so
OBJS    := $(BUILD_DIR)/crtprx.o $(BUILD_DIR)/main.o
RAW_SO  := $(BUILD_DIR)/$(MODULE_NAME).so
OELF    := $(BUILD_DIR)/$(MODULE_NAME).oelf
OUT_PRX := $(BUILD_DIR)/$(TARGET)

.PHONY: all clean deploy

all: $(OUT_PRX)

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(STUB_SO): stubs/libkernel_stub.c | $(BUILD_DIR)
	$(CC) --target=x86_64-pc-freebsd12-elf -fPIC -ffreestanding -fno-builtin -O2 -c $< -o $(BUILD_DIR)/libkernel_stub.o
	$(LD) -shared -soname libkernel.prx -o $@ $(BUILD_DIR)/libkernel_stub.o

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(RAW_SO): $(OBJS) $(STUB_SO) prx.ld
	$(LD) -shared -Bsymbolic --hash-style=sysv -z max-page-size=0x4000 \
		--init=_init --fini=_fini \
		-T prx.ld \
		-o $@ $(OBJS) $(STUB_SO)

$(OUT_PRX): $(RAW_SO) tools/elf2ps5prx.py
	$(PYTHON) tools/elf2ps5prx.py \
		--module-name $(MODULE_NAME) \
		--sdk-ver $(SDK_VER) \
		--ppr-sdk $(PPR_SDK) \
		--sdk-unk $(SDK_UNK) \
		--oelf $(OELF) \
		$(RAW_SO) $@

# Optional convenience target: make deploy PS5_IP=192.168.31.48
PS5_IP   ?= 192.168.31.48
FTP_PORT ?= 2121
deploy: $(OUT_PRX)
	curl -fsS --disable-epsv -T $(OUT_PRX) ftp://$(PS5_IP):$(FTP_PORT)/data/OnionHEN/sprx/$(TARGET)

clean:
	rm -rf $(BUILD_DIR)
