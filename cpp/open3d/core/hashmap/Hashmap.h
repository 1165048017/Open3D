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

#include "open3d/core/hashmap/DeviceHashmap.h"

namespace open3d {
namespace core {

class Hashmap {
public:
    /// Comprehensive constructor for the developer.
    /// The developer knows all the parameter settings.
    Hashmap(size_t init_buckets,
            size_t init_capacity,
            size_t dsize_key,
            size_t dsize_value,
            Device device);

    Hashmap(size_t init_capacity,
            size_t dsize_key,
            size_t dsize_value,
            Device device);

    ~Hashmap(){};

    /// Rehash expects extra memory space at runtime, since it consists of
    /// 1) dumping all key value pairs to a buffer
    /// 2) create a new hash table
    /// 3) parallel insert dumped key value pairs
    /// 4) deallocate old hash table
    void Rehash(size_t buckets);

    /// Parallel insert contiguous arrays of keys and values.
    /// Output iterators and masks can be nullptrs if return iterators are not
    /// to be processed.
    void Insert(const void* input_keys,
                const void* input_values,
                iterator_t* output_iterators,
                bool* output_masks,
                size_t count);

    /// Parallel activate contiguous arrays of keys without copying values.
    /// Specifically useful for large value elements (e.g., a tensor), where we
    /// can do in-place management after activation.
    void Activate(const void* input_keys,
                  iterator_t* output_iterators,
                  bool* output_masks,
                  size_t count);

    /// Parallel find a contiguous array of keys.
    /// Output iterators and masks CANNOT be nullptrs as we have to interpret
    /// them.
    void Find(const void* input_keys,
              iterator_t* output_iterators,
              bool* output_masks,
              size_t count);

    /// Parallel erase a contiguous array of keys.
    /// Output masks can be a nullptr if return results are not to be processed.
    void Erase(const void* input_keys, bool* output_masks, size_t count);

    /// Parallel collect all iterators in the hash table
    size_t GetIterators(iterator_t* output_iterators);

    /// Parallel unpack iterators to contiguous arrays of keys and/or values.
    /// Output keys and values can be nullptrs if they are not to be
    /// processed/stored.
    void UnpackIterators(const iterator_t* input_iterators,
                         const bool* input_masks,
                         void* output_keys,
                         void* output_values,
                         size_t count);

    /// Parallel assign iterators in-place with associated values.
    /// Note: users should manage the key-value correspondences around
    /// iterators.
    void AssignIterators(iterator_t* input_iterators,
                         const bool* input_masks,
                         const void* input_values,
                         size_t count);

    size_t Size();

    /// Return number of elems per bucket.
    /// High performance not required, so directly returns a vector.
    std::vector<size_t> BucketSizes();

    /// Return size / bucket_count.
    float LoadFactor();

private:
    std::shared_ptr<DefaultDeviceHashmap> device_hashmap_;
};

}  // namespace core
}  // namespace open3d
