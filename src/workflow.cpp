#include "dag/workflow.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace dag {
namespace {

std::string trim(const std::string& s) {
    const auto begin = std::find_if_not(s.begin(), s.end(), [](unsigned char ch) { return std::isspace(ch); });
    if (begin == s.end()) {
        return "";
    }
    const auto end = std::find_if_not(s.rbegin(), s.rend(), [](unsigned char ch) { return std::isspace(ch); }).base();
    return std::string(begin, end);
}

bool starts_with(const std::string& s, const std::string& prefix) {
    return s.rfind(prefix, 0) == 0;
}

std::vector<std::string> split_csv(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss{s};
    std::string item;
    while (std::getline(ss, item, ',')) {
        item = trim(item);
        if (!item.empty()) {
            out.push_back(item);
        }
    }
    return out;
}

std::string strip_inline_comment(const std::string& line) {
    bool in_single_quote = false;
    bool in_double_quote = false;
    bool escaped = false;

    for (std::size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];

        if (escaped) {
            escaped = false;
            continue;
        }

        if (ch == '\\') {
            escaped = true;
            continue;
        }

        if (ch == '\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
            continue;
        }
        if (ch == '"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
            continue;
        }

        if (ch == '#' && !in_single_quote && !in_double_quote) {
            return line.substr(0, i);
        }
    }
    return line;
}

int parse_int(const std::string& key, const std::string& val) {
    try {
        std::size_t consumed = 0;
        const int parsed = std::stoi(val, &consumed);
        if (consumed != val.size()) {
            throw std::runtime_error("invalid integer for key '" + key + "': " + val);
        }
        return parsed;
    } catch (const std::exception&) {
        throw std::runtime_error("invalid integer for key '" + key + "': " + val);
    }
}

bool parse_bool(const std::string& val) {
    if (val == "true" || val == "1" || val == "yes") {
        return true;
    }
    if (val == "false" || val == "0" || val == "no") {
        return false;
    }
    throw std::runtime_error("invalid bool value: " + val);
}

TaskResourceClass parse_resource_class(const std::string& val) {
    if (val == "default") {
        return TaskResourceClass::Default;
    }
    if (val == "cpu") {
        return TaskResourceClass::Cpu;
    }
    if (val == "io") {
        return TaskResourceClass::Io;
    }
    throw std::runtime_error("invalid resource value: " + val);
}

std::pair<std::string, std::string> parse_key_value(const std::string& line) {
    const auto colon = line.find(':');
    if (colon != std::string::npos) {
        return {trim(line.substr(0, colon)), trim(line.substr(colon + 1))};
    }

    const auto space = line.find(' ');
    if (space == std::string::npos) {
        return {trim(line), ""};
    }
    return {trim(line.substr(0, space)), trim(line.substr(space + 1))};
}

std::uint64_t fnv1a_64(const std::string& data) {
    constexpr std::uint64_t kOffset = 1469598103934665603ULL;
    constexpr std::uint64_t kPrime = 1099511628211ULL;
    std::uint64_t hash = kOffset;
    for (unsigned char ch : data) {
        hash ^= static_cast<std::uint64_t>(ch);
        hash *= kPrime;
    }
    return hash;
}

}  // namespace

