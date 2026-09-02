# Host unit tests for OnionHEN shared libs + util cheats (no PS5 SDK).
# Usage (from this directory):
#   make
#   make test
#   ONION_TEST_VERBOSE=1 make test

UTIL_ROOT := $(abspath ..)
REPO_ROOT := $(abspath $(UTIL_ROOT)/../..)
THIRD_PARTY_ROOT := $(REPO_ROOT)/third_party
BUILD_DIR := $(UTIL_ROOT)/build/host-tests
BIN := $(BUILD_DIR)/onion-host-tests
TEST_DATA_ROOT := $(BUILD_DIR)/runtime

HOST_CC ?= cc
HOST_CXX ?= c++
PYTHON ?= python3
KEYSTONE_PREFIX ?= /opt/homebrew
KEYSTONE_CFLAGS := -I$(THIRD_PARTY_ROOT)/keystone/include
KEYSTONE_LDFLAGS := -L$(KEYSTONE_PREFIX)/lib -lkeystone

DEPFLAGS := -MMD -MP
COMMON_FLAGS := -Wall -Wextra -g -O0 $(DEPFLAGS) -DONION_HOST_TEST=1 -DCBC=1 \
	-DONION_TEST_DIR=\"$(CURDIR)\" \
	-DONION_DATA_ROOT=\"$(TEST_DATA_ROOT)\" \
	-I$(UTIL_ROOT)/include \
	-I$(UTIL_ROOT)/tests \
	-I$(REPO_ROOT)/source/include \
	-I$(REPO_ROOT)/source/libonion_settings/include \
	-I$(REPO_ROOT)/source/libonion_platform/include \
	-I$(REPO_ROOT)/source/libonion_payload/include \
	-I$(REPO_ROOT)/source/libonion_proc/include \
	-I$(REPO_ROOT)/source/libonion_ipc/include \
	-I$(REPO_ROOT)/source/libonion_detour/include \
	-I$(REPO_ROOT)/source/libonion_ready \
	-I$(REPO_ROOT)/source/libonion_fps/include \
	-I$(REPO_ROOT)/source/libonion_plugin_ui/include \
	-I$(REPO_ROOT)/source/libonion_plugin_bridge/include \
	-I$(REPO_ROOT)/source/libonion_plugin_session/include \
	-I$(REPO_ROOT)/source/libonion_plugin_manager/include \
	-I$(REPO_ROOT)/source/daemon/include \
	-I$(THIRD_PARTY_ROOT)/cjson \
	-I$(THIRD_PARTY_ROOT)/cheat_support \
	-I$(THIRD_PARTY_ROOT)/pkgserver \
	-I$(REPO_ROOT)/source/shellui/include \
	-I$(BUILD_DIR)/generated \
	$(KEYSTONE_CFLAGS)

