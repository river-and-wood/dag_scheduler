#include "dag/scheduler.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace dag {
namespace {

bool is_terminal(TaskStatus status) {
    return status == TaskStatus::Succeeded || status == TaskStatus::Failed ||
           status == TaskStatus::TimedOut || status == TaskStatus::Skipped ||
           status == TaskStatus::Canceled;
}

}  // namespace

Scheduler::Scheduler(const WorkflowSpec& spec,
                     const DagGraph& graph,
                     ThreadPoolExecutor& executor,
                     StateStore& state_store,
                     Observer& observer,
                     SchedulerOptions options,
                     std::unordered_map<std::string, TaskStatus> resume_states)
    : spec_(spec),
      graph_(graph),
      executor_(executor),
      state_store_(state_store),
      observer_(observer),
      options_(options),
      resume_states_(std::move(resume_states)) {
    for (const auto& task : spec_.tasks) {
        tasks_[task.id] = task;
        remaining_deps_[task.id] = graph_.indegree.at(task.id);
        failed_deps_[task.id] = 0;
        attempts_[task.id] = 0;
    }
}

RunResult Scheduler::run() {
    state_store_.initialize(spec_);
    apply_resume_state();

    if (spec_.fail_fast &&
        (state_store_.failed_count() > 0 || state_store_.timed_out_count() > 0)) {
        for (const auto& [task_id, _] : tasks_) {
            const TaskRuntimeSnapshot task = state_store_.snapshot(task_id);
            if (!is_terminal(task.status) && task.status != TaskStatus::Running) {
                skip_subtree_if_needed(task_id, "fail_fast triggered");
            }
        }
    }

    for (const auto& [task_id, deg] : remaining_deps_) {
        const TaskRuntimeSnapshot task = state_store_.snapshot(task_id);
        if (deg == 0 && task.status == TaskStatus::Pending) {
            enqueue_ready(task_id);
        }
    }

    const auto run_start = std::chrono::steady_clock::now();

    while (state_store_.terminal_count() < static_cast<int>(spec_.tasks.size())) {
        while (running_ < static_cast<int>(executor_.worker_count()) && !ready_.empty()) {
            if (!try_dispatch_one()) {
                break;
            }
        }

        if (running_ == 0 && ready_.empty()) {
            break;
        }

        auto maybe_result = executor_.wait_for_result();
        if (!maybe_result.has_value()) {
            break;
        }

        ExecutionResult result = *maybe_result;

        const TaskSpec& spec = tasks_.at(result.task_id);
        --running_;
        on_task_completed(spec);

        const bool success = (!result.timed_out && result.exit_code == 0);
        const bool can_retry = !success && attempts_.at(result.task_id) <= spec.max_retries;

        if (can_retry) {
            state_store_.mark_retrying(result.task_id, result.message);
            observer_.task_event(state_store_.snapshot(result.task_id), "retrying");
            enqueue_ready(result.task_id);
            continue;
        }

        const TaskStatus final_status = success ? TaskStatus::Succeeded
                                                : (result.timed_out ? TaskStatus::TimedOut : TaskStatus::Failed);
        handle_terminal(result, final_status);

        if (spec_.fail_fast && final_status != TaskStatus::Succeeded) {
            for (const auto& [id, _] : tasks_) {
                const TaskRuntimeSnapshot task = state_store_.snapshot(id);
                if (!is_terminal(task.status) && task.status != TaskStatus::Running) {
                    skip_subtree_if_needed(id, "fail_fast triggered");
                }
            }
        }
    }

    const long long duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - run_start)
                                      .count();

    RunResult summary;
    summary.total_tasks = static_cast<int>(spec_.tasks.size());
    summary.succeeded = state_store_.succeeded_count();
    summary.failed = state_store_.failed_count();
    summary.timed_out = state_store_.timed_out_count();
    summary.skipped = state_store_.skipped_count();
    summary.retries = state_store_.retry_count();
    summary.duration_ms = duration_ms;
    summary.success = (summary.failed == 0 && summary.timed_out == 0);

    long long queue_wait_total_ms = 0;
    long long queue_wait_max_ms = 0;
    int queue_wait_samples = 0;
    int failed_nonzero = 0;
    int failed_signal = 0;
    const auto snapshots = state_store_.all_snapshots();
    for (const auto& task : snapshots) {
        if (task.start_time.time_since_epoch().count() != 0) {
            const long long queue_wait_ms = task.queue_wait.count();
            queue_wait_total_ms += queue_wait_ms;
            queue_wait_max_ms = std::max(queue_wait_max_ms, queue_wait_ms);
            ++queue_wait_samples;
        }
        if (task.status == TaskStatus::Failed) {
            if (task.message == "terminated by signal") {
                ++failed_signal;
            } else {
                ++failed_nonzero;
            }
        }
    }

    observer_.set_summary(summary.total_tasks,
                          summary.succeeded,
                          summary.failed,
                          summary.timed_out,
                          summary.skipped,
                          summary.retries,
                          summary.duration_ms,
                          queue_wait_total_ms,
                          queue_wait_max_ms,
                          queue_wait_samples,
                          failed_nonzero,
                          failed_signal);
    return summary;
}

