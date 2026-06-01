import sys
import numpy as np

import torch
import torch.nn as nn
import torch.nn.functional as F


class MulticlassClassifier(nn.Module):

    def __init__( self, input_dim=14, num_classes=3, hidden1=128, hidden2=64):
        super().__init__()

        self.fc1 = nn.Linear(input_dim, hidden1)
        self.fc2 = nn.Linear(hidden1, hidden2)

        self.class_logits = nn.Linear(hidden2, num_classes)

    def forward(self, x):

        x = F.relu(self.fc1(x))
        x = F.relu(self.fc2(x))

        logits = self.class_logits(x)

        return logits



if len(sys.argv) != 3:
    print("uso: python worker_train.py input.bin output.bin")
    sys.exit(1)

input_file = sys.argv[1]
output_file = sys.argv[2]


with open(input_file, "rb") as f:

    batch_x = np.load(f)
    batch_y = np.load(f)

    fc1_w = np.load(f)
    fc1_b = np.load(f)

    fc2_w = np.load(f)
    fc2_b = np.load(f)

    out_w = np.load(f)
    out_b = np.load(f)


model = MulticlassClassifier()


with torch.no_grad():

    model.fc1.weight.copy_(
        torch.tensor(fc1_w)
    )

    model.fc1.bias.copy_(
        torch.tensor(fc1_b)
    )

    model.fc2.weight.copy_(
        torch.tensor(fc2_w)
    )

    model.fc2.bias.copy_(
        torch.tensor(fc2_b)
    )

    model.class_logits.weight.copy_(
        torch.tensor(out_w)
    )

    model.class_logits.bias.copy_(
        torch.tensor(out_b)
    )


batch_x = torch.tensor(
    batch_x,
    dtype=torch.float32
)

batch_y = torch.tensor(
    batch_y,
    dtype=torch.float32
)


criterion = nn.CrossEntropyLoss()

logits = model(batch_x)

loss = criterion(
    logits,
    batch_y
)

model.zero_grad()

loss.backward()


fc1_w_grad = (model.fc1.weight.grad.detach().numpy())
fc1_b_grad = (model.fc1.bias.grad.detach().numpy())
fc2_w_grad = (model.fc2.weight.grad.detach().numpy())
fc2_b_grad = (model.fc2.bias.grad.detach().numpy())
out_w_grad = (model.class_logits.weight.grad.detach().numpy())
out_b_grad = (model.class_logits.bias.grad.detach().numpy())



with open(output_file, "wb") as f:

    np.save(f, fc1_w_grad)
    np.save(f, fc1_b_grad)

    np.save(f, fc2_w_grad)
    np.save(f, fc2_b_grad)

    np.save(f, out_w_grad)
    np.save(f, out_b_grad)

print("GradientResult generado:", output_file)