#pragma once

#include "dag/dag_builder.hpp"
#include "dag/executor.hpp"
#include "dag/observer.hpp"
#include "dag/state_store.hpp"
#include "dag/types.hpp"

#include <chrono>
#include <queue>
#include <string>
#include <unordered_map>

namespace dag {

struct RunOptions {
    std::size_t workers{1};
    std::string run_id{"run-unknown"};
};

struct RunResult {
    bool success{false};
    int total_tasks{0};
    int succeeded{0};
    int failed{0};
    int timed_out{0};
    int skipped{0};
    int retries{0};
    long long duration_ms{0};
};

class Scheduler {
public:
    Scheduler(const WorkflowSpec& spec,
              const DagGraph& graph,
              ThreadPoolExecutor& executor,
              StateStore& state_store,
              Observer& observer);

    RunResult run();

private:
    struct ReadyTask {
        int priority{0};
        std::string id;

        bool operator<(const ReadyTask& rhs) const {
            if (priority == rhs.priority) {
                return id > rhs.id;
            }
            return priority < rhs.priority;
        }
    };

    void enqueue_ready(const std::string& task_id);
    void handle_terminal(const ExecutionResult& result, TaskStatus final_status);
    void propagate_dependency_result(const std::string& task_id, bool success);
    void skip_subtree_if_needed(const std::string& task_id, const std::string& reason);

    const WorkflowSpec& spec_;
    const DagGraph& graph_;
    ThreadPoolExecutor& executor_;
    StateStore& state_store_;
    Observer& observer_;

    std::unordered_map<std::string, TaskSpec> tasks_;
    std::unordered_map<std::string, int> remaining_deps_;
    std::unordered_map<std::string, int> failed_deps_;
    std::unordered_map<std::string, int> attempts_;
    std::priority_queue<ReadyTask> ready_;

    int running_{0};
};

}  // namespace dag
