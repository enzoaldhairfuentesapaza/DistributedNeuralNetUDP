import pickle

with open(
    "results/worker_1.bin",
    "rb"
) as f:
    result = pickle.load(f)

for name, grad in result["gradients"].items():
    print(
        name,
        grad.shape,
        grad.abs().sum().item()
    )