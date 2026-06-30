import subprocess
from pathlib import Path
from serialization import deserialize_gradient



class DistributedTransport:

    def __init__(self, num_workers):

        self.num_workers = num_workers

        self.assignment_dir = Path("assignments")
        self.result_dir = Path("results")

        self.assignment_dir.mkdir(exist_ok=True)
        self.result_dir.mkdir(exist_ok=True)

    def save_assignments(self, assignments):
        print("\nSaving assignments...")
        for worker_id, assignment in assignments.items():
            filename = (self.assignment_dir / f"worker_{worker_id}.bin")
            with open(filename, "wb") as f:
                f.write(assignment)
            print(f"Worker {worker_id} -> {filename}")

    def exchange(self, assignments):
        self.clear_assignments()
        self.clear_results()
        self.save_assignments(assignments)
        self.run_master()
        return self.load_results()
    
    def run_master(self):
        print("\nLaunching C++ Master...\n")
        result = subprocess.run(
            [
                "bin/master",
                "assignments",
                "results",
                str(self.num_workers)
            ]
        )

        if result.returncode != 0:
            raise RuntimeError(
                "Master execution failed."
            )

    def load_results(self):
        results = {}
        for result_file in sorted(self.result_dir.glob("worker_*.bin")):
            worker_id = int(result_file.stem.split("_")[1])
            with open(result_file, "rb") as f:
                results[worker_id] = f.read()
        return results

    def clear_results(self):
        for file in self.result_dir.glob("*.bin"):
            file.unlink()

    def clear_assignments(self):
        for file in self.assignment_dir.glob("*.bin"):
            file.unlink()