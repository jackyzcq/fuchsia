// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.boot/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <lib/service/llcpp/service.h>
#include <lib/zxio/cpp/inception.h>
#include <lib/zxio/zxio.h>

#include <array>
#include <thread>
#include <utility>

#include <zxtest/zxtest.h>

#include "sdk/lib/zxio/private.h"

namespace {

zx::status<zx::debuglog> GetDebugLogHandle() {
  zx::status log_service = service::Connect<fuchsia_boot::WriteOnlyLog>();
  if (!log_service.is_ok()) {
    return zx::error(log_service.status_value());
  }
  auto response = fidl::WireCall(*log_service).Get();
  if (!response.ok()) {
    return zx::error(response.status());
  }
  return zx::ok(zx::debuglog(std::move(response.value().log)));
}

TEST(DebugLog, Create) {
  zx::status log = GetDebugLogHandle();
  ASSERT_OK(log.status_value());
  zxio_storage_t storage;
  ASSERT_OK(zxio_create(log->release(), &storage));
  zxio_t* io = &storage.io;

  ASSERT_OK(zxio_close(io));
}

class DebugLogTest : public zxtest::Test {
 protected:
  void SetUp() override final {
    zx::status log = GetDebugLogHandle();
    ASSERT_OK(log.status_value());

    storage_ = std::make_unique<zxio_storage_t>();
    ASSERT_OK(zxio_debuglog_init(storage_.get(), std::move(*log)));
    logger_ = &storage_->io;
    ASSERT_NE(logger_, nullptr);
  }

  void TearDown() override final { ASSERT_OK(zxio_close(logger_)); }

  zx::channel local_;
  zx::channel remote_;
  std::unique_ptr<zxio_storage_t> storage_;
  zxio_t* logger_;
};

constexpr size_t kNumThreads = 256;

TEST_F(DebugLogTest, ThreadSafety) {
  std::array<std::thread, kNumThreads> threads;

  for (size_t i = 0; i < kNumThreads; ++i) {
    threads[i] = std::thread([=]() {
      std::string log_str = "output from " + std::to_string(i) + "\n";
      size_t actual;
      ASSERT_OK(zxio_write(logger_, log_str.c_str(), log_str.size(), 0, &actual));
      ASSERT_EQ(actual, log_str.size());
    });
  }

  for (auto& t : threads) {
    t.join();
  }
}

}  // namespace
