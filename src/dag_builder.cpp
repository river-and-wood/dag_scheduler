#include "dag/dag_builder.hpp"

#include <queue>
#include <stdexcept>
#include <unordered_set>

namespace dag {

DagGraph DagBuilder::build(const WorkflowSpec& spec) const {
    DagGraph graph;
    std::unordered_set<std::string> ids;
    ids.reserve(spec.tasks.size());

    for (const auto& task : spec.tasks) {
        if (task.id.empty()) {
            throw_config_error("task id cannot be empty");
        }
        if (!ids.insert(task.id).second) {
            throw_config_error("duplicate task id: " + task.id);
        }
        graph.indegree[task.id] = 0;
        graph.children[task.id] = {};
    }

    for (const auto& task : spec.tasks) {
        for (const auto& dep : task.dependencies) {
            if (!ids.contains(dep)) {
                throw_config_error("task '" + task.id + "' depends on undefined task '" + dep + "'");
            }
            graph.children[dep].push_back(task.id);
            graph.indegree[task.id] += 1;
        }
    }

    std::queue<std::string> q;
    for (const auto& [id, deg] : graph.indegree) {
        if (deg == 0) {
            q.push(id);
        }
    }

    std::unordered_map<std::string, int> indegree = graph.indegree;
    while (!q.empty()) {
        const std::string node = q.front();
        q.pop();
        graph.order.push_back(node);
        for (const auto& child : graph.children[node]) {
            auto it = indegree.find(child);
            if (it == indegree.end()) {
                throw_config_error("internal graph error: missing indegree for child " + child);
            }
            it->second -= 1;
            if (it->second == 0) {
                q.push(child);
            }
        }
    }

    if (graph.order.size() != spec.tasks.size()) {
        throw_config_error("cycle detected in workflow DAG");
    }

    return graph;
}

[[noreturn]] void DagBuilder::throw_config_error(const std::string& msg) {
    throw std::runtime_error("workflow config error: " + msg);
}

}  // namespace dag
