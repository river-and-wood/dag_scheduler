#include "dag/executor.hpp"

#include <cerrno>
#include <cctype>
#include <csignal>
#include <stdexcept>
#include <vector>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace dag {
namespace {

bool has_shell_metachar(const std::string& command) {
    constexpr const char* kMetachars = "|&;<>()$`'\"*?![]{}~\\";
    for (char ch : command) {
        if (ch == '\n' || ch == '\r') {
            return true;
        }
        for (const char* p = kMetachars; *p != '\0'; ++p) {
            if (ch == *p) {
                return true;
            }
        }
    }
    return false;
}

std::vector<std::string> split_simple_argv(const std::string& command) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < command.size()) {
        while (i < command.size() && std::isspace(static_cast<unsigned char>(command[i]))) {
            ++i;
        }
        if (i >= command.size()) {
            break;
        }
        std::size_t start = i;
        while (i < command.size() && !std::isspace(static_cast<unsigned char>(command[i]))) {
            ++i;
        }
        out.push_back(command.substr(start, i - start));
    }
    return out;
}

bool can_use_execvp_fast_path(const std::string& command) {
    if (command.empty() || has_shell_metachar(command)) {
        return false;
    }
    const std::vector<std::string> argv_tokens = split_simple_argv(command);
    return !argv_tokens.empty();
}

[[noreturn]] void exec_command_fast_or_shell(const std::string& command) {
    if (can_use_execvp_fast_path(command)) {
        const std::vector<std::string> argv_tokens = split_simple_argv(command);
        std::vector<char*> argv;
        argv.reserve(argv_tokens.size() + 1);
        for (const auto& token : argv_tokens) {
            argv.push_back(const_cast<char*>(token.c_str()));
        }
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
    }

    execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));
    _exit(127);
}

void signal_task_process(pid_t pid, int sig) {
    if (pid <= 0) {
        return;
    }

    const pid_t process_group = getpgid(pid);
    if (process_group == pid) {
        if (kill(-pid, sig) == 0 || errno != ESRCH) {
            return;
        }
    }

    (void)kill(pid, sig);
}

}  // namespace

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
        // Ensure each task runs in its own process group so timeout signals can
        // terminate the whole command subtree (shell + descendants).
        (void)setpgid(0, 0);
        exec_command_fast_or_shell(spec.command);
    }

    (void)setpgid(pid, pid);

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
            if (errno == EINTR) {
                continue;
            }
            result.exit_code = 1;
            result.message = "waitpid failed";
            signal_task_process(pid, SIGKILL);
            (void)waitpid(pid, &status, 0);
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
            signal_task_process(pid, SIGTERM);

            for (int i = 0; i < 20; ++i) {
                w = waitpid(pid, &status, WNOHANG);
                if (w == pid) {
                    done = true;
                    break;
                }
                if (w < 0 && errno == EINTR) {
                    continue;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            if (!done) {
                signal_task_process(pid, SIGKILL);
                (void)waitpid(pid, &status, 0);
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
