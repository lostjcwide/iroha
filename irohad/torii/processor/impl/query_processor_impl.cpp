/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "torii/processor/query_processor_impl.hpp"

#include <boost/range/size.hpp>
#include <rxcpp/operators/rx-concat.hpp>
#include <rxcpp/operators/rx-map.hpp>
#include <rxcpp/sources/rx-iterate.hpp>
#include "common/bind.hpp"
#include "interfaces/queries/blocks_query.hpp"
#include "interfaces/queries/query.hpp"
#include "interfaces/query_responses/block_query_response.hpp"
#include "interfaces/query_responses/block_response.hpp"
#include "interfaces/query_responses/query_response.hpp"
#include "validation/utils.hpp"

namespace iroha {
  namespace torii {

    QueryProcessorImpl::QueryProcessorImpl(
        std::shared_ptr<ametsuchi::Storage> storage,
        std::shared_ptr<ametsuchi::QueryExecutorFactory> qry_exec,
        std::shared_ptr<iroha::PendingTransactionStorage> pending_transactions,
        std::shared_ptr<shared_model::interface::QueryResponseFactory>
            response_factory,
        logger::Logger log)
        : storage_{std::move(storage)},
          qry_exec_{std::move(qry_exec)},
          pending_transactions_{std::move(pending_transactions)},
          response_factory_{std::move(response_factory)},
          log_{std::move(log)} {
      storage_->on_commit().subscribe(
          [this](std::shared_ptr<shared_model::interface::Block> block) {
            auto block_response =
                response_factory_->createBlockQueryResponse(clone(*block));
            blocks_query_subject_.get_subscriber().on_next(
                std::move(block_response));
          });
    }

    std::unique_ptr<shared_model::interface::QueryResponse>
    QueryProcessorImpl::queryHandle(const shared_model::interface::Query &qry) {
      auto executor = qry_exec_->createQueryExecutor(pending_transactions_,
                                                     response_factory_);
      if (not executor) {
        log_->error("Cannot create query executor");
        return nullptr;
      }

      return executor.value()->validateAndExecute(qry, true);
    }

    rxcpp::observable<iroha::torii::QueryProcessorImpl::wBlockQueryResponse>
    QueryProcessorImpl::blocksQueryHandle(
        const shared_model::interface::BlocksQuery &qry) {
      auto exec = qry_exec_->createQueryExecutor(pending_transactions_,
                                                 response_factory_);
      if (not exec or not(exec | [this, &qry](const auto &executor) {
            return executor->validate(
                qry, storage_->getBlockQuery()->getTopBlockHeight(), true);
          })) {
        std::shared_ptr<shared_model::interface::BlockQueryResponse> response =
            response_factory_->createBlockQueryResponse("stateful invalid");
        return rxcpp::observable<>::just(response);
      }

      if (not qry.height()) {
        // default case - return blocks starting from the next one
        return blocks_query_subject_.get_observable();
      }

      // buffer of blocks in case something was committed between retrieving
      // blocks from ledger and final concatenation of the observables; without
      // the buffer, such blocks would disappear
      std::vector<wBlockQueryResponse> blocks_buffer;
      auto buffer_subscription =
          blocks_query_subject_.get_observable().subscribe(
              [&blocks_buffer](auto block_resp) {
                blocks_buffer.push_back(std::move(block_resp));
              });

      auto desired_blocks =
          storage_->getBlockQuery()->getBlocksFrom(*qry.height());

      return rxcpp::observable<>::iterate(std::move(desired_blocks))
          // transform retrieved blocks to query responses
          .map([this](std::shared_ptr<shared_model::interface::Block> block) {
            return wBlockQueryResponse{
                response_factory_->createBlockQueryResponse(clone(*block))};
          })
          // concatenate retrieved blocks with blocks buffer and filter out
          // repeated ones by checking the height - if it is less or equal than
          // the query's one, this block is already in the set of retrieved
          .concat(
              rxcpp::observable<>::iterate(blocks_buffer)
                  .filter([this, &qry](wBlockQueryResponse block_query_resp) {
                    return visit_in_place(
                        block_query_resp->get(),
                        [&qry](const shared_model::interface::BlockResponse
                                   &block_resp) {
                          return block_resp.block().height() > *qry.height();
                        },
                        [this](const auto &err_resp) {
                          log_->error(
                              "Commit observable returned error block "
                              "response - definitely should not happen");
                          return false;
                        });
                  }))
          .finally([buffer_subscription] { buffer_subscription.unsubscribe(); })
          // finally concatenate observer to all future blocks
          .concat(blocks_query_subject_.get_observable());
    }

  }  // namespace torii
}  // namespace iroha
