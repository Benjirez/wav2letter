/**
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <cstdio>
#include <string>

namespace w2l {
namespace streaming {

enum DataType : uint32_t {
  UNINITIALIZED = 0,
  FLOAT = 1,
  FLOAT1 = 2,
};

bool dataTypeIsValid(DataType type);

const std::string dataTypeString(DataType type);

int dataTypeNumberOfBytes(DataType type);

} // namespace streaming
} // namespace w2l
