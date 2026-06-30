from pathlib import Path

from serialization import serialize_assignment


class DistributedTransport:

    def __init__(self):

        self.assignment_dir = Path("assignments")
        self.result_dir = Path("results")

        self.assignment_dir.mkdir(exist_ok=True)
        self.result_dir.mkdir(exist_ok=True)


    def save_assignments(self, assignments):

        print("\nSaving assignments...")

        for worker_id, assignment in assignments.items():

            filename = (
                self.assignment_dir /
                f"worker_{worker_id}.bin"
            )

            with open(filename, "wb") as f:

                f.write(assignment)

            print(
                f"Worker {worker_id} -> {filename}"
            )


    def exchange(self, assignments):

        self.save_assignments(assignments)

        print("\nDistributedTransport ready.")

        return {}