#pragma once

#include <string>

namespace puzzle71 {

struct SolverOptions {
    std::string keyspace_start_hex;
    std::string keyspace_end_hex;
    std::string target_address;
    std::string operator_id;
    std::string operator_purpose;
    bool enable_checkpoint{false};
};

class Puzzle71Solver {
public:
    explicit Puzzle71Solver(SolverOptions options);

    void Run();

private:
    SolverOptions options_;
};

}  // namespace puzzle71
