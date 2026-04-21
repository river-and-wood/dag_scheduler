#include "dag/dag_builder.hpp"
#include "dag/executor.hpp"
#include "dag/observer.hpp"
#include "dag/replay.hpp"
#include "dag/scheduler.hpp"
#include "dag/state_store.hpp"
#include "dag/workflow.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

#define ASSERT_TRUE(cond)                                                                          \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            throw std::runtime_error(std::string("assert failed: ") + #cond);                    \
        }                                                                                          \
    } while (false)

#define ASSERT_EQ(lhs, rhs)                                                                        \
    do {                                                                                           \
        if (!((lhs) == (rhs))) {                                                                   \
            throw std::runtime_error(std::string("assert failed: ") + #lhs + " == " + #rhs);   \
        }                                                                                          \
    } while (false)

struct TempDir {
    fs::path path;

    TempDir() {
        const auto ticks = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
                               .count();
        path = fs::temp_directory_path() / ("dag_tests_" + std::to_string(ticks));
        fs::create_directories(path);
    }

    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

void write_file(const fs::path& path, const std::string& content) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("cannot write file: " + path.string());
    }
    out << content;
}

std::vector<std::string> read_lines(const fs::path& path) {
    std::vector<std::string> lines;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        lines.push_back(line);
    }
    return lines;
}

dag::RunResult run_workflow(const fs::path& workflow_path, std::size_t workers = 2) {
    dag::WorkflowParser parser;
    dag::WorkflowSpec spec = parser.parse_file(workflow_path.string());

    dag::DagBuilder builder;
    dag::DagGraph graph = builder.build(spec);

    dag::ThreadPoolExecutor executor(workers);
    dag::StateStore store;
    dag::Observer observer("test-run", std::nullopt);

    dag::Scheduler scheduler(spec, graph, executor, store, observer);
    return scheduler.run();
}

void test_parse_and_validate_success() {
    TempDir t;
    const fs::path workflow = t.path / "ok.workflow";
    write_file(workflow,
               "workflow test\n"
               "task A\n"
               "  cmd: true\n"
               "end\n"
               "task B\n"
               "  deps: A\n"
               "  cmd: true\n"
               "end\n");

    dag::WorkflowParser parser;
    dag::WorkflowSpec spec = parser.parse_file(workflow.string());
    dag::DagBuilder builder;
    dag::DagGraph graph = builder.build(spec);

    ASSERT_EQ(spec.tasks.size(), 2u);
    ASSERT_EQ(graph.order.size(), 2u);
}

void test_cycle_detection() {
    TempDir t;
    const fs::path workflow = t.path / "cycle.workflow";
    write_file(workflow,
               "workflow cycle\n"
               "task A\n"
               "  deps: B\n"
               "  cmd: true\n"
               "end\n"
               "task B\n"
               "  deps: A\n"
               "  cmd: true\n"
               "end\n");

    dag::WorkflowParser parser;
    dag::WorkflowSpec spec = parser.parse_file(workflow.string());

    dag::DagBuilder builder;
    bool threw = false;
    try {
        (void)builder.build(spec);
    } catch (const std::exception&) {
        threw = true;
    }
    ASSERT_TRUE(threw);
}

void test_duplicate_task_id_rejected() {
    TempDir t;
    const fs::path workflow = t.path / "dup.workflow";
    write_file(workflow,
               "workflow dup\n"
               "task A\n"
               "  cmd: true\n"
               "end\n"
               "task A\n"
               "  cmd: true\n"
               "end\n");

    dag::WorkflowParser parser;
    dag::WorkflowSpec spec = parser.parse_file(workflow.string());

    dag::DagBuilder builder;
    bool threw = false;
    try {
        (void)builder.build(spec);
    } catch (const std::exception&) {
        threw = true;
    }
    ASSERT_TRUE(threw);
}

void test_missing_dependency_rejected() {
    TempDir t;
    const fs::path workflow = t.path / "missing_dep.workflow";
    write_file(workflow,
               "workflow missing_dep\n"
               "task A\n"
               "  deps: B\n"
               "  cmd: true\n"
               "end\n");

    dag::WorkflowParser parser;
    dag::WorkflowSpec spec = parser.parse_file(workflow.string());

    dag::DagBuilder builder;
    bool threw = false;
    try {
        (void)builder.build(spec);
    } catch (const std::exception&) {
        threw = true;
    }
    ASSERT_TRUE(threw);
}

void test_run_success_and_dependency_order() {
    TempDir t;
    const fs::path output = t.path / "run.txt";
    const fs::path workflow = t.path / "success.workflow";

    write_file(workflow,
               "workflow success\n"
               "task A\n"
               "  cmd: echo A >> " + output.string() + "\n"
               "end\n"
               "task B\n"
               "  deps: A\n"
               "  cmd: echo B >> " + output.string() + "\n"
               "end\n"
               "task C\n"
               "  deps: A\n"
               "  cmd: echo C >> " + output.string() + "\n"
               "end\n"
               "task D\n"
               "  deps: B, C\n"
               "  cmd: echo D >> " + output.string() + "\n"
               "end\n");

    dag::RunResult result = run_workflow(workflow, 3);
    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.succeeded, 4);

    std::vector<std::string> lines = read_lines(output);
    ASSERT_EQ(lines.size(), 4u);
    ASSERT_EQ(lines.back(), std::string("D"));
}

void test_retry_then_success() {
    TempDir t;
    const fs::path marker = t.path / "marker";
    const fs::path workflow = t.path / "retry.workflow";

    write_file(workflow,
               "workflow retry\n"
               "task flaky\n"
               "  retries: 1\n"
               "  cmd: if [ ! -f " + marker.string() + " ]; then touch " + marker.string() +
                   "; exit 1; fi; exit 0\n"
               "end\n");

    dag::RunResult result = run_workflow(workflow, 1);
    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.succeeded, 1);
    ASSERT_EQ(result.retries, 1);
}

