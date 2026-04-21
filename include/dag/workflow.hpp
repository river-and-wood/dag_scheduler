#pragma once

#include "dag/types.hpp"

#include <string>

namespace dag {

class WorkflowParser {
public:
    WorkflowSpec parse_file(const std::string& path) const;
};

std::string compute_workflow_fingerprint(const WorkflowSpec& spec);

}  // namespace dag
