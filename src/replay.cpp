#include "dag/replay.hpp"

#include <fstream>
#include <optional>
#include <stdexcept>

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

}  // namespace

std::unordered_map<std::string, TaskStatus> EventReplayer::replay_file(const std::string& event_log_path) const {
    std::ifstream in(event_log_path);
    if (!in) {
        throw std::runtime_error("cannot open event log file: " + event_log_path);
    }

    std::unordered_map<std::string, TaskStatus> states;

    std::string line;
    while (std::getline(in, line)) {
        const std::string task_id = extract_json_string(line, "task_id");
        const std::string event = extract_json_string(line, "event");
        const std::string details = extract_json_string(line, "details");

        if (task_id.empty() || event.empty()) {
            continue;
        }

        if (event == "init") {
            states[task_id] = TaskStatus::Pending;
        } else if (event == "ready") {
            states[task_id] = TaskStatus::Ready;
        } else if (event == "running") {
            states[task_id] = TaskStatus::Running;
        } else if (event == "retrying") {
            states[task_id] = TaskStatus::Retrying;
        } else if (event == "skipped") {
            states[task_id] = TaskStatus::Skipped;
        } else if (event == "terminal") {
            const auto status = parse_terminal_details(details);
            if (status.has_value()) {
                states[task_id] = *status;
            }
        }
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
