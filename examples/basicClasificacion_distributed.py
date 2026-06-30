import torch
import torch.nn as nn
import torch.nn.functional as F

from torch.utils.data import (
    DataLoader,
    TensorDataset,
    random_split
)

import pandas as pd
import numpy as np
import matplotlib.pyplot as plt

from sklearn.metrics import (
    confusion_matrix,
    ConfusionMatrixDisplay,
    classification_report
)

# Librerías personalizadas de tu entorno distribuido
import dnn_udp
from serialization import (
    serialize_assignment,
    deserialize_gradient
)
from gradient_utils import (
    average_gradients
)

# --------------------------------------------------
# MODELO (Definido explícitamente desde tu primer código)
# --------------------------------------------------
class MulticlassClassifier(nn.Module):
    def __init__(self, input_dim: int, num_classes: int, hidden1: int = 128, hidden2: int = 64):
        super(MulticlassClassifier, self).__init__()
        self.fc1 = nn.Linear(input_dim, hidden1)
        self.fc2 = nn.Linear(hidden1, hidden2)
        self.class_logits = nn.Linear(hidden2, num_classes)      # Predice scores de clases
        self.class_log_vars = nn.Linear(hidden2, num_classes)    # Predice log-varianza para cada clase

    def forward(self, x: torch.Tensor):
        x = F.relu(self.fc1(x))
        x = F.relu(self.fc2(x))
        logits = self.class_logits(x)
        log_vars = self.class_log_vars(x)
        return logits, log_vars

# --------------------------------------------------
# CONFIGURACION
# --------------------------------------------------
NUM_WORKERS = 10
NUM_EPOCHS = 360  # Mantenemos los 360 del código original
INPUT_DIM = 14
NUM_CLASSES = 3
BATCH_SIZE = 50

# --------------------------------------------------
# CARGAR DATASET
# --------------------------------------------------
csv_path = "Dataset of Diabetes.csv"

df = pd.read_csv(
    csv_path,
    header=None,
    skiprows=1
)

X_np = df.iloc[:, :INPUT_DIM].values.astype(np.float32)
y_onehot_np = df.iloc[:, -NUM_CLASSES:].values.astype(np.float32)

X = torch.tensor(X_np)
y = torch.tensor(y_onehot_np)

print("X shape:", X.shape)
print("y shape:", y.shape)

# --------------------------------------------------
# SPLIT TRAIN / TEST
# --------------------------------------------------
dataset = TensorDataset(X, y)
train_size = int(0.8 * len(dataset))
test_size = len(dataset) - train_size

torch.manual_seed(42) # Mantener consistencia de aleatoriedad
train_dataset, test_dataset = random_split(
    dataset, 
    [train_size, test_size]
)

train_loader = DataLoader(
    train_dataset,
    batch_size=BATCH_SIZE,
    shuffle=True
)

test_loader = DataLoader(
    test_dataset,
    batch_size=BATCH_SIZE,
    shuffle=False
)

# --------------------------------------------------
# MODELO E INICIALIZACIÓN
# --------------------------------------------------
model = MulticlassClassifier(input_dim=INPUT_DIM, num_classes=NUM_CLASSES)
criterion = nn.CrossEntropyLoss()
optimizer = torch.optim.Adam(model.parameters(), lr=0.001)

# --------------------------------------------------
# TRACKERS
# --------------------------------------------------
train_tracker = []
test_tracker = []
accuracy_tracker = []

