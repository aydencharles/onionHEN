/* Host tests for shared IPC protocol surface (msg.hpp + reply body). */
#include "test_harness.h"

#include <msg.hpp>
#include <onion/ipc_server.hpp>

#include <cstring>
#include <string>
#include <type_traits>

static int test_ipc_paths_and_magic(void) {
  TEST_ASSERT_STREQ("/tmp/onionhen/ipc/crit_service", CRIT_IPC_SOC);
  TEST_ASSERT_STREQ("/tmp/onionhen/ipc/util_service", UTIL_IPC_SOC);
  TEST_ASSERT_STREQ("/tmp/onionhen/ipc/plugin_service",
                    ONION_SYSTEM_TMP_PLUGIN_SOCKET);
  TEST_ASSERT_EQ_INT(0x1000, DAEMON_BUFF_MAX);

  IPCMessage msg{};
  TEST_ASSERT_EQ_INT(0xDEADBABE, msg.magic);
  TEST_ASSERT_EQ_INT(0, msg.error);
  TEST_ASSERT_EQ_U64(sizeof(msg.msg), (unsigned long long)DAEMON_BUFF_MAX);
  return 0;
}

static int test_crit_command_base_and_order(void) {
  TEST_ASSERT_EQ_U64(0x9000000u, static_cast<unsigned>(BREW_TEST_CONNECTION));
  TEST_ASSERT_EQ_U64(0x9000002u, static_cast<unsigned>(BREW_RETURN_VALUE));
  /* Original etaHEN ABI used by released Itemzflow binaries. */
  TEST_ASSERT_EQ_U64(0x9000003u, static_cast<unsigned>(BREW_REMOUNT_FOLDER));
  TEST_ASSERT_EQ_U64(0x9000004u,
                     static_cast<unsigned>(BREW_UNUSED_ACTIVATE_DUMPER));
  TEST_ASSERT_EQ_U64(0x9000005u, static_cast<unsigned>(BREW_STAT_CMD));
  TEST_ASSERT_EQ_U64(0x9000006u, static_cast<unsigned>(BREW_CALC_DIR_SIZE));
  TEST_ASSERT_EQ_U64(0x9000007u, static_cast<unsigned>(BREW_COPY_FILE));
  TEST_ASSERT_EQ_U64(0x9000008u, static_cast<unsigned>(BREW_COPY_DIR));
  TEST_ASSERT_EQ_U64(0x9000009u, static_cast<unsigned>(BREW_DELETE_DIR));
  TEST_ASSERT_EQ_U64(0x900000au, static_cast<unsigned>(BREW_UNUSED_1));
  TEST_ASSERT_EQ_U64(0x900000bu, static_cast<unsigned>(BREW_TEST_SB_FILE));
  TEST_ASSERT_EQ_U64(0x900000cu, static_cast<unsigned>(BREW_DAEMON_PID));
  TEST_ASSERT_EQ_U64(0x900000du,
                     static_cast<unsigned>(BREW_UNUSED_STORE_INSTALLER));
  TEST_ASSERT_EQ_U64(0x900000eu,
                     static_cast<unsigned>(BREW_UNUSED_DECRYPT_DIR));
  TEST_ASSERT_EQ_U64(0x900000fu, static_cast<unsigned>(BREW_LAST_RET));
  TEST_ASSERT_EQ_U64(0x9000010u,
                     static_cast<unsigned>(BREW_UNUSED_TESTKIT_CHECK));
  TEST_ASSERT_EQ_U64(0x9000011u, static_cast<unsigned>(BREW_ENABLE_TOOLBOX));
  TEST_ASSERT_EQ_U64(0x9000012u, static_cast<unsigned>(BREW_CHMOD_DIR));
  TEST_ASSERT_EQ_U64(0x9000013u,
                     static_cast<unsigned>(BREW_ADJUST_FAN_SPEED));
  return 0;
}

static int test_util_command_base(void) {
  TEST_ASSERT_EQ_U64(0x8000000u, static_cast<unsigned>(BREW_UTIL_TEST_CONNECTION));
  TEST_ASSERT_EQ_U64(0x8000002u, static_cast<unsigned>(BREW_UTIL_RETURN_VALUE));
  TEST_ASSERT_EQ_U64(0x8000004u,
                     static_cast<unsigned>(BREW_UTIL_UNUSED_FTP_TOGGLE));
  TEST_ASSERT_TRUE(static_cast<unsigned>(BREW_UTIL_TOGGLE_PKGNET) >
                   static_cast<unsigned>(BREW_UTIL_RETURN_VALUE));
  /* Retains the former DirectPKGInstaller toggle ordinal. */
  TEST_ASSERT_EQ_U64(0x8000006u,
                     static_cast<unsigned>(BREW_UTIL_TOGGLE_PKGNET));
  TEST_ASSERT_EQ_U64(0x8000010u,
                     static_cast<unsigned>(BREW_UTIL_CHEAT_SYNC_STATUS));
  TEST_ASSERT_EQ_U64(0x8000011u,
                     static_cast<unsigned>(BREW_UTIL_CANCEL_CHEAT_SYNC));
  TEST_ASSERT_EQ_U64(0x8000014u,
                     static_cast<unsigned>(BREW_UTIL_UNUSED_FTP_STATUS));
  TEST_ASSERT_EQ_U64(0x8000015u,
                     static_cast<unsigned>(BREW_UTIL_UNUSED_FTP_RECOVER));
  TEST_ASSERT_EQ_U64(0x8000016u,
                     static_cast<unsigned>(BREW_UTIL_TOGGLE_SHADOWMOUNT));
  TEST_ASSERT_EQ_U64(0x8000017u,
                     static_cast<unsigned>(BREW_UTIL_SHADOWMOUNT_STATUS));
  TEST_ASSERT_EQ_U64(0x8000018u,
                     static_cast<unsigned>(BREW_UTIL_PKGNET_STATUS));
  TEST_ASSERT_EQ_U64(0x8000019u,
                     static_cast<unsigned>(BREW_UTIL_SET_SYSTEM_LANG));
  return 0;
}

