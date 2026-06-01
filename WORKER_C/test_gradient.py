import numpy as np

with open("gradient_1.bin", "rb") as f:

    fc1_w = np.load(f)
    fc1_b = np.load(f)

    fc2_w = np.load(f)
    fc2_b = np.load(f)

    out_w = np.load(f)
    out_b = np.load(f)

print("fc1 weight:", fc1_w.shape)
print("fc1 bias:", fc1_b.shape)

print("fc2 weight:", fc2_w.shape)
print("fc2 bias:", fc2_b.shape)

print("output weight:", out_w.shape)
print("output bias:", out_b.shape)