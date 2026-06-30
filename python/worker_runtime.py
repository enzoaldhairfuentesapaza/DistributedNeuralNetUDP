import torch
import torch.nn as nn
from model import MulticlassClassifier

DEBUG_GRADIENTS = False
criterion = nn.CrossEntropyLoss()

def compute_gradients(assignment):

    model = MulticlassClassifier(
        input_dim=14,
        num_classes=3
    )

    model.load_state_dict(
        assignment["model_state"]
    )

    model.train()

    X = assignment["X"]
    y = assignment["y"]

    logits, log_vars = model(X)

    loss = criterion(logits, y)

    loss.backward()

    gradients = {}

    if DEBUG_GRADIENTS:

        print("\n===== GRADIENT CHECK =====")

        for name, param in model.named_parameters():

            print(
                name,
                " -> grad is None:",
                param.grad is None
            )

        print("==========================\n")

    print("\nPARAMETER CHECK")

    for name, param in model.named_parameters():

        print(
            name,
            "grad is None:",
            param.grad is None
        )

    for name, param in model.named_parameters():

        if param.grad is not None:

            gradients[name] = (
                param.grad.detach().clone()
            )

    return {
        "worker_id": assignment["worker_id"],
        "gradients": gradients
    }