from api_rdt_udp import MasterTransport, WorkerTransport, default_workers


def master_example():
    # Build one WORK_ASSIGNMENT per worker as raw bytes.
    assignments = {
        worker.id: b"example assignment data for worker %d" % worker.id
        for worker in default_workers()
    }

    master = MasterTransport()
    results = master.exchange(assignments)

    for worker_id, gradient_data in results.items():
        print(f"Master received gradient from worker {worker_id}: {len(gradient_data)} bytes")


def worker_example(worker_id: int):
    worker = WorkerTransport(worker_id)
    assignment = worker.receive_assignment()
    print(f"Worker {worker_id} received assignment: {len(assignment)} bytes")

    # In a real training loop, compute gradients from assignment data here.
    gradient_result = b"example gradient result from worker %d" % worker_id
    worker.send_gradient(gradient_result)
    print(f"Worker {worker_id} sent gradient result")


if __name__ == "__main__":
    print("This example shows how Python can call the C++ transport layer.")
    print("Run master_example() or worker_example(worker_id) after building the module.")
