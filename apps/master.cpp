#include "API_RDT_UDP.hpp"

#include <sys/stat.h>

#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <string>

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
    if (argc != 3) {
        cerr << "usage: ./master <assignments_dir> <results_dir>\n";
        return 1;
    }

    const string assignments_dir = argv[1];
    const string results_dir = argv[2];

    struct stat info {};
    const bool results_dir_exists = stat(results_dir.c_str(), &info) == 0;
    if ((results_dir_exists && !S_ISDIR(info.st_mode)) ||
        (!results_dir_exists && mkdir(results_dir.c_str(), 0755) != 0)) {
        cerr << "could not create or access results directory: " << results_dir << "\n";
        return 1;
    }

    map<uint16_t, API_RDT_UDP::Bytes> assignments;
    for (const API_RDT_UDP::WorkerEndpoint& worker : API_RDT_UDP::default_workers()) {
        const string assignment_path = assignments_dir + "/worker_" + to_string(worker.id) + ".bin";
        API_RDT_UDP::Bytes assignment;
        if (!read_file(assignment_path, assignment)) {
            cerr << "Worker " << worker.id << ": could not read " << assignment_path << "\n";
            return 1;
        }
        assignments[worker.id] = assignment;
        cout << "Worker " << worker.id << ": queued WORK_ASSIGNMENT ("
             << assignment.size() << " bytes)\n";
    }

    try {
        API_RDT_UDP::MasterTransport master;
        const map<uint16_t, API_RDT_UDP::Bytes> results = master.exchange(assignments);

        for (const auto& result : results) {
            const string result_path = results_dir + "/worker_" + to_string(result.first) + ".bin";
            if (!write_file(result_path, result.second)) {
                cerr << "Worker " << result.first << ": could not write " << result_path << "\n";
                return 1;
            }
            cout << "Worker " << result.first << ": wrote " << result.second.size()
                 << " bytes to " << result_path << "\n";
        }
    } catch (const exception& error) {
        cerr << error.what() << "\n";
        return 1;
    }

    return 0;
}
