#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <lobcore/allocation.hpp>
#include <lobcore/book.hpp>
#include <lobcore/event_log.hpp>
#include <lobcore/kernel/batch.hpp>
#include <lobcore/kernel/config.hpp>
#include <lobcore/kernel/event.hpp>
#include <lobcore/kernel/kernel.hpp>
#include <lobcore/kernel/market.hpp>
#include <lobcore/kernel/order_message.hpp>
#include <lobcore/kernel/rng.hpp>
#include <lobcore/types.hpp>

namespace py = pybind11;

namespace {

py::bytes log_to_bytes(const std::vector<lobcore::LogRecord>& log) {
  if (log.empty()) {
    return py::bytes();
  }
  // Serialize fields into zeroed records. C++ memberwise copies are not required
  // to preserve padding, so do not export vector storage as an object dump.
  using lobcore::LogRecord;
  std::string bytes(log.size() * sizeof(LogRecord), '\0');
  for (std::size_t i = 0; i < log.size(); ++i) {
    const auto& rec = log[i];
    char* destination = bytes.data() + i * sizeof(LogRecord);
    const auto copy = [destination](std::size_t offset, const auto& value) {
      std::memcpy(destination + offset, &value, sizeof(value));
    };
    copy(offsetof(LogRecord, seq), rec.seq);
    copy(offsetof(LogRecord, kind), rec.kind);
    copy(offsetof(LogRecord, side), rec.side);
    copy(offsetof(LogRecord, reason), rec.reason);
    copy(offsetof(LogRecord, decided_at), rec.decided_at);
    copy(offsetof(LogRecord, received_at), rec.received_at);
    copy(offsetof(LogRecord, order_id), rec.order_id);
    copy(offsetof(LogRecord, maker_id), rec.maker_id);
    copy(offsetof(LogRecord, price), rec.price);
    copy(offsetof(LogRecord, qty), rec.qty);
    copy(offsetof(LogRecord, best_bid_price), rec.best_bid_price);
    copy(offsetof(LogRecord, best_bid_qty), rec.best_bid_qty);
    copy(offsetof(LogRecord, best_ask_price), rec.best_ask_price);
    copy(offsetof(LogRecord, best_ask_qty), rec.best_ask_qty);
  }
  return py::bytes(bytes);
}

lobcore::BatchAction call_python_step(const py::object& step_fn,
                                      const lobcore::BatchObservation& obs) {
  py::gil_scoped_acquire gil;
  py::object result = step_fn(obs);
  return result.cast<lobcore::BatchAction>();
}

}  // namespace

