// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <fidl/fuchsia.boot/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/io.h>
#include <lib/fdio/spawn.h>
#include <lib/zx/debuglog.h>
#include <lib/zx/process.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/compiler.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <fs-management/mount.h>

namespace {

namespace fboot = fuchsia_boot;

void InitArgvAndActions(zx_handle_t* handles, uint32_t* types, size_t len,
                        fdio_spawn_action_t* actions_out) {
  for (size_t i = 0; i < len; ++i) {
    actions_out[i].action = FDIO_SPAWN_ACTION_ADD_HANDLE;
    actions_out[i].h.id = types[i];
    actions_out[i].h.handle = handles[i];
  }
}

constexpr size_t kMaxStdioActions = 1;

enum class StdioType {
  kLog,
  kClone,
  kNone,
};

zx_handle_t RetriveWriteOnlyDebuglogHandle() {
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    fprintf(stderr, "fs-management: Cannot create channel: %d (%s)\n", status,
            zx_status_get_string(status));
    return ZX_HANDLE_INVALID;
  }

  status = fdio_service_connect(fidl::DiscoverableProtocolDefaultPath<fboot::WriteOnlyLog>,
                                remote.release());
  if (status != ZX_OK) {
    fprintf(stderr, "fs-management: Failed to connect to WriteOnlyLog: %d (%s)\n", status,
            zx_status_get_string(status));
    return ZX_HANDLE_INVALID;
  }

  auto resp = fidl::WireCall<fboot::WriteOnlyLog>(zx::unowned(local)).Get();
  if (!resp.ok()) {
    fprintf(stderr, "fs-management: WriteOnlyLogGet failed: %d (%s)\n", resp.status(),
            zx_status_get_string(resp.status()));
    return ZX_HANDLE_INVALID;
  }

  return resp.value().log.release();
}

// Initializes Stdio.
//
// If necessary, updates the |actions| which will be sent to fdio_spawn.
// |action_count| is an in/out parameter which may be increased if an action is
// added.
// |flags| is an in/out parameter which may be modified to alter the cloning of
// STDIO.
void InitStdio(StdioType stdio, fdio_spawn_action_t* actions, size_t* action_count,
               uint32_t* flags) {
  switch (stdio) {
    case StdioType::kLog: {
      zx_handle_t h = RetriveWriteOnlyDebuglogHandle();
      if (h != ZX_HANDLE_INVALID) {
        actions[*action_count].action = FDIO_SPAWN_ACTION_ADD_HANDLE;
        actions[*action_count].h.id = PA_HND(PA_FD, FDIO_FLAG_USE_FOR_STDIO);
        actions[*action_count].h.handle = h;
        *action_count += 1;
      }
      *flags &= ~FDIO_SPAWN_CLONE_STDIO;
      break;
    }
    case StdioType::kClone:
      *flags |= FDIO_SPAWN_CLONE_STDIO;
      break;
    case StdioType::kNone:
      *flags &= ~FDIO_SPAWN_CLONE_STDIO;
      break;
  }
}

enum class ProcessAction {
  kBlock,
  kNonBlock,
};

// Spawns a process.
//
// Optionally blocks, waiting for the process to terminate, depending
// the value provided in |block|.
zx_status_t Spawn(ProcessAction proc_action, uint32_t flags, const char** argv, size_t action_count,
                  const fdio_spawn_action_t* actions) {
  zx::process proc;
  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  zx_status_t status = fdio_spawn_etc(ZX_HANDLE_INVALID, flags, argv[0], argv, nullptr,
                                      action_count, actions, proc.reset_and_get_address(), err_msg);
  if (status != ZX_OK) {
    fprintf(stderr, "fs-management: Cannot spawn %s: %d (%s): %s\n", argv[0], status,
            zx_status_get_string(status), err_msg);
    return status;
  }

  if (proc_action == ProcessAction::kBlock) {
    status = proc.wait_one(ZX_PROCESS_TERMINATED, zx::time::infinite(), nullptr);
    if (status != ZX_OK) {
      fprintf(stderr, "spawn: Error waiting for process to terminate\n");
      return status;
    }

    zx_info_process_t info;
    status = proc.get_info(ZX_INFO_PROCESS, &info, sizeof(info), nullptr, nullptr);
    if (status != ZX_OK) {
      fprintf(stderr, "spawn: Failed to get process info\n");
      return status;
    }

    if (!(info.flags & ZX_INFO_PROCESS_FLAG_EXITED) || info.return_code != 0) {
      return ZX_ERR_BAD_STATE;
    }
  }
  return ZX_OK;
}

zx_status_t Launch(StdioType stdio, ProcessAction proc_action, int argc, const char** argv,
                   zx_handle_t* handles, uint32_t* types, size_t len) {
  fdio_spawn_action_t actions[len + kMaxStdioActions];
  InitArgvAndActions(handles, types, len, actions);

  size_t action_count = len;
  uint32_t flags = FDIO_SPAWN_CLONE_ALL;
  InitStdio(stdio, actions, &action_count, &flags);

  return Spawn(proc_action, flags, argv, action_count, actions);
}

}  // namespace

__EXPORT
zx_status_t launch_silent_sync(int argc, const char** argv, zx_handle_t* handles, uint32_t* types,
                               size_t len) {
  return Launch(StdioType::kNone, ProcessAction::kBlock, argc, argv, handles, types, len);
}

__EXPORT
zx_status_t launch_silent_async(int argc, const char** argv, zx_handle_t* handles, uint32_t* types,
                                size_t len) {
  return Launch(StdioType::kNone, ProcessAction::kNonBlock, argc, argv, handles, types, len);
}

__EXPORT
zx_status_t launch_stdio_sync(int argc, const char** argv, zx_handle_t* handles, uint32_t* types,
                              size_t len) {
  return Launch(StdioType::kClone, ProcessAction::kBlock, argc, argv, handles, types, len);
}

__EXPORT
zx_status_t launch_stdio_async(int argc, const char** argv, zx_handle_t* handles, uint32_t* types,
                               size_t len) {
  return Launch(StdioType::kClone, ProcessAction::kNonBlock, argc, argv, handles, types, len);
}

__EXPORT
zx_status_t launch_logs_async(int argc, const char** argv, zx_handle_t* handles, uint32_t* types,
                              size_t len) {
  return Launch(StdioType::kLog, ProcessAction::kNonBlock, argc, argv, handles, types, len);
}
