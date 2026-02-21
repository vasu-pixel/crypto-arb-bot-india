#pragma once

#include <atomic>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

/// Single-Producer Single-Consumer lock-free ring buffer.
/// Used to decouple the main bot threads from the WebSocket broadcast path
/// so that broadcast() never blocks the caller.
class BroadcastQueue {
public:
    explicit BroadcastQueue(size_t capacity = 8192);

    /// Push a message to the queue.  Returns false (and increments the drop
    /// counter) if the buffer is full -- the message is silently discarded.
    bool try_push(const std::string& message);

    /// Pop the next message from the queue.  Returns std::nullopt when the
    /// queue is empty.
    std::optional<std::string> try_pop();

    /// Approximate number of messages currently buffered.
    size_t size() const;

    /// Total number of messages that were dropped because the queue was full.
    size_t dropped_count() const;

private:
    std::vector<std::string> buffer_;
    size_t capacity_;
    std::atomic<size_t> head_{0};    // write index (producer)
    std::atomic<size_t> tail_{0};    // read  index (consumer)
    std::atomic<size_t> dropped_{0};
};
