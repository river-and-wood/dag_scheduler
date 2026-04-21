#pragma once

#include "dag/types.hpp"

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace dag {

class ThreadPoolExecutor {
public:
    explicit ThreadPoolExecutor(std::size_t workers);
    ~ThreadPoolExecutor();

    ThreadPoolExecutor(const ThreadPoolExecutor&) = delete;
    ThreadPoolExecutor& operator=(const ThreadPoolExecutor&) = delete;

    void submit(const TaskSpec& spec, int attempt);
    std::optional<ExecutionResult> wait_for_result();
    std::size_t worker_count() const { return workers_.size(); }
    void shutdown();

private:
    struct Job {
        TaskSpec spec;
        int attempt{1};
    };

    void worker_loop();
    static ExecutionResult run_command(const TaskSpec& spec, int attempt);

    mutable std::mutex jobs_mutex_;
    std::condition_variable jobs_cv_;
    std::queue<Job> jobs_;

    mutable std::mutex results_mutex_;
    std::condition_variable results_cv_;
    std::queue<ExecutionResult> results_;

    bool stop_{false};
    bool stopped_{false};
    std::vector<std::thread> workers_;
};

}  // namespace dag