bool Scheduler::try_dispatch_one() {
    std::vector<ReadyTask> blocked_by_resource;
    while (!ready_.empty()) {
        ReadyTask ready = ready_.top();
        ready_.pop();

        TaskRuntimeSnapshot snap = state_store_.snapshot(ready.id);
        if (is_terminal(snap.status) || snap.status == TaskStatus::Running) {
            continue;
        }

        const TaskSpec& spec = tasks_.at(ready.id);
        if (!can_dispatch(spec)) {
            blocked_by_resource.push_back(ready);
            continue;
        }

        const int attempt = attempts_[ready.id] + 1;
        attempts_[ready.id] = attempt;

        state_store_.mark_running(ready.id, attempt);
        observer_.task_event(state_store_.snapshot(ready.id), "running");
        executor_.submit(spec, attempt);
        ++running_;
        on_task_dispatched(spec);

        for (const auto& blocked : blocked_by_resource) {
            ready_.push(blocked);
        }
        return true;
    }

    for (const auto& blocked : blocked_by_resource) {
        ready_.push(blocked);
    }
    return false;
}

bool Scheduler::can_dispatch(const TaskSpec& spec) const {
    if (spec.resource_class == TaskResourceClass::Cpu && options_.max_cpu_running > 0 &&
        running_cpu_ >= options_.max_cpu_running) {
        return false;
    }
    if (spec.resource_class == TaskResourceClass::Io && options_.max_io_running > 0 &&
        running_io_ >= options_.max_io_running) {
        return false;
    }
    return true;
}

void Scheduler::on_task_dispatched(const TaskSpec& spec) {
    if (spec.resource_class == TaskResourceClass::Cpu) {
        ++running_cpu_;
    } else if (spec.resource_class == TaskResourceClass::Io) {
        ++running_io_;
    }
}

void Scheduler::on_task_completed(const TaskSpec& spec) {
    if (spec.resource_class == TaskResourceClass::Cpu && running_cpu_ > 0) {
        --running_cpu_;
    } else if (spec.resource_class == TaskResourceClass::Io && running_io_ > 0) {
        --running_io_;
    }
}

void Scheduler::apply_resume_state() {
    for (const auto& [task_id, status] : resume_states_) {
        if (!tasks_.contains(task_id)) {
            continue;
        }

        const TaskRuntimeSnapshot current = state_store_.snapshot(task_id);
        if (is_terminal(current.status)) {
            continue;
        }

        if (status == TaskStatus::Succeeded) {
            ExecutionResult resumed;
            resumed.task_id = task_id;
            resumed.attempt = 1;
            resumed.exit_code = 0;
            resumed.message = "resumed terminal state";
            state_store_.mark_terminal(task_id, TaskStatus::Succeeded, resumed);
            observer_.task_event(state_store_.snapshot(task_id), "resumed_terminal");
            propagate_dependency_result(task_id, true);
        } else if (status == TaskStatus::Failed || status == TaskStatus::TimedOut) {
            ExecutionResult resumed;
            resumed.task_id = task_id;
            resumed.attempt = 1;
            resumed.exit_code = (status == TaskStatus::TimedOut) ? -1 : 1;
            resumed.timed_out = (status == TaskStatus::TimedOut);
            resumed.message = "resumed terminal state";
            state_store_.mark_terminal(task_id, status, resumed);
            observer_.task_event(state_store_.snapshot(task_id), "resumed_terminal");
            propagate_dependency_result(task_id, false);
        } else if (status == TaskStatus::Skipped || status == TaskStatus::Canceled) {
            state_store_.mark_skipped(task_id, "resumed skipped state");
            observer_.task_event(state_store_.snapshot(task_id), "resumed_skipped");
            propagate_dependency_result(task_id, false);
        }
    }
}

void Scheduler::enqueue_ready(const std::string& task_id) {
    ready_.push(ReadyTask{tasks_.at(task_id).priority, task_id});
    state_store_.mark_ready(task_id);
    observer_.task_event(state_store_.snapshot(task_id), "ready");
}

void Scheduler::handle_terminal(const ExecutionResult& result, TaskStatus final_status) {
    state_store_.mark_terminal(result.task_id, final_status, result);
    observer_.task_event(state_store_.snapshot(result.task_id), "terminal");

    const bool success = (final_status == TaskStatus::Succeeded);
    propagate_dependency_result(result.task_id, success);
}

void Scheduler::propagate_dependency_result(const std::string& task_id, bool success) {
    for (const auto& child : graph_.children.at(task_id)) {
        if (remaining_deps_.at(child) > 0) {
            --remaining_deps_[child];
        }
        if (!success) {
            ++failed_deps_[child];
        }

        if (remaining_deps_.at(child) == 0) {
            if (failed_deps_.at(child) > 0) {
                skip_subtree_if_needed(child, "dependency failed");
            } else {
                enqueue_ready(child);
            }
        }
    }
}

void Scheduler::skip_subtree_if_needed(const std::string& task_id, const std::string& reason) {
    const TaskRuntimeSnapshot task = state_store_.snapshot(task_id);
    if (is_terminal(task.status) || task.status == TaskStatus::Running) {
        return;
    }

    state_store_.mark_skipped(task_id, reason);
    observer_.task_event(state_store_.snapshot(task_id), "skipped");

    propagate_dependency_result(task_id, false);
}

}  // namespace dag
