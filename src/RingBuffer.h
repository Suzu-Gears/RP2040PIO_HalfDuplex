#pragma once

#include <atomic>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

template <class T> class RingBuffer {
public:
  void init(size_t capacity) {
    buf_.resize(capacity + 1);
  }

  bool push(const T &value) {
    size_t write_idx = write_idx_.load(std::memory_order_relaxed);
    size_t next_idx = write_idx + 1;
    if (next_idx == buf_.size()) {
      next_idx = 0;
    }
    if (next_idx == read_idx_.load(std::memory_order_acquire)) {
      return false;
    }
    buf_[write_idx] = value;
    write_idx_.store(next_idx, std::memory_order_release);
    return true;
  }

  std::optional<T> pop() {
    size_t write_idx = write_idx_.load(std::memory_order_acquire);
    size_t read_idx = read_idx_.load(std::memory_order_relaxed);
    if (write_idx == read_idx) {
      return std::nullopt;
    }
    std::optional<T> value = std::move(buf_[read_idx]);
    size_t next_idx = read_idx + 1;
    if (next_idx == buf_.size()) {
      next_idx = 0;
    }
    read_idx_.store(next_idx, std::memory_order_release);
    return value;
  }

  std::optional<T> peek() const {
    size_t write_idx = write_idx_.load(std::memory_order_acquire);
    size_t read_idx = read_idx_.load(std::memory_order_relaxed);
    if (write_idx == read_idx) {
      return std::nullopt;
    }
    return buf_[read_idx];
  }

  void clear() {
    write_idx_.store(0, std::memory_order_relaxed);
    read_idx_.store(0, std::memory_order_relaxed);
  }

  size_t size() const {
    size_t write_idx = write_idx_.load(std::memory_order_acquire);
    size_t read_idx = read_idx_.load(std::memory_order_relaxed);
    if (write_idx >= read_idx) {
      return write_idx - read_idx;
    }
    return write_idx + buf_.size() - read_idx;
  }

  size_t capacity() const { return buf_.size() - 1; }

private:
  std::vector<T> buf_;
  std::atomic<size_t> write_idx_{0};
  std::atomic<size_t> read_idx_{0};
};
