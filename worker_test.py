import pickle

from worker_runtime import compute_gradients


print("Reading assignment...")

with open(
    "test_exchange/assignment.bin",
    "rb"
) as f:

    assignment = pickle.load(f)


print(
    "Worker:",
    assignment["worker_id"]
)

print(
    "X shape:",
    assignment["X"].shape
)

print(
    "y shape:",
    assignment["y"].shape
)


print("Computing gradients...")

result = compute_gradients(
    assignment
)

print("Gradient computation complete")


with open(
    "test_exchange/gradient.bin",
    "wb"
) as f:

    pickle.dump(
        result,
        f
    )

print("gradient.bin generated")