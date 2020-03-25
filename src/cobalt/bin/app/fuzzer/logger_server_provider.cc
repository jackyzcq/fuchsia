// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/cobalt/cpp/fidl.h>
#include <lib/fidl/cpp/fuzzing/server_provider.h>

#include <chrono>
#include <future>

#include "lib/async/default.h"
#include "lib/sys/cpp/component_context.h"
#include "src/cobalt/bin/app/logger_impl.h"
#include "src/cobalt/bin/app/timer_manager.h"
#include "src/cobalt/bin/utils/base64.h"
#include "third_party/cobalt/src/lib/util/posix_file_system.h"
#include "third_party/cobalt/src/public/cobalt_service.h"

// Source of cobalt::logger::kConfig
#include "third_party/cobalt/src/logger/internal_metrics_config.cb.h"

namespace {

::fidl::fuzzing::ServerProvider<::fuchsia::cobalt::Logger, ::cobalt::LoggerImpl>*
    fuzzer_server_provider;

cobalt::CobaltConfig cfg = {
    .file_system = std::make_unique<cobalt::util::PosixFileSystem>(),
    .use_memory_observation_store = true,
    .max_bytes_per_event = 100,
    .max_bytes_per_envelope = 100,
    .max_bytes_total = 1000,

    .local_aggregate_proto_store_path = "/tmp/local_agg",
    .obs_history_proto_store_path = "/tmp/obs_hist",

    .target_interval = std::chrono::seconds(10),
    .min_interval = std::chrono::seconds(10),
    .initial_interval = std::chrono::seconds(10),

    .target_pipeline = std::make_unique<cobalt::LocalPipeline>(),

    .api_key = "",
    .client_secret = cobalt::encoder::ClientSecret::GenerateNewSecret(),

    .local_aggregation_backfill_days = 4,
};

cobalt::CobaltService cobalt_service(std::move(cfg));

cobalt::TimerManager timer_manager(nullptr);

}  // namespace

// See https://fuchsia.dev/fuchsia-src/development/workflows/libfuzzer_fidl for explanations and
// documentations for these functions.
extern "C" {
zx_status_t fuzzer_init() {
  if (fuzzer_server_provider == nullptr) {
    fuzzer_server_provider =
        new ::fidl::fuzzing::ServerProvider<::fuchsia::cobalt::Logger, ::cobalt::LoggerImpl>(
            ::fidl::fuzzing::ServerProviderDispatcherMode::kFromCaller);
  }

  std::string config = cobalt::Base64Decode(
      // Generated by //third_party/cobalt/src/logger:internal_metrics_config
      cobalt::logger::kConfig);
  auto factory = std::make_shared<cobalt::logger::ProjectContextFactory>(config);
  return fuzzer_server_provider->Init(cobalt_service.NewLogger(factory->TakeSingleProjectContext()),
                                      &timer_manager);
}

zx_status_t fuzzer_connect(zx_handle_t channel_handle, async_dispatcher_t* dispatcher) {
  timer_manager.UpdateDispatcher(dispatcher);
  return fuzzer_server_provider->Connect(channel_handle, dispatcher);
}

zx_status_t fuzzer_disconnect(zx_handle_t channel_handle, async_dispatcher_t* dispatcher) {
  timer_manager.UpdateDispatcher(nullptr);
  return fuzzer_server_provider->Disconnect(channel_handle, dispatcher);
}

zx_status_t fuzzer_clean_up() { return fuzzer_server_provider->CleanUp(); }
}
