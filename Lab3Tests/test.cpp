#include "pch.h"
#include "SharedArray.h"

TEST(SharedArrayTest, ClaimsEmptyCell) {
    SharedArray arr(5);
    EXPECT_TRUE(arr.TryClaim(2, 7));
    EXPECT_EQ(arr.Get(2), 7);
}

TEST(SharedArrayTest, RejectsAlreadyClaimedCell) {
    SharedArray arr(5);
    ASSERT_TRUE(arr.TryClaim(0, 1));
    EXPECT_FALSE(arr.TryClaim(0, 2));
    EXPECT_EQ(arr.Get(0), 1);
}

TEST(SharedArrayTest, ResetOwnedClearsOnlyGivenIndices) {
    SharedArray arr(4);
    ASSERT_TRUE(arr.TryClaim(0, 1));
    ASSERT_TRUE(arr.TryClaim(1, 1));
    ASSERT_TRUE(arr.TryClaim(2, 2));
    arr.ResetOwned({ 0, 1 });
    EXPECT_EQ(arr.Get(0), 0);
    EXPECT_EQ(arr.Get(1), 0);
    EXPECT_EQ(arr.Get(2), 2);
}

TEST(SharedArrayTest, OnlyOneThreadWinsSameIndex) {
    const int kThreads = 8;
    SharedArray arr(1);
    std::atomic<int> successCount{ 0 };

    std::vector<std::thread> pool;
    pool.reserve(kThreads);
    for (int t = 1; t <= kThreads; ++t) {
        pool.emplace_back([&, t]() {
            if (arr.TryClaim(0, t)) successCount++;
            });
    }
    for (auto& th : pool) th.join();

    EXPECT_EQ(successCount.load(), 1);

    int owner = arr.Get(0);
    EXPECT_GE(owner, 1);
    EXPECT_LE(owner, kThreads);
}