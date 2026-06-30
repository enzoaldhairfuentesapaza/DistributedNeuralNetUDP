#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/pytypes.h>

#include "API_RDT_UDP.hpp"

namespace py = pybind11;


std::map<uint16_t, py::bytes>
exchange(
    const std::map<uint16_t, py::bytes>& assignments
)
{
    std::map<uint16_t, API_RDT_UDP::Bytes> native_assignments;

    for (const auto& entry : assignments)
    {
        std::string buffer = entry.second;

        native_assignments[entry.first] =
            API_RDT_UDP::Bytes(
                buffer.begin(),
                buffer.end()
            );
    }

    API_RDT_UDP::MasterTransport transport;

    auto native_results =
        transport.exchange(
            native_assignments
        );

    std::map<uint16_t, py::bytes> results;

    for (const auto& entry : native_results)
    {
        results.emplace(
            entry.first,
            py::bytes(
                reinterpret_cast<
                    const char*
                >(entry.second.data()),
                entry.second.size()
            )
        );
    }

    return results;
}

PYBIND11_MODULE(dnn_udp, m)
{
    m.doc() =
        "Distributed Neural Network UDP";

    m.def(
        "exchange",
        &exchange
    );
}


