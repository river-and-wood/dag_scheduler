#include "dag/dag_builder.hpp"
#include "dag/executor.hpp"
#include "dag/observer.hpp"
#include "dag/replay.hpp"
#include "dag/scheduler.hpp"
#include "dag/state_store.hpp"
#include "dag/workflow.hpp"

#include <chrono>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

struct CliOptions {
    std::string command;
    std::string workflow_path;
    std::size_t workers{std::max(1u, std::thread::hardware_concurrency())};
    std::string report_path{"report.json"};
    std::string metrics_path{"metrics.prom"};
    std::string events_path{"events.jsonl"};
    std::optional<std::string> log_path{std::string{"run.log"}};
    std::string run_id;
    bool run_id_explicit{false};
    bool resume{false};
    int max_cpu_running{0};
    int max_io_running{0};
};

std::string timestamp_id() {
    const auto now = std::chrono::system_clock::now();
    const auto sec = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    return "run-" + std::to_string(sec);
}

void print_usage() {
    std::cout << "Usage:\n"
              << "  dag_scheduler validate --workflow <path>\n"
              << "  dag_scheduler run --workflow <path> [--workers N] [--report <path>] [--metrics <path>]\n"
              << "                    [--events <path>] [--log <path>] [--run-id <id>] [--resume]\n"
              << "                    [--max-cpu N] [--max-io N]\n"
              << "  dag_scheduler replay --events <path>\n";
}

CliOptions parse_args(int argc, char** argv) {
    if (argc < 2) {
        throw std::runtime_error("missing sub-command");
    }

    CliOptions opts;
    opts.command = argv[1];
    opts.run_id = timestamp_id();

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        auto need_value = [&](const std::string& key) -> std::string {
            if (i + 1 >= argc) {
                throw std::runtime_error("missing value for " + key);
            }
            ++i;
            return argv[i];
        };

        if (arg == "--workflow") {
            opts.workflow_path = need_value(arg);
        } else if (arg == "--workers") {
            opts.workers = static_cast<std::size_t>(std::stoul(need_value(arg)));
        } else if (arg == "--report") {
            opts.report_path = need_value(arg);
        } else if (arg == "--metrics") {
            opts.metrics_path = need_value(arg);
        } else if (arg == "--events") {
            opts.events_path = need_value(arg);
        } else if (arg == "--log") {
            const std::string value = need_value(arg);
            if (value == "none") {
                opts.log_path = std::nullopt;
            } else {
                opts.log_path = value;
            }
        } else if (arg == "--run-id") {
            opts.run_id = need_value(arg);
            opts.run_id_explicit = true;
        } else if (arg == "--resume") {
            opts.resume = true;
        } else if (arg == "--max-cpu") {
            opts.max_cpu_running = std::stoi(need_value(arg));
        } else if (arg == "--max-io") {
            opts.max_io_running = std::stoi(need_value(arg));
        } else {
            throw std::runtime_error("unknown option: " + arg);
        }
    }

    if (opts.command != "replay" && opts.workflow_path.empty()) {
        throw std::runtime_error("--workflow is required");
    }
    if (opts.workers == 0) {
        opts.workers = 1;
    }
    if (opts.max_cpu_running < 0 || opts.max_io_running < 0) {
        throw std::runtime_error("resource limits cannot be negative");
    }
    return opts;
}

int run_validate(const CliOptions& opts) {
    dag::WorkflowParser parser;
    const auto spec = parser.parse_file(opts.workflow_path);
    dag::DagBuilder builder;
    const auto graph = builder.build(spec);

    std::cout << "Workflow valid. name=" << spec.name << ", tasks=" << spec.tasks.size()
              << ", topo_nodes=" << graph.order.size() << "\n";
    return 0;
}

int run_scheduler(const CliOptions& opts) {
    dag::WorkflowParser parser;
    const dag::WorkflowSpec spec = parser.parse_file(opts.workflow_path);

    dag::DagBuilder builder;
    const dag::DagGraph graph = builder.build(spec);

    dag::ThreadPoolExecutor executor(opts.workers);
    dag::StateStore state_store(opts.events_path);
    state_store.set_event_context(opts.run_id, spec.name);
    dag::Observer observer(opts.run_id, opts.log_path);
    observer.info("starting workflow: " + spec.name);

    std::unordered_map<std::string, dag::TaskStatus> resume_states;
    if (opts.resume) {
        dag::EventReplayer replayer;
        dag::ReplayFilter filter;
        filter.workflow = spec.name;
        if (opts.run_id_explicit) {
            filter.run_id = opts.run_id;
        }
        resume_states = replayer.replay_file(opts.events_path, filter);
        observer.info("resume mode enabled");
    }

    dag::SchedulerOptions scheduler_options;
    scheduler_options.max_cpu_running = opts.max_cpu_running;
    scheduler_options.max_io_running = opts.max_io_running;

    dag::Scheduler scheduler(
        spec, graph, executor, state_store, observer, scheduler_options, std::move(resume_states));
    const dag::RunResult summary = scheduler.run();

    observer.write_report(opts.report_path, spec.name, state_store.all_snapshots());
    observer.write_metrics(opts.metrics_path);

    std::cout << "Run finished. success=" << (summary.success ? "true" : "false")
              << ", total=" << summary.total_tasks << ", succeeded=" << summary.succeeded
              << ", failed=" << summary.failed << ", timed_out=" << summary.timed_out
              << ", skipped=" << summary.skipped << ", retries=" << summary.retries
              << ", duration_ms=" << summary.duration_ms << "\n";

    return summary.success ? 0 : 2;
}

int run_replay(const CliOptions& opts) {
    dag::EventReplayer replayer;
    const auto states = replayer.replay_file(opts.events_path);
    const auto summary = replayer.summarize(states);

    std::cout << "Replay summary. total=" << summary.total << ", succeeded=" << summary.succeeded
              << ", failed=" << summary.failed << ", timed_out=" << summary.timed_out
              << ", skipped=" << summary.skipped
              << ", running_or_pending=" << summary.running_or_pending << "\n";
    return (summary.failed == 0 && summary.timed_out == 0) ? 0 : 2;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const CliOptions opts = parse_args(argc, argv);

        if (opts.command == "validate") {
            return run_validate(opts);
        }
        if (opts.command == "run") {
            return run_scheduler(opts);
        }
        if (opts.command == "replay") {
            return run_replay(opts);
        }

        print_usage();
        std::cerr << "unknown sub-command: " << opts.command << "\n";
        return 1;
    } catch (const std::exception& ex) {
        print_usage();
        std::cerr << "error: " << ex.what() << "\n";
        return 1;
    }
}
