import numpy as np
with open("assignments/worker_1.bin","rb") as f:

    x = np.load(f)
    y = np.load(f)

    w1 = np.load(f)
    b1 = np.load(f)

    w2 = np.load(f)
    b2 = np.load(f)

    w3 = np.load(f)
    b3 = np.load(f)

print(x.shape)
print(y.shape)
print(w1.shape)