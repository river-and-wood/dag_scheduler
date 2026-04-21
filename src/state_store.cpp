#include "dag/state_store.hpp"

#include <chrono>
#include <iomanip>
#include <sstream>

namespace dag {
namespace {

bool is_terminal(TaskStatus status) {
    return status == TaskStatus::Succeeded || status == TaskStatus::Failed ||
           status == TaskStatus::TimedOut || status == TaskStatus::Skipped ||
           status == TaskStatus::Canceled;
}

std::string now_iso8601() {
    using clock = std::chrono::system_clock;
    const auto now = clock::now();
    const std::time_t t = clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&t, &tm);

    std::ostringstream os;
    os << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return os.str();
}

std::string escape_json(const std::string& input) {
    std::string out;
    out.reserve(input.size() + 8);
    for (char ch : input) {
        switch (ch) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out.push_back(ch);
                break;
        }
    }
    return out;
}

}  // namespace

StateStore::StateStore(std::string event_log_path) {
    event_log_.emplace(std::move(event_log_path), std::ios::out | std::ios::app);
    if (!event_log_->is_open()) {
        throw std::runtime_error("cannot open event log file");
    }
}

void StateStore::set_event_context(std::string run_id,
                                   std::string workflow_name,
                                   std::string workflow_fingerprint) {
    std::lock_guard<std::mutex> lock(mutex_);
    event_run_id_ = std::move(run_id);
    event_workflow_name_ = std::move(workflow_name);
    event_workflow_fingerprint_ = std::move(workflow_fingerprint);
}

void StateStore::initialize(const WorkflowSpec& spec) {
    std::lock_guard<std::mutex> lock(mutex_);
    states_.clear();
    terminal_count_ = 0;
    succeeded_ = 0;
    failed_ = 0;
    timed_out_ = 0;
    skipped_ = 0;
    retries_ = 0;

    for (const auto& task : spec.tasks) {
        TaskRuntimeSnapshot snap;
        snap.id = task.id;
        snap.status = TaskStatus::Pending;
        snap.max_retries = task.max_retries;
        states_[task.id] = snap;
        append_event_unlocked(task.id, "init", "pending");
    }
}

void StateStore::mark_ready(const std::string& task_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& task = states_.at(task_id);
    task.status = TaskStatus::Ready;
    task.ready_time = std::chrono::system_clock::now();
    append_event_unlocked(task_id, "ready", "");
}

void StateStore::mark_running(const std::string& task_id, int attempt) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& task = states_.at(task_id);
    task.status = TaskStatus::Running;
    task.attempt = attempt;
    task.start_time = std::chrono::system_clock::now();
    if (task.ready_time.time_since_epoch().count() != 0) {
        task.queue_wait = std::chrono::duration_cast<std::chrono::milliseconds>(task.start_time - task.ready_time);
    }
    append_event_unlocked(task_id, "running", "attempt=" + std::to_string(attempt));
}

void StateStore::mark_retrying(const std::string& task_id, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& task = states_.at(task_id);
    task.status = TaskStatus::Retrying;
    task.message = message;
    ++retries_;
    append_event_unlocked(task_id, "retrying", message);
}

void StateStore::mark_terminal(const std::string& task_id, TaskStatus status, const ExecutionResult& result) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& task = states_.at(task_id);

    if (!is_terminal(task.status)) {
        ++terminal_count_;
    }

    task.status = status;
    task.exit_code = result.exit_code;
    task.timed_out = result.timed_out;
    task.message = result.message;
    task.duration = result.duration;
    task.end_time = std::chrono::system_clock::now();

    if (status == TaskStatus::Succeeded) {
        ++succeeded_;
    } else if (status == TaskStatus::Failed) {
        ++failed_;
    } else if (status == TaskStatus::TimedOut) {
        ++timed_out_;
    }

    append_event_unlocked(task_id,
                          "terminal",
                          to_string(status) + ", exit_code=" + std::to_string(result.exit_code));
}

void StateStore::mark_skipped(const std::string& task_id, const std::string& reason) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& task = states_.at(task_id);

    if (!is_terminal(task.status)) {
        ++terminal_count_;
    }

    task.status = TaskStatus::Skipped;
    task.message = reason;
    task.end_time = std::chrono::system_clock::now();
    ++skipped_;

    append_event_unlocked(task_id, "skipped", reason);
}

TaskRuntimeSnapshot StateStore::snapshot(const std::string& task_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return states_.at(task_id);
}

std::vector<TaskRuntimeSnapshot> StateStore::all_snapshots() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<TaskRuntimeSnapshot> out;
    out.reserve(states_.size());
    for (const auto& [_, task] : states_) {
        out.push_back(task);
    }
    return out;
}

int StateStore::terminal_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return terminal_count_;
}

int StateStore::succeeded_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return succeeded_;
}

int StateStore::failed_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return failed_;
}

int StateStore::timed_out_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return timed_out_;
}

int StateStore::skipped_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return skipped_;
}

int StateStore::retry_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return retries_;
}

void StateStore::append_event_unlocked(const std::string& task_id,
                                       const std::string& event,
                                       const std::string& details) {
    if (!event_log_) {
        return;
    }
    *event_log_ << "{\"ts\":\"" << now_iso8601() << "\","
                << "\"run_id\":\"" << escape_json(event_run_id_) << "\","
                << "\"workflow\":\"" << escape_json(event_workflow_name_) << "\","
                << "\"workflow_fingerprint\":\"" << escape_json(event_workflow_fingerprint_) << "\","
                << "\"task_id\":\"" << escape_json(task_id) << "\","
                << "\"event\":\"" << escape_json(event) << "\","
                << "\"details\":\"" << escape_json(details) << "\"}"
                << '\n';
    event_log_->flush();
}

}  // namespace dag
