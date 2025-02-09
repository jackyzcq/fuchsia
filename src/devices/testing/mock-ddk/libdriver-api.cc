// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>
#include <lib/syslog/logger.h>
#include <stdarg.h>
#include <stdlib.h>
#include <zircon/assert.h>
#include <zircon/types.h>

#include <utility>

#include "src/devices/testing/mock-ddk/mock-device.h"

namespace mock_ddk {

fx_log_severity_t kMinLogSeverity = FX_LOG_INFO;

}  // namespace mock_ddk

// Checks to possibly keep:
// InitReply:
//   If the init fails, the device should be automatically unbound and removed.
// AsyncRemove
//   We should not call unbind until the init hook has been replied to.

__EXPORT
zx_status_t device_add_from_driver(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                                   zx_device_t** out) {
  return MockDevice::Create(args, parent, out);
}

// These calls are not supported by root parent devices:
__EXPORT
void device_async_remove(zx_device_t* device) {
  if (!device) {
    zxlogf(ERROR, "Error: %s passed an null device\n", __func__);
    return;
  }
  if (device->IsRootParent()) {
    zxlogf(ERROR, "Error: Mock parent device does not support %s\n", __func__);
    return;
  }
  device->RecordAsyncRemove(ZX_OK);
}

__EXPORT
void device_init_reply(zx_device_t* device, zx_status_t status,
                       const device_init_reply_args_t* args) {
  if (!device) {
    zxlogf(ERROR, "Error: %s passed an null device\n", __func__);
    return;
  }
  if (device->IsRootParent()) {
    zxlogf(ERROR, "Error: Mock parent device does not support %s\n", __func__);
    return;
  }
  device->RecordInitReply(status);
}

__EXPORT
void device_unbind_reply(zx_device_t* device) {
  if (!device) {
    zxlogf(ERROR, "Error: %s passed an null device\n", __func__);
    return;
  }
  if (device->IsRootParent()) {
    zxlogf(ERROR, "Error: Mock parent device does not support %s\n", __func__);
    return;
  }
  device->RecordUnbindReply(ZX_OK);
}

__EXPORT void device_suspend_reply(zx_device_t* device, zx_status_t status, uint8_t out_state) {
  if (!device) {
    zxlogf(ERROR, "Error: %s passed an null device\n", __func__);
    return;
  }
  if (device->IsRootParent()) {
    zxlogf(ERROR, "Error: Mock parent device does not support %s\n", __func__);
    return;
  }
  device->RecordSuspendReply(status);
}

__EXPORT void device_resume_reply(zx_device_t* device, zx_status_t status, uint8_t out_power_state,
                                  uint32_t out_perf_state) {
  if (!device) {
    zxlogf(ERROR, "Error: %s passed an null device\n", __func__);
    return;
  }
  if (device->IsRootParent()) {
    zxlogf(ERROR, "Error: Mock parent device does not support %s\n", __func__);
    return;
  }
  device->RecordResumeReply(status);
}

__EXPORT
const char* device_get_name(zx_device_t* device) {
  if (!device) {
    zxlogf(ERROR, "Error: %s passed an null device\n", __func__);
    return nullptr;
  }
  if (device->IsRootParent()) {
    zxlogf(ERROR, "Error: Mock parent device does not support %s\n", __func__);
    return nullptr;
  }
  return device->name();
}

// These functions TODO(will be) supported by devices created as root parents:
__EXPORT
zx_status_t device_get_protocol(const zx_device_t* device, uint32_t proto_id, void* protocol) {
  if (!device) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  return device->GetProtocol(proto_id, protocol);
}

__EXPORT
zx_status_t device_add_metadata(zx_device_t* device, uint32_t type, const void* data,
                                size_t length) {
  return ZX_ERR_NOT_SUPPORTED;
}

__EXPORT
zx_status_t device_get_metadata(zx_device_t* device, uint32_t type, void* buf, size_t buflen,
                                size_t* actual) {
  return device->GetMetadata(type, buf, buflen, actual);
}

__EXPORT
zx_status_t device_get_metadata_size(zx_device_t* device, uint32_t type, size_t* out_size) {
  return device->GetMetadataSize(type, out_size);
}

