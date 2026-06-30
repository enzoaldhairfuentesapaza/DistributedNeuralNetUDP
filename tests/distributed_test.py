from distributed_master import exchange

from model import MulticlassClassifier
from gradient_utils import average_gradients


import torch


model = MulticlassClassifier(
    input_dim=14,
    num_classes=3
)

X = torch.randn(110,14)

y = torch.zeros(
    (110,3)
)

y[:,0] = 1


chunks_X = torch.chunk(
    X,
    11
)

chunks_y = torch.chunk(
    y,
    11
)


assignments = {}


for i in range(11):

    assignments[i + 1] = {
        "worker_id": i + 1,
        "X": chunks_X[i],
        "y": chunks_y[i],
        "model_state":
            model.state_dict()
    }


results = exchange(
    assignments
)

print(
    results.keys()
)

avg_grads = average_gradients(
    results
)

print(
    "\nAveraged gradients:"
)

for name, grad in avg_grads.items():

    print(
        name,
        grad.shape,
        grad.abs().sum().item()
    )

print(
    "Results:",
    len(results)
)