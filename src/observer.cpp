#include "dag/observer.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace dag {
namespace {

std::string timepoint_to_iso8601(const std::chrono::system_clock::time_point& tp) {
    if (tp.time_since_epoch().count() == 0) {
        return "";
    }

    const std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
    gmtime_r(&t, &tm);

    std::ostringstream os;
    os << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return os.str();
}

}  // namespace

Observer::Observer(std::string run_id, std::optional<std::string> log_path)
    : run_id_(std::move(run_id)) {
    if (log_path.has_value()) {
        log_file_.emplace(*log_path, std::ios::out | std::ios::app);
        if (!log_file_->is_open()) {
            throw std::runtime_error("cannot open log file: " + *log_path);
        }
    }
}

void Observer::info(const std::string& message) {
    write_json_line("INFO", "info", "{\"message\":\"" + escape_json(message) + "\"}");
}

void Observer::task_event(const TaskRuntimeSnapshot& task, const std::string& event) {
    std::ostringstream payload;
    payload << "{\"task_id\":\"" << escape_json(task.id) << "\","
            << "\"event\":\"" << escape_json(event) << "\","
            << "\"status\":\"" << escape_json(to_string(task.status)) << "\","
            << "\"attempt\":" << task.attempt << ","
            << "\"exit_code\":" << task.exit_code << ","
            << "\"timed_out\":" << (task.timed_out ? "true" : "false") << ","
            << "\"queue_wait_ms\":" << task.queue_wait.count() << ","
            << "\"duration_ms\":" << task.duration.count() << ","
            << "\"message\":\"" << escape_json(task.message) << "\"}";
    write_json_line("INFO", "task", payload.str());
}

void Observer::set_summary(int total,
                           int succeeded,
                           int failed,
                           int timed_out,
                           int skipped,
                           int retries,
                           long long duration_ms,
                           long long queue_wait_total_ms,
                           long long queue_wait_max_ms,
                           int queue_wait_samples,
                           int failed_nonzero,
                           int failed_signal) {
    std::lock_guard<std::mutex> lock(mutex_);
    total_ = total;
    succeeded_ = succeeded;
    failed_ = failed;
    timed_out_ = timed_out;
    skipped_ = skipped;
    retries_ = retries;
    duration_ms_ = duration_ms;
    queue_wait_total_ms_ = queue_wait_total_ms;
    queue_wait_max_ms_ = queue_wait_max_ms;
    queue_wait_samples_ = queue_wait_samples;
    failed_nonzero_ = failed_nonzero;
    failed_signal_ = failed_signal;
}

void Observer::write_metrics(const std::string& path) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("cannot open metrics output: " + path);
    }

    out << "# HELP dag_run_total Total DAG runs.\n";
    out << "# TYPE dag_run_total counter\n";
    out << "dag_run_total{run_id=\"" << run_id_ << "\"} 1\n";

    out << "# HELP dag_tasks_total Total tasks by final state.\n";
    out << "# TYPE dag_tasks_total gauge\n";
    out << "dag_tasks_total{run_id=\"" << run_id_ << "\",status=\"succeeded\"} " << succeeded_ << "\n";
    out << "dag_tasks_total{run_id=\"" << run_id_ << "\",status=\"failed\"} " << failed_ << "\n";
    out << "dag_tasks_total{run_id=\"" << run_id_ << "\",status=\"timed_out\"} " << timed_out_ << "\n";
    out << "dag_tasks_total{run_id=\"" << run_id_ << "\",status=\"skipped\"} " << skipped_ << "\n";

    out << "# HELP dag_retries_total Total task retries.\n";
    out << "# TYPE dag_retries_total counter\n";
    out << "dag_retries_total{run_id=\"" << run_id_ << "\"} " << retries_ << "\n";

    out << "# HELP dag_run_duration_ms Total run duration in milliseconds.\n";
    out << "# TYPE dag_run_duration_ms gauge\n";
    out << "dag_run_duration_ms{run_id=\"" << run_id_ << "\"} " << duration_ms_ << "\n";

    out << "# HELP dag_queue_wait_total_ms Sum of task queue wait time in milliseconds.\n";
    out << "# TYPE dag_queue_wait_total_ms gauge\n";
    out << "dag_queue_wait_total_ms{run_id=\"" << run_id_ << "\"} " << queue_wait_total_ms_ << "\n";

    out << "# HELP dag_queue_wait_max_ms Max task queue wait time in milliseconds.\n";
    out << "# TYPE dag_queue_wait_max_ms gauge\n";
    out << "dag_queue_wait_max_ms{run_id=\"" << run_id_ << "\"} " << queue_wait_max_ms_ << "\n";

    out << "# HELP dag_queue_wait_avg_ms Average task queue wait time in milliseconds.\n";
    out << "# TYPE dag_queue_wait_avg_ms gauge\n";
    const double avg_queue_wait = queue_wait_samples_ > 0
                                      ? static_cast<double>(queue_wait_total_ms_) /
                                            static_cast<double>(queue_wait_samples_)
                                      : 0.0;
    out << "dag_queue_wait_avg_ms{run_id=\"" << run_id_ << "\"} " << avg_queue_wait << "\n";

    out << "# HELP dag_failures_total Total terminal failures by reason.\n";
    out << "# TYPE dag_failures_total gauge\n";
    out << "dag_failures_total{run_id=\"" << run_id_ << "\",reason=\"non_zero_exit\"} "
        << failed_nonzero_ << "\n";
    out << "dag_failures_total{run_id=\"" << run_id_ << "\",reason=\"signal\"} " << failed_signal_
        << "\n";
    out << "dag_failures_total{run_id=\"" << run_id_ << "\",reason=\"timed_out\"} " << timed_out_
        << "\n";
}

