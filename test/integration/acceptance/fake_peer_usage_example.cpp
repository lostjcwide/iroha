/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "framework/integration_framework/fake_peer/behaviour/honest.hpp"
#include "framework/integration_framework/fake_peer/fake_peer.hpp"
#include "framework/integration_framework/integration_test_framework.hpp"
#include "integration/acceptance/acceptance_fixture.hpp"
#include "module/irohad/multi_sig_transactions/mst_mocks.hpp"

using namespace common_constants;
using namespace shared_model;
using namespace integration_framework;
using namespace shared_model::interface::permissions;

using ::testing::_;
using ::testing::Invoke;

static constexpr std::chrono::seconds kMstStateWaitingTime(10);

class FakePeerExampleFixture : public AcceptanceFixture {
 public:
  using FakePeerPtr = std::shared_ptr<fake_peer::FakePeer>;

  std::unique_ptr<IntegrationTestFramework> itf_;

  /**
   * Prepare state of ledger:
   * - create honest fake iroha peers
   * - create account of target user
   * - add assets to admin
   *
   * @param num_fake_peers - the amount of fake peers to create
   * @return reference to ITF
   */
  IntegrationTestFramework &prepareState(size_t num_fake_peers) {
    // request the fake peers construction
    std::vector<std::future<FakePeerPtr>>
        fake_peers_futures;
    std::generate_n(std::back_inserter(fake_peers_futures),
                    num_fake_peers,
                    [this] { return itf_->addInitailPeer({}); });

    itf_->setInitialState(kAdminKeypair);

    auto permissions =
        interface::RolePermissionSet({Role::kReceive, Role::kTransfer});

    // get the constructed fake peers & set honest behaviour
    for (auto &fake_peer_future : fake_peers_futures) {
      assert(fake_peer_future.valid() && "fake peer must be ready");
      FakePeerPtr fake_peer = fake_peer_future.get();
      fake_peer->setBehaviour(std::make_shared<fake_peer::HonestBehaviour>());
      fake_peers_.emplace_back(std::move(fake_peer));
    }

    // inside prepareState we can use lambda for such assert, since
    // prepare transactions are not going to fail
    auto block_with_tx = [](auto &block) {
      ASSERT_EQ(block->transactions().size(), 1);
    };

    return itf_->sendTxAwait(makeUserWithPerms(permissions), block_with_tx)
        .sendTxAwait(
            complete(baseTx(kAdminId).addAssetQuantity(kAssetId, "20000.0"),
                     kAdminKeypair),
            block_with_tx);
  }

 protected:
  void SetUp() override {
    itf_ = std::make_unique<IntegrationTestFramework>(
        1, boost::none, true, true);
  }

  std::vector<FakePeerPtr> fake_peers_;
};

/**
 * Check that after sending a not fully signed transaction, an MST state
 * propagtes to another peer
 * @given a not fully signed transaction
 * @when such transaction is sent to one of two iroha peers in the network
 * @then that peer propagates MST state to another peer
 */
TEST_F(FakePeerExampleFixture,
       MstStateOfTransactionWithoutAllSignaturesPropagtesToOtherPeer) {
  std::mutex mst_mutex;
  std::condition_variable mst_cv;
  std::atomic_bool got_state_notification(false);
  auto &itf = prepareState(1);
  fake_peers_.front()->get_mst_states_observable().subscribe(
      [&mst_cv, &got_state_notification](const auto &state) {
        got_state_notification.store(true);
        mst_cv.notify_one();
      });
  itf.sendTxWithoutValidation(complete(
      baseTx(kAdminId)
          .transferAsset(kAdminId, kUserId, kAssetId, "income", "500.0")
          .quorum(2),
      kAdminKeypair));
  std::unique_lock<std::mutex> mst_lock(mst_mutex);
  mst_cv.wait_for(mst_lock, kMstStateWaitingTime, [&got_state_notification] {
    return got_state_notification.load();
  });
  EXPECT_TRUE(got_state_notification.load())
      << "Reached timeout waiting for MST State.";
}
