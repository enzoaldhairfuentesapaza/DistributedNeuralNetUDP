#include "protocolo/API_RDT_UDP.hpp"

#include <sys/stat.h>

#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
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
    if (argc != 3) {
        cerr << "usage: ./master <worker_payloads_dir> <master_payloads_dir>\n";
        return 1;
    }

    const string worker_payloads_dir = argv[1];
    const string master_payloads_dir = argv[2];

    struct stat info {};
    const bool master_payloads_dir_exists = stat(master_payloads_dir.c_str(), &info) == 0;
    if ((master_payloads_dir_exists && !S_ISDIR(info.st_mode)) ||
        (!master_payloads_dir_exists && mkdir(master_payloads_dir.c_str(), 0755) != 0)) {
        cerr << "could not create or access master_payloads directory: " << master_payloads_dir << "\n";
        return 1;
    }

    map<uint16_t, Bytes> worker_payloads;
    for (const WorkerEndpoint& worker : default_workers()) {
        const string worker_payload_path = worker_payloads_dir + "/worker_" + to_string(worker.id) + ".bin";
        Bytes worker_payload;
        if (!read_file(worker_payload_path, worker_payload)) {
            cerr << "Worker " << worker.id << ": could not read " << worker_payload_path << "\n";
            return 1;
        }
        worker_payloads[worker.id] = worker_payload;
        cout << "Worker " << worker.id << ": queued WORKER_PAYLOAD ("
             << worker_payload.size() << " bytes)\n";
    }

    try {
        MasterTransport master;
        const map<uint16_t, Bytes> master_payloads = master.exchange(worker_payloads);

        for (const auto& payload : master_payloads) {
            const string master_payload_path =
                master_payloads_dir + "/worker_" + to_string(payload.first) + ".bin";
            if (!write_file(master_payload_path, payload.second)) {
                cerr << "Worker " << payload.first << ": could not write " << master_payload_path << "\n";
                return 1;
            }
            cout << "Worker " << payload.first << ": wrote " << payload.second.size()
                 << " bytes to " << master_payload_path << "\n";
        }
    } catch (const exception& error) {
        cerr << error.what() << "\n";
        return 1;
    }

    return 0;
}
