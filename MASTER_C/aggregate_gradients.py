import os
import numpy as np

RESULTS_DIR = "results"


def load_gradient(path):

    with open(path, "rb") as f:

        fc1_w = np.load(f)
        fc1_b = np.load(f)

        fc2_w = np.load(f)
        fc2_b = np.load(f)

        out_w = np.load(f)
        out_b = np.load(f)

    return {
        "fc1_w": fc1_w,
        "fc1_b": fc1_b,

        "fc2_w": fc2_w,
        "fc2_b": fc2_b,

        "out_w": out_w,
        "out_b": out_b
    }


gradient_files = sorted([os.path.join(RESULTS_DIR, f) for f in os.listdir(RESULTS_DIR) if f.endswith(".bin")])

if len(gradient_files) == 0:
    raise RuntimeError( "No gradient files found")


gradients = [load_gradient(path) for path in gradient_files]


num_workers = len(gradients)


avg = {}

for key in gradients[0]:

    avg[key] = np.zeros_like( gradients[0][key])

    for g in gradients:
        avg[key] += g[key]

    avg[key] /= num_workers


print("\nWorkers encontrados:", num_workers)

print("\nPromedios calculados:")

for key, value in avg.items():
    print(key, value.shape)

with open("avg_gradient.bin", "wb") as f:

    np.save(f, avg["fc1_w"])
    np.save(f, avg["fc1_b"])

    np.save(f, avg["fc2_w"])
    np.save(f, avg["fc2_b"])

    np.save(f, avg["out_w"])
    np.save(f, avg["out_b"])

print("\navg_gradient.bin generado")