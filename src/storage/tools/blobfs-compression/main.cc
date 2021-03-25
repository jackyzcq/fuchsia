// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <cstdio>
#include <string>

#include <fbl/unique_fd.h>

#include "src/lib/chunked-compression/chunked-compressor.h"
#include "src/lib/chunked-compression/status.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "src/storage/blobfs/compression/configs/chunked_compression_params.h"
#include "src/storage/tools/blobfs-compression/blobfs-compression.h"

namespace {

using ::chunked_compression::ChunkedCompressor;
using ::chunked_compression::CompressionParams;

void usage(const char* fname) {
  fprintf(stderr, "Usage: %s [--option=value ...]\n\n", fname);
  fprintf(stderr, "Options:\n");
  fprintf(stderr, "  %-20s%s\n", "source_file", "(required) the file to be compressed.");
  fprintf(stderr, "  %-20s%s\n", "compressed_file",
          "(optional) the compressed file output path (override if existing).");
  fprintf(stderr, "  %-20s%s\n", "help", "print this usage message.");
  fprintf(stderr, "  %-20s%s\n", "verbose", "show debugging information.");
}

// Truncates |fd| to |write_size|, and mmaps the file for writing.
// Returns the mapped buffer in |out_write_buf| of length |write_size|.
// This method can fail only with user-input-irrelevant errors.
zx_status_t MapFileForWriting(const fbl::unique_fd& fd, const char* file, size_t write_size,
                              uint8_t** out_write_buf) {
  if (ftruncate(fd.get(), write_size)) {
    fprintf(stderr, "Failed to truncate '%s': %s\n", file, strerror(errno));
    return ZX_ERR_NO_SPACE;
  }

  void* data = nullptr;
  if (write_size > 0) {
    data = mmap(NULL, write_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd.get(), 0);
    if (data == MAP_FAILED) {
      fprintf(stderr, "mmap failed: %s\n", strerror(errno));
      return ZX_ERR_NO_MEMORY;
    }
  }

  *out_write_buf = static_cast<uint8_t*>(data);
  return ZX_OK;
}

// Mmaps the |fd| for reading.
// Returns the  size of the file in |out_size|, and the managed FD in |out_fd|.
// This method can fail only with user-input-irrelevant errors.
zx_status_t MapFileForReading(const fbl::unique_fd& fd, const uint8_t** out_buf, size_t* out_size) {
  struct stat info;
  fstat(fd.get(), &info);
  size_t size = info.st_size;

  void* data = nullptr;
  if (size > 0) {
    data = mmap(NULL, size, PROT_READ, MAP_SHARED, fd.get(), 0);
    if (data == MAP_FAILED) {
      fprintf(stderr, "mmap failed: %s\n", strerror(errno));
      return ZX_ERR_NO_MEMORY;
    }
  }

  *out_buf = static_cast<uint8_t*>(data);
  *out_size = size;
  return ZX_OK;
}
}  // namespace

int main(int argc, char** argv) {
  const auto cl = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(cl)) {
    return 1;
  }
  // TODO: check unknown inputs.

  if (cl.HasOption("verbose", nullptr)) {
    printf("Received flags:\n");
    for (const auto& option : cl.options()) {
      printf("  %s = \"%s\"\n", option.name.c_str(), option.value.c_str());
    }
    printf("\n");
  }
  if (cl.HasOption("help", nullptr)) {
    usage(argv[0]);
    return 0;
  }

  // Parse command line options.
  blobfs_compress::CompressionCliOptionStruct options;
  cl.GetOptionValue("source_file", &options.source_file);
  options.source_file_fd.reset(open(options.source_file.c_str(), O_RDONLY));
  cl.GetOptionValue("compressed_file", &options.compressed_file);
  options.compressed_file_fd.reset(
      open(options.compressed_file.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644));

  auto error_code = blobfs_compress::ValidateCliOptions(options);
  if (error_code) {
    usage(argv[0]);
    return error_code;
  }

  const uint8_t* src_data;
  size_t src_size;
  error_code = MapFileForReading(options.source_file_fd, &src_data, &src_size);
  if (error_code) {
    return error_code;
  }

  uint8_t* dest_write_buf = nullptr;
  CompressionParams params = blobfs::GetDefaultChunkedCompressionParams(src_size);
  if (!options.compressed_file.empty()) {
    error_code = MapFileForWriting(options.compressed_file_fd, options.compressed_file.c_str(),
                                   params.ComputeOutputSizeLimit(src_size), &dest_write_buf);
    if (error_code) {
      return error_code;
    }
  }

  size_t compressed_size;
  if (blobfs_compress::BlobfsCompress(src_data, src_size, dest_write_buf, &compressed_size,
                                      params)) {
    return ZX_ERR_INTERNAL;
  }

  if (!options.compressed_file.empty()) {
    ftruncate(options.compressed_file_fd.get(), compressed_size);
  }
  return ZX_OK;
}
