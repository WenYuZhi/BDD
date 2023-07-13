#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "bdd_solver.h"
#include "ILP_input.h"

namespace py=pybind11;

std::tuple<double, std::vector<int>> round(LPMP::bdd_solver& solver)
{
    const auto output = solver.round();
    const double obj = std::get<0>(output);
    const auto sol = std::get<1>(output);
    const std::vector<int> sol_double(sol.begin(), sol.end());
    return {obj, sol_double}; 
}

PYBIND11_MODULE(bdd_solver_py, m) {
    m.doc() = "Bindings for BDD solver.";
    py::class_<LPMP::bdd_solver_options> bdd_opts(m, "bdd_solver_options");

    bdd_opts.def(py::init<>())
        .def(py::init<const std::string&>())
        .def(py::init<LPMP::ILP_input&>())
        .def_readwrite("dual_max_iter", &LPMP::bdd_solver_options::max_iter)
        .def_readwrite("dual_tolerance", &LPMP::bdd_solver_options::tolerance)
        .def_readwrite("dual_improvement_slope", &LPMP::bdd_solver_options::improvement_slope)
        .def_readwrite("dual_time_limit", &LPMP::bdd_solver_options::time_limit)
        .def_readwrite("bdd_solver_type", &LPMP::bdd_solver_options::bdd_solver_impl_)
        .def_readwrite("incremental_primal_rounding", &LPMP::bdd_solver_options::incremental_primal_rounding)
        .def_readwrite("incremental_initial_perturbation", &LPMP::bdd_solver_options::incremental_initial_perturbation)
        .def_readwrite("incremental_growth_rate", &LPMP::bdd_solver_options::incremental_growth_rate)
        .def_readwrite("incremental_primal_num_itr_lb", &LPMP::bdd_solver_options::incremental_primal_num_itr_lb)
        .def_readwrite("incremental_primal_num_rounds", &LPMP::bdd_solver_options::incremental_primal_rounding_num_itr)
        .def_readwrite("wedelin_primal_rounding", &LPMP::bdd_solver_options::wedelin_primal_rounding)
        .def_readwrite("wedelin_alpha", &LPMP::bdd_solver_options::wedelin_alpha)
        .def_readwrite("wedelin_delta", &LPMP::bdd_solver_options::wedelin_delta)
        .def_readwrite("wedelin_theta", &LPMP::bdd_solver_options::wedelin_theta)
        .def_readwrite("wedelin_kappa_min", &LPMP::bdd_solver_options::wedelin_kappa_min)
        .def_readwrite("wedelin_kappa_max", &LPMP::bdd_solver_options::wedelin_kappa_max)
        .def_readwrite("wedelin_kappa_step", &LPMP::bdd_solver_options::wedelin_kappa_step)
        .def("auto_set_parameters",  &LPMP::bdd_solver_options::set_solver_type);

    py::enum_<LPMP::bdd_solver_options::bdd_solver_impl>(bdd_opts, "bdd_solver_types")
        .value("sequential_mma", LPMP::bdd_solver_options::bdd_solver_impl::sequential_mma)
        .value("mma_cuda", LPMP::bdd_solver_options::bdd_solver_impl::mma_cuda)
        .value("parallel_mma", LPMP::bdd_solver_options::bdd_solver_impl::parallel_mma)
        .value("hybrid_parallel_mma", LPMP::bdd_solver_options::bdd_solver_impl::hybrid_parallel_mma);

     py::class_<LPMP::bdd_solver>(m, "bdd_solver")
        .def(py::init<LPMP::bdd_solver_options>())
        .def("solve_dual", &LPMP::bdd_solver::solve)
        .def("lower_bound", &LPMP::bdd_solver::lower_bound)
        .def("round", [](LPMP::bdd_solver& solver) { 
            return round(solver);
        });
}