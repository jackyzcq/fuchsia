// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/public/commit.h"

#include <tuple>

namespace storage {

bool Commit::TimestampOrdered(const std::unique_ptr<const Commit>& commit1,
                              const std::unique_ptr<const Commit>& commit2) {
  return std::forward_as_tuple(commit1->GetTimestamp(), commit1->GetId()) <
         std::forward_as_tuple(commit2->GetTimestamp(), commit2->GetId());
}

}  // namespace storage
