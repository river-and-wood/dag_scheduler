#pragma once

#include "dag/types.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace dag {

struct DagGraph {
    std::vector<std::string> order;
    std::unordered_map<std::string, std::vector<std::string>> children;
    std::unordered_map<std::string, int> indegree;
};

class DagBuilder {
public:
    DagGraph build(const WorkflowSpec& spec) const;

private:
    [[noreturn]] static void throw_config_error(const std::string& msg);
};

}  // namespace dag