__EXPORT zx_status_t device_get_fragment_protocol(zx_device_t* device, const char* name,
                                                  uint32_t proto_id, void* protocol) {
  if (!device) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  return device->GetProtocol(proto_id, protocol, name);
}

__EXPORT
zx_status_t device_get_fragment_metadata(zx_device_t* device, const char* name, uint32_t type,
                                         void* buf, size_t buflen, size_t* actual) {
  if (!device) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  return device_get_metadata(device, type, buf, buflen, actual);
}

// Unsupported calls:
__EXPORT
zx_status_t device_open_protocol_session_multibindable(const zx_device_t* dev, uint32_t proto_id,
                                                       void* protocol) {
  return ZX_ERR_NOT_SUPPORTED;
}

__EXPORT
zx_off_t device_get_size(zx_device_t* device) { return device->GetSize(); }

__EXPORT
void device_state_clr_set(zx_device_t* dev, zx_signals_t clearflag, zx_signals_t setflag) {
  // This is currently a no-op.
}

__EXPORT
zx_status_t device_get_profile(zx_device_t* device, uint32_t priority, const char* name,
                               zx_handle_t* out_profile) {
  // This is currently a no-op.
  *out_profile = ZX_HANDLE_INVALID;
  return ZX_OK;
}

__EXPORT
zx_status_t device_get_deadline_profile(zx_device_t* device, uint64_t capacity, uint64_t deadline,
                                        uint64_t period, const char* name,
                                        zx_handle_t* out_profile) {
  // This is currently a no-op.
  *out_profile = ZX_HANDLE_INVALID;
  return ZX_OK;
}

__EXPORT
void device_fidl_transaction_take_ownership(fidl_txn_t* txn, device_fidl_txn_t* new_txn) {
  // auto fidl_txn = mock_ddk::FromDdkInternalTransaction(ddk::internal::Transaction::FromTxn(txn));

  // ZX_ASSERT_MSG(std::holds_alternative<fidl::Transaction*>(fidl_txn),
  // "Can only take ownership of transaction once\n");

  // auto result = std::get<fidl::Transaction*>(fidl_txn)->TakeOwnership();
  // We call this to mimic what devhost does.
  // result->EnableNextDispatch();
  // auto new_ddk_txn = mock_ddk::MakeDdkInternalTransaction(std::move(result));
  // *new_txn = *new_ddk_txn.DeviceFidlTxn();
}

__EXPORT __WEAK zx_status_t load_firmware_from_driver(zx_driver_t* drv, zx_device_t* device,
                                                      const char* path, zx_handle_t* fw,
                                                      size_t* size) {
  if (!device) {
    zxlogf(ERROR, "Error: %s passed an null device\n", __func__);
    return ZX_ERR_INVALID_ARGS;
  }
  return device->LoadFirmware(path, fw, size);
}

__EXPORT
zx_status_t device_rebind(zx_device_t* device) { return ZX_ERR_NOT_SUPPORTED; }

__EXPORT
zx_handle_t get_root_resource() { return ZX_HANDLE_INVALID; }

__EXPORT zx_status_t driver_log_set_tags_internal(const zx_driver_t* drv, const char* const* tags,
                                                  size_t num_tags) {
  return ZX_ERR_NOT_SUPPORTED;
}

extern "C" bool driver_log_severity_enabled_internal(const zx_driver_t* drv,
                                                     fx_log_severity_t flag) {
  return flag >= mock_ddk::kMinLogSeverity;
}

extern "C" void driver_logvf_internal(const zx_driver_t* drv, fx_log_severity_t flag,
                                      const char* tag, const char* file, int line, const char* msg,
                                      va_list args) {
  vfprintf(stdout, msg, args);
  putchar('\n');
  fflush(stdout);
}

extern "C" void driver_logf_internal(const zx_driver_t* drv, fx_log_severity_t flag,
                                     const char* tag, const char* file, int line, const char* msg,
                                     ...) {
  va_list args;
  va_start(args, msg);
  driver_logvf_internal(drv, flag, tag, file, line, msg, args);
  va_end(args);
}

__EXPORT
__WEAK zx_driver_rec __zircon_driver_rec__ = {
    .ops = {},
    .driver = {},
    .log_flags = 0,
};
