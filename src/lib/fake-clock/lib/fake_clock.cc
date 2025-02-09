// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.testing.deadline/cpp/wire.h>
#include <fidl/fuchsia.testing/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/port.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/port.h>

#include <thread>

#include <src/lib/fake-clock/named-timer/named_timer.h>

namespace fake_clock = fuchsia_testing;
namespace fake_clock_deadline = fuchsia_testing_deadline;

namespace {
zx::unowned_channel GetService() {
  static std::once_flag svc_connect_once;
  static zx::channel fake_clock;

  std::call_once(svc_connect_once, []() {
    if (!fake_clock.is_valid()) {
      zx::channel req;
      if (zx::channel::create(0, &fake_clock, &req) != ZX_OK) {
        FX_LOGS(ERROR) << "Failed to create channel to FakeClock service";
        fake_clock.reset();
        return;
      }
      if (fdio_service_connect("/svc/fuchsia.testing.FakeClock", req.release()) != ZX_OK) {
        FX_LOGS(ERROR) << "Failed to connect to fuchsia.testing.FakeClock service";
        fake_clock.reset();
        return;
      }
    }
  });
  return zx::unowned_channel(fake_clock);
}

zx::eventpair MakeEvent(zx_time_t deadline) {
  zx::eventpair l, r;
  ZX_ASSERT(zx::eventpair::create(0, &l, &r) == ZX_OK);
  ZX_ASSERT(fidl::WireCall<fake_clock::FakeClock>(GetService())
                .RegisterEvent(std::move(r), deadline)
                .ok());
  return l;
}

}  // namespace

__EXPORT zx_status_t zx_futex_wait(const zx_futex_t* value_ptr, zx_futex_t current_value,
                                   zx_handle_t new_futex_owner, zx_time_t deadline) {
  ZX_ASSERT_MSG(deadline == ZX_TIME_INFINITE,
                "zx_futex_wait with deadline is currently supported by FakeClock library");
  return _zx_futex_wait(value_ptr, current_value, new_futex_owner, deadline);
}

__EXPORT zx_status_t zx_channel_call(zx_handle_t handle, uint32_t options, zx_time_t deadline,
                                     const zx_channel_call_args_t* args, uint32_t* actual_bytes,
                                     uint32_t* actual_handles) {
  // TODO(brunodalbo) There may be a way to get channel_call working if we create a temporary
  // channel and an auxiliary thread, but looks like most channel_call call sites don't define
  // deadlines.
  ZX_ASSERT_MSG(deadline == ZX_TIME_INFINITE,
                "zx_channel_call with deadline is not yet supported by FakeClock library");
  return _zx_channel_call(handle, options, deadline, args, actual_bytes, actual_handles);
}

__EXPORT zx_time_t zx_clock_get_monotonic() {
  auto result = fidl::WireCall<fake_clock::FakeClock>(GetService()).Get();
  ZX_ASSERT(result.ok());
  return result.value().time;
}

__EXPORT zx_time_t zx_deadline_after(zx_duration_t duration) {
  return zx_time_add_duration(zx_clock_get_monotonic(), duration);
}

__EXPORT zx_status_t zx_nanosleep(zx_time_t deadline) {
  auto e = MakeEvent(deadline);
  ZX_ASSERT(_zx_object_wait_one(e.get(), ZX_EVENTPAIR_SIGNALED, ZX_TIME_INFINITE, nullptr) ==
            ZX_OK);
  return ZX_OK;
}

