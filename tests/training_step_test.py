from model import MulticlassClassifier

from distributed_master import exchange
from gradient_utils import average_gradients

import torch
import torch.nn as nn


model = MulticlassClassifier(
    input_dim=14,
    num_classes=3
)

optimizer = torch.optim.Adam(
    model.parameters(),
    lr=0.001
)

criterion = nn.CrossEntropyLoss()


before = {}

for name, param in model.named_parameters():

    before[name] = param.detach().clone()


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


assignments = []

for i in range(11):

    assignments.append(
        {
            "worker_id": i,

            "X": chunks_X[i],

            "y": chunks_y[i],

            "model_state":
                model.state_dict()
        }
    )


results = exchange(
    assignments
)

avg_grads = average_gradients(
    results
)


optimizer.zero_grad()


for name, param in model.named_parameters():

    if name in avg_grads:

        param.grad = avg_grads[name]


optimizer.step()


print(
    "\nWeight changes:\n"
)

for name, param in model.named_parameters():

    diff = (
        param.detach()
        -
        before[name]
    ).abs().sum().item()

    print(
        name,
        diff
    )