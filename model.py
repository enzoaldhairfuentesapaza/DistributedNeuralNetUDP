import torch
import torch.nn as nn
import torch.nn.functional as F

class MulticlassClassifier(nn.Module):
    def __init__(self, input_dim: int, num_classes: int, hidden1: int = 128, hidden2: int = 64):
        super(MulticlassClassifier, self).__init__()
        self.fc1 = nn.Linear(input_dim, hidden1)
        self.fc2 = nn.Linear(hidden1, hidden2)
        self.class_logits = nn.Linear(hidden2, num_classes)      # Predict class scores
        self.class_log_vars = nn.Linear(hidden2, num_classes)     # Predict log-variance for each class

    def forward(self, x: torch.Tensor):
        x = F.relu(self.fc1(x))
        x = F.relu(self.fc2(x))
        logits = self.class_logits(x)
        log_vars = self.class_log_vars(x)
        return logits, log_vars