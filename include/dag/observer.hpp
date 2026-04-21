#pragma once

#include "dag/types.hpp"

#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace dag {

class Observer {
public:
    Observer(std::string run_id, std::optional<std::string> log_path);

    void info(const std::string& message);
    void task_event(const TaskRuntimeSnapshot& task, const std::string& event);

    void set_summary(int total,
                     int succeeded,
                     int failed,
                     int timed_out,
                     int skipped,
                     int retries,
                     long long duration_ms);

    void write_metrics(const std::string& path) const;
    void write_report(const std::string& path,
                      const std::string& workflow_name,
                      const std::vector<TaskRuntimeSnapshot>& tasks) const;

private:
    void write_json_line(const std::string& level,
                         const std::string& event,
                         const std::string& payload);
    static std::string now_iso8601();
    static std::string escape_json(const std::string& input);

    std::string run_id_;
    mutable std::mutex mutex_;
    std::optional<std::ofstream> log_file_;

    int total_{0};
    int succeeded_{0};
    int failed_{0};
    int timed_out_{0};
    int skipped_{0};
    int retries_{0};
    long long duration_ms_{0};
};

}  // namespace dag
