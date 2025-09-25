#include "solver.h"

#include <iostream>

namespace puzzle71 {

Puzzle71Solver::Puzzle71Solver(SolverOptions options) : options_(std::move(options)) {}

void Puzzle71Solver::Run() {
    // TODO(T040): Wire GPU kernels, CPU parity checks, luck.txt append, and telemetry flow.
    std::cout << "Puzzle71Solver::Run invoked (stub)" << std::endl;
}

}  // namespace puzzle71
