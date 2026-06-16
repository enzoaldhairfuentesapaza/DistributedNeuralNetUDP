import torch

def average_gradients(results):

    averaged = {}

    first_result = next(
        iter(results.values())
    )

    first_gradients = first_result["gradients"]

    for name in first_gradients:

        averaged[name] = torch.zeros_like(
            first_gradients[name]
        )

    for result in results.values():

        for name, grad in result["gradients"].items():

            averaged[name] += grad

    total = len(results)

    for name in averaged:

        averaged[name] /= total

    return averaged