static int test_special_commands_stable(void) {
  /* Compatibility ordinals — do not renumber casually. */
  TEST_ASSERT_EQ_U64(0xE1F1D8u, static_cast<unsigned>(BREW_UTIL_LAUNCH_ELFLDR));
  TEST_ASSERT_EQ_U64(0xC0FFEEu, static_cast<unsigned>(BREW_RELOAD_SETTINGS));
  TEST_ASSERT_EQ_U64(0xDEAD0001u, static_cast<unsigned>(BREW_KILL_DAEMON));
  TEST_ASSERT_EQ_U64(0xDEAD0002u, static_cast<unsigned>(BREW_SHUTDOWN_STACK));
  TEST_ASSERT_EQ_U64(0xDEADCAFEu, static_cast<unsigned>(BREW_FORCE_KILL_PID));
  TEST_ASSERT_EQ_INT(9048, ONION_CTRL_TCP_PORT);
  TEST_ASSERT_EQ_U64(0x4F4E494Fu, static_cast<unsigned>(ONION_CTRL_TCP_MAGIC));
  return 0;
}

static int test_plugin_commands_stable(void) {
  TEST_ASSERT_EQ_U64(0x9000100u, static_cast<unsigned>(BREW_PLUGIN_LIST));
  TEST_ASSERT_EQ_U64(0x9000101u, static_cast<unsigned>(BREW_PLUGIN_START));
  TEST_ASSERT_EQ_U64(0x9000102u, static_cast<unsigned>(BREW_PLUGIN_STOP));
  TEST_ASSERT_EQ_U64(0x9000103u, static_cast<unsigned>(BREW_PLUGIN_RELOAD));
  TEST_ASSERT_EQ_U64(0x9000104u, static_cast<unsigned>(BREW_PLUGIN_DELETE));
  return 0;
}

static int test_ipc_ret_distinct(void) {
  TEST_ASSERT_EQ_INT(0, static_cast<int>(IPC_Ret::NO_ERROR));
  TEST_ASSERT_EQ_INT(-1, static_cast<int>(IPC_Ret::INVALID));
  TEST_ASSERT_EQ_INT(-2, static_cast<int>(IPC_Ret::OPERATION_FAILED));
  TEST_ASSERT_TRUE(static_cast<int>(IPC_Ret::INVALID) !=
                   static_cast<int>(IPC_Ret::OPERATION_FAILED));
  return 0;
}

static int test_message_pod_layout(void) {
  IPCMessage a{};
  a.cmd = BREW_ENABLE_TOOLBOX;
  a.error = 1;
  std::snprintf(a.msg, sizeof(a.msg), "%s", R"({"titleId":"ETAH00002"})");
  TEST_ASSERT_TRUE(std::strstr(a.msg, "ETAH00002") != nullptr);
  TEST_ASSERT_EQ_U64(static_cast<unsigned>(BREW_ENABLE_TOOLBOX),
                     static_cast<unsigned>(a.cmd));
  return 0;
}

static int test_ipc_format_reply_body(void) {
  using onion::ipc_format_reply_body;
  std::string ok = ipc_format_reply_body(false, "Nothing");
  std::string err = ipc_format_reply_body(true, "fail");
  std::string tid = ipc_format_reply_body(false, "CUSA12345");
  /* Compact JSON; var is escaped (no space after colon). */
  TEST_ASSERT_STREQ("{\"res\":0,\"var\":\"Nothing\"}", ok.c_str());
  TEST_ASSERT_STREQ("{\"res\":-1,\"var\":\"fail\"}", err.c_str());
  TEST_ASSERT_STREQ("{\"res\":0,\"var\":\"CUSA12345\"}", tid.c_str());
  return 0;
}

extern "C" int test_msg_protocol_suite(void) {
  int failures = 0;
  failures += onion_test_run("ipc_paths_magic", test_ipc_paths_and_magic);
  failures += onion_test_run("crit_cmd_base_order", test_crit_command_base_and_order);
  failures += onion_test_run("util_cmd_base", test_util_command_base);
  failures += onion_test_run("special_cmds_stable", test_special_commands_stable);
  failures += onion_test_run("plugin_cmds_stable", test_plugin_commands_stable);
  failures += onion_test_run("ipc_ret_distinct", test_ipc_ret_distinct);
  failures += onion_test_run("message_pod_layout", test_message_pod_layout);
  failures += onion_test_run("ipc_format_reply_body", test_ipc_format_reply_body);
  return failures;
}