I18N_DIR := $(REPO_ROOT)/source/i18n
I18N_JSON := $(wildcard $(I18N_DIR)/*.json)
I18N_CATALOG := $(BUILD_DIR)/generated/toolbox_i18n_catalog.inc
NOTIFY_I18N_CATALOG := $(BUILD_DIR)/generated/notify_i18n_catalog.inc

# ready.h lives under source/include/onion/
COMMON_FLAGS += -I$(REPO_ROOT)/source/include

CFLAGS := $(COMMON_FLAGS) -std=c11 -D_DEFAULT_SOURCE
CXXFLAGS := $(COMMON_FLAGS) -std=c++20 -D_DEFAULT_SOURCE
LDFLAGS := $(KEYSTONE_LDFLAGS) -pthread

# Production sources under test
PROD_C_SRCS := \
	$(UTIL_ROOT)/source/util_language.c \
	$(UTIL_ROOT)/source/cheats/cheat_engine_utils.c \
	$(UTIL_ROOT)/source/cheats/cheat_engine_parser_shnext.c \
	$(UTIL_ROOT)/source/cheats/cheat_flatten.c \
	$(REPO_ROOT)/source/libonion_platform/source/fs.c \
	$(REPO_ROOT)/source/libonion_platform/source/log.c \
	$(REPO_ROOT)/source/libonion_platform/source/net.c \
	$(REPO_ROOT)/source/libonion_platform/source/notify.c \
	$(REPO_ROOT)/source/libonion_platform/source/notify_i18n.c \
	$(REPO_ROOT)/source/libonion_ready/source/ready.c \
	$(REPO_ROOT)/source/libonion_payload/source/payload.c

PROD_CXX_SRCS := \
	$(UTIL_ROOT)/source/cheats/json_cheat_parser.cpp \
	$(UTIL_ROOT)/source/cheats/xml_cheat_parser.cpp \
	$(UTIL_ROOT)/source/cheats/mc4_cheat_parser.cpp \
	$(UTIL_ROOT)/source/cheats/shn_ext_cheat_parser.cpp \
	$(UTIL_ROOT)/source/cheats/cheat_parser_factory.cpp \
	$(UTIL_ROOT)/source/cheats/cheat_repository.cpp \
	$(UTIL_ROOT)/source/cheats/sync/catalog_hen_collection.cpp \
	$(UTIL_ROOT)/source/cheats/sync/catalog_registry.cpp \
	$(UTIL_ROOT)/source/cheats/sync/mirror_github.cpp \
	$(UTIL_ROOT)/source/cheats/sync/mirror_cnb.cpp \
	$(UTIL_ROOT)/source/cheats/sync/mirror_factory.cpp \
	$(UTIL_ROOT)/source/cheats/sync/cheat_sync_engine.cpp \
	$(UTIL_ROOT)/source/cheats/sync/cheat_sync_service.cpp \
	$(UTIL_ROOT)/source/cheats/sync/http_transport_ps5.cpp \
	$(UTIL_ROOT)/source/cheats/sync/zip_archive.cpp \
	$(THIRD_PARTY_ROOT)/cjson/cJSON.cpp \
	$(REPO_ROOT)/source/libonion_settings/source/settings.cpp \
	$(REPO_ROOT)/source/libonion_ipc/source/ipc_server.cpp \
	$(REPO_ROOT)/source/libonion_plugin_manager/source/plugin_descriptor.cpp \
	$(REPO_ROOT)/source/libonion_plugin_manager/source/plugin_manager.cpp \
	$(REPO_ROOT)/source/shellui/src/ps5_settings_ui.cpp \
	$(REPO_ROOT)/source/shellui/src/toolbox_route.cpp \
	$(REPO_ROOT)/source/shellui/src/toolbox_i18n.cpp \
	$(REPO_ROOT)/source/shellui/src/toolbox_pkg_xml.cpp \
	$(REPO_ROOT)/source/shellui/src/dynamic_ui_xml.cpp \
	$(REPO_ROOT)/source/shellui/src/dynamic_ui_runtime.cpp \
	$(REPO_ROOT)/source/libonion_fps/source/fps_formula.cpp

TEST_C_SRCS := \
	test_harness.c \
	test_support.c \
	test_cheat_utils.c \
	test_cheat_flatten.c \
	test_payload.c \
	test_base64.c \
	test_aes_cbc.c \
	test_hde64.c \
	test_reg_entity.c \
	test_ready.c \
	test_platform_fs.c \
	test_platform_log.c \
	test_platform_net.c \
	test_platform_notify.c \
	test_util_language.c \
	test_builtin_services.c \
	test_multipart.c \
	test_hijack_retry.c \
	stubs/host_stubs.c

TEST_CXX_SRCS := \
	test_cheat_parsers.cpp \
	test_cheat_repository.cpp \
	test_git_mirror_factory.cpp \
	test_cheat_catalog.cpp \
	test_cheat_sync.cpp \
	test_settings.cpp \
	test_msg_protocol.cpp \
	test_app_jailbreak_policy.cpp \
	test_debug_settings_route_policy.cpp \
	test_ipc_harden.cpp \
	test_ps5_settings_ui.cpp \
	test_toolbox_route.cpp \
	test_onpress_policy.cpp \
	test_toolbox_helpers.cpp \
  test_toolbox_i18n.cpp \
  test_toolbox_pkg_xml.cpp \
  test_toolbox_injection.cpp \
  test_overlay_text_metrics.cpp \
  test_overlay_layout.cpp \
  test_fps_formula.cpp \
	test_plugin_ui.cpp \
	test_plugin_manager.cpp \
	test_trampoline_arena.cpp \
	test_hook_lifecycle.cpp \
	test_hotpatch.cpp \
	test_x64_relocator.cpp \
	test_main.cpp

PROD_C_OBJS := $(patsubst $(UTIL_ROOT)/%.c,$(BUILD_DIR)/prod/%.o,$(filter $(UTIL_ROOT)/%,$(PROD_C_SRCS)))
PROD_C_OBJS += $(BUILD_DIR)/prod/libonion_platform/fs.o
PROD_C_OBJS += $(BUILD_DIR)/prod/libonion_platform/log.o
PROD_C_OBJS += $(BUILD_DIR)/prod/libonion_platform/net.o
PROD_C_OBJS += $(BUILD_DIR)/prod/libonion_platform/notify.o
PROD_C_OBJS += $(BUILD_DIR)/prod/libonion_platform/notify_i18n.o
PROD_C_OBJS += $(BUILD_DIR)/prod/libonion_ready/ready.o
PROD_C_OBJS += $(BUILD_DIR)/prod/libonion_payload/payload.o
PROD_C_OBJS += $(BUILD_DIR)/prod/third_party/pkgserver/multipart.o
PROD_C_OBJS += $(BUILD_DIR)/prod/libonion_detour/hde64.o

CHEAT_SUPPORT_SRCS := $(wildcard $(THIRD_PARTY_ROOT)/cheat_support/*.c)
CHEAT_SUPPORT_OBJS := $(patsubst $(THIRD_PARTY_ROOT)/cheat_support/%.c,$(BUILD_DIR)/prod/third_party/cheat_support/%.o,$(CHEAT_SUPPORT_SRCS))

PROD_CXX_OBJS := $(patsubst $(UTIL_ROOT)/%.cpp,$(BUILD_DIR)/prod/%.o,$(filter $(UTIL_ROOT)/%,$(PROD_CXX_SRCS)))
PROD_CXX_OBJS += $(BUILD_DIR)/prod/third_party/cjson.o
PROD_CXX_OBJS += $(BUILD_DIR)/prod/libonion_settings/settings.o
PROD_CXX_OBJS += $(BUILD_DIR)/prod/libonion_ipc/ipc_server.o
PROD_CXX_OBJS += $(BUILD_DIR)/prod/libonion_plugin_manager/plugin_descriptor.o
PROD_CXX_OBJS += $(BUILD_DIR)/prod/libonion_plugin_manager/plugin_manager.o
PROD_CXX_OBJS += $(BUILD_DIR)/prod/libonion_detour/hotpatch.o
PROD_CXX_OBJS += $(BUILD_DIR)/prod/libonion_detour/trampoline_arena.o
PROD_CXX_OBJS += $(BUILD_DIR)/prod/libonion_detour/x64_relocator.o
PROD_CXX_OBJS += $(BUILD_DIR)/prod/shellui/hook_lifecycle.o
PROD_CXX_OBJS += $(BUILD_DIR)/prod/shellui/ps5_settings_ui.o
PROD_CXX_OBJS += $(BUILD_DIR)/prod/shellui/toolbox_route.o
PROD_CXX_OBJS += $(BUILD_DIR)/prod/shellui/toolbox_i18n.o
PROD_CXX_OBJS += $(BUILD_DIR)/prod/shellui/toolbox_pkg_xml.o
PROD_CXX_OBJS += $(BUILD_DIR)/prod/libonion_fps/fps_formula.o
PROD_CXX_OBJS += $(BUILD_DIR)/prod/libonion_plugin_ui/plugin_ui.o
PROD_CXX_OBJS += $(BUILD_DIR)/prod/libonion_plugin_bridge/plugin_ui_bridge.o
PROD_CXX_OBJS += $(BUILD_DIR)/prod/libonion_plugin_session/plugin_session.o
PROD_CXX_OBJS += $(BUILD_DIR)/prod/daemon/plugin_ipc_server.o
PROD_CXX_OBJS += $(BUILD_DIR)/prod/daemon/plugin_ui_bridge_server.o
PROD_CXX_OBJS += $(BUILD_DIR)/prod/shellui/dynamic_ui_xml.o
PROD_CXX_OBJS += $(BUILD_DIR)/prod/shellui/dynamic_ui_runtime.o

TEST_C_OBJS := $(patsubst %.c,$(BUILD_DIR)/tests/%.o,$(TEST_C_SRCS))
TEST_CXX_OBJS := $(patsubst %.cpp,$(BUILD_DIR)/tests/%.o,$(TEST_CXX_SRCS))

ALL_OBJS := $(PROD_C_OBJS) $(CHEAT_SUPPORT_OBJS) $(PROD_CXX_OBJS) $(TEST_C_OBJS) $(TEST_CXX_OBJS)

# Soften third_party noise
THIRD_PARTY_FLAGS := -Wno-error -Wno-unused-function -Wno-sign-compare \
	-Wno-unused-parameter -Wno-missing-field-initializers

.PHONY: all test clean

all: $(BIN)

test: $(BIN)
	cd $(CURDIR) && ONION_TEST_ROOT=$(CURDIR) $(BIN)

$(BIN): $(ALL_OBJS) | $(BUILD_DIR)
	$(HOST_CXX) -o $@ $^ $(LDFLAGS)

$(BUILD_DIR)/prod/third_party/cheat_support/%.o: $(THIRD_PARTY_ROOT)/cheat_support/%.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(CFLAGS) $(THIRD_PARTY_FLAGS) -c -o $@ $<

$(BUILD_DIR)/prod/third_party/pkgserver/multipart.o: $(THIRD_PARTY_ROOT)/pkgserver/multipart.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/prod/source/cheats/cheat_engine_parser_shnext.o: $(UTIL_ROOT)/source/cheats/cheat_engine_parser_shnext.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(CFLAGS) -Wno-unused-function -c -o $@ $<

$(BUILD_DIR)/prod/%.o: $(UTIL_ROOT)/%.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/prod/%.o: $(UTIL_ROOT)/%.cpp | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/prod/third_party/cjson.o: $(THIRD_PARTY_ROOT)/cjson/cJSON.cpp | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(CXXFLAGS) $(THIRD_PARTY_FLAGS) -c -o $@ $<

$(BUILD_DIR)/prod/libonion_settings/settings.o: $(REPO_ROOT)/source/libonion_settings/source/settings.cpp | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/prod/libonion_ipc/ipc_server.o: $(REPO_ROOT)/source/libonion_ipc/source/ipc_server.cpp | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/prod/libonion_plugin_manager/%.o: $(REPO_ROOT)/source/libonion_plugin_manager/source/%.cpp | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/prod/libonion_detour/x64_relocator.o: $(REPO_ROOT)/source/libonion_detour/source/x64_relocator.cpp | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/prod/libonion_detour/hotpatch.o: $(REPO_ROOT)/source/libonion_detour/source/hotpatch.cpp | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/prod/libonion_detour/trampoline_arena.o: $(REPO_ROOT)/source/libonion_detour/source/trampoline_arena.cpp | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/prod/shellui/hook_lifecycle.o: $(REPO_ROOT)/source/shellui/src/hook_lifecycle.cpp | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/prod/shellui/ps5_settings_ui.o: $(REPO_ROOT)/source/shellui/src/ps5_settings_ui.cpp | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/prod/shellui/toolbox_route.o: $(REPO_ROOT)/source/shellui/src/toolbox_route.cpp | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/prod/shellui/toolbox_pkg_xml.o: $(REPO_ROOT)/source/shellui/src/toolbox_pkg_xml.cpp | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/prod/shellui/dynamic_ui_xml.o: $(REPO_ROOT)/source/shellui/src/dynamic_ui_xml.cpp | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/prod/shellui/dynamic_ui_runtime.o: $(REPO_ROOT)/source/shellui/src/dynamic_ui_runtime.cpp | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/prod/libonion_plugin_ui/plugin_ui.o: $(REPO_ROOT)/source/libonion_plugin_ui/source/plugin_ui.cpp | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/prod/libonion_plugin_bridge/plugin_ui_bridge.o: $(REPO_ROOT)/source/libonion_plugin_bridge/source/plugin_ui_bridge.cpp | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/prod/libonion_plugin_session/plugin_session.o: $(REPO_ROOT)/source/libonion_plugin_session/source/plugin_session.cpp | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/prod/daemon/plugin_ipc_server.o: $(REPO_ROOT)/source/daemon/source/plugin_ipc_server.cpp | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/prod/daemon/plugin_ui_bridge_server.o: $(REPO_ROOT)/source/daemon/source/plugin_ui_bridge_server.cpp | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(CXXFLAGS) -c -o $@ $<

$(I18N_CATALOG): $(I18N_DIR)/generate_catalog.py $(I18N_JSON) | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(PYTHON) $(I18N_DIR)/generate_catalog.py \
		--locales-dir $(I18N_DIR) \
		--catalog toolbox \
		--output $@

$(NOTIFY_I18N_CATALOG): $(I18N_DIR)/generate_catalog.py $(I18N_JSON) | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(PYTHON) $(I18N_DIR)/generate_catalog.py \
		--locales-dir $(I18N_DIR) \
		--catalog notifications \
		--output $@

$(BUILD_DIR)/prod/shellui/toolbox_i18n.o: $(REPO_ROOT)/source/shellui/src/toolbox_i18n.cpp $(I18N_CATALOG) | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/prod/libonion_fps/fps_formula.o: $(REPO_ROOT)/source/libonion_fps/source/fps_formula.cpp | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/prod/libonion_ready/ready.o: $(REPO_ROOT)/source/libonion_ready/source/ready.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/prod/libonion_platform/fs.o: $(REPO_ROOT)/source/libonion_platform/source/fs.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/prod/libonion_platform/log.o: $(REPO_ROOT)/source/libonion_platform/source/log.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/prod/libonion_platform/net.o: $(REPO_ROOT)/source/libonion_platform/source/net.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/prod/libonion_platform/notify.o: $(REPO_ROOT)/source/libonion_platform/source/notify.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/prod/libonion_platform/notify_i18n.o: $(REPO_ROOT)/source/libonion_platform/source/notify_i18n.c $(NOTIFY_I18N_CATALOG) | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/prod/libonion_payload/payload.o: $(REPO_ROOT)/source/libonion_payload/source/payload.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/prod/libonion_detour/hde64.o: $(REPO_ROOT)/source/libonion_detour/source/hde64.cpp | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/tests/%.o: %.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/tests/%.o: %.cpp | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)

# Header dependencies generated by -MMD; keeps rebuilds correct.
-include $(shell find $(BUILD_DIR) -name '*.d' 2>/dev/null)
