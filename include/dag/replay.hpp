#pragma once

#include "dag/types.hpp"

#include <string>
#include <unordered_map>

namespace dag {

struct ReplayFilter {
    std::string workflow;
    std::string run_id;
};

struct ReplaySummary {
    int total{0};
    int succeeded{0};
    int failed{0};
    int timed_out{0};
    int skipped{0};
    int running_or_pending{0};
};

class EventReplayer {
public:
    std::unordered_map<std::string, TaskStatus> replay_file(const std::string& event_log_path) const;
    std::unordered_map<std::string, TaskStatus> replay_file(
        const std::string& event_log_path, const ReplayFilter& filter) const;
    ReplaySummary summarize(const std::unordered_map<std::string, TaskStatus>& states) const;
};

}  // namespace dag
