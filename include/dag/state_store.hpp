#pragma once

#include "dag/types.hpp"

#include <condition_variable>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace dag {

class StateStore {
public:
    StateStore() = default;
    explicit StateStore(std::string event_log_path);

    void set_event_context(std::string run_id, std::string workflow_name);
    void initialize(const WorkflowSpec& spec);
    void mark_ready(const std::string& task_id);
    void mark_running(const std::string& task_id, int attempt);
    void mark_retrying(const std::string& task_id, const std::string& message);
    void mark_terminal(const std::string& task_id, TaskStatus status, const ExecutionResult& result);
    void mark_skipped(const std::string& task_id, const std::string& reason);

    TaskRuntimeSnapshot snapshot(const std::string& task_id) const;
    std::vector<TaskRuntimeSnapshot> all_snapshots() const;

    int terminal_count() const;
    int succeeded_count() const;
    int failed_count() const;
    int timed_out_count() const;
    int skipped_count() const;
    int retry_count() const;

private:
    void append_event_unlocked(const std::string& task_id,
                               const std::string& event,
                               const std::string& details = "");

    mutable std::mutex mutex_;
    std::unordered_map<std::string, TaskRuntimeSnapshot> states_;

    int terminal_count_{0};
    int succeeded_{0};
    int failed_{0};
    int timed_out_{0};
    int skipped_{0};
    int retries_{0};

    std::optional<std::ofstream> event_log_;
    std::string event_run_id_;
    std::string event_workflow_name_;
};

}  // namespace dag
