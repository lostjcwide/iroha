#include "framework/integration_framework/fake_peer/behaviour/behaviour.hpp"

namespace integration_framework {
  namespace fake_peer {

    Behaviour::~Behaviour() {
      absolve();
    }

    void Behaviour::adopt(const std::shared_ptr<FakePeer> &fake_peer) {
      // This code feels like part of constructor, but the use of `this'
      // to call virtual functions from base class constructor seems wrong.
      // Hint: such calls would precede the derived class construction.
      fake_peer_wptr_ = fake_peer;
      log_ = logger::log(getName() + " of fake peer at "
                         + fake_peer->getAddress());
      // subscribe for all messages
      subscriptions_.emplace_back(
          getFakePeer().get_mst_states_observable().subscribe(
              [this](const auto &message) {
                this->processMstMessage(message);
              }));
      subscriptions_.emplace_back(
          getFakePeer().get_yac_states_observable().subscribe(
              [this](const auto &message) {
                this->processYacMessage(message);
              }));
      subscriptions_.emplace_back(
          getFakePeer().get_os_batches_observable().subscribe(
              [this](const auto &batch) { this->processOsBatch(batch); }));
      subscriptions_.emplace_back(
          getFakePeer().get_og_proposals_observable().subscribe(
              [this](const auto &proposal) {
                this->processOgProposal(proposal);
              }));
    }

    void Behaviour::absolve() {
      for (auto &subscription : subscriptions_) {
        subscription.unsubscribe();
      }
      fake_peer_wptr_.reset();
    }

    FakePeer &Behaviour::getFakePeer() {
      auto fake_peer = fake_peer_wptr_.lock();
      assert(fake_peer && "Fake peer shared pointer is not set!"
        " Probably the fake peer has gone before the associated behaviour.");
      return *fake_peer;
    }

  }  // namespace fake_peer
}  // namespace integration_framework
