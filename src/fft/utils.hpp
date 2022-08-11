// Copyright (c) 2022 Graphcore Ltd. All rights reserved.

#pragma once

#include <poplar/Graph.hpp>

poplar::Tensor vstack(const std::vector<poplar::Tensor>& vectors);
poplar::Tensor hstack(const std::vector<poplar::Tensor>& vectors);