void Observer::write_report(const std::string& path,
                            const std::string& workflow_name,
                            const std::vector<TaskRuntimeSnapshot>& tasks) const {
    std::vector<TaskRuntimeSnapshot> ordered = tasks;
    std::sort(ordered.begin(), ordered.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.id < rhs.id;
    });

    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("cannot open report output: " + path);
    }

    out << "{\n";
    out << "  \"workflow\": \"" << escape_json(workflow_name) << "\",\n";
    out << "  \"run_id\": \"" << escape_json(run_id_) << "\",\n";
    out << "  \"summary\": {\n";
    out << "    \"total\": " << total_ << ",\n";
    out << "    \"succeeded\": " << succeeded_ << ",\n";
    out << "    \"failed\": " << failed_ << ",\n";
    out << "    \"timed_out\": " << timed_out_ << ",\n";
    out << "    \"skipped\": " << skipped_ << ",\n";
    out << "    \"retries\": " << retries_ << ",\n";
    out << "    \"duration_ms\": " << duration_ms_ << "\n";
    out << "  },\n";
    out << "  \"tasks\": [\n";

    for (std::size_t i = 0; i < ordered.size(); ++i) {
        const auto& t = ordered[i];
        out << "    {\n";
        out << "      \"id\": \"" << escape_json(t.id) << "\",\n";
        out << "      \"status\": \"" << escape_json(to_string(t.status)) << "\",\n";
        out << "      \"attempt\": " << t.attempt << ",\n";
        out << "      \"max_retries\": " << t.max_retries << ",\n";
        out << "      \"exit_code\": " << t.exit_code << ",\n";
        out << "      \"timed_out\": " << (t.timed_out ? "true" : "false") << ",\n";
        out << "      \"queue_wait_ms\": " << t.queue_wait.count() << ",\n";
        out << "      \"duration_ms\": " << t.duration.count() << ",\n";
        out << "      \"start_time\": \"" << timepoint_to_iso8601(t.start_time) << "\",\n";
        out << "      \"end_time\": \"" << timepoint_to_iso8601(t.end_time) << "\",\n";
        out << "      \"message\": \"" << escape_json(t.message) << "\"\n";
        out << "    }";
        if (i + 1 < ordered.size()) {
            out << ',';
        }
        out << '\n';
    }

    out << "  ]\n";
    out << "}\n";
}

void Observer::write_json_line(const std::string& level,
                               const std::string& event,
                               const std::string& payload) {
    std::ostringstream line;
    line << "{\"ts\":\"" << now_iso8601() << "\","
         << "\"level\":\"" << escape_json(level) << "\","
         << "\"run_id\":\"" << escape_json(run_id_) << "\","
         << "\"event\":\"" << escape_json(event) << "\","
         << "\"payload\":" << payload << "}";

    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << line.str() << '\n';
    if (log_file_) {
        *log_file_ << line.str() << '\n';
        log_file_->flush();
    }
}

std::string Observer::now_iso8601() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&t, &tm);

    std::ostringstream os;
    os << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return os.str();
}

std::string Observer::escape_json(const std::string& input) {
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

}  // namespace dag
