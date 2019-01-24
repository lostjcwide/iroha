/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <boost/variant.hpp>
#include "backend/protobuf/block.hpp"
#include "backend/protobuf/proto_query_response_factory.hpp"
#include "backend/protobuf/query_responses/proto_error_query_response.hpp"
#include "cryptography/crypto_provider/crypto_defaults.hpp"
#include "cryptography/keypair.hpp"
#include "framework/test_subscriber.hpp"
#include "interfaces/query_responses/block_query_response.hpp"
#include "interfaces/query_responses/block_response.hpp"
#include "module/irohad/ametsuchi/ametsuchi_mocks.hpp"
#include "module/irohad/validation/validation_mocks.hpp"
#include "module/shared_model/builders/protobuf/test_block_builder.hpp"
#include "module/shared_model/builders/protobuf/test_query_builder.hpp"
#include "module/shared_model/builders/protobuf/test_transaction_builder.hpp"
#include "network/ordering_gate.hpp"
#include "torii/processor/query_processor_impl.hpp"
#include "utils/query_error_response_visitor.hpp"
#include "validators/permissions.hpp"

using namespace iroha;
using namespace iroha::ametsuchi;
using namespace iroha::validation;
using namespace framework::test_subscriber;

using ::testing::_;
using ::testing::A;
using ::testing::ByMove;
using ::testing::Invoke;
using ::testing::Return;

class QueryProcessorTest : public ::testing::Test {
 public:
  void SetUp() override {
    qry_exec = std::make_shared<MockQueryExecutor>();
    storage = std::make_shared<MockStorage>();
    query_response_factory =
        std::make_shared<shared_model::proto::ProtoQueryResponseFactory>();
    qpi = std::make_shared<torii::QueryProcessorImpl>(
        storage, storage, nullptr, query_response_factory);
    block_queries = std::make_shared<MockBlockQuery>();
    EXPECT_CALL(*storage, getBlockQuery())
        .WillRepeatedly(Return(block_queries));
    EXPECT_CALL(*storage, createQueryExecutor(_, _))
        .WillRepeatedly(Return(
            boost::make_optional(std::shared_ptr<QueryExecutor>(qry_exec))));
  }

  template <bool SetHeight = false>
  auto getBlocksQuery(const std::string &creator_account_id,
                      shared_model::interface::types::HeightType height = 0) {
    auto half_builder = TestUnsignedBlocksQueryBuilder()
                            .createdTime(kCreatedTime)
                            .creatorAccountId(creator_account_id)
                            .queryCounter(kCounter);
    if (SetHeight) {
      return half_builder.height(height)
          .build()
          .signAndAddSignature(keypair)
          .finish();
    }
    return half_builder.build().signAndAddSignature(keypair).finish();
  }

  const decltype(iroha::time::now()) kCreatedTime = iroha::time::now();
  const std::string kAccountId = "account@domain";
  const uint64_t kCounter = 1048576;
  shared_model::crypto::Keypair keypair =
      shared_model::crypto::DefaultCryptoAlgorithmType::generateKeypair();

  std::vector<shared_model::interface::types::PubkeyType> signatories = {
      keypair.publicKey()};
  std::shared_ptr<MockQueryExecutor> qry_exec;
  std::shared_ptr<MockBlockQuery> block_queries;
  std::shared_ptr<MockStorage> storage;
  std::shared_ptr<shared_model::interface::QueryResponseFactory>
      query_response_factory;
  std::shared_ptr<torii::QueryProcessorImpl> qpi;
};

/**
 * @given QueryProcessorImpl and GetAccountDetail query
 * @when queryHandle called at normal flow
 * @then the mocked value of validateAndExecute is returned
 */
TEST_F(QueryProcessorTest, QueryProcessorWhereInvokeInvalidQuery) {
  auto qry = TestUnsignedQueryBuilder()
                 .creatorAccountId(kAccountId)
                 .getAccountDetail(kAccountId)
                 .build()
                 .signAndAddSignature(keypair)
                 .finish();
  auto *qry_resp =
      query_response_factory->createAccountDetailResponse("", qry.hash())
          .release();

  EXPECT_CALL(*qry_exec, validateAndExecute_(_)).WillOnce(Return(qry_resp));

  auto response = qpi->queryHandle(qry);
  ASSERT_TRUE(response);
  ASSERT_NO_THROW(
      boost::get<const shared_model::interface::AccountDetailResponse &>(
          response->get()));
}

/**
 * @given QueryProcessorImpl and GetAccountDetail query with wrong signature
 * @when queryHandle called at normal flow
 * @then Query Processor returns StatefulFailed response
 */
