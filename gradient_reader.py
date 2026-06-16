import pickle

with open(
    "test_exchange/gradient.bin",
    "rb"
) as f:

    result = pickle.load(f)

print(result.keys())

print(
    result["gradients"].keys()
)

for name, grad in result["gradients"].items():

    print(
        name,
        grad.shape,
        grad.abs().sum().item()
    )