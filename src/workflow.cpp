#include "dag/workflow.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
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

int parse_int(const std::string& key, const std::string& val) {
    try {
        return std::stoi(val);
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
        auto pos = line.find('#');
        if (pos != std::string::npos) {
            line = line.substr(0, pos);
        }
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

}  // namespace dag
