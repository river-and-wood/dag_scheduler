#include "dag/replay.hpp"

#include <cctype>
#include <fstream>
#include <optional>
#include <string_view>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace dag {
namespace {

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
    std::string workflow_fingerprint;
};

class JsonLineParser {
public:
    explicit JsonLineParser(std::string_view input) : input_(input) {}

    std::unordered_map<std::string, std::string> parse_string_object() {
        std::unordered_map<std::string, std::string> out;

        skip_ws();
        expect('{');
        skip_ws();

        if (consume_if('}')) {
            return out;
        }

        while (true) {
            skip_ws();
            const std::string key = parse_string();
            skip_ws();
            expect(':');
            skip_ws();

            if (peek() == '"') {
                out[key] = parse_string();
            } else {
                skip_non_string_value();
            }

            skip_ws();
            if (consume_if(',')) {
                continue;
            }
            expect('}');
            break;
        }

        skip_ws();
        if (pos_ != input_.size()) {
            throw std::runtime_error("trailing content after JSON object");
        }
        return out;
    }

private:
    void skip_ws() {
        while (pos_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[pos_]))) {
            ++pos_;
        }
    }

    char peek() const {
        if (pos_ >= input_.size()) {
            return '\0';
        }
        return input_[pos_];
    }

    bool consume_if(char ch) {
        if (peek() != ch) {
            return false;
        }
        ++pos_;
        return true;
    }

    void expect(char ch) {
        if (!consume_if(ch)) {
            throw std::runtime_error("invalid JSON format");
        }
    }

    std::string parse_string() {
        expect('"');
        std::string out;
        while (pos_ < input_.size()) {
            char ch = input_[pos_++];
            if (ch == '"') {
                return out;
            }
            if (ch != '\\') {
                out.push_back(ch);
                continue;
            }

            if (pos_ >= input_.size()) {
                throw std::runtime_error("invalid JSON escape");
            }
            const char esc = input_[pos_++];
            switch (esc) {
                case '"':
                    out.push_back('"');
                    break;
                case '\\':
                    out.push_back('\\');
                    break;
                case '/':
                    out.push_back('/');
                    break;
                case 'b':
                    out.push_back('\b');
                    break;
                case 'f':
                    out.push_back('\f');
                    break;
                case 'n':
                    out.push_back('\n');
                    break;
                case 'r':
                    out.push_back('\r');
                    break;
                case 't':
                    out.push_back('\t');
                    break;
                case 'u': {
                    // Keep parser lightweight: consume 4 hex digits and store raw marker.
                    for (int i = 0; i < 4; ++i) {
                        if (pos_ >= input_.size() || !std::isxdigit(static_cast<unsigned char>(input_[pos_]))) {
                            throw std::runtime_error("invalid unicode escape");
                        }
                        ++pos_;
                    }
                    out.push_back('?');
                    break;
                }
                default:
                    throw std::runtime_error("invalid JSON escape");
            }
        }
        throw std::runtime_error("unterminated JSON string");
    }

    void skip_string_value() {
        (void)parse_string();
    }

    void skip_non_string_value() {
        const char ch = peek();
        if (ch == '"') {
            skip_string_value();
            return;
        }
        if (ch == '{' || ch == '[') {
            skip_composite_value();
            return;
        }

        while (pos_ < input_.size()) {
            const char cur = input_[pos_];
            if (cur == ',' || cur == '}' || std::isspace(static_cast<unsigned char>(cur))) {
                break;
            }
            ++pos_;
        }
    }

    void skip_composite_value() {
        std::vector<char> stack;
        const char start = peek();
        stack.push_back(start == '{' ? '}' : ']');
        ++pos_;

        while (pos_ < input_.size() && !stack.empty()) {
            const char ch = input_[pos_];
            if (ch == '"') {
                skip_string_value();
                continue;
            }

            if (ch == '{') {
                stack.push_back('}');
                ++pos_;
                continue;
            }
            if (ch == '[') {
                stack.push_back(']');
                ++pos_;
                continue;
            }
            if (ch == stack.back()) {
                stack.pop_back();
                ++pos_;
                continue;
            }
            ++pos_;
        }

        if (!stack.empty()) {
            throw std::runtime_error("unterminated JSON composite value");
        }
    }

    std::string_view input_;
    std::size_t pos_{0};
};

std::optional<ParsedEvent> parse_event_line(const std::string& line) {
    try {
        JsonLineParser parser(line);
        const auto obj = parser.parse_string_object();

        ParsedEvent entry;
        auto get_value = [&](const std::string& key) -> std::string {
            const auto it = obj.find(key);
            if (it == obj.end()) {
                return "";
            }
            return it->second;
        };

        entry.task_id = get_value("task_id");
        entry.event = get_value("event");
        entry.details = get_value("details");
        entry.run_id = get_value("run_id");
        entry.workflow = get_value("workflow");
        entry.workflow_fingerprint = get_value("workflow_fingerprint");

        if (entry.task_id.empty() || entry.event.empty()) {
            return std::nullopt;
        }
        return entry;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

bool matches_filter(const ParsedEvent& entry,
                    const ReplayFilter& filter,
                    const std::string& effective_run_id) {
    if (!filter.workflow.empty()) {
        if (entry.workflow.empty() || entry.workflow != filter.workflow) {
            return false;
        }
    }

    if (!filter.workflow_fingerprint.empty()) {
        if (entry.workflow_fingerprint.empty() ||
            entry.workflow_fingerprint != filter.workflow_fingerprint) {
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
    std::string latest_run_id_for_scope;

    std::string line;
    while (std::getline(in, line)) {
        const std::optional<ParsedEvent> parsed = parse_event_line(line);
        if (!parsed.has_value()) {
            continue;
        }
        entries.push_back(*parsed);

        if (!filter.run_id.empty()) {
            continue;
        }

        const ParsedEvent& entry = entries.back();
        if (!filter.workflow.empty() && entry.workflow != filter.workflow) {
            continue;
        }
        if (!filter.workflow_fingerprint.empty() &&
            entry.workflow_fingerprint != filter.workflow_fingerprint) {
            continue;
        }
        if (!entry.run_id.empty()) {
            latest_run_id_for_scope = entry.run_id;
        }
    }

    std::string effective_run_id = filter.run_id;
    if (effective_run_id.empty() && (!filter.workflow.empty() || !filter.workflow_fingerprint.empty())) {
        effective_run_id = latest_run_id_for_scope;
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
