#pragma once

#include <cstddef>
#include <optional>

#include <pico/util/queue.h>

template <class T> class Queue {
public:
  void init(size_t element_count) { queue_init(&queue_, sizeof(T), element_count); }

  void free() { queue_free(&queue_); }

  size_t get_level() { return queue_get_level(&queue_); }

  bool is_empty() { return queue_is_empty(&queue_); }

  bool is_full() { return queue_is_full(&queue_); }

  bool try_add(const T &value) { return queue_try_add(&queue_, &value); }

  std::optional<T> try_remove() {
    T value;
    if (!queue_try_remove(&queue_, &value)) {
      return std::nullopt;
    }
    return value;
  }

  std::optional<T> try_peek() {
    T value;
    if (!queue_try_peek(&queue_, &value)) {
      return std::nullopt;
    }
    return value;
  }

  void add_blocking(const T &value) { queue_add_blocking(&queue_, &value); }

  T remove_blocking() {
    T value;
    queue_remove_blocking(&queue_, &value);
    return value;
  }

  T peek_blocking() {
    T value;
    queue_peek_blocking(&queue_, &value);
    return value;
  }

private:
  queue_t queue_;
};