from serialization import *

import pickle


obj = {
    "worker_id": 1,
    "value": 123
}

data = serialize_assignment(
    obj
)

print(type(data))

restored = deserialize_assignment(
    data
)

print(restored)