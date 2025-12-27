#include <gtest/gtest.h>

#include "engine/matching_engine.h"

using engine::CancelRequest;
using engine::MatchingEngine;
using engine::OrderID;
using engine::OrderRequest;
using engine::OrderType;
using engine::Price;
using engine::Qty;
using engine::Side;
using engine::SymbolID;
using engine::Timestamp;

namespace {

constexpr SymbolId kSym = 1;

OrderRequest limit_buy(OrderID id, Price px, Qty qty, Timestamp ts) {
  return OrderRequest{.id = id,
                      .sym = kSym,
                      .side = Side::Buy,
                      .type = OrderType::Limit,
                      .price = px,
                      .qty = qty,
                      .ts = ts};
}

OrderRequest limit_sell(OrderID id, Price px, Qty qty, Timestamp ts) {
  return OrderRequest{.id = id,
                      .sym = kSym,
                      .side = Side::Sell,
                      .type = OrderType::Limit,
                      .price = px,
                      .qty = qty,
                      .ts = ts};
}

OrderRequest market_buy(OrderID id, Qty qty, Timestamp ts) {
  return OrderRequest{.id = id,
                      .sym = kSym,
                      .side = Side::Buy,
                      .type = OrderType::Market,
                      .price = 0,
                      .qty = qty,
                      .ts = ts};
}

OrderRequest market_sell(OrderID id, Qty qty, Timestamp ts) {
  return OrderRequest{.id = id,
                      .sym = kSym,
                      .side = Side::Sell,
                      .type = OrderType::Market,
                      .price = 0,
                      .qty = qty,
                      .ts = ts};
}

CancelRequest cancel(OrderID id, Timestamp ts) {
  return CancelRequest{.id = id, .sym = kSym, .ts = ts};
}

}  // namespace

// Partial cross should fill available size and leave the remainder resting for
// later matches.
TEST(MatchingEngineTest, LimitCrossPartialFillLeavesRestOnBook) {
  MatchingEngine eng;

  auto no_fills = eng.on_order(limit_sell(1, 101, 10, 1'000));
  EXPECT_TRUE(no_fills.empty());

  auto fills = eng.on_order(limit_buy(2, 102, 6, 2'000));
  ASSERT_EQ(fills.size(), 1);
  EXPECT_EQ(fills[0].maker_id, 1);
  EXPECT_EQ(fills[0].taker_id, 2);
  EXPECT_EQ(fills[0].price, 101);
  EXPECT_EQ(fills[0].qty, 6);
  EXPECT_EQ(fills[0].taker_side, Side::Buy);

  // Remaining ask quantity (4) should be matched by the next market buy.
  auto fills2 = eng.on_order(market_buy(3, 10, 3'000));
  ASSERT_EQ(fills2.size(), 1);
  EXPECT_EQ(fills2[0].maker_id, 1);
  EXPECT_EQ(fills2[0].price, 101);
  EXPECT_EQ(fills2[0].qty, 4);
}

// Market buy should sweep best ask levels in price priority order.
TEST(MatchingEngineTest, MarketOrderConsumesBestPriceFirstAcrossLevels) {
  MatchingEngine eng;
  eng.on_order(limit_sell(1, 100, 3, 1'000));
  eng.on_order(limit_sell(2, 101, 4, 2'000));

  auto fills = eng.on_order(market_buy(3, 5, 3'000));
  ASSERT_EQ(fills.size(), 2);
  EXPECT_EQ(fills[0].maker_id, 1);
  EXPECT_EQ(fills[0].price, 100);
  EXPECT_EQ(fills[0].qty, 3);

  EXPECT_EQ(fills[1].maker_id, 2);
  EXPECT_EQ(fills[1].price, 101);
  EXPECT_EQ(fills[1].qty, 2);
}

// Orders at the same price should honor time priority.
TEST(MatchingEngineTest, PriceTimePriorityWithinSameLevel) {
  MatchingEngine eng;
  eng.on_order(limit_sell(1, 100, 1, 1'000));  // earlier
  eng.on_order(limit_sell(2, 100, 2, 2'000));  // later at same price

  auto fills = eng.on_order(market_buy(3, 2, 3'000));
  ASSERT_EQ(fills.size(), 2);
  EXPECT_EQ(fills[0].maker_id, 1);
  EXPECT_EQ(fills[0].qty, 1);
  EXPECT_EQ(fills[0].price, 100);

  EXPECT_EQ(fills[1].maker_id, 2);
  EXPECT_EQ(fills[1].qty, 1);
  EXPECT_EQ(fills[1].price, 100);
}

// Resting bid/ask stay on the book until a later crossing order executes at
// maker price.
TEST(MatchingEngineTest, RestingLimitOrdersMatchWhenCrossedLater) {
  MatchingEngine eng;
  auto fills1 = eng.on_order(limit_buy(1, 99, 5, 1'000));
  auto fills2 = eng.on_order(limit_sell(2, 101, 5, 2'000));
  EXPECT_TRUE(fills1.empty());
  EXPECT_TRUE(fills2.empty());

  // Crossing sell should execute against resting bid at the bid price (maker
  // price).
  auto fills3 = eng.on_order(market_sell(3, 3, 3'000));
  ASSERT_EQ(fills3.size(), 1);
  EXPECT_EQ(fills3[0].maker_id, 1);
  EXPECT_EQ(fills3[0].price, 99);
  EXPECT_EQ(fills3[0].qty, 3);
}

// More aggressive sell should still execute at the resting bid price.
TEST(MatchingEngineTest, AggressiveSellExecutesAtRestingBidPrice) {
  MatchingEngine eng;
  eng.on_order(limit_buy(1, 101, 4, 1'000));  // resting bid

  auto fills =
      eng.on_order(limit_sell(2, 100, 2, 2'000));  // more aggressive price
  ASSERT_EQ(fills.size(), 1);
  EXPECT_EQ(fills[0].maker_id, 1);
  EXPECT_EQ(fills[0].taker_id, 2);
  EXPECT_EQ(fills[0].price, 101);  // execute at resting bid price
  EXPECT_EQ(fills[0].qty, 2);
  EXPECT_EQ(fills[0].taker_side, Side::Sell);
}

// Cancel should remove the resting order so later market orders see nothing.
TEST(MatchingEngineTest, CancelRemovesOrder) {
  MatchingEngine eng;
  eng.on_order(limit_sell(1, 101, 5, 1'000));
  EXPECT_TRUE(eng.on_cancel(cancel(1, 1'500)));

  auto fills = eng.on_order(market_buy(2, 5, 2'000));
  EXPECT_TRUE(fills.empty());
}

TEST(MatchingEngineTest, CancelUnknownOrderReturnsFalse) {
  MatchingEngine eng;
  EXPECT_FALSE(eng.on_cancel(cancel(42, 1'000)));
}

// Market orders on an empty book should generate no fills.
TEST(MatchingEngineTest, MarketOnEmptyBookProducesNoFills) {
  MatchingEngine eng;
  auto fills = eng.on_order(market_buy(1, 10, 1'000));
  EXPECT_TRUE(fills.empty());

  auto fills2 = eng.on_order(market_sell(2, 10, 2'000));
  EXPECT_TRUE(fills2.empty());
}
