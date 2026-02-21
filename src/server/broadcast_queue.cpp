#include "server/broadcast_queue.h"

#include <algorithm>

BroadcastQueue::BroadcastQueue(size_t capacity)
    : capacity_(std::max<size_t>(capacity, 2))
{
    buffer_.resize(capacity_);
}

bool BroadcastQueue::try_push(const std::string& message) {
    const size_t cur_head = head_.load(std::memory_order_relaxed);
    const size_t next_head = (cur_head + 1) % capacity_;

    // If the next slot is the tail the buffer is full.
    if (next_head == tail_.load(std::memory_order_acquire)) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    buffer_[cur_head] = message;

    // Publish the write.
    head_.store(next_head, std::memory_order_release);
    return true;
}

std::optional<std::string> BroadcastQueue::try_pop() {
    const size_t cur_tail = tail_.load(std::memory_order_relaxed);

    // Empty when head == tail.
    if (cur_tail == head_.load(std::memory_order_acquire)) {
        return std::nullopt;
    }

    std::string msg = std::move(buffer_[cur_tail]);

    const size_t next_tail = (cur_tail + 1) % capacity_;
    tail_.store(next_tail, std::memory_order_release);

    return msg;
}

size_t BroadcastQueue::size() const {
    const size_t h = head_.load(std::memory_order_acquire);
    const size_t t = tail_.load(std::memory_order_acquire);
    return (h >= t) ? (h - t) : (capacity_ - t + h);
}

size_t BroadcastQueue::dropped_count() const {
    return dropped_.load(std::memory_order_relaxed);
}
