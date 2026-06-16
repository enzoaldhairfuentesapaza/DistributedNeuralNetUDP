import dnn_udp

print("before")

try:
    dnn_udp.exchange({})
except Exception as e:
    print("exception:", e)

print("after")