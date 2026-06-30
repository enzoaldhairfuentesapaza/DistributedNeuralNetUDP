import sys
from unittest import result

from serialization import (
    deserialize_assignment,
    serialize_gradient
)

from worker_runtime import (
    compute_gradients
)


def main():

    if len(sys.argv) != 3:

        print(
            "usage: python3 worker_runtime_server.py "
            "<assignment_file> <gradient_file>"
        )

        return 1

    assignment_file = sys.argv[1]
    gradient_file = sys.argv[2]

    print(
        f"Reading {assignment_file}"
    )

    with open(
        assignment_file,
        "rb"
    ) as f:

        assignment_bytes = f.read()

    assignment = deserialize_assignment(
        assignment_bytes
    )

    print(
        f"Worker {assignment['worker_id']} processing..."
    )

    result = compute_gradients(
        assignment
    )
    
    print(result.keys())
    print(result["gradients"].keys())

    gradient_bytes = serialize_gradient(
        result
    )

    with open(gradient_file,"wb") as f:
        f.write(gradient_bytes)
    print(f"Wrote {gradient_file}")

    return 0

if __name__ == "__main__":
    raise SystemExit(main())