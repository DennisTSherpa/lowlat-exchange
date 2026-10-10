#include "exchange/matching_engine.hpp"

namespace exchange {

std::vector<Trade> MatchingEngine::process(const Event& event) {
    if (log_ != nullptr) {
        log_->append(event);
    }

    switch (event.type) {
        case EventType::AddLimit:
            return book_.add_limit(event.order);
        case EventType::AddMarket:
            return book_.add_market(event.order);
        case EventType::Cancel:
            book_.cancel(event.order.id);
            return {};
    }
    return {};
}

std::vector<Trade> replay(const std::string& log_path, MatchingEngine& engine) {
    std::vector<Trade> all_trades;
    for (const Event& event : read_event_log(log_path)) {
        for (const Trade& trade : engine.process(event)) {
            all_trades.push_back(trade);
        }
    }
    return all_trades;
}

}  // namespace exchange
