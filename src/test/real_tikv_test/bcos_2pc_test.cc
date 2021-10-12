#include <pingcap/Exception.h>
#include <pingcap/kv/Scanner.h>
#include <pingcap/kv/Snapshot.h>
#include <pingcap/kv/Txn.h>
#include <pingcap/kv/BCOS2PC.h>
#include <iostream>

#include "../test_helper.h"

namespace pingcap
{
    namespace kv
    {

    } // namespace kv
} // namespace pingcap

namespace
{

    using namespace pingcap;
    using namespace pingcap::kv;

    struct TestUtil
    {
        static std::string get_random_string(size_t length)
        {
            auto randchar = []() -> char
            {
                const char charset[] = "0123456789"
                                       "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                       "abcdefghijklmnopqrstuvwxyz";
                const size_t max_index = (sizeof(charset) - 1);
                return charset[rand() % max_index];
            };
            std::string str(length, 0);
            std::generate_n(str.begin(), length, randchar);
            return str;
        }
    };

    class TestWith2PCRealTiKV : public testing::Test
    {
    protected:
        void SetUp() override
        {
            std::vector<std::string> pd_addrs{"127.0.0.1:2379"};

            test_cluster = createCluster(pd_addrs);
        }
        void clean()
        {
            Txn txn(test_cluster.get());
            txn.set("a", "");
            txn.set("b", "");
            txn.set("c", "");
            txn.set("d", "");
            txn.set("e", "");
            txn.set("f", "");
            txn.commit();
        }
        ClusterPtr test_cluster;
    };

    TEST_F(TestWith2PCRealTiKV, bcosCommmit)
    {
        // Prewrite
        {
            clean();
            // scheduler prewrite
            std::unordered_map<std::string, std::string> mutations;
            mutations["a"] = "a1";
            mutations["b"] = "b1";
            mutations["c"] = "c1";

            BCOSTwoPhaseCommitter committer{test_cluster.get(), "a", std::move(mutations)};
            auto result = committer.prewriteKeys();
            auto primary_key = result.primary_lock;
            ASSERT_EQ(primary_key, "a");
            auto start_ts = result.start_ts;
            ASSERT_NE(start_ts, 0);

            // executor prewrite
            std::unordered_map<std::string, std::string> mutations2;
            mutations2["d"] = "d";
            mutations2["e"] = "e";
            mutations2["f"] = "f";
            BCOSTwoPhaseCommitter committer2(test_cluster.get(), "a", std::move(mutations2));
            // std::this_thread::sleep_for(std::chrono::seconds(20));
            committer2.prewriteKeys(start_ts);

            // scheduler commit
            committer.commitKeys();

            // std::this_thread::sleep_for(std::chrono::seconds(2));
            // query and check
            Snapshot snap(test_cluster.get());
            ASSERT_EQ(snap.Get("a"), "a1");
            ASSERT_EQ(snap.Get("b"), "b1");
            ASSERT_EQ(snap.Get("c"), "c1");
            ASSERT_EQ(snap.Get("d"), "d");
            ASSERT_EQ(snap.Get("e"), "e");
            ASSERT_EQ(snap.Get("f"), "f");

            // BatchGet
            auto result2 = snap.BatchGet({"a", "b", "c", "d", "e", "f"});
            ASSERT_EQ(result2["a"], "a1");
            ASSERT_EQ(result2["b"], "b1");
            ASSERT_EQ(result2["c"], "c1");
            ASSERT_EQ(result2["d"], "d");
            ASSERT_EQ(result2["e"], "e");
            ASSERT_EQ(result2["f"], "f");
        }
    }

