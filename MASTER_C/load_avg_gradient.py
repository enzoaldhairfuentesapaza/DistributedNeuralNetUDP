import numpy as np
import torch


def load_average_gradients(model, filename="avg_gradient.bin"):

    with open(filename, "rb") as f:

        fc1_w = np.load(f)
        fc1_b = np.load(f)

        fc2_w = np.load(f)
        fc2_b = np.load(f)

        out_w = np.load(f)
        out_b = np.load(f)

    model.fc1.weight.grad = torch.tensor(fc1_w,dtype=torch.float32)
    model.fc1.bias.grad = torch.tensor(fc1_b,dtype=torch.float32)
    model.fc2.weight.grad = torch.tensor(fc2_w,dtype=torch.float32)
    model.fc2.bias.grad = torch.tensor(fc2_b,dtype=torch.float32)
    model.class_logits.weight.grad = torch.tensor(out_w,dtype=torch.float32)
    model.class_logits.bias.grad = torch.tensor(out_b, dtype=torch.float32)