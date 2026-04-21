#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace dag {

enum class TaskStatus {
    Pending,
    Ready,
    Running,
    Retrying,
    Succeeded,
    Failed,
    TimedOut,
    Skipped,
    Canceled
};

struct TaskSpec {
    std::string id;
    std::string command;
    std::vector<std::string> dependencies;
    int max_retries{0};
    int timeout_ms{0};
    int priority{0};
};

struct WorkflowSpec {
    std::string name;
    bool fail_fast{false};
    std::vector<TaskSpec> tasks;
};

struct ExecutionResult {
    std::string task_id;
    int attempt{1};
    int exit_code{0};
    bool timed_out{false};
    std::string message;
    std::chrono::milliseconds duration{0};
};

struct TaskRuntimeSnapshot {
    std::string id;
    TaskStatus status{TaskStatus::Pending};
    int attempt{0};
    int max_retries{0};
    int exit_code{0};
    bool timed_out{false};
    std::string message;
    std::chrono::system_clock::time_point start_time{};
    std::chrono::system_clock::time_point end_time{};
    std::chrono::milliseconds duration{0};
};

std::string to_string(TaskStatus status);

}  // namespace dag
