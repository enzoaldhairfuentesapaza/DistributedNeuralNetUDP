import pickle


def serialize_assignment(assignment):

    return pickle.dumps(
        assignment,
        protocol=pickle.HIGHEST_PROTOCOL
    )


def deserialize_assignment(data):

    return pickle.loads(data)


def serialize_gradient(result):

    return pickle.dumps(
        result,
        protocol=pickle.HIGHEST_PROTOCOL
    )


def deserialize_gradient(data):

    return pickle.loads(data)