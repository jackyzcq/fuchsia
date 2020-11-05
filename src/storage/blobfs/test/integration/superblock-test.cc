// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>

#include <blobfs/format.h>
#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

#include "blobfs_fixtures.h"

namespace blobfs {
namespace {

using SuperblockTest = BlobfsTest;
using SuperblockTestWithFvm = BlobfsTestWithFvm;

void FsyncFilesystem(fs_test::TestFilesystem& fs) {
  // Open the root directory to fsync the filesystem.
  fbl::unique_fd fd_mount(open(fs.mount_path().c_str(), O_RDONLY));
  ASSERT_TRUE(fd_mount);
  ASSERT_EQ(fsync(fd_mount.get()), 0);
}

void ReadSuperblock(const std::string& device_path, Superblock* info) {
  fbl::unique_fd device(open(device_path.c_str(), O_RDWR));
  ASSERT_TRUE(device);
  ASSERT_EQ(kBlobfsBlockSize, pread(device.get(), info, kBlobfsBlockSize, 0));
}

void RunCheckDirtyBitOnMountTest(fs_test::TestFilesystem& fs) {
  Superblock info;

  ASSERT_NO_FATAL_FAILURE(FsyncFilesystem(fs));

  // Check if clean bit is unset.
  ReadSuperblock(fs.DevicePath().value(), &info);
  ASSERT_EQ(info.flags & kBlobFlagClean, 0u);

  // Unmount and check if clean bit is set.
  ASSERT_TRUE(fs.Unmount().is_ok());

  ReadSuperblock(fs.DevicePath().value(), &info);
  ASSERT_EQ(kBlobFlagClean, info.flags & kBlobFlagClean);
}

TEST_F(SuperblockTest, CheckDirtyBitOnMount) { RunCheckDirtyBitOnMountTest(fs()); }

TEST_F(SuperblockTestWithFvm, CheckDirtyBitOnMount) { RunCheckDirtyBitOnMountTest(fs()); }

}  // namespace
}  // namespace blobfs