# --------------------------------------------------
# TRAINING DISTRIBUIDO (Local / Lineal por Workers)
# --------------------------------------------------
for epoch in range(NUM_EPOCHS):
    print(f"\n========== EPOCH {epoch + 1}/{NUM_EPOCHS} ==========")
    
    model.train()
    
    train_X = []
    train_y = []
    
    # Consolidamos el dataset de entrenamiento para repartir a los Workers
    for batch_x, batch_y in train_loader:
        train_X.append(batch_x)
        train_y.append(batch_y)
        
    train_X = torch.cat(train_X)
    train_y = torch.cat(train_y)
    
    assignments = {}
    chunk_size = len(train_X) // NUM_WORKERS
    
    # Distribución lineal de los datos entre los Workers locales
    for worker_id in range(1, NUM_WORKERS + 1):
        start = (worker_id - 1) * chunk_size
        end = len(train_X) if worker_id == NUM_WORKERS else worker_id * chunk_size
        
        assignment = {
            "worker_id": worker_id,
            "X": train_X[start:end],
            "y": train_y[start:end],
            "model_state": model.state_dict()
        }
        
        assignments[worker_id] = serialize_assignment(assignment)
        
    print("Sending work to workers...")
    results = dnn_udp.exchange(assignments)
    print(f"Received gradients from {len(results)} workers")
    
    # Decodificar gradientes y promediarlos
    decoded_results = {}
    for worker_id, payload in results.items():
        decoded_results[worker_id] = deserialize_gradient(payload)
        
    avg_grads = average_gradients(decoded_results)
    
    # Aplicar gradientes distribuidos al modelo central
    optimizer.zero_grad()
    for name, param in model.named_parameters():
        if name in avg_grads:
            param.grad = avg_grads[name]
    optimizer.step()
    
    # ------------------------------------------
    # CÁLCULO DEL TRAIN LOSS (Evaluación Post-Step)
    # ------------------------------------------
    model.eval()
    with torch.no_grad():
        # Desempaquetamos logits y log_vars tal como define tu arquitectura original
        logits, _ = model(train_X)
        train_loss = criterion(logits, train_y)
        
    train_tracker.append(train_loss.item())
    
    # ------------------------------------------
    # EVALUACIÓN EN EL TEST SET
    # ------------------------------------------
    y_true = []
    y_pred = []
    test_loss = 0
    total = 0
    correct = 0
    
    with torch.no_grad():
        for batch_x, batch_y in test_loader:
            # Desempaquetamos logits y omitimos log_vars para CrossEntropy
            logits, _ = model(batch_x)
            loss = criterion(logits, batch_y)
            test_loss += loss.item()
            
            predictions = torch.argmax(logits, dim=1)
            targets = torch.argmax(batch_y, dim=1)
            
            correct += (predictions == targets).sum().item()
            total += batch_x.size(0)
            
            y_true.extend(targets.tolist())
            y_pred.extend(predictions.tolist())
            
    test_loss /= len(test_loader)
    accuracy = correct / total
    
    test_tracker.append(test_loss)
    accuracy_tracker.append(accuracy)
    
    print(f"Train Loss : {train_loss.item():.6f}")
    print(f"Test Loss  : {test_loss:.6f}")
    print(f"Accuracy   : {accuracy:.6f}")

# --------------------------------------------------
# GRAFICAS HISTÓRICAS
# --------------------------------------------------
plt.figure(figsize=(10, 5))
plt.plot(train_tracker, label='Training Loss')
plt.plot(test_tracker, label='Test Loss')
plt.plot(accuracy_tracker, label='Test Accuracy')
plt.title("Historial de Entrenamiento Distribuido")
plt.xlabel("Epoch")
plt.ylabel("Valor")
plt.legend()
plt.grid(True)
plt.show()

# --------------------------------------------------
# MATRIZ DE CONFUSION
# --------------------------------------------------
cm = confusion_matrix(y_true, y_pred)
disp = ConfusionMatrixDisplay(confusion_matrix=cm, display_labels=list(range(NUM_CLASSES)))
disp.plot(cmap=plt.cm.Blues)
plt.title("Confusion Matrix")
plt.show()

# --------------------------------------------------
# REPORTE DE CLASIFICACIÓN FINAL
# --------------------------------------------------
print("\nClassification Report:")
print(classification_report(y_true, y_pred, digits=3))