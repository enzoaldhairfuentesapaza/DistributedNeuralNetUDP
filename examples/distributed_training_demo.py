import torch
import dnn_udp
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt

from model import MulticlassClassifier

from serialization import (
    serialize_assignment,
    deserialize_gradient
)

from gradient_utils import average_gradients


NUM_WORKERS = 10
NUM_EPOCHS = 20

model = MulticlassClassifier(
    input_dim=14,
    num_classes=3
)

optimizer = torch.optim.Adam(
    model.parameters(),
    lr=0.001
)

criterion = torch.nn.CrossEntropyLoss()
loss_history = []

csv_path = "Dataset of Diabetes.csv"

df = pd.read_csv(
    csv_path,
    header=None,
    skiprows=1
)

input_dim = 14
num_classes = 3

X_np = df.iloc[:, :input_dim].values.astype(np.float32)

y_np = df.iloc[:, -num_classes:].values.astype(np.float32)

X = torch.tensor(X_np)

y = torch.tensor(y_np)

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

    chunk_size = len(X) // NUM_WORKERS

    for worker_id in range(
        1,
        NUM_WORKERS + 1
    ):

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
            "X": X[start:end],
            "y": y[start:end],
            "model_state": model.state_dict()
        }

        assignments[
            worker_id
        ] = serialize_assignment(
            assignment
        )

    print("Sending work...")

    results = dnn_udp.exchange(
        assignments
    )

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

    avg_grads = average_gradients(
        decoded_results
    )

    optimizer.zero_grad()

    for name, param in model.named_parameters():

        if name in avg_grads:

            param.grad = avg_grads[
                name
            ]

    optimizer.step()

    model.eval()

    with torch.no_grad():

        logits, _ = model(X)

        epoch_loss = criterion(
            logits,
            y
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