PYBIND11_MODULE(_core, m) {
  m.doc() = "lobcore Python bindings (Stage 5)";
  m.attr("LOG_RECORD_SIZE") = sizeof(lobcore::LogRecord);
  m.attr("LOG_RECORD_ALIGN") = alignof(lobcore::LogRecord);

  py::enum_<lobcore::Side>(m, "Side")
      .value("Buy", lobcore::Side::Buy)
      .value("Sell", lobcore::Side::Sell);

  py::class_<lobcore::Level>(m, "Level")
      .def(py::init<>())
      .def_readwrite("price", &lobcore::Level::price)
      .def_readwrite("qty", &lobcore::Level::qty);

  py::class_<lobcore::Order>(m, "Order")
      .def(py::init<>())
      .def_readwrite("id", &lobcore::Order::id)
      .def_readwrite("side", &lobcore::Order::side)
      .def_readwrite("price", &lobcore::Order::price)
      .def_readwrite("qty", &lobcore::Order::qty)
      .def_readwrite("decided_at", &lobcore::Order::decided_at);

  py::class_<lobcore::Trade>(m, "Trade")
      .def_readonly("maker_id", &lobcore::Trade::maker_id)
      .def_readonly("taker_id", &lobcore::Trade::taker_id)
      .def_readonly("price", &lobcore::Trade::price)
      .def_readonly("qty", &lobcore::Trade::qty);

  py::class_<lobcore::OrderBook>(m, "OrderBook")
      .def(py::init<>())
      .def("add_limit", &lobcore::OrderBook::add_limit)
      .def("cancel", &lobcore::OrderBook::cancel)
      .def("best_bid", &lobcore::OrderBook::best_bid)
      .def("best_ask", &lobcore::OrderBook::best_ask)
      .def("remaining", &lobcore::OrderBook::remaining)
      .def("state_hash", &lobcore::OrderBook::state_hash);

  py::class_<lobcore::Rng>(m, "Rng")
      .def("next_u64", &lobcore::Rng::next_u64)
      .def("uniform", &lobcore::Rng::uniform)
      .def("normal", &lobcore::Rng::normal)
      .def("exponential", &lobcore::Rng::exponential)
      .def("uniform_array", [](lobcore::Rng& rng, std::size_t n) {
        py::array_t<double> out(static_cast<py::ssize_t>(n));
        auto buf = out.mutable_unchecked<1>();
        for (std::size_t i = 0; i < n; ++i) {
          buf(i) = rng.uniform();
        }
        return out;
      });

  py::class_<lobcore::MarketSnapshot>(m, "MarketSnapshot")
      .def(py::init<>())
      .def_readwrite("best_bid", &lobcore::MarketSnapshot::best_bid)
      .def_readwrite("best_ask", &lobcore::MarketSnapshot::best_ask);

  py::class_<lobcore::BatchObservation>(m, "BatchObservation")
      .def(py::init<>())
      .def_readwrite("now", &lobcore::BatchObservation::now)
      .def_readwrite("agent_ids", &lobcore::BatchObservation::agent_ids)
      .def_readwrite("markets", &lobcore::BatchObservation::markets);

  py::class_<lobcore::AddLimit>(m, "AddLimit")
      .def(py::init<>())
      .def_readwrite("id", &lobcore::AddLimit::id)
      .def_readwrite("side", &lobcore::AddLimit::side)
      .def_readwrite("price", &lobcore::AddLimit::price)
      .def_readwrite("qty", &lobcore::AddLimit::qty)
      .def_readwrite("decided_at", &lobcore::AddLimit::decided_at);

  py::class_<lobcore::CancelOrder>(m, "CancelOrder")
      .def(py::init<>())
      .def_readwrite("id", &lobcore::CancelOrder::id);

  py::class_<lobcore::OrderSubmission>(m, "OrderSubmission")
      .def(py::init<>())
      .def_readwrite("agent_id", &lobcore::OrderSubmission::agent_id)
      .def_readwrite("market_id", &lobcore::OrderSubmission::market_id)
      .def_property(
          "msg",
          [](const lobcore::OrderSubmission& s) -> py::object {
            if (const auto* add = std::get_if<lobcore::AddLimit>(&s.msg)) {
              return py::cast(*add);
            }
            return py::cast(std::get<lobcore::CancelOrder>(s.msg));
          },
          [](lobcore::OrderSubmission& s, py::object obj) {
            if (py::isinstance<lobcore::AddLimit>(obj)) {
              s.msg = obj.cast<lobcore::AddLimit>();
            } else {
              s.msg = obj.cast<lobcore::CancelOrder>();
            }
          });

  py::class_<lobcore::BatchAction>(m, "BatchAction")
      .def(py::init<>())
      .def_readwrite("orders", &lobcore::BatchAction::orders)
      .def_readwrite("next_wakeups", &lobcore::BatchAction::next_wakeups);

  py::class_<lobcore::BatchRejectCounts>(m, "BatchRejectCounts")
      .def_readonly("invalid_wakeup", &lobcore::BatchRejectCounts::invalid_wakeup)
      .def_readonly("order_from_sleeping_agent",
                    &lobcore::BatchRejectCounts::order_from_sleeping_agent);

  py::class_<lobcore::KernelConfig>(m, "KernelConfig")
      .def(py::init<>())
      .def_readwrite("end_time", &lobcore::KernelConfig::end_time)
      .def_readwrite("master_seed", &lobcore::KernelConfig::master_seed);

  py::class_<lobcore::Kernel>(m, "Kernel")
      .def(py::init<lobcore::KernelConfig>(), py::arg("config") = lobcore::KernelConfig{})
      .def(
          "add_market",
          [](lobcore::Kernel& k, const std::string& rule) {
            std::unique_ptr<lobcore::AllocationRule> r;
            if (rule == "pro_rata") {
              r = std::make_unique<lobcore::ProRata>();
            } else {
              r = std::make_unique<lobcore::PriceTimePriority>();
            }
            return k.add_market(std::make_unique<lobcore::ContinuousMarket>(std::move(r)));
          },
          py::arg("rule") = "price_time")
      .def(
          "add_batch_agents",
          [](lobcore::Kernel& k, py::object step_fn, std::size_t n) {
            return k.add_batch_agents(
                [step_fn](const lobcore::BatchObservation& obs) {
                  return call_python_step(step_fn, obs);
                },
                n);
          },
          py::arg("step_fn"), py::arg("n"))
      .def("schedule_wakeup",
           [](lobcore::Kernel& k, lobcore::Timestamp t, lobcore::AgentId id) {
             k.schedule(t, lobcore::AgentWakeup{id});
           })
      .def("run", &lobcore::Kernel::run)
      .def("now", &lobcore::Kernel::now)
      .def("agent_count", &lobcore::Kernel::agent_count)
      .def("market_count", &lobcore::Kernel::market_count)
      .def("master_seed", &lobcore::Kernel::master_seed)
      .def("end_time", &lobcore::Kernel::end_time)
      .def("batch_rejects", &lobcore::Kernel::batch_rejects)
      .def("market_state_hash", &lobcore::Kernel::market_state_hash)
      .def("market_best_bid",
           [](const lobcore::Kernel& k, lobcore::MarketId id) { return k.market(id).best_bid(); })
      .def("market_best_ask",
           [](const lobcore::Kernel& k, lobcore::MarketId id) { return k.market(id).best_ask(); })
      .def("market_remaining",
           [](const lobcore::Kernel& k, lobcore::MarketId mid, lobcore::OrderId oid) {
             return k.market(mid).remaining(oid);
           })
      .def("log_bytes", [](const lobcore::Kernel& k) { return log_to_bytes(k.emitted_log()); })
      .def("suppress_agent", &lobcore::Kernel::suppress_agent)
      .def("is_agent_suppressed", &lobcore::Kernel::is_agent_suppressed)
      // Streams belong to the kernel. Copying one on each Python lookup
      // restarts its sequence; keep the owner alive while a stream is held.
      .def("sentinel_rng", &lobcore::Kernel::sentinel_rng,
           py::return_value_policy::reference_internal)
      .def("rng_for", &lobcore::Kernel::rng_for,
           py::return_value_policy::reference_internal);
}
