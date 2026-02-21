#include <gtest/gtest.h>
#include "server/broadcast_queue.h"
#include <thread>
#include <vector>
#include <atomic>

class BroadcastQueueTest : public ::testing::Test {
protected:
    BroadcastQueue queue{64};  // Small capacity for testing
};

TEST_F(BroadcastQueueTest, PushAndPopSingleItem) {
    EXPECT_TRUE(queue.try_push("hello"));
    auto item = queue.try_pop();
    ASSERT_TRUE(item.has_value());
    EXPECT_EQ(*item, "hello");
}

TEST_F(BroadcastQueueTest, PopFromEmptyReturnsNullopt) {
    auto item = queue.try_pop();
    EXPECT_FALSE(item.has_value());
}

TEST_F(BroadcastQueueTest, FIFOOrder) {
    queue.try_push("first");
    queue.try_push("second");
    queue.try_push("third");

    auto a = queue.try_pop();
    auto b = queue.try_pop();
    auto c = queue.try_pop();

    ASSERT_TRUE(a.has_value());
    ASSERT_TRUE(b.has_value());
    ASSERT_TRUE(c.has_value());

    EXPECT_EQ(*a, "first");
    EXPECT_EQ(*b, "second");
    EXPECT_EQ(*c, "third");
}

TEST_F(BroadcastQueueTest, SizeTracking) {
    EXPECT_EQ(queue.size(), 0u);

    queue.try_push("a");
    EXPECT_EQ(queue.size(), 1u);

    queue.try_push("b");
    EXPECT_EQ(queue.size(), 2u);

    queue.try_pop();
    EXPECT_EQ(queue.size(), 1u);

    queue.try_pop();
    EXPECT_EQ(queue.size(), 0u);
}

TEST_F(BroadcastQueueTest, OverflowDropsOldest) {
    // Fill queue to capacity
    for (size_t i = 0; i < 64; ++i) {
        EXPECT_TRUE(queue.try_push("msg_" + std::to_string(i)));
    }

    // Check initial dropped count
    size_t initial_dropped = queue.dropped_count();

    // Push one more - should drop the oldest
    queue.try_push("overflow");

    // Dropped count should have increased
    EXPECT_GT(queue.dropped_count(), initial_dropped);
}

TEST_F(BroadcastQueueTest, ConcurrentPushPop) {
    BroadcastQueue big_queue(8192);
    std::atomic<int> consumed{0};
    const int num_items = 1000;

    // Producer thread
    std::thread producer([&]() {
        for (int i = 0; i < num_items; ++i) {
            big_queue.try_push("item_" + std::to_string(i));
        }
    });

    // Consumer thread
    std::thread consumer([&]() {
        while (consumed.load() < num_items) {
            auto item = big_queue.try_pop();
            if (item.has_value()) {
                consumed.fetch_add(1);
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    // All items should have been consumed (though some may have been dropped)
    EXPECT_EQ(consumed.load() + static_cast<int>(big_queue.dropped_count()), num_items);
}
