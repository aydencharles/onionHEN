/* Copyright (C) 2025 OnionHEN / LightningMods — P0 split. */


#include "hooked_funcs.hpp"
#include "ipc.hpp"
#include <string>

#include "shellui_state.hpp"

void *load_payload_thread(void *args) {
  PayloadEntry *entry = (PayloadEntry *)args;

  notify("notify.payload.loading", entry->path.c_str());
  IPC_Client &util_ipc = IPC_Client::getInstance(true);
  if (util_ipc.LaunchPayload(entry->shellui_path, entry->tid) !=
      IPC_Ret::NO_ERROR) {
    notify("notify.payload.launch_failed", entry->path.c_str(),
           entry->tid.c_str());
  }

  delete entry;
  return nullptr;
}
