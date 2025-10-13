#pragma once

#include <cuda_runtime.h>

#include <cstddef>
#include <stdexcept>
#include <string>

namespace puzzle71::gpu {

inline void CheckCuda(cudaError_t status, const char* message) {
    if (status != cudaSuccess) {
        throw std::runtime_error(std::string(message) + ": " + cudaGetErrorString(status));
    }
}

template <typename T>
class DeviceArray {
public:
    DeviceArray() = default;
    ~DeviceArray() { Release(); }
    DeviceArray(const DeviceArray&) = delete;
    DeviceArray& operator=(const DeviceArray&) = delete;
    DeviceArray(DeviceArray&& other) noexcept { Swap(other); }
    DeviceArray& operator=(DeviceArray&& other) noexcept {
        if (this != &other) {
            Release();
            Swap(other);
        }
        return *this;
    }

    void Allocate(std::size_t count) {
        if (count == 0) {
            Release();
            return;
        }
        if (count == size_) {
            return;
        }
        Release();
        CheckCuda(cudaMalloc(&data_, count * sizeof(T)), "cudaMalloc(DeviceArray)");
        size_ = count;
    }

    void Release() {
        if (data_) {
            cudaFree(data_);
            data_ = nullptr;
        }
        size_ = 0;
    }

    void Upload(const T* host_data, std::size_t count) {
        if (count == 0) {
            return;
        }
        if (count > size_) {
            throw std::runtime_error("DeviceArray::Upload exceeds allocation");
        }
        CheckCuda(cudaMemcpy(data_, host_data, count * sizeof(T), cudaMemcpyHostToDevice),
                  "cudaMemcpy(DeviceArray::Upload)");
    }

    [[nodiscard]] T* data() const { return data_; }
    [[nodiscard]] std::size_t size() const { return size_; }

    void Swap(DeviceArray& other) noexcept {
        std::swap(data_, other.data_);
        std::swap(size_, other.size_);
    }

private:
    T* data_{nullptr};
    std::size_t size_{0};
};

}  // namespace puzzle71::gpu

