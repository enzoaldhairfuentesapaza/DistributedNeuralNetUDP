# Distributed Neural Network UDP

Sistema de entrenamiento distribuido de redes neuronales utilizando un protocolo RDT implementado sobre UDP.

## Arquitectura

El sistema está compuesto por:

* Master (C++)
* Workers (C++)
* Runtime de gradientes (Python + PyTorch)
* Binding C++ ↔ Python mediante PyBind11
* Protocolo RDT confiable sobre UDP

Flujo general:

```text
Python Training Script
        |
        v
   dnn_udp.so
        |
        v
 MasterTransport
        |
        v
+---- Worker 1 ----+
| Python Runtime   |
+------------------+
+---- Worker 2 ----+
| Python Runtime   |
+------------------+
...
+---- Worker 10 ---+
| Python Runtime   |
+------------------+

        |
        v

Promedio de gradientes
        |
        v

Actualización del modelo
```

---

# Dependencias del sistema

## Ubuntu / WSL

```bash
sudo apt update

sudo apt install -y \
build-essential \
g++ \
clang \
python3-dev \
python3-pip \
python3-venv
```

---

# Crear entorno virtual

Desde la raíz del proyecto:

```bash
python3 -m venv .venv
```

Activar:

```bash
source .venv/bin/activate
```

---

# Dependencias Python

Instalar:

```bash
pip install torch
pip install numpy
pip install pandas
pip install matplotlib
pip install scikit-learn
pip install pybind11
```

Verificar:

```bash
python -c "import torch"
python -c "import pybind11"
```

---

# Compilar librería UDP

Limpiar:

```bash
make clean
```

Compilar:

```bash
make
```

Esto genera:

```text
master
worker
libAPI_RDT_UDP.a
```

---

# Compilar módulo Python

Desde el entorno virtual:

```bash
g++ \
-O3 \
-Wall \
-shared \
-std=c++17 \
-fPIC \
$(python -m pybind11 --includes) \
dnn_udp_bindings.cpp \
API_RDT_UDP.cpp \
protocol.cpp \
-o dnn_udp$(python3-config --extension-suffix)
```

Verificar:

```bash
ls dnn_udp*.so
```

Ejemplo:

```text
dnn_udp.cpython-312-x86_64-linux-gnu.so
```

Probar:

```bash
python -c "
import dnn_udp
print(dir(dnn_udp))
"
```

Debe aparecer:

```text
['exchange']
```

---

# Iniciar workers

Abrir 10 terminales.

Worker 1:

```bash
./worker 1 worker_1_gradient.bin worker_1_assignment.bin
```

Worker 2:

```bash
./worker 2 worker_2_gradient.bin worker_2_assignment.bin
```

...

Worker 10:

```bash
./worker 10 worker_10_gradient.bin worker_10_assignment.bin
```

Cada worker escuchará:

```text
127.0.0.1:9001
127.0.0.1:9002
...
127.0.0.1:9010
```

---

# Ejecutar entrenamiento distribuido

Con los 10 workers activos:

```bash
python basicClasification_distributed.py
```

Salida esperada:

```text
========== EPOCH 1/20 ==========
Sending work...
Received gradients from 10 workers

Train Loss : ...
Test Loss  : ...
Accuracy   : ...
```

---

# Estructura importante

```text
API_RDT_UDP.cpp
API_RDT_UDP.hpp

protocol.cpp
protocol.hpp

worker.cpp
master.cpp

dnn_udp_bindings.cpp

worker_runtime.py
worker_runtime_server.py

serialization.py
gradient_utils.py

model.py

basicClasification_distributed.py
```

---

# Notas importantes

## Consistencia del modelo

El master y los workers deben utilizar exactamente la misma arquitectura.

Actualmente:

```python
MulticlassClassifier(
    input_dim=14,
    num_classes=3
)
```

Si alguno utiliza otro valor (por ejemplo 11), aparecerá:

```text
RuntimeError:
size mismatch for fc1.weight
```

---

## Compilar nuevamente

Si se modifica:

```text
API_RDT_UDP.cpp
API_RDT_UDP.hpp
protocol.cpp
protocol.hpp
worker.cpp
master.cpp
```

ejecutar:

```bash
make clean
make
```

Si se modifica:

```text
dnn_udp_bindings.cpp
API_RDT_UDP.cpp
protocol.cpp
```

recompilar el módulo Python:

```bash
rm -f dnn_udp*.so
```

```bash
g++ \
-O3 \
-Wall \
-shared \
-std=c++17 \
-fPIC \
$(python -m pybind11 --includes) \
dnn_udp_bindings.cpp \
API_RDT_UDP.cpp \
protocol.cpp \
-o dnn_udp$(python3-config --extension-suffix)
```

---

# Problemas conocidos

## Timeout de workers

Verificar que los 10 workers estén activos antes de iniciar el entrenamiento.

Error típico:

```text
RuntimeError:
Worker X: transfer timed out
```

---

## Error de memoria

Durante entrenamientos largos puede aparecer:

```text
OSError: [Errno 12] Cannot allocate memory
```

debido a que cada assignment lanza un proceso Python nuevo.

La solución futura es implementar workers persistentes que mantengan el runtime Python cargado entre épocas.

---

# Autores

Proyecto de entrenamiento distribuido sobre UDP confiable utilizando C++, PyBind11 y PyTorch.
