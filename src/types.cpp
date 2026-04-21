#include "dag/types.hpp"

namespace dag {

std::string to_string(TaskStatus status) {
    switch (status) {
        case TaskStatus::Pending:
            return "Pending";
        case TaskStatus::Ready:
            return "Ready";
        case TaskStatus::Running:
            return "Running";
        case TaskStatus::Retrying:
            return "Retrying";
        case TaskStatus::Succeeded:
            return "Succeeded";
        case TaskStatus::Failed:
            return "Failed";
        case TaskStatus::TimedOut:
            return "TimedOut";
        case TaskStatus::Skipped:
            return "Skipped";
        case TaskStatus::Canceled:
            return "Canceled";
    }
    return "Unknown";
}

}  // namespace dag
