/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "framework/integration_framework/fake_peer/network/ordering_service_network_notifier.hpp"

#include "interfaces/iroha_internal/transaction_batch.hpp"

namespace integration_framework {
  namespace fake_peer {

    void OsNetworkNotifier::onBatch(std::unique_ptr<TransactionBatch> batch) {
      auto batch_ptr = std::shared_ptr<TransactionBatch>(batch.release());
      batches_subject_.get_subscriber().on_next(std::move(batch_ptr));
    }

    rxcpp::observable<OsNetworkNotifier::TransactionBatchPtr>
    OsNetworkNotifier::get_observable() {
      return batches_subject_.get_observable();
    }

  }  // namespace fake_peer
}  // namespace integration_framework
