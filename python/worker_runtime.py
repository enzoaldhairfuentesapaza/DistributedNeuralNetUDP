import torch
import torch.nn as nn
from model import MulticlassClassifier
from torch.utils.data import TensorDataset, DataLoader

DEBUG_GRADIENTS = False
criterion = nn.CrossEntropyLoss()

def compute_gradients(assignment):
    INPUT_DIM = 11
    NUM_CLASSES = 3

    model = MulticlassClassifier(input_dim=INPUT_DIM, num_classes=NUM_CLASSES)
    
    model.load_state_dict(assignment["model_state"])

    model.train()

    X = assignment["X"]
    y = assignment["y"]

    dataset = TensorDataset(X, y)

    loader = DataLoader(
        dataset,
        batch_size=50,
        shuffle=True
    )

    model.zero_grad()

    for batch_x, batch_y in loader:

        logits, _ = model(batch_x)

        loss = criterion(logits, batch_y)

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

    for name, param in model.named_parameters():
        if param.grad is not None:
            gradients[name] = (param.grad.detach().clone())

    return {
        "worker_id": assignment["worker_id"],
        "gradients": gradients
    }