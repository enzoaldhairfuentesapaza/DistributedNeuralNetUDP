#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "API_RDT_UDP.hpp"

namespace py = pybind11;

static API_RDT_UDP::Bytes bytes_from_py(const py::bytes& value) {
    std::string temp = static_cast<std::string>(value);
    return API_RDT_UDP::Bytes(temp.begin(), temp.end());
}

static py::bytes bytes_to_py(const API_RDT_UDP::Bytes& value) {
    return py::bytes(reinterpret_cast<const char*>(value.data()), value.size());
}

PYBIND11_MODULE(api_rdt_udp, m) {
    m.doc() = "Python bindings for the API_RDT_UDP C++ transport API";

    py::class_<API_RDT_UDP::WorkerEndpoint>(m, "WorkerEndpoint")
        .def(py::init<std::string, uint16_t, uint16_t>(),
             py::arg("host"), py::arg("port"), py::arg("id"))
        .def_readwrite("host", &API_RDT_UDP::WorkerEndpoint::host)
        .def_readwrite("port", &API_RDT_UDP::WorkerEndpoint::port)
        .def_readwrite("id", &API_RDT_UDP::WorkerEndpoint::id);

    m.def("default_workers", &API_RDT_UDP::default_workers,
          "Return the default worker endpoints for the master.");

    py::class_<API_RDT_UDP::MasterTransport>(m, "MasterTransport")
        .def(py::init<>())
        .def(py::init<std::vector<API_RDT_UDP::WorkerEndpoint>>(),
             py::arg("workers"))
        .def("exchange",
             [](const API_RDT_UDP::MasterTransport& self,
                const std::map<uint16_t, py::bytes>& assignments) {
                 std::map<uint16_t, API_RDT_UDP::Bytes> cpp_assignments;
                 for (const auto& assignment : assignments) {
                     cpp_assignments[assignment.first] = bytes_from_py(assignment.second);
                 }

                 std::map<uint16_t, API_RDT_UDP::Bytes> cpp_results = self.exchange(cpp_assignments);
                 py::dict results;
                 for (const auto& result : cpp_results) {
                     results[py::int_(result.first)] = bytes_to_py(result.second);
                 }
                 return results;
             },
             py::arg("assignments"),
             "Send WORK_ASSIGNMENT bytes to all workers and return GRADIENT_RESULT bytes.");

    py::class_<API_RDT_UDP::WorkerTransport>(m, "WorkerTransport")
        .def(py::init<uint16_t>(), py::arg("worker_id"))
        .def("receive_assignment",
             [](API_RDT_UDP::WorkerTransport& self) {
                 API_RDT_UDP::Bytes assignment = self.receive_assignment();
                 return bytes_to_py(assignment);
             },
             "Receive a WORK_ASSIGNMENT object from the master.")
        .def("send_gradient",
             [](API_RDT_UDP::WorkerTransport& self, const py::bytes& gradient) {
                 self.send_gradient(bytes_from_py(gradient));
             },
             py::arg("gradient_result"),
             "Send a GRADIENT_RESULT object back to the master.");
}
