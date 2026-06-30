#include "API_RDT_UDP.hpp"
#include "protocol.hpp"

#include <cstdlib>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <sstream>

using namespace std;

bool read_file(const string& path, API_RDT_UDP::Bytes& data) {
    ifstream file(path, ios::binary);
    if (!file) {
        return false;
    }
    data.assign(istreambuf_iterator<char>(file), istreambuf_iterator<char>());
    return true;
}

bool write_file(const string& path, const API_RDT_UDP::Bytes& data) {
    ofstream file(path, ios::binary);
    if (!file) {
        return false;
    }
    file.write(reinterpret_cast<const char*>(data.data()), static_cast<streamsize>(data.size()));
    return file.good();
}

int main(int argc, char* argv[]) {
    if (argc != 3 && argc != 4) {
        cerr << "usage: ./worker <worker_id> <gradient_result_file> [assignment_output_file]\n";
        return 1;
    }

    const int parsed_worker_id = atoi(argv[1]);
    if (parsed_worker_id < FIRST_WORKER_ID || parsed_worker_id > LAST_WORKER_ID) {
        cerr << "worker_id must be between 1 and 10\n";
        return 1;
    }

    const uint16_t worker_id = static_cast<uint16_t>(parsed_worker_id);
    const string gradient_path = argv[2];
    const bool save_assignment = argc == 4;
    const string assignment_output_path = save_assignment ? argv[3] : "";

    try {
        API_RDT_UDP::WorkerTransport worker(worker_id);

        cout << "Worker " << worker_id
            << " listening on 127.0.0.1:"
            << static_cast<int>(9000 + worker_id)
            << "\n";
        cout << "WORKER BUILD: PERSISTENT MODE ENABLED"
            << endl;

        while (true)
        {
            const API_RDT_UDP::Bytes assignment =
                worker.receive_assignment();

            cout
                << "Worker "
                << worker_id
                << ": received WORK_ASSIGNMENT ("
                << assignment.size()
                << " bytes)\n";

            if (!save_assignment)
            {
                cerr
                    << "Worker "
                    << worker_id
                    << ": assignment output path required\n";

                return 1;
            }

            if (!write_file(
                    assignment_output_path,
                    assignment))
            {
                cerr
                    << "Worker "
                    << worker_id
                    << ": could not write "
                    << assignment_output_path
                    << "\n";

                return 1;
            }

            stringstream command;

            command
                << ".venv/bin/python "
                << "worker_runtime_server.py "
                << assignment_output_path
                << " "
                << gradient_path;

            cout
                << "Worker "
                << worker_id
                << ": running "
                << command.str()
                << "\n";

            const int status =
                system(command.str().c_str());

            if (status != 0)
            {
                cerr
                    << "Worker "
                    << worker_id
                    << ": python worker failed\n";

                continue;
            }

            API_RDT_UDP::Bytes gradient_result;

            if (!read_file(
                    gradient_path,
                    gradient_result))
            {
                cerr
                    << "Worker "
                    << worker_id
                    << ": could not read "
                    << gradient_path
                    << "\n";

                continue;
            }

            cout
                << "Worker "
                << worker_id
                << ": sending GRADIENT_RESULT ("
                << gradient_result.size()
                << " bytes)\n";

            worker.send_gradient(
                gradient_result
            );

            cout
                << "Worker "
                << worker_id
                << ": transfer complete\n";
        }
    } catch (const exception& error) {
        cerr << "Worker " << worker_id << ": " << error.what() << "\n";
        return 1;
    }

    return 0;
}
