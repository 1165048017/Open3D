// ----------------------------------------------------------------------------
// -                        Open3D: www.open3d.org                            -
// ----------------------------------------------------------------------------
// The MIT License (MIT)
//
// Copyright (c) 2018 www.open3d.org
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
// ----------------------------------------------------------------------------

#include "open3d/tgeometry/PointCloud.h"
#include "open3d/geometry/PointCloud.h"

#include <Eigen/Core>
#include <unordered_map>

#include "open3d/core/ShapeUtil.h"
#include "open3d/core/Tensor.h"
#include "open3d/core/TensorList.h"
#include "open3d/core/algebra/Matmul.h"
#include "open3d/core/hashmap/TensorHash.h"
#include "open3d/utility/Timer.h"

namespace open3d {
namespace tgeometry {

using namespace core;

PointCloud::PointCloud(const Tensor &points_tensor)
    : Geometry3D(Geometry::GeometryType::PointCloud),
      dtype_(points_tensor.GetDtype()),
      device_(points_tensor.GetDevice()) {
    auto shape = points_tensor.GetShape();
    if (shape[1] != 3) {
        utility::LogError("PointCloud must be constructed from (N, 3) points.");
    }
    point_dict_.emplace("points", TensorList(points_tensor));
}

PointCloud::PointCloud(
        const std::unordered_map<std::string, TensorList> &point_dict)
    : Geometry3D(Geometry::GeometryType::PointCloud) {
    auto it = point_dict.find("points");
    if (it == point_dict.end()) {
        utility::LogError("PointCloud must include key \"points\".");
    }

    dtype_ = it->second.GetDtype();
    device_ = it->second.GetDevice();

    auto shape = it->second.GetShape();
    if (shape[0] != 3) {
        utility::LogError("PointCloud must be constructed from (N, 3) points.");
    }

    for (auto kv : point_dict) {
        if (device_ != kv.second.GetDevice()) {
            utility::LogError("TensorList device mismatch!");
        }
        point_dict_.emplace(kv.first, kv.second);
    }
}

TensorList &PointCloud::operator[](const std::string &key) {
    auto it = point_dict_.find(key);
    if (it == point_dict_.end()) {
        utility::LogError("Unknown key {} in PointCloud point_dict_.", key);
    }

    return it->second;
}

void PointCloud::SyncPushBack(
        const std::unordered_map<std::string, Tensor> &point_struct) {
    // Check if "point"" exists
    auto it = point_struct.find("points");
    if (it == point_struct.end()) {
        utility::LogError("Point must include key \"points\".");
    }

    auto size = point_dict_.find("points")->second.GetSize();
    for (auto kv : point_struct) {
        // Check existance of key in point_dict
        auto it = point_dict_.find(kv.first);
        if (it == point_dict_.end()) {
            utility::LogError("Unknown key {} in PointCloud dictionary.",
                              kv.first);
        }

        // Check size consistency
        auto size_it = it->second.GetSize();
        if (size_it != size) {
            utility::LogError("Size mismatch ({}, {}) between ({}, {}).",
                              "points", size, kv.first, size_it);
        }
        it->second.PushBack(kv.second);
    }
}

PointCloud &PointCloud::Clear() {
    point_dict_.clear();
    return *this;
}

bool PointCloud::IsEmpty() const { return !HasPoints(); }

Tensor PointCloud::GetMinBound() const {
    point_dict_.at("points").AssertShape({3});
    return point_dict_.at("points").AsTensor().Min({0});
}

Tensor PointCloud::GetMaxBound() const {
    point_dict_.at("points").AssertShape({3});
    return point_dict_.at("points").AsTensor().Max({0});
}

Tensor PointCloud::GetCenter() const {
    point_dict_.at("points").AssertShape({3});
    return point_dict_.at("points").AsTensor().Mean({0});
}

PointCloud &PointCloud::Transform(const Tensor &transformation) {
    Tensor R = transformation.Slice(0, 0, 3).Slice(1, 0, 3);
    Tensor t = transformation.Slice(0, 0, 3).Slice(1, 3, 4);
    Tensor points_transformed =
            Matmul(point_dict_.at("points").AsTensor(), R.T()) + t.T();
    point_dict_.at("points").AsTensor() = points_transformed;
    return *this;
}

PointCloud &PointCloud::Translate(const Tensor &translation, bool relative) {
    shape_util::AssertShape(translation, {3},
                            "translation must have shape (3,)");
    Tensor transform = translation.Copy();
    if (!relative) {
        transform -= GetCenter();
    }
    point_dict_.at("points").AsTensor() += transform;
    return *this;
}

PointCloud &PointCloud::Scale(double scale, const Tensor &center) {
    shape_util::AssertShape(center, {3}, "center must have shape (3,)");
    point_dict_.at("points") = TensorList(
            (point_dict_.at("points").AsTensor() - center) * scale + center);
    return *this;
}

PointCloud &PointCloud::Rotate(const Tensor &R, const Tensor &center) {
    utility::LogError("Unimplemented");
    return *this;
}

PointCloud PointCloud::VoxelDownSample(
        float voxel_size,
        const std::unordered_set<std::string> &properties_to_skip) const {
    utility::Timer timer;
    timer.Start();
    auto tensor_quantized =
            point_dict_.find("points")->second.AsTensor() / voxel_size;
    timer.Stop();
    utility::LogInfo("[PointCloud] operator Div takes {}", timer.GetDuration());

    timer.Start();
    auto tensor_quantized_int64 = tensor_quantized.To(Dtype::Int64);
    timer.Stop();
    utility::LogInfo("[PointCloud] To(Int64) takes {}", timer.GetDuration());
    CudaSync();

    timer.Start();
    auto result = Unique(tensor_quantized_int64);
    timer.Stop();
    utility::LogInfo("[PointCloud] Unique takes {}", timer.GetDuration());

    timer.Start();
    Tensor coords = result.first;
    Tensor masks = result.second;
    auto pcd_down_map = std::unordered_map<std::string, TensorList>();
    auto tl_pts = TensorList(coords.IndexGet({masks}).To(Dtype::Float32),
                             /* inplace = */ false);
    timer.Stop();
    utility::LogInfo("[PointCloud] pts IndexGet takes {}", timer.GetDuration());

    timer.Start();
    pcd_down_map.emplace(std::make_pair("points", tl_pts));
    for (auto kv : point_dict_) {
        if (kv.first != "points" &&
            properties_to_skip.find(kv.first) == properties_to_skip.end()) {
            timer.Start();
            auto tl = TensorList(kv.second.AsTensor().IndexGet({masks}), false);
            pcd_down_map.emplace(std::make_pair(kv.first, tl));
            timer.Stop();
            utility::LogInfo("[PointCloud] {} IndexGet takes {}", kv.first,
                             timer.GetDuration());
        }
    }

    PointCloud pcd_down(pcd_down_map);
    timer.Stop();
    utility::LogInfo("[PointCloud] constructor {}", timer.GetDuration());

    return pcd_down;
}

}  // namespace tgeometry
}  // namespace open3d
