#ifndef API_RDT_UDP_LIBRARY_HPP
#define API_RDT_UDP_LIBRARY_HPP

#include <netinet/in.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace API_RDT_UDP {

using Bytes = std::vector<uint8_t>;

struct WorkerEndpoint {
    std::string host;
    uint16_t port;
    uint16_t id;
};

std::vector<WorkerEndpoint> default_workers();

class MasterTransport {
public:
    explicit MasterTransport(std::vector<WorkerEndpoint> workers = default_workers());

    std::map<uint16_t, Bytes> exchange(const std::map<uint16_t, Bytes>& assignments) const;

private:
    std::vector<WorkerEndpoint> workers_;
};

class WorkerTransport {
public:
    explicit WorkerTransport(uint16_t worker_id);
    ~WorkerTransport();

    WorkerTransport(const WorkerTransport&) = delete;
    WorkerTransport& operator=(const WorkerTransport&) = delete;

    Bytes receive_assignment();
    void send_gradient(const Bytes& gradient_result);

private:
    int sock_ = -1;
    uint16_t worker_id_ = 0;
    bool has_master_address_ = false;
    sockaddr_in master_address_ {};
};

}  // namespace API_RDT_UDP

#endif
