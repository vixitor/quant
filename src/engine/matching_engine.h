#include <cstddef>
#include <cstdint>
#include <queue>
#include <set>
#include <vector>
namespace engine {
using OrderID = uint64_t;
using SymbolID = uint64_t;
using Price = int64_t;
using Timestamp = int64_t;
using Qty = uint64_t;
using TradeID = uint64_t;
enum class Side : uint8_t {
  Buy,
  Sell,
};
static TradeID enum OrderType : uint8_t {
  Limit,
  Market,
};

struct OrderRequest {
  OrderID id;
  SymbolID sym;
  Side side;
  OrderType type;
  Price price;
  Qty qty;
  Timestamp ts;
};

struct CancelRequest {
  OrderID id;
  SymbolID sym;
  Timestamp ts;
};

struct Fill {
  TradeID trade_id;
  OrderID taker_id;
  OrderID maker_id;
  SymbolID sym;
  Side taker_side;
  Price price;
  Qty qty;
  Timestamp ts;
};

class OrderBook {};
struct OrderNode {
  OrderNode* next;
  OrderNode* prev;
  OrderRequest order;
};
struct OrderHeader {
  Price price;
  OrderNode* firstorder;
};
using Price2Header = std::unordered_map<Price, OrderHeader*>;

using PriceTable = std::set<Price>;
struct Book {
  PriceTable bBook;
  PriceTable sBook;
  Price2Header bHeaders;
  Price2Header sHeaders;
};
class MatchingEngine {
 public:
  std::vector<Fill> on_order(const OrderRequest& req);
  bool on_cancel(const CancelRequest& req);
  const OrderBook& book(SymbolID sym) const;
  TradeID getTradeID();
  Timestamp getEngineTime();

 private:
  static TradeID tradeid;
  static Timestamp engine_time;
  std::unordered_map<SymbolID, Book*> Symbol2Book;
};
TradeID MatchingEngine::tradeid = 100000000;
Timestamp MatchingEngine::engine_time = 0;
}  // namespace engine