// wait_one is implemented by making it a wait_many on an infinite deadline with two items: one is
// the original handle+signals, the other is the eventpair created from the fake-clock service.
__EXPORT zx_status_t zx_object_wait_one(zx_handle_t handle, zx_signals_t signals,
                                        zx_time_t deadline, zx_signals_t* observed) {
  if (deadline == ZX_TIME_INFINITE || deadline == 0) {
    return _zx_object_wait_one(handle, signals, deadline, observed);
  }
  auto e = MakeEvent(deadline);
  zx_wait_item_t items[] = {{.handle = e.get(), .waitfor = ZX_EVENTPAIR_SIGNALED, .pending = 0},
                            {.handle = handle, .waitfor = signals, .pending = 0}};

  auto status = _zx_object_wait_many(items, 2, ZX_TIME_INFINITE);
  if (observed) {
    *observed = items[1].pending;
  }
  if (status != ZX_OK || (items[0].pending & ZX_EVENTPAIR_SIGNALED) == 0) {
    return status;
  } else {
    return ZX_ERR_TIMED_OUT;
  }
}

// wait_many is implemented by adding an extra eventpair handle extracted from fake-clock to the
// wait list, and changing the deadline to infinite. If the number of items on the wait is already
// ZX_WAIT_MANY_MAX_ITEMS (meaning we can't add an extra item), we create a port instead and
// register all the wait items to it.
__EXPORT zx_status_t zx_object_wait_many(zx_wait_item_t* items, size_t num_items,
                                         zx_time_t deadline) {
  if (deadline == ZX_TIME_INFINITE || deadline == 0 || num_items > ZX_WAIT_MANY_MAX_ITEMS) {
    return _zx_object_wait_many(items, num_items, deadline);
  } else if (num_items == ZX_WAIT_MANY_MAX_ITEMS) {
    // can't add a new item, we need to build a port and wait on it.
    zx::port port;
    ZX_ASSERT(zx::port::create(0, &port) == ZX_OK);
    zx_status_t status;
    for (size_t i = 0; i < num_items; i++) {
      if ((status = zx_object_wait_async(items[i].handle, port.get(), i, items[i].waitfor, 0)) !=
          ZX_OK) {
        return status;
      }
    }
    auto event = MakeEvent(deadline);
    ZX_ASSERT(zx_object_wait_async(event.get(), port.get(), num_items, ZX_EVENTPAIR_SIGNALED, 0) ==
              ZX_OK);

    auto update_item = [&items, num_items](const zx_port_packet& packet) {
      if (packet.key == num_items) {
        if (packet.signal.observed & ZX_EVENTPAIR_SIGNALED) {
          return true;
        }
      } else {
        auto* item = &items[packet.key];
        item->pending = packet.signal.observed;
      }
      return false;
    };

    zx_port_packet_t packet;
    if ((status = port.wait(zx::time::infinite(), &packet)) != ZX_OK) {
      return status;
    }
    // update_item will return true if the first packet out of the port is a timeout.
    if (update_item(packet)) {
      return ZX_ERR_TIMED_OUT;
    }
    // many things may have happened at once, how we just keep polling the port with a zero deadline
    // and updating the items
    while (port.wait(zx::time(0), &packet) == ZX_OK) {
      if (update_item(packet)) {
        break;
      }
    }
    return ZX_OK;
  } else {
    // we can just add an extra item, but we'll need to copy all the wait items
    zx_wait_item_t tmp[ZX_WAIT_MANY_MAX_ITEMS];
    memcpy(tmp, items, num_items * sizeof(zx_wait_item_t));
    auto event = MakeEvent(deadline);
    tmp[num_items].pending = 0;
    tmp[num_items].waitfor = ZX_EVENTPAIR_SIGNALED;
    tmp[num_items].handle = event.get();
    auto status = _zx_object_wait_many(tmp, num_items + 1, ZX_TIME_INFINITE);
    // copy everything back:
    memcpy(items, tmp, num_items * sizeof(zx_wait_item_t));
    if (status != ZX_OK || (tmp[num_items].pending & ZX_EVENTPAIR_SIGNALED) == 0) {
      return status;
    } else {
      return ZX_ERR_TIMED_OUT;
    }
  }
}

