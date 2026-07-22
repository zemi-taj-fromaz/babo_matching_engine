# babobook API contract

This document defines the public behavior of `babo::book::matching_book<SIZE>`.
The benchmark C ABI is a separate integration boundary documented in
`benchmark/api/matching_engine_api.h`.

## Minimal integration

Include `book/matching_book.h`, construct a scoped book, register an optional
`OrderListener`, and submit `simple::SimpleOrder` values. The book stores resting
and parked orders by value; callers subsequently address them by order ID.

See [`libs/babobook/examples/basic_book.cpp`](../libs/babobook/examples/basic_book.cpp)
and its adjacent `CMakeLists.txt` for a complete add/match/cancel/depth program
and the minimal CMake linkage.

## Complexity

Let:

- `L` be the number of price levels on one side;
- `F` be the number of makers examined or filled by an incoming order;
- `E` be the number of price levels emptied by those fills.

| Operation | Complexity | Notes |
|---|---:|---|
| Find resting order by ID | average O(1) | `std::unordered_map<id, slot>`; standard hash-table worst-case applies. |
| Cancel within a non-empty level | average O(1) | ID lookup plus intrusive PIN-slot unlink; no scan and no payload movement. |
| Cancel the last order at a level | average O(1) + O(log L) | The order unlink is constant-time; removing the now-empty RB-tree price level is O(log L). |
| Rest at an existing/new level | O(log L) general case | Best/second-best fast paths can avoid the tree search; hash insertion is average O(1). |
| Match incoming order | O(F + E log L) after lookup | The global priority chain advances O(1) per maker; each emptied price level requires RB-tree removal. |
| Same-price quantity replace | average O(1) | Direct ID-to-slot lookup and in-place quantity update; time priority is retained. |
| Reprice | cancel + add | Reinserted order loses time priority and may immediately match. |
| `depth()` | O(SIZE) | Pull-based snapshot of the best `SIZE` levels per side; no eager depth work occurs on the matching hot path. |

“O(1) cancel” refers specifically to locating and unlinking an order from a
populated price level: babobook does not rescan the orders at that price. A cancel
that also removes the price level pays the RB-tree O(log L) teardown. For a fixed
number of populated levels and increasing orders per level, cancel cost therefore
remains approximately flat with book depth, as shown by the scaling benchmark.

## Threading and reentrancy

`matching_book` is a single-threaded matching core. Its four `narb_tree` members
and the two process-wide allocation pools are unsynchronized.

- Construct, operate, and destroy every book in a process/module on one matching
  thread.
- Multiple books may be owned sequentially by that same thread.
- Concurrent books on different threads are unsupported because they share the
  pools.
- Listener callbacks execute synchronously inside the operation that caused them.
- A listener must not call back into or mutate the same book (reentrancy is
  unsupported) and must not throw exceptions.

## Ownership and lifetime

- A `matching_book` owns four trees: live bids, live asks, parked buy stops, and
  parked sell stops.
- Trees store orders by value in PIN slots. The caller's original `SimpleOrder`
  is not updated after submission.
- The listener is non-owning. It must outlive every book operation for which it is
  registered; pass `nullptr` to detach it.
- Destroying a tree returns its live PIN nodes and price-level descriptors to the
  shared pools. Pool backing blocks are retained for reuse until process/module
  shutdown.
- Books must have ordinary scoped lifetime. Static-storage-duration books are
  unsupported because their destruction may race the function-static pools.
- `matching_book`/`narb_tree` are intentionally non-copyable and non-movable.

## IDs, prices, and quantities

The native library currently uses 32-bit unsigned IDs, prices, and quantities.

- Order ID `0` is reserved as the empty/default sentinel. IDs must be unique for
  the lifetime of a book; the venue/integrator is responsible for uniqueness.
- Limit prices are positive `std::uint32_t` integer ticks. Price `0` represents a
  market order and is also the `PRICE_UNCHANGED` sentinel in replace APIs.
- Order quantity is `std::uint32_t`; zero-quantity submissions are rejected.
- Aggregate quantity at one price level is also `std::uint32_t` and must not
  exceed `UINT32_MAX`.
- Fill cost is currently `quantity * maker_price` in `std::uint32_t`. Integrators
  must keep both each fill cost and an order's accumulated fill cost within
  `UINT32_MAX`.
- Quantity changes use `std::int32_t`; callers must keep the resulting total and
  level aggregate within their representable ranges.

The native API does not currently perform checked arithmetic for these aggregate
limits. The benchmark generator stays inside them; external integrations must
validate their input domain before submission.

## Listener callback rules

Callbacks are synchronous and preserve matching order.

- `on_accept(id)` occurs once for every positive-quantity submission, before it
  is parked, matched, or rested.
- `on_reject(id, reason)` occurs for a rejected submission; currently zero
  quantity is the native rejection case.
- `on_fill(taker, maker, qty, cost)` occurs once per execution. The first ID is
  the incoming aggressor, the second is the resting maker, and execution uses the
  maker's price. `cost == qty * maker_price` within the numeric limits above.
- `on_cancel(id)` occurs after a resting or parked order is removed.
- `on_cancel_reject(id, reason)` occurs when the ID is not present.
- `on_replace(...)` occurs for a successful size change or reprice;
  `on_replace_reject(...)` occurs when the ID is not resting.

Callbacks are notifications, not ownership transfers. Callback arguments and
reason strings must not be retained as mutable book state.

## Depth snapshots

`depth()` rebuilds and returns a reference to storage owned by the book. Bids are
ordered highest price first; asks lowest price first. Empty entries use
`INVALID_LEVEL_PRICE`. A snapshot is overwritten by the next `depth()` call and
becomes invalid when the book is destroyed. Book operations do not automatically
refresh a previously obtained snapshot.