    TEST_F(TestWith2PCRealTiKV, testPrewriteRollback)
    {
        // Prewrite
        {
            clean();
            Txn txn(test_cluster.get());
            txn.set("a", "a");
            txn.set("b", "b");
            txn.set("c", "c");
            txn.commit();
            // scheduler prewrite
            std::unordered_map<std::string, std::string> mutations;
            mutations["a"] = "a1";
            mutations["b"] = "b1";
            mutations["c"] = "c1";

            BCOSTwoPhaseCommitter committer{test_cluster.get(), "a", std::move(mutations)};
            auto result = committer.prewriteKeys();
            auto primary_key = result.primary_lock;
            ASSERT_EQ(primary_key, "a");
            auto start_ts = result.start_ts;
            ASSERT_NE(start_ts, 0);

            // executor prewrite
            std::unordered_map<std::string, std::string> mutations2;
            mutations2["d"] = "d1";
            mutations2["e"] = "e1";
            mutations2["f"] = "f1";
            BCOSTwoPhaseCommitter committer2(test_cluster.get(), "a", std::move(mutations2));
            // std::this_thread::sleep_for(std::chrono::seconds(20));
            committer2.prewriteKeys(start_ts);
            // std::this_thread::sleep_for(std::chrono::seconds(20));

            // scheduler rollback
            committer.rollback();
            committer2.rollback();

            // query and check
            Snapshot snap(test_cluster.get());
            ASSERT_EQ(snap.Get("a"), "a");
            ASSERT_EQ(snap.Get("b"), "b");
            ASSERT_EQ(snap.Get("c"), "c");
            ASSERT_EQ(snap.Get("d"), "");
            ASSERT_EQ(snap.Get("e"), "");
            ASSERT_EQ(snap.Get("f"), "");

            // BatchGet
            auto result2 = snap.BatchGet({"a", "b", "c", "d", "e", "f"});
            ASSERT_EQ(result2["a"], "a");
            ASSERT_EQ(result2["b"], "b");
            ASSERT_EQ(result2["c"], "c");
            ASSERT_EQ(result2["d"], "");
            ASSERT_EQ(result2["e"], "");
            ASSERT_EQ(result2["f"], "");
        }
    }

    TEST_F(TestWith2PCRealTiKV, reCommit)
    {
        // Prewrite
        {
            clean();
            Txn txn(test_cluster.get());
            txn.set("a", "a");
            txn.set("b", "b");
            txn.set("c", "c");
            txn.commit();
            // scheduler prewrite
            std::unordered_map<std::string, std::string> mutations;
            mutations["a"] = "a1";
            mutations["b"] = "b1";
            mutations["c"] = "c1";

            BCOSTwoPhaseCommitter committer{test_cluster.get(), "a", std::move(mutations)};
            auto result = committer.prewriteKeys();
            auto primary_key = result.primary_lock;
            ASSERT_EQ(primary_key, "a");
            auto start_ts = result.start_ts;
            ASSERT_NE(start_ts, 0);

            // executor prewrite
            std::unordered_map<std::string, std::string> mutations2;
            mutations2["d"] = "d1";
            mutations2["e"] = "e1";
            mutations2["f"] = "f1";
            BCOSTwoPhaseCommitter committer2(test_cluster.get(), "a", std::move(mutations2));
            // std::this_thread::sleep_for(std::chrono::seconds(20));
            committer2.prewriteKeys(start_ts);
            // std::this_thread::sleep_for(std::chrono::seconds(20));

            // scheduler rollback
            committer.rollback();
            committer2.rollback();

            // query and check
            Snapshot snap(test_cluster.get());
            ASSERT_EQ(snap.Get("a"), "a");
            ASSERT_EQ(snap.Get("b"), "b");
            ASSERT_EQ(snap.Get("c"), "c");
            ASSERT_EQ(snap.Get("d"), "");
            ASSERT_EQ(snap.Get("e"), "");
            ASSERT_EQ(snap.Get("f"), "");

            Txn txn2(test_cluster.get());
            txn2.set("a", "a1");
            txn2.set("b", "b1");
            txn2.set("c", "c1");
            txn2.commit();

            ASSERT_EQ(snap.Get("a"), "a");
            ASSERT_EQ(snap.Get("b"), "b");
            ASSERT_EQ(snap.Get("c"), "c");
            ASSERT_EQ(snap.Get("d"), "");
            ASSERT_EQ(snap.Get("e"), "");
            ASSERT_EQ(snap.Get("f"), "");

            Snapshot snap2(test_cluster.get());
            ASSERT_EQ(snap2.Get("a"), "a1");
            ASSERT_EQ(snap2.Get("b"), "b1");
            ASSERT_EQ(snap2.Get("c"), "c1");
            ASSERT_EQ(snap2.Get("d"), "");
            ASSERT_EQ(snap2.Get("e"), "");
            ASSERT_EQ(snap2.Get("f"), "");

            // BatchGet
            auto result2 = snap2.BatchGet({"a", "b", "c", "d", "e", "f"});
            ASSERT_EQ(result2["a"], "a1");
            ASSERT_EQ(result2["b"], "b1");
            ASSERT_EQ(result2["c"], "c1");
            ASSERT_EQ(result2["d"], "");
            ASSERT_EQ(result2["e"], "");
            ASSERT_EQ(result2["f"], "");
        }
    }

} // namespace
