/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Extracted from mono_utils.cpp for module locality.
 */

#include "hooked_funcs.hpp"
#include <onion/proc_query.h>
#include <onion/platform.h>
#include "ipc.hpp" // shellui_log + IPC_Client
#include "external_symbols.hpp"
#include "proc.h"
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <unistd.h>
#include <fcntl.h>
#include <sys/sysctl.h>
#include <string>

extern "C" {
int sceShellCoreUtilIsUsbMassStorageMounted(int num);
}

int usbpath()
{
    int usb_index = -1;
    for (int i = 0; i < 8; i++) {
        if (sceShellCoreUtilIsUsbMassStorageMounted((unsigned int)i)) {
            usb_index = i;
            break;
        }
    }
    return usb_index;
}



bool write_asset(const char *path, const void *start, uint32_t size)
{
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0777);
  if (fd < 0)
  {
    LOG_ERROR("failed to create trainer for %s | error: %s", path, strerror(errno));
    return false;
  }
  ssize_t written = write(fd, start, size);
  close(fd);
  if (written < 0)
  {
    LOG_ERROR("Failed to write trainer for %s | size %u, error: %s", path, size, strerror(errno));
    return false;
  }
  else if ((unsigned int)written != size)
  {
    LOG_DEBUG("incomplete write: expected %u bytes, wrote %zd bytes", size, written);
    return false;
  }
  return true;
}

std::string remove_ps5_suffix(const std::string& filename) {
    size_t pos = filename.find("-ps5");
    if (pos == std::string::npos) {
        return filename; // No "-ps5" found, return copy
    }
    
    return filename.substr(0, pos) + filename.substr(pos + 4);
}

int sceSystemServiceGetAppId(const char * tid){
   // LOG_DEBUG("looking for tid %s", tid);
    pid_t success = onion_find_pid_ex(tid, false, false);
    if(success < 0){
       success = onion_find_pid_ex(remove_ps5_suffix(tid).c_str(), false, false);
    }
    return success;
}

void KillAllWithName(const char * name, int signal){
    int pid = -1;
    IPC_Client& main_ipc = IPC_Client::getInstance(false);
    while ((pid = onion_find_pid_ex(name, true, false)) > 0) {
        main_ipc.ForceKillPID(pid);
    }
}

bool Get_Running_App_TID(std::string &title_id, int &BigAppid)
{
  char tid[255];
  BigAppid = sceSystemServiceGetAppIdOfRunningBigApp();
  if (BigAppid < 0)
  {
   // LOG_ERROR("Failed to get bigapp id 0x%x", BigAppid);
    return false;
  }
  (void)memset(tid, 0, sizeof tid);

  if (sceSystemServiceGetAppTitleId(BigAppid, &tid[0]) != 0)
  {
    //LOG_ERROR("Failed to get title id for bigapp id 0x%x", BigAppid);
    return false;
  }

  title_id = std::string(tid);

  return true;
}

