import torch
import dnn_udp

from model import MulticlassClassifier

from serialization import (
    serialize_assignment,
    deserialize_gradient
)

model = MulticlassClassifier(
    input_dim=14,
    num_classes=3
)

X = torch.randn(500, 14)

y = torch.nn.functional.one_hot(
    torch.randint(
        0,
        3,
        (500,)
    ),
    num_classes=3
).float()

assignments = {}

chunk_size = len(X) // 10

for worker_id in range(1, 11):

    start = (worker_id - 1) * chunk_size

    end = (
        len(X)
        if worker_id == 10
        else worker_id * chunk_size
    )

    assignment = {
        "worker_id": worker_id,
        "X": X[start:end],
        "y": y[start:end],
        "model_state": model.state_dict()
    }

    assignments[worker_id] = (
        serialize_assignment(
            assignment
        )
    )

print("Sending assignments...")

results = dnn_udp.exchange(
    assignments
)

print(type(results))
print(results.keys())

first_worker = next(iter(results))
print("first worker =", first_worker)
print("payload type =", type(results[first_worker]))
print("payload size =", len(results[first_worker]))

from serialization import deserialize_gradient

for worker_id, payload in results.items():

    result = deserialize_gradient(payload)

    print(
        worker_id,
        result.keys()
    )

    print(
        result["gradients"].keys()
    )
    
print(
    "\nReceived workers:",
    results.keys()
)

for worker_id, payload in results.items():

    result = deserialize_gradient(
        payload
    )

    print(
        worker_id,
        list(
            result["gradients"].keys()
        )
    )