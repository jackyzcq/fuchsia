// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/fdio/fd.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include "src/storage/fs_test/fs_test_fixture.h"
#include "src/storage/minfs/format.h"

namespace fs_test {
namespace {

using BasicTest = FilesystemTest;

TEST_P(BasicTest, Basic) {
  ASSERT_EQ(mkdir(GetPath("alpha").c_str(), 0755), 0);
  ASSERT_EQ(mkdir(GetPath("alpha/bravo").c_str(), 0755), 0);
  ASSERT_EQ(mkdir(GetPath("alpha/bravo/charlie").c_str(), 0755), 0);
  ASSERT_EQ(mkdir(GetPath("alpha/bravo/charlie/delta").c_str(), 0755), 0);
  ASSERT_EQ(mkdir(GetPath("alpha/bravo/charlie/delta/echo").c_str(), 0755), 0);
  int fd1 = open(GetPath("alpha/bravo/charlie/delta/echo/foxtrot").c_str(), O_RDWR | O_CREAT, 0644);
  ASSERT_GT(fd1, 0);
  int fd2 = open(GetPath("alpha/bravo/charlie/delta/echo/foxtrot").c_str(), O_RDWR, 0644);
  ASSERT_GT(fd2, 0);

  std::string input("Hello, World!\n");
  ASSERT_EQ(write(fd1, input.c_str(), input.length()), static_cast<ssize_t>(input.length()));
  ASSERT_EQ(close(fd1), 0);

  char output[input.length() + 1];
  output[input.length()] = '\0';
  ASSERT_EQ(pread(fd2, output, input.length(), 0), static_cast<ssize_t>(input.length()));
  ASSERT_EQ(std::string(output, input.length()), input);
  ASSERT_EQ(close(fd2), 0);

  fd1 = open(GetPath("file.txt").c_str(), O_CREAT | O_RDWR, 0644);
  ASSERT_GT(fd1, 0);
  ASSERT_EQ(close(fd1), 0);

  ASSERT_EQ(unlink(GetPath("file.txt").c_str()), 0);
  ASSERT_EQ(mkdir(GetPath("emptydir").c_str(), 0755), 0);
  fd1 = open(GetPath("emptydir").c_str(), O_RDONLY, 0644);
  ASSERT_GT(fd1, 0);

  // Zero-sized reads should always succeed
  ASSERT_EQ(read(fd1, nullptr, 0), 0);
  // But nonzero reads to directories should always fail
  char buf;
  ASSERT_EQ(read(fd1, &buf, 1), -1);
  ASSERT_EQ(write(fd1, "Don't write to directories", 26), -1);
  ASSERT_EQ(ftruncate(fd1, 0), -1);
  ASSERT_EQ(rmdir(GetPath("emptydir").c_str()), 0);
  ASSERT_EQ(rmdir(GetPath("emptydir").c_str()), -1);
  ASSERT_EQ(close(fd1), 0);
  ASSERT_EQ(rmdir(GetPath("emptydir").c_str()), -1);
}

TEST_P(BasicTest, UncleanClose) {
  int fd = open(GetPath("foobar").c_str(), O_CREAT | O_RDWR);
  ASSERT_GT(fd, 0);

  // Try closing a connection to a file with an "unclean" shutdown,
  // noticed by the filesystem server as a closed handle rather than
  // an explicit "Close" call.
  zx_handle_t handle = ZX_HANDLE_INVALID;
  fdio_fd_transfer(fd, &handle);
  // TODO: Should we check the status returned by fdio_fd_transfer?
  ASSERT_GE(fd, 0);
  if (handle != ZX_HANDLE_INVALID) {
    ASSERT_EQ(zx_handle_close(handle), ZX_OK);
  }

  ASSERT_EQ(unlink(GetPath("foobar").c_str()), 0);
}

TEST_P(BasicTest, GrowingVolumeWithFileCount) {
  // Minfs will start with 1 slice worth of inodes.  Write enough files to cause that to grow and
  // make sure fsck (which runs automatically at the end as part of the test fixture) passes.
  for (unsigned i = 0; i < fs().options().fvm_slice_size / minfs::kMinfsInodeSize + 1; ++i) {
    fbl::unique_fd fd(open(GetPath(std::to_string(i)).c_str(), O_CREAT, 0666));
    EXPECT_TRUE(fd);
  }
}

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, BasicTest, testing::ValuesIn(AllTestFilesystems()),
                         testing::PrintToStringParamName());

using FsckAfterEveryTransactionTest = FilesystemTest;

TEST_P(FsckAfterEveryTransactionTest, SimpleOperationsSucceeds) {
  EXPECT_EQ(fs().Unmount().status_value(), ZX_OK);
  MountOptions mount_options;
  mount_options.fsck_after_every_transaction = true;
  EXPECT_EQ(fs().Mount(mount_options).status_value(), ZX_OK);

  std::string path = GetPath("foobar");
  fbl::unique_fd fd(open(path.c_str(), O_CREAT | O_RDWR, 0666));
  EXPECT_TRUE(fd);
  EXPECT_EQ(write(fd.get(), "hello", 5), 5);
  fd.reset();
  EXPECT_EQ(unlink(path.c_str()), 0);
  EXPECT_EQ(mkdir(path.c_str(), 0777), 0);
  EXPECT_EQ(unlink(path.c_str()), 0);
}

TEST_P(FsckAfterEveryTransactionTest, PurgeOnRemountSucceeds) {
  std::string foo = GetPath("foo").c_str();
  std::string bar = GetPath("bar").c_str();
  fbl::unique_fd fd1(open(foo.c_str(), O_CREAT | O_RDWR, 0666));
  fbl::unique_fd fd2(open(bar.c_str(), O_CREAT | O_RDWR, 0666));

  EXPECT_EQ(unlink(foo.c_str()), 0);
  EXPECT_EQ(unlink(bar.c_str()), 0);
  EXPECT_EQ(fsync(fd1.get()), 0);

  // Stop any more writes going to the ram disk (so that the inodes aren't purged).
  auto* ram_disk = fs().GetRamDisk();
  EXPECT_EQ(ram_disk->SleepAfter(0).status_value(), ZX_OK);

  EXPECT_EQ(fs().Unmount().status_value(), ZX_OK);

  EXPECT_EQ(ram_disk->Wake().status_value(), ZX_OK);

  // Now remount and the two files we deleted earlier should get purged and fsck will run after each
  // one is purged.
  MountOptions mount_options;
  mount_options.fsck_after_every_transaction = true;
  EXPECT_EQ(fs().Mount(mount_options).status_value(), ZX_OK);
}

INSTANTIATE_TEST_SUITE_P(
    /*no prefix*/, FsckAfterEveryTransactionTest,
    testing::ValuesIn(MapAndFilterAllTestFilesystems(
        [](const TestFilesystemOptions& options) -> std::optional<TestFilesystemOptions> {
          if (options.filesystem->GetTraits().supports_fsck_after_every_transaction) {
            return options;
          } else {
            return std::nullopt;
          }
        })),
    testing::PrintToStringParamName());

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(FsckAfterEveryTransactionTest);

}  // namespace
}  // namespace fs_test
