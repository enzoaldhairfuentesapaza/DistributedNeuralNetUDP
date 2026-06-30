import torch
from torch.utils.data import TensorDataset, random_split
#import dnn_udp

import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.append(str(ROOT / "python"))

from distributed_transport import DistributedTransport
from model import MulticlassClassifier
from serialization import (
    serialize_assignment,
    deserialize_gradient
)
from gradient_utils import average_gradients

torch.manual_seed(42)
np.random.seed(42)


NUM_WORKERS = 4
NUM_EPOCHS = 1

transport = DistributedTransport(NUM_WORKERS)
criterion = torch.nn.CrossEntropyLoss()

loss_history = []

csv_path = ROOT / "dataset" / "Dataset of Diabetes.csv"

df = pd.read_csv(
    csv_path,
    header=None,
    skiprows=1
)

input_dim = 11
num_classes = 3

model = MulticlassClassifier(input_dim=input_dim, num_classes=num_classes)
optimizer = torch.optim.Adam(model.parameters(), lr=0.0003) # 0.001

X_np = df.iloc[:, :input_dim].values.astype(np.float32)
y_np = df.iloc[:, -num_classes:].values.astype(np.float32)

X = torch.tensor(X_np)
y = torch.tensor(y_np)

dataset = TensorDataset(X, y)

train_size = int(0.8 * len(dataset))
test_size = len(dataset) - train_size

train_dataset, test_dataset = random_split(dataset, [train_size, test_size])

train_X = train_dataset[:][0]
train_y = train_dataset[:][1]

test_X = test_dataset[:][0]
test_y = test_dataset[:][1]

print("X:", X.shape)
print("y:", y.shape)

# -----------------------------
# LOSS ANTES
# -----------------------------

model.eval()

with torch.no_grad():

    logits, _ = model(X)

    loss_before = criterion(
        logits,
        y
    )

print(
    "Loss before:",
    loss_before.item()
)

for epoch in range(NUM_EPOCHS):

    print(
        f"\n========== EPOCH {epoch + 1}/{NUM_EPOCHS} =========="
    )

    assignments = {}

    chunk_size = len(train_X) // NUM_WORKERS

    for worker_id in range(1,NUM_WORKERS + 1):

        start = (
            worker_id - 1
        ) * chunk_size

        end = (
            len(X)
            if worker_id == NUM_WORKERS
            else worker_id * chunk_size
        )

        assignment = {
            "worker_id": worker_id,
            "X": train_X[start:end],
            "y": train_y[start:end],
            "model_state": model.state_dict()
        }

        assignments[
            worker_id
        ] = serialize_assignment(
            assignment
        )

    print("Sending work...")

    results = transport.exchange(assignments)

    print(
        "Received:",
        len(results),
        "workers"
    )

    decoded_results = {}

    for worker_id, payload in results.items():

        decoded_results[
            worker_id
        ] = deserialize_gradient(
            payload
        )
    
    if not decoded_results:
        raise RuntimeError("No gradients were received from workers.")
    avg_grads = average_gradients(decoded_results)

    optimizer.zero_grad()

    for name, param in model.named_parameters():

        if name in avg_grads:

            param.grad = avg_grads[
                name
            ]

    optimizer.step()

    model.eval()

    with torch.no_grad():

        logits, _ = model(test_X)

        epoch_loss = criterion(
            logits,
            test_y
        )

    loss_history.append(
        epoch_loss.item()
    )

    print(
        "Loss:",
        epoch_loss.item()
    )

plt.figure(
    figsize=(8, 4)
)

plt.plot(
    loss_history,
    marker="o"
)

plt.title(
    "Distributed Training Loss"
)

plt.xlabel(
    "Epoch"
)

plt.ylabel(
    "Loss"
)

plt.grid(True)

plt.tight_layout()

plt.show()