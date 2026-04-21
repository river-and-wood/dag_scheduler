#include "dag/executor.hpp"

#include <csignal>
#include <stdexcept>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace dag {

ThreadPoolExecutor::ThreadPoolExecutor(std::size_t workers) {
    if (workers == 0) {
        workers = 1;
    }
    workers_.reserve(workers);
    for (std::size_t i = 0; i < workers; ++i) {
        workers_.emplace_back(&ThreadPoolExecutor::worker_loop, this);
    }
}

ThreadPoolExecutor::~ThreadPoolExecutor() {
    shutdown();
}

void ThreadPoolExecutor::submit(const TaskSpec& spec, int attempt) {
    {
        std::lock_guard<std::mutex> lock(jobs_mutex_);
        if (stop_) {
            throw std::runtime_error("executor is stopped");
        }
        jobs_.push(Job{spec, attempt});
    }
    jobs_cv_.notify_one();
}

std::optional<ExecutionResult> ThreadPoolExecutor::wait_for_result() {
    std::unique_lock<std::mutex> lock(results_mutex_);
    results_cv_.wait(lock, [this] { return !results_.empty() || stopped_; });
    if (results_.empty()) {
        return std::nullopt;
    }
    ExecutionResult result = results_.front();
    results_.pop();
    return result;
}

void ThreadPoolExecutor::shutdown() {
    {
        std::lock_guard<std::mutex> lock(jobs_mutex_);
        if (stop_) {
            return;
        }
        stop_ = true;
    }
    jobs_cv_.notify_all();
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    {
        std::lock_guard<std::mutex> lock(results_mutex_);
        stopped_ = true;
    }
    results_cv_.notify_all();
}

void ThreadPoolExecutor::worker_loop() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(jobs_mutex_);
            jobs_cv_.wait(lock, [this] { return stop_ || !jobs_.empty(); });
            if (stop_ && jobs_.empty()) {
                break;
            }
            job = jobs_.front();
            jobs_.pop();
        }

        ExecutionResult result = run_command(job.spec, job.attempt);
        {
            std::lock_guard<std::mutex> lock(results_mutex_);
            results_.push(std::move(result));
        }
        results_cv_.notify_one();
    }
}

ExecutionResult ThreadPoolExecutor::run_command(const TaskSpec& spec, int attempt) {
    ExecutionResult result;
    result.task_id = spec.id;
    result.attempt = attempt;

    const auto start = std::chrono::steady_clock::now();

    pid_t pid = fork();
    if (pid < 0) {
        result.exit_code = 1;
        result.message = "fork failed";
        result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
        return result;
    }

    if (pid == 0) {
        execl("/bin/sh", "sh", "-c", spec.command.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }

    int status = 0;
    bool done = false;
    const auto timeout = std::chrono::milliseconds(spec.timeout_ms);

    while (!done) {
        pid_t w = waitpid(pid, &status, spec.timeout_ms > 0 ? WNOHANG : 0);
        if (w == pid) {
            done = true;
            break;
        }
        if (w < 0) {
            result.exit_code = 1;
            result.message = "waitpid failed";
            done = true;
            break;
        }

        if (spec.timeout_ms <= 0) {
            continue;
        }

        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
        if (elapsed >= timeout) {
            result.timed_out = true;
            kill(pid, SIGTERM);

            for (int i = 0; i < 20; ++i) {
                w = waitpid(pid, &status, WNOHANG);
                if (w == pid) {
                    done = true;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            if (!done) {
                kill(pid, SIGKILL);
                waitpid(pid, &status, 0);
                done = true;
            }
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (result.timed_out) {
        result.exit_code = -1;
        result.message = "timed out";
    } else if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
        result.message = (result.exit_code == 0) ? "ok" : "non-zero exit";
    } else if (WIFSIGNALED(status)) {
        result.exit_code = 128 + WTERMSIG(status);
        result.message = "terminated by signal";
    } else {
        result.exit_code = 1;
        result.message = "unknown process state";
    }

    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    return result;
}

}  // namespace dag
