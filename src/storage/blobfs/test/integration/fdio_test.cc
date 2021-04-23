// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/test/integration/fdio_test.h"

#include <fuchsia/inspect/cpp/fidl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <lib/async/cpp/executor.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/inspect/cpp/hierarchy.h>
#include <lib/inspect/service/cpp/reader.h>

#include <fs-management/admin.h>

#include "src/storage/blobfs/mkfs.h"

namespace blobfs {

constexpr uint32_t kBlockSize = 512;
constexpr uint32_t kNumBlocks = 8192;

void FdioTest::SetUp() {
  loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);

  auto device = std::make_unique<block_client::FakeBlockDevice>(kNumBlocks, kBlockSize);
  block_device_ = device.get();
  ASSERT_EQ(FormatFilesystem(block_device_,
                             FilesystemOptions{
                                 .blob_layout_format = GetBlobLayoutFormat(),
                                 .oldest_minor_version = GetOldestMinorVersion(),
                             }),
            ZX_OK);

  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  ASSERT_EQ(endpoints.status_value(), ZX_OK);
  auto [export_root_client, export_root_server] = *std::move(endpoints);

  auto runner_or =
      Runner::Create(loop_.get(), std::move(device), mount_options_, std::move(vmex_resource_));
  ASSERT_TRUE(runner_or.is_ok());
  runner_ = std::move(runner_or.value());
  ASSERT_EQ(runner_->ServeRoot(std::move(export_root_server), ServeLayout::kExportDirectory),
            ZX_OK);
  ASSERT_EQ(loop_->StartThread("blobfs test dispatcher"), ZX_OK);

  zx::channel root_client;
  ASSERT_EQ(fs_root_handle(export_root_client.channel().get(), root_client.reset_and_get_address()),
            ZX_OK);

  // FDIO serving the root directory.
  ASSERT_EQ(fdio_fd_create(root_client.release(), root_fd_.reset_and_get_address()), ZX_OK);
  ASSERT_TRUE(root_fd_.is_valid());
  ASSERT_EQ(fdio_fd_create(export_root_client.TakeChannel().release(),
                           export_root_fd_.reset_and_get_address()),
            ZX_OK);
  ASSERT_TRUE(export_root_fd_.is_valid());
}

void FdioTest::TearDown() {
  zx::channel root_client;
  ASSERT_EQ(fdio_fd_transfer(root_fd_.release(), root_client.reset_and_get_address()), ZX_OK);
  ASSERT_EQ(fidl::WireCall(fidl::ClientEnd<fuchsia_io::DirectoryAdmin>(std::move(root_client)))
                .Unmount()
                .status(),
            ZX_OK);
}

zx_handle_t FdioTest::export_root() {
  zx::channel export_root;
  fdio_fd_clone(export_root_fd_.get(), export_root.reset_and_get_address());
  return export_root.release();
}

fit::result<inspect::Hierarchy> FdioTest::TakeSnapshot() {
  async::Loop loop = async::Loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  loop.StartThread("metric-collection-thread");
  async::Executor executor(loop.dispatcher());

  fuchsia::inspect::TreePtr tree;
  async_dispatcher_t* dispatcher = executor.dispatcher();
  zx_status_t status = fdio_service_connect_at(export_root(), "diagnostics/fuchsia.inspect.Tree",
                                               tree.NewRequest(dispatcher).TakeChannel().release());
  if (status != ZX_OK) {
    return fit::error();
  }

  std::condition_variable cv;
  std::mutex m;
  bool done = false;
  fit::result<inspect::Hierarchy> hierarchy_or_error;

  auto promise =
      inspect::ReadFromTree(std::move(tree)).then([&](fit::result<inspect::Hierarchy>& result) {
        {
          std::unique_lock<std::mutex> lock(m);
          hierarchy_or_error = std::move(result);
          done = true;
        }
        cv.notify_all();
      });

  executor.schedule_task(std::move(promise));

  std::unique_lock<std::mutex> lock(m);
  cv.wait(lock, [&done]() { return done; });

  loop.Quit();
  loop.JoinThreads();

  return hierarchy_or_error;
}

void FdioTest::GetUintMetricFromHierarchy(const inspect::Hierarchy& hierarchy,
                                          const std::vector<std::string>& path,
                                          const std::string& property, uint64_t* value) {
  ASSERT_NE(value, nullptr);
  const inspect::Hierarchy* direct_parent = hierarchy.GetByPath(path);
  ASSERT_NE(direct_parent, nullptr);

  const inspect::UintPropertyValue* property_node =
      direct_parent->node().get_property<inspect::UintPropertyValue>(property);
  ASSERT_NE(property_node, nullptr);

  *value = property_node->value();
}

void FdioTest::GetUintMetric(const std::vector<std::string>& path, const std::string& property,
                             uint64_t* value) {
  fit::result<inspect::Hierarchy> hierarchy_or_error = TakeSnapshot();
  ASSERT_TRUE(hierarchy_or_error.is_ok());
  GetUintMetricFromHierarchy(hierarchy_or_error.value(), path, property, value);
}

}  // namespace blobfs
