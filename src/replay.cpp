#include "dag/replay.hpp"

#include <fstream>
#include <optional>
#include <stdexcept>
#include <vector>

namespace dag {
namespace {

std::string extract_json_string(const std::string& line, const std::string& key) {
    const std::string pattern = "\"" + key + "\":\"";
    const auto start = line.find(pattern);
    if (start == std::string::npos) {
        return "";
    }
    std::size_t i = start + pattern.size();
    std::string out;
    bool escaped = false;
    for (; i < line.size(); ++i) {
        const char ch = line[i];
        if (escaped) {
            out.push_back(ch);
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            break;
        }
        out.push_back(ch);
    }
    return out;
}

std::optional<TaskStatus> parse_terminal_details(const std::string& details) {
    if (details.rfind("Succeeded", 0) == 0) {
        return std::optional<TaskStatus>{TaskStatus::Succeeded};
    }
    if (details.rfind("Failed", 0) == 0) {
        return std::optional<TaskStatus>{TaskStatus::Failed};
    }
    if (details.rfind("TimedOut", 0) == 0) {
        return std::optional<TaskStatus>{TaskStatus::TimedOut};
    }
    if (details.rfind("Skipped", 0) == 0) {
        return std::optional<TaskStatus>{TaskStatus::Skipped};
    }
    if (details.rfind("Canceled", 0) == 0) {
        return std::optional<TaskStatus>{TaskStatus::Canceled};
    }
    return std::nullopt;
}

struct ParsedEvent {
    std::string task_id;
    std::string event;
    std::string details;
    std::string run_id;
    std::string workflow;
};

bool matches_filter(const ParsedEvent& entry,
                    const ReplayFilter& filter,
                    const std::string& effective_run_id) {
    if (!filter.workflow.empty()) {
        if (entry.workflow.empty() || entry.workflow != filter.workflow) {
            return false;
        }
    }

    if (!effective_run_id.empty()) {
        if (entry.run_id.empty() || entry.run_id != effective_run_id) {
            return false;
        }
    } else if (!filter.run_id.empty()) {
        return false;
    }

    return true;
}

void apply_event(std::unordered_map<std::string, TaskStatus>& states, const ParsedEvent& entry) {
    if (entry.event == "init") {
        states[entry.task_id] = TaskStatus::Pending;
    } else if (entry.event == "ready") {
        states[entry.task_id] = TaskStatus::Ready;
    } else if (entry.event == "running") {
        states[entry.task_id] = TaskStatus::Running;
    } else if (entry.event == "retrying") {
        states[entry.task_id] = TaskStatus::Retrying;
    } else if (entry.event == "skipped") {
        states[entry.task_id] = TaskStatus::Skipped;
    } else if (entry.event == "terminal") {
        const auto status = parse_terminal_details(entry.details);
        if (status.has_value()) {
            states[entry.task_id] = *status;
        }
    }
}

}  // namespace

std::unordered_map<std::string, TaskStatus> EventReplayer::replay_file(const std::string& event_log_path) const {
    return replay_file(event_log_path, ReplayFilter{});
}

std::unordered_map<std::string, TaskStatus> EventReplayer::replay_file(
    const std::string& event_log_path, const ReplayFilter& filter) const {
    std::ifstream in(event_log_path);
    if (!in) {
        throw std::runtime_error("cannot open event log file: " + event_log_path);
    }

    std::vector<ParsedEvent> entries;
    std::string latest_run_id_for_workflow;

    std::string line;
    while (std::getline(in, line)) {
        ParsedEvent entry;
        entry.task_id = extract_json_string(line, "task_id");
        entry.event = extract_json_string(line, "event");
        entry.details = extract_json_string(line, "details");
        entry.run_id = extract_json_string(line, "run_id");
        entry.workflow = extract_json_string(line, "workflow");

        if (entry.task_id.empty() || entry.event.empty()) {
            continue;
        }
        entries.push_back(entry);

        if (!filter.workflow.empty() && entry.workflow == filter.workflow && !entry.run_id.empty()) {
            latest_run_id_for_workflow = entry.run_id;
        }
    }

    std::string effective_run_id = filter.run_id;
    if (effective_run_id.empty() && !filter.workflow.empty()) {
        effective_run_id = latest_run_id_for_workflow;
    }

    std::unordered_map<std::string, TaskStatus> states;
    for (const auto& entry : entries) {
        if (!matches_filter(entry, filter, effective_run_id)) {
            continue;
        }
        apply_event(states, entry);
    }

    return states;
}

ReplaySummary EventReplayer::summarize(const std::unordered_map<std::string, TaskStatus>& states) const {
    ReplaySummary summary;
    summary.total = static_cast<int>(states.size());

    for (const auto& [_, status] : states) {
        if (status == TaskStatus::Succeeded) {
            ++summary.succeeded;
        } else if (status == TaskStatus::Failed) {
            ++summary.failed;
        } else if (status == TaskStatus::TimedOut) {
            ++summary.timed_out;
        } else if (status == TaskStatus::Skipped) {
            ++summary.skipped;
        } else {
            ++summary.running_or_pending;
        }
    }

    return summary;
}

}  // namespace dag
