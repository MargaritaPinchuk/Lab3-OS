#pragma once
#include <vector>
#include <mutex>

class SharedArray {
public:
    explicit SharedArray(int size) : data_(static_cast<size_t>(size), 0) {}

    int Size() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return static_cast<int>(data_.size());
    }

    bool TryClaim(int index, int ownerId) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (data_[static_cast<size_t>(index)] != 0) return false;
        data_[static_cast<size_t>(index)] = ownerId;
        return true;
    }

    int Get(int index) const {
        std::lock_guard<std::mutex> lock(mtx_);
        return data_[static_cast<size_t>(index)];
    }

    void ResetOwned(const std::vector<int>& indices) {
        std::lock_guard<std::mutex> lock(mtx_);
        for (int i : indices) data_[static_cast<size_t>(i)] = 0;
    }

private:
    mutable std::mutex mtx_;
    std::vector<int> data_;
};