void test_failure_propagation_to_skip() {
    TempDir t;
    const fs::path output = t.path / "should_not_exist.txt";
    const fs::path workflow = t.path / "fail.workflow";

    write_file(workflow,
               "workflow fail\n"
               "task A\n"
               "  cmd: exit 1\n"
               "end\n"
               "task B\n"
               "  deps: A\n"
               "  cmd: echo B >> " + output.string() + "\n"
               "end\n"
               "task C\n"
               "  deps: B\n"
               "  cmd: echo C >> " + output.string() + "\n"
               "end\n");

    dag::RunResult result = run_workflow(workflow, 2);
    ASSERT_TRUE(!result.success);
    ASSERT_EQ(result.failed, 1);
    ASSERT_EQ(result.skipped, 2);
    ASSERT_TRUE(!fs::exists(output));
}

void test_fail_fast_skips_remaining_ready_tasks() {
    TempDir t;
    const fs::path output = t.path / "should_not_exist.txt";
    const fs::path workflow = t.path / "fail_fast.workflow";

    write_file(workflow,
               "workflow fail_fast\n"
               "fail_fast true\n"
               "task A\n"
               "  cmd: exit 1\n"
               "end\n"
               "task B\n"
               "  cmd: echo B >> " + output.string() + "\n"
               "end\n"
               "task C\n"
               "  deps: A\n"
               "  cmd: echo C >> " + output.string() + "\n"
               "end\n");

    dag::RunResult result = run_workflow(workflow, 1);
    ASSERT_TRUE(!result.success);
    ASSERT_EQ(result.failed, 1);
    ASSERT_EQ(result.skipped, 2);
    ASSERT_TRUE(!fs::exists(output));
}

void test_timeout() {
    TempDir t;
    const fs::path workflow = t.path / "timeout.workflow";

    write_file(workflow,
               "workflow timeout\n"
               "task slow\n"
               "  timeout_ms: 100\n"
               "  cmd: sleep 1\n"
               "end\n");

    dag::RunResult result = run_workflow(workflow, 1);
    ASSERT_TRUE(!result.success);
    ASSERT_EQ(result.timed_out, 1);
}

void test_event_replay_summary() {
    TempDir t;
    const fs::path events = t.path / "events.jsonl";
    write_file(events,
               "{\"ts\":\"2026-01-01T00:00:00Z\",\"task_id\":\"A\",\"event\":\"init\",\"details\":\"pending\"}\n"
               "{\"ts\":\"2026-01-01T00:00:01Z\",\"task_id\":\"A\",\"event\":\"terminal\",\"details\":\"Succeeded, exit_code=0\"}\n"
               "{\"ts\":\"2026-01-01T00:00:00Z\",\"task_id\":\"B\",\"event\":\"init\",\"details\":\"pending\"}\n"
               "{\"ts\":\"2026-01-01T00:00:01Z\",\"task_id\":\"B\",\"event\":\"terminal\",\"details\":\"Failed, exit_code=1\"}\n"
               "{\"ts\":\"2026-01-01T00:00:00Z\",\"task_id\":\"C\",\"event\":\"init\",\"details\":\"pending\"}\n"
               "{\"ts\":\"2026-01-01T00:00:01Z\",\"task_id\":\"C\",\"event\":\"skipped\",\"details\":\"dependency failed\"}\n");

    dag::EventReplayer replayer;
    auto states = replayer.replay_file(events.string());
    auto summary = replayer.summarize(states);

    ASSERT_EQ(summary.total, 3);
    ASSERT_EQ(summary.succeeded, 1);
    ASSERT_EQ(summary.failed, 1);
    ASSERT_EQ(summary.skipped, 1);
}

void test_parser_rejects_unknown_task_key() {
    TempDir t;
    const fs::path workflow = t.path / "unknown_key.workflow";
    write_file(workflow,
               "workflow bad_key\n"
               "task A\n"
               "  unknown_key: 1\n"
               "  cmd: true\n"
               "end\n");

    dag::WorkflowParser parser;
    bool threw = false;
    try {
        (void)parser.parse_file(workflow.string());
    } catch (const std::exception&) {
        threw = true;
    }
    ASSERT_TRUE(threw);
}

}  // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"parse_and_validate_success", test_parse_and_validate_success},
        {"cycle_detection", test_cycle_detection},
        {"duplicate_task_id_rejected", test_duplicate_task_id_rejected},
        {"missing_dependency_rejected", test_missing_dependency_rejected},
        {"run_success_and_dependency_order", test_run_success_and_dependency_order},
        {"retry_then_success", test_retry_then_success},
        {"failure_propagation_to_skip", test_failure_propagation_to_skip},
        {"fail_fast_skips_remaining_ready_tasks", test_fail_fast_skips_remaining_ready_tasks},
        {"timeout", test_timeout},
        {"event_replay_summary", test_event_replay_summary},
        {"parser_rejects_unknown_task_key", test_parser_rejects_unknown_task_key},
    };

    int passed = 0;
    for (const auto& [name, fn] : tests) {
        try {
            fn();
            ++passed;
            std::cout << "[PASS] " << name << "\n";
        } catch (const std::exception& ex) {
            std::cerr << "[FAIL] " << name << ": " << ex.what() << "\n";
            return 1;
        }
    }

    std::cout << "All tests passed: " << passed << "/" << tests.size() << "\n";
    return 0;
}