// port_wait adds an extra fake-clock eventpair handle to the port and changes the deadline to
// ZX_TIME_INFINITE.
__EXPORT zx_status_t zx_port_wait(zx_handle_t handle, zx_time_t deadline,
                                  zx_port_packet_t* packet) {
  if (deadline == ZX_TIME_INFINITE) {
    return _zx_port_wait(handle, deadline, packet);
  }

  auto event = MakeEvent(deadline);
  uint64_t key = 0xFACEFACE00000000 | event.get();
  ZX_ASSERT(zx_object_wait_async(event.get(), handle, key, ZX_EVENTPAIR_SIGNALED, 0) == ZX_OK);
  zx_port_packet_t tmp;
  auto status = _zx_port_wait(handle, ZX_TIME_INFINITE, &tmp);
  // always cancel the wait in case it wasn't a timeout
  zx_port_cancel(handle, event.get(), key);
  if (status != ZX_OK) {
    return status;
  } else if (tmp.type == ZX_PKT_TYPE_SIGNAL_ONE && tmp.key == key &&
             tmp.signal.observed == ZX_EVENTPAIR_SIGNALED) {
    return ZX_ERR_TIMED_OUT;
  } else {
    *packet = tmp;
    return ZX_OK;
  }
}

// timer_create changes the type of returned handle from an actual timer to one side of an eventpair
// created by fake-clock. It relies on the fact that ZX_EVENTPAIR_SIGNALED is the same bit as
// ZX_TIMER_SIGNALED, meaning unless clients are inspecting the handle type, they shouldn't be able
// to tell the difference.
__EXPORT zx_status_t zx_timer_create(uint32_t options, zx_clock_t clock_id, zx_handle_t* out) {
  // We're replacing a timer with an event, and shamelessly using the fact that
  // ZX_EVENTPAIR_SIGNALED and ZX_TIMER_SIGNAL collide, this assertion protects that assumption more
  // strongly.
  static_assert(ZX_EVENTPAIR_SIGNALED == ZX_TIMER_SIGNALED);
  if (clock_id != ZX_CLOCK_MONOTONIC) {
    // NOTE: _zx_timer_create will just fail according to the docs.
    return _zx_timer_create(options, clock_id, out);
  }
  // Create an event with infinite deadline and return that instead of a timer handle
  *out = MakeEvent(ZX_TIME_INFINITE).release();
  return ZX_OK;
}

__EXPORT zx_status_t zx_timer_set(zx_handle_t handle, zx_time_t deadline, zx_duration_t slack) {
  zx::eventpair e;
  zx_status_t status;
  if ((status = zx_handle_duplicate(handle, ZX_RIGHT_SAME_RIGHTS, e.reset_and_get_address())) !=
      ZX_OK) {
    return status;
  }
  // reschedule the event with the fake clock service:
  ZX_ASSERT(fidl::WireCall<fake_clock::FakeClock>(GetService())
                .RescheduleEvent(std::move(e), deadline)
                .ok());
  return ZX_OK;
}

__EXPORT zx_status_t zx_timer_cancel(zx_handle_t handle) {
  zx::eventpair e;
  zx_status_t status;
  if ((status = zx_handle_duplicate(handle, ZX_RIGHT_SAME_RIGHTS, e.reset_and_get_address())) !=
      ZX_OK) {
    return status;
  }
  ZX_ASSERT(fidl::WireCall<fake_clock::FakeClock>(GetService()).CancelEvent(std::move(e)).ok());
  return ZX_OK;
}

__EXPORT bool create_named_deadline(char* component, size_t component_len, char* code,
                                    size_t code_len, zx_time_t duration, zx_time_t* out) {
  fake_clock_deadline::wire::DeadlineId id;
  id.component_id = fidl::StringView::FromExternal(component, component_len);
  id.code = fidl::StringView::FromExternal(code, code_len);
  auto result = fidl::WireCall<fake_clock::FakeClock>(GetService())
                    .CreateNamedDeadline(std::move(id), duration);
  ZX_ASSERT(result.ok());
  *out = result->deadline;
  return true;
}
