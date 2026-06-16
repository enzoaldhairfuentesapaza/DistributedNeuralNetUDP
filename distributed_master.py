from worker_runtime import compute_gradients


def exchange(assignments):
    results = {}
    for worker_id, assignment in assignments.items():
        result = compute_gradients(
            assignment
        )
        results[worker_id] = result
    return results