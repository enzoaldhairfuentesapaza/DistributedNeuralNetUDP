#include "protocolo/API_RDT_UDP.hpp"
#include "protocolo/protocol.hpp"

#include <cstdlib>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

using namespace std;

bool read_file(const string& path, Bytes& data) {
    ifstream file(path, ios::binary);
    if (!file) {
        return false;
    }
    data.assign(istreambuf_iterator<char>(file), istreambuf_iterator<char>());
    return true;
}

bool write_file(const string& path, const Bytes& data) {
    ofstream file(path, ios::binary);
    if (!file) {
        return false;
    }
    file.write(reinterpret_cast<const char*>(data.data()), static_cast<streamsize>(data.size()));
    return file.good();
}

int main(int argc, char* argv[]) {
    if (argc != 3 && argc != 4) {
        cerr << "usage: ./worker <worker_id> <master_payload_file> [worker_payload_output_file]\n";
        return 1;
    }

    const int parsed_worker_id = atoi(argv[1]);
    if (parsed_worker_id < FIRST_WORKER_ID || parsed_worker_id > LAST_WORKER_ID) {
        cerr << "worker_id must be between 1 and 10\n";
        return 1;
    }

    const uint16_t worker_id = static_cast<uint16_t>(parsed_worker_id);
    const string master_payload_path = argv[2];
    const bool save_worker_payload = argc == 4;
    const string worker_payload_output_path = save_worker_payload ? argv[3] : "";

    try {
        WorkerTransport worker(worker_id);
        cout << "Worker " << worker_id << " listening on 127.0.0.1:"
             << static_cast<int>(9000 + worker_id) << "\n";

        const Bytes worker_payload = worker.receive_worker_payload();
        cout << "Worker " << worker_id << ": received WORKER_PAYLOAD ("
             << worker_payload.size() << " bytes)\n";

        if (save_worker_payload && !write_file(worker_payload_output_path, worker_payload)) {
            cerr << "Worker " << worker_id << ": could not write " << worker_payload_output_path << "\n";
            return 1;
        }

        Bytes master_payload;
        if (!read_file(master_payload_path, master_payload)) {
            cerr << "Worker " << worker_id << ": could not read " << master_payload_path << "\n";
            return 1;
        }

        cout << "Worker " << worker_id << ": sending MASTER_PAYLOAD ("
             << master_payload.size() << " bytes)\n";
        worker.send_master_payload(master_payload);
        cout << "Worker " << worker_id << ": transfer complete\n";
    } catch (const exception& error) {
        cerr << "Worker " << worker_id << ": " << error.what() << "\n";
        return 1;
    }

    return 0;
}