WorkflowSpec WorkflowParser::parse_file(const std::string& path) const {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("cannot open workflow file: " + path);
    }

    WorkflowSpec spec;
    bool in_task = false;
    TaskSpec current;

    std::string line;
    int line_no = 0;
    while (std::getline(in, line)) {
        ++line_no;
        line = strip_inline_comment(line);
        line = trim(line);
        if (line.empty()) {
            continue;
        }

        if (starts_with(line, "workflow ")) {
            if (in_task) {
                throw std::runtime_error("line " + std::to_string(line_no) + ": workflow key cannot appear inside task block");
            }
            spec.name = trim(line.substr(9));
            continue;
        }

        if (starts_with(line, "fail_fast ")) {
            if (in_task) {
                throw std::runtime_error("line " + std::to_string(line_no) + ": fail_fast cannot appear inside task block");
            }
            spec.fail_fast = parse_bool(trim(line.substr(10)));
            continue;
        }

        if (starts_with(line, "task ")) {
            if (in_task) {
                throw std::runtime_error("line " + std::to_string(line_no) + ": nested task block is not allowed");
            }
            in_task = true;
            current = TaskSpec{};
            current.id = trim(line.substr(5));
            if (current.id.empty()) {
                throw std::runtime_error("line " + std::to_string(line_no) + ": task id cannot be empty");
            }
            continue;
        }

        if (line == "end") {
            if (!in_task) {
                throw std::runtime_error("line " + std::to_string(line_no) + ": 'end' without open task block");
            }
            if (current.command.empty()) {
                throw std::runtime_error("line " + std::to_string(line_no) + ": task '" + current.id + "' has empty command");
            }
            spec.tasks.push_back(current);
            in_task = false;
            continue;
        }

        auto [key, val] = parse_key_value(line);
        if (in_task) {
            if (key == "cmd" || key == "command") {
                current.command = val;
            } else if (key == "deps" || key == "dependencies") {
                current.dependencies = split_csv(val);
            } else if (key == "retries" || key == "max_retries") {
                current.max_retries = parse_int(key, val);
                if (current.max_retries < 0) {
                    throw std::runtime_error("line " + std::to_string(line_no) + ": retries cannot be negative");
                }
            } else if (key == "timeout_ms") {
                current.timeout_ms = parse_int(key, val);
                if (current.timeout_ms < 0) {
                    throw std::runtime_error("line " + std::to_string(line_no) + ": timeout_ms cannot be negative");
                }
            } else if (key == "priority") {
                current.priority = parse_int(key, val);
            } else if (key == "resource") {
                current.resource_class = parse_resource_class(val);
            } else {
                throw std::runtime_error("line " + std::to_string(line_no) + ": unknown task key '" + key + "'");
            }
        } else {
            throw std::runtime_error("line " + std::to_string(line_no) + ": unknown top-level directive '" + key + "'");
        }
    }

    if (in_task) {
        throw std::runtime_error("workflow ended before task '" + current.id + "' was closed");
    }
    if (spec.name.empty()) {
        spec.name = "default-workflow";
    }
    if (spec.tasks.empty()) {
        throw std::runtime_error("workflow has no tasks");
    }
    return spec;
}

std::string compute_workflow_fingerprint(const WorkflowSpec& spec) {
    std::vector<TaskSpec> tasks = spec.tasks;
    std::sort(tasks.begin(), tasks.end(), [](const TaskSpec& lhs, const TaskSpec& rhs) {
        return lhs.id < rhs.id;
    });

    std::ostringstream canonical;
    canonical << "workflow:" << spec.name << '\n';
    canonical << "fail_fast:" << (spec.fail_fast ? "1" : "0") << '\n';

    for (const auto& task : tasks) {
        std::vector<std::string> deps = task.dependencies;
        std::sort(deps.begin(), deps.end());
        canonical << "task:" << task.id << '\n';
        canonical << "cmd:" << task.command << '\n';
        canonical << "retries:" << task.max_retries << '\n';
        canonical << "timeout_ms:" << task.timeout_ms << '\n';
        canonical << "priority:" << task.priority << '\n';
        canonical << "resource:" << to_string(task.resource_class) << '\n';
        canonical << "deps:";
        for (std::size_t i = 0; i < deps.size(); ++i) {
            if (i > 0) {
                canonical << ',';
            }
            canonical << deps[i];
        }
        canonical << '\n';
    }

    const std::uint64_t hash = fnv1a_64(canonical.str());
    std::ostringstream out;
    out << std::hex << std::nouppercase << std::setfill('0') << std::setw(16) << hash;
    return out.str();
}

}  // namespace dag