TEST_F(QueryProcessorTest, QueryProcessorWithWrongKey) {
  auto query = TestUnsignedQueryBuilder()
                   .creatorAccountId(kAccountId)
                   .getAccountDetail(kAccountId)
                   .build()
                   .signAndAddSignature(
                       shared_model::crypto::DefaultCryptoAlgorithmType::
                           generateKeypair())
                   .finish();
  auto *qry_resp = query_response_factory
                       ->createErrorQueryResponse(
                           shared_model::interface::QueryResponseFactory::
                               ErrorQueryType::kStatefulFailed,
                           "query signatories did not pass validation",
                           3,
                           query.hash())
                       .release();

  EXPECT_CALL(*qry_exec, validateAndExecute_(_)).WillOnce(Return(qry_resp));

  auto response = qpi->queryHandle(query);
  ASSERT_TRUE(response);
  ASSERT_NO_THROW(boost::apply_visitor(
      shared_model::interface::QueryErrorResponseChecker<
          shared_model::interface::StatefulFailedErrorResponse>(),
      response->get()));
}

/**
 * @given account, ametsuchi queries
 * @when valid block query without specified height is sent
 * @then Query Processor should start emitting BlockQueryResponses to the
 * observable
 */
TEST_F(QueryProcessorTest, GetBlocksQueryWithoutHeight) {
  auto block_number = 5;
  auto block_query = getBlocksQuery(kAccountId);

  EXPECT_CALL(*qry_exec, validate(_, _, _)).WillOnce(Return(true));
  EXPECT_CALL(*block_queries, getTopBlockHeight()).WillOnce(Return(1));
  EXPECT_CALL(*block_queries, getBlocksFrom(_)).Times(0);

  auto wrapper = make_test_subscriber<CallExact>(
      qpi->blocksQueryHandle(block_query), block_number);
  wrapper.subscribe([](auto response) {
    ASSERT_NO_THROW({
      boost::get<const shared_model::interface::BlockResponse &>(
          response->get());
    });
  });
  for (int i = 0; i < block_number; i++) {
    storage->notifier.get_subscriber().on_next(
        clone(TestBlockBuilder().build()));
  }
  ASSERT_TRUE(wrapper.validate());
}

/**
 * @given account, ametsuchi queries @and mocked ledger with some blocks
 * @when valid block query with specified height is sent @and this height is
 * less than the ledger's one
 * @then Query Processor should emit block of this height
 */
TEST_F(QueryProcessorTest, GetBlocksQueryWithHeight) {
  auto ledger_height = 4;
  auto query_height = 2;
  auto blocks_query = getBlocksQuery<true>(kAccountId, query_height);

  EXPECT_CALL(*qry_exec, validate(_, _, _)).WillOnce(Return(true));
  EXPECT_CALL(*block_queries, getTopBlockHeight())
      .WillOnce(Return(ledger_height));

  // these blocks are the ones lying in the ledger for current moment
  std::vector<std::unique_ptr<shared_model::interface::Block>> all_blocks;
  for (auto i = 0; i < ledger_height; ++i) {
    all_blocks.push_back(clone(TestBlockBuilder().height(i + 1).build()));
  }

  EXPECT_CALL(*block_queries, getBlock(query_height))
      .WillOnce(Return(ByMove(
          iroha::expected::makeValue(clone(*all_blocks[query_height - 1])))));

  auto wrapper =
      make_test_subscriber<CallExact>(qpi->blocksQueryHandle(blocks_query), 1);
  wrapper.subscribe([query_height](auto response) {
    ASSERT_NO_THROW({
      const auto &block =
          boost::get<const shared_model::interface::BlockResponse &>(
              response->get())
              .block();
      ASSERT_EQ(block.height(), query_height);
    });
  });

  ASSERT_TRUE(wrapper.validate());
}

/**
 * @given account, ametsuchi queries
 * @when valid block query is invalid (no can_get_blocks permission)
 * @then Query Processor should return an observable with BlockError
 */
TEST_F(QueryProcessorTest, GetBlocksQueryNoPerms) {
  auto block_number = 5;
  auto block_query = getBlocksQuery(kAccountId);

  EXPECT_CALL(*qry_exec, validate(_, _, _)).WillOnce(Return(false));

  auto wrapper =
      make_test_subscriber<CallExact>(qpi->blocksQueryHandle(block_query), 1);
  wrapper.subscribe([](auto response) {
    ASSERT_NO_THROW({
      boost::get<const shared_model::interface::BlockErrorResponse &>(
          response->get());
    });
  });
  for (int i = 0; i < block_number; i++) {
    storage->notifier.get_subscriber().on_next(
        clone(TestBlockBuilder()
                  .height(1)
                  .prevHash(shared_model::crypto::Hash(std::string(32, '0')))
                  .build()));
  }
  ASSERT_TRUE(wrapper.validate());
}
