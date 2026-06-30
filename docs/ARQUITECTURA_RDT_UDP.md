# Arquitectura de la API RDT UDP

Este documento explica el funcionamiento de los archivos principales que implementan el protocolo UDP/RDT del proyecto:

- `datagram.hpp`
- `protocol.hpp`
- `protocol.cpp`
- `API_RDT_UDP.hpp`
- `API_RDT_UDP.cpp`

La idea general es separar el sistema en capas. Cada capa resuelve un problema distinto y usa la capa inferior sin mezclar responsabilidades.

```text
API_RDT_UDP.hpp / API_RDT_UDP.cpp
    API reusable para el master y los workers.
    Expone clases simples que mueven bytes opacos.

protocol.hpp / protocol.cpp
    Implementa el protocolo confiable sobre UDP.
    Maneja Go-Back-N, ACK acumulativo, timeouts, retries y reconstruccion de objetos.

datagram.hpp
    Define el formato binario de cada datagrama.
    Serializa, parsea y valida CRC32.
```

La capa C++ no interpreta tensores, pesos, gradientes ni objetos de Python. Solo transporta `std::vector<uint8_t>`. Python u otra capa superior sera responsable de convertir objetos reales a bytes y bytes a objetos reales.

## 1. `datagram.hpp`

`datagram.hpp` define la unidad minima que viaja por UDP: el datagrama.

Un datagrama contiene un header fijo de `36 bytes` y un payload maximo de `476 bytes`. Por eso el datagrama completo nunca supera `512 bytes`.

```cpp
constexpr size_t DATAGRAM_SIZE = 512;
constexpr size_t HEADER_SIZE = 36;
constexpr size_t MAX_PAYLOAD = DATAGRAM_SIZE - HEADER_SIZE; // 476
```

### Tipos de datagrama

```cpp
enum class DatagramType : uint8_t {
    Data = 0x01,
    Ack = 0x02,
    Start = 0x03,
    End = 0x04,
};
```

- `Start`: anuncia el inicio de una transferencia de objeto.
- `Data`: transporta un fragmento del objeto.
- `Ack`: confirma recepcion acumulativa.
- `End`: cierra la transferencia cuando todos los fragmentos fueron confirmados.

### Tipos de objeto

```cpp
enum class ObjectType : uint8_t {
    None = 0x00,
    WorkAssignment = 0x01,
    GradientResult = 0x02,
    Control = 0x03,
};
```

- `WorkAssignment`: objeto que el master envia a un worker.
- `GradientResult`: objeto que un worker devuelve al master.
- `None`: usado en ACK, porque el ACK no transporta objeto de aplicacion.
- `Control`: reservado para mensajes de control futuros.

### Estructura logica del datagrama

```cpp
struct Datagram {
    DatagramType type;
    uint32_t seq;
    uint32_t ack;
    uint32_t transfer_id;
    uint16_t sender_id;
    uint16_t receiver_id;
    ObjectType object_type;
    uint32_t object_size;
    uint32_t fragment;
    uint32_t total_fragments;
    std::vector<uint8_t> payload;
};
```

Campos principales:

- `type`: indica si es `START`, `DATA`, `ACK` o `END`.
- `seq`: numero de secuencia del datagrama.
- `ack`: ACK acumulativo. En `ACK n`, significa que todo hasta `n` llego correctamente en orden.
- `transfer_id`: identifica una transferencia completa.
- `sender_id`: nodo que envia.
- `receiver_id`: nodo destino.
- `object_type`: tipo de objeto transportado.
- `object_size`: tamano total del objeto original.
- `fragment`: indice del fragmento dentro del objeto.
- `total_fragments`: cantidad total de fragmentos.
- `payload`: bytes utiles del fragmento.

### Header binario

La serializacion usa big-endian para los campos numericos:

| Offset | Tamano | Campo |
| --- | ---: | --- |
| 0 | 1 | `type` |
| 1 | 4 | `seq` |
| 5 | 4 | `ack` |
| 9 | 4 | `transfer_id` |
| 13 | 2 | `sender_id` |
| 15 | 2 | `receiver_id` |
| 17 | 1 | `object_type` |
| 18 | 4 | `object_size` |
| 22 | 4 | `fragment` |
| 26 | 4 | `total_fragments` |
| 30 | 2 | `payload_size` |
| 32 | 4 | `crc32` |

El CRC se calcula sobre:

1. Header con el campo CRC temporalmente en cero.
2. Payload valido.

Si el CRC recibido no coincide con el CRC recalculado, `parse_datagram(...)` devuelve `false` y la capa superior trata el datagrama como corrupto.

### Funciones importantes

`make_datagram(...)`

Crea la base de un datagrama con los campos comunes.

`make_ack_datagram(...)`

Crea un ACK con:

- `object_type = ObjectType::None`
- `object_size = 0`
- `total_fragments = 0`
- `ack = ack_value`

`make_data_datagram(...)`

Toma un objeto completo en bytes y extrae el fragmento correspondiente segun `index`.

`serialize_datagram(...)`

Convierte un `Datagram` en bytes listos para enviar por UDP.

`parse_datagram(...)`

Convierte bytes recibidos por UDP en un `Datagram`, validando:

- tamano minimo;
- tipo de datagrama valido;
- tipo de objeto valido;
- payload no mayor a `476 bytes`;
- longitud exacta;
- CRC32 correcto.

## 2. `protocol.hpp` y `protocol.cpp`

Estos archivos implementan el protocolo confiable sobre UDP. A diferencia de `datagram.hpp`, aqui ya existen sockets, timeouts, retransmisiones y reconstruccion de objetos completos.

`protocol.hpp` declara las constantes y funciones disponibles:

```cpp
constexpr uint16_t MASTER_ID = 0;
constexpr uint16_t FIRST_WORKER_ID = 1;
constexpr uint16_t LAST_WORKER_ID = 10;

constexpr int WINDOW_SIZE = 8;
constexpr int TIMEOUT_MS = 500;
constexpr int MAX_RETRIES = 5;
```

IDs:

- Master: `0`
- Workers: `1..10`

Transfer IDs:

```cpp
assignment_transfer_id(worker_id) = 1000 + worker_id
gradient_transfer_id(worker_id)   = 2000 + worker_id
```

Esto permite distinguir claramente:

- transferencia master -> worker;
- transferencia worker -> master.

### Funciones de bajo nivel con socket

`send_datagram(...)`

Serializa un `Datagram` y lo envia usando `sendto(...)`.

`receive_datagram(...)`

Recibe bytes con `recvfrom(...)` y llama a `parse_datagram(...)`.

Devuelve:

- `true` si el datagrama fue recibido y parseado correctamente;
- `false` si no se pudo recibir o si esta corrupto;
- `corrupt = true` si llego algo pero fallo la validacion de formato o CRC.

`same_address(...)`

Compara dos direcciones UDP (`sockaddr_in`) para confirmar que pertenecen al mismo peer.

### Envio confiable de objetos: `send_object(...)`

```cpp
bool send_object(int sock,
                 const sockaddr_in& address,
                 const std::vector<uint8_t>& object,
                 uint32_t transfer_id,
                 uint16_t sender_id,
                 uint16_t receiver_id,
                 ObjectType object_type);
```

Esta funcion envia un objeto completo como bytes opacos. No interpreta el contenido.

Proceso:

1. Calcula `total_fragments`.
2. Envia un `START`.
3. Espera `ACK_NONE` para confirmar que el receptor esta listo.
4. Envia datagramas `DATA` usando Go-Back-N con ventana `8`.
5. Espera ACKs acumulativos.
6. Si hay timeout, retransmite desde `base`.
7. Si supera `MAX_RETRIES`, falla.
8. Cuando todos los datos fueron confirmados, envia `END`.
9. Espera ACK final con `ack = total_fragments`.

### Recepcion confiable de objetos: `receive_object(...)`

```cpp
bool receive_object(int sock,
                    uint32_t transfer_id,
                    uint16_t expected_sender,
                    uint16_t expected_receiver,
                    ObjectType object_type,
                    std::vector<uint8_t>& object,
                    sockaddr_in& peer_address);
```

Esta funcion reconstruye un objeto completo a partir de varios datagramas.

Proceso:

1. Espera `START` con el `transfer_id`, `sender_id`, `receiver_id` y `object_type` esperados.
2. Responde `ACK_NONE`.
3. Recibe `DATA` en orden.
4. Si llega el fragmento esperado, lo agrega al buffer y responde `ACK expected_seq`.
5. Si llega un fragmento fuera de orden, repite el ultimo ACK valido.
6. Si llega un datagrama corrupto, responde el ultimo ACK valido o `ACK_NONE`.
7. Cuando llega `END`, valida que:
   - llegaron todos los fragmentos;
   - `seq == total_fragments`;
   - el tamano reconstruido coincide con `object_size`.
8. Responde ACK final.
9. Entrega el objeto reconstruido en `object`.

### ACK acumulativo

El protocolo no usa NACK.

```text
ACK_NONE = 0xFFFFFFFF
```

Significa: todavia no llego ningun `DATA` valido.

Despues:

```text
ACK 0 -> llego correctamente el fragmento 0
ACK 1 -> llegaron correctamente los fragmentos 0 y 1
ACK n -> llego todo hasta n en orden
```

Si se pierde o corrompe un datagrama, el receptor repite el ultimo ACK valido. El emisor, al no avanzar su ventana o al llegar a timeout, retransmite desde `base`.

## 3. `API_RDT_UDP.hpp`

Este header es la API publica pensada para ser usada por una capa superior, por ejemplo Python mediante pybind11.

Todo esta dentro del namespace:

```cpp
namespace API_RDT_UDP
```

### Tipo `Bytes`

```cpp
using Bytes = std::vector<uint8_t>;
```

Representa un objeto serializado opaco. Para C++, el contenido es solo una secuencia de bytes.

Ejemplos de datos que podrian viajar dentro de `Bytes`:

- un `pickle` generado desde Python;
- un arreglo NumPy serializado;
- un archivo binario;
- JSON;
- cualquier formato propio.

### `WorkerEndpoint`

```cpp
struct WorkerEndpoint {
    std::string host;
    uint16_t port;
    uint16_t id;
};
```

Describe donde esta escuchando un worker.

La funcion `default_workers()` devuelve:

```text
worker 1  -> 127.0.0.1:9001
worker 2  -> 127.0.0.1:9002
...
worker 10 -> 127.0.0.1:9010
```

### `MasterTransport`

```cpp
class MasterTransport {
public:
    explicit MasterTransport(std::vector<WorkerEndpoint> workers = default_workers());

    std::map<uint16_t, Bytes> exchange(
        const std::map<uint16_t, Bytes>& assignments
    ) const;
};
```

Responsabilidad:

- recibir un mapa de assignments;
- enviar un `WORK_ASSIGNMENT` a cada worker;
- recibir un `GRADIENT_RESULT` de cada worker;
- devolver los resultados reconstruidos.

Entrada:

```cpp
std::map<uint16_t, Bytes> assignments;
```

Cada clave es el `worker_id`.

Ejemplo:

```cpp
assignments[1] = bytes_para_worker_1;
assignments[2] = bytes_para_worker_2;
```

Salida:

```cpp
std::map<uint16_t, Bytes> results;
```

Cada clave es el `worker_id` que envio el resultado.

### `WorkerTransport`

```cpp
class WorkerTransport {
public:
    explicit WorkerTransport(uint16_t worker_id);
    ~WorkerTransport();

    Bytes receive_assignment();
    void send_gradient(const Bytes& gradient_result);
};
```

Responsabilidad:

1. Abrir un socket UDP en el puerto del worker.
2. Esperar un `WORK_ASSIGNMENT`.
3. Recordar la direccion del master.
4. Enviar de vuelta un `GRADIENT_RESULT`.

Restriccion importante:

```cpp
send_gradient(...)
```

debe llamarse despues de:

```cpp
receive_assignment()
```

porque el worker aprende la direccion del master cuando recibe el assignment.

## 4. `API_RDT_UDP.cpp`

Este archivo implementa la API publica y coordina los workers.

No define el formato de datagrama ni implementa el RDT generico desde cero. Usa las funciones de `protocol.cpp`.

### Estado interno del master

Para manejar 10 workers al mismo tiempo sin threads, el master usa estructuras de estado por worker:

```cpp
struct SendState
```

Guarda el progreso del envio del `WORK_ASSIGNMENT`:

- objeto a enviar;
- `transfer_id`;
- `base`;
- `next`;
- cantidad de fragmentos;
- fase actual;
- retries;
- ultimo envio.

```cpp
struct ReceiveState
```

Guarda el progreso de recepcion del `GRADIENT_RESULT`:

- `transfer_id`;
- `expected_seq`;
- datos reconstruidos;
- tamano total esperado;
- si ya termino.

```cpp
struct WorkerSession
```

Une:

- datos del worker;
- direccion UDP;
- estado de envio;
- estado de recepcion.

### Fases de envio

```cpp
enum class SendPhase {
    Start,
    Data,
    End,
    Done,
    Failed,
};
```

Cada worker avanza por estas fases:

```text
Start -> Data -> End -> Done
```

Si hay demasiados timeouts:

```text
Failed
```

### Event loop del master

El master usa `select()` para estar atento a datagramas de cualquier worker.

No crea 10 threads. En su lugar:

1. Recorre las sesiones y empuja envios pendientes.
2. Usa `select()` para esperar datagramas entrantes.
3. Cuando llega algo, identifica de que worker vino.
4. Si es ACK de assignment, actualiza `SendState`.
5. Si es `GRADIENT_RESULT`, actualiza `ReceiveState`.
6. Cuando todas las sesiones estan en `Done`, termina.

Esto permite recibir datagramas de varios workers aunque lleguen muy cerca en el tiempo, porque el socket UDP acumula datagramas en el buffer del sistema operativo y el event loop los drena.

### Worker real

`WorkerTransport` es mas simple:

1. Abre un socket UDP en `9000 + worker_id`.
2. Llama a `receive_object(...)` esperando:
   - `transfer_id = 1000 + worker_id`;
   - sender master `0`;
   - receiver worker `worker_id`;
   - object type `WorkAssignment`.
3. Guarda la direccion del master.
4. Cuando se llama a `send_gradient(...)`, usa `send_object(...)` con:
   - `transfer_id = 2000 + worker_id`;
   - sender worker `worker_id`;
   - receiver master `0`;
   - object type `GradientResult`.

## 5. Ejemplo de uso

Este ejemplo muestra como se usaria la API desde C++. La integracion con Python podria exponer estas mismas clases con pybind11.

### Master

```cpp
#include "API_RDT_UDP.hpp"

#include <cstdint>
#include <iostream>
#include <map>
#include <string>

int main() {
    std::map<uint16_t, API_RDT_UDP::Bytes> assignments;

    for (const auto& worker : API_RDT_UDP::default_workers()) {
        std::string text = "assignment para worker " + std::to_string(worker.id);
        assignments[worker.id] = API_RDT_UDP::Bytes(text.begin(), text.end());
    }

    API_RDT_UDP::MasterTransport master;
    std::map<uint16_t, API_RDT_UDP::Bytes> results = master.exchange(assignments);

    for (const auto& item : results) {
        uint16_t worker_id = item.first;
        const API_RDT_UDP::Bytes& gradient = item.second;

        std::cout << "Worker " << worker_id
                  << " devolvio " << gradient.size()
                  << " bytes\n";
    }
}
```

### Worker

```cpp
#include "API_RDT_UDP.hpp"

#include <cstdint>
#include <string>

int main() {
    uint16_t worker_id = 1;

    API_RDT_UDP::WorkerTransport worker(worker_id);

    API_RDT_UDP::Bytes assignment = worker.receive_assignment();

    // Aqui la capa superior deserializaria assignment,
    // ejecutaria backpropagation y produciria gradientes.
    std::string fake_result = "gradient result del worker 1";
    API_RDT_UDP::Bytes gradient(fake_result.begin(), fake_result.end());

    worker.send_gradient(gradient);
}
```

## 6. Proceso completo paso a paso

Supongamos que el master tiene un objeto serializado para el worker 1.

```text
worker_id = 1
assignment_transfer_id = 1001
gradient_transfer_id = 2001
```

### Envio master -> worker

1. El master recibe `assignments[1]`.
2. `MasterTransport::exchange(...)` crea una sesion para worker 1.
3. El master envia `START`:

```text
type = START
transfer_id = 1001
sender_id = 0
receiver_id = 1
object_type = WORK_ASSIGNMENT
object_size = tamano total
total_fragments = cantidad de DATA necesarios
```

4. El worker responde:

```text
type = ACK
ack = ACK_NONE
transfer_id = 1001
sender_id = 1
receiver_id = 0
object_type = NONE
```

5. El master envia hasta 8 `DATA` por ventana.
6. El worker acepta solo el siguiente fragmento esperado.
7. Si recibe correctamente el fragmento 0, responde `ACK 0`.
8. Si recibe correctamente hasta el fragmento 7, responde `ACK 7`.
9. Si se pierde el fragmento 4, el worker sigue repitiendo `ACK 3`.
10. El master retransmite desde `base = 4` cuando corresponde.
11. Cuando todo llega, el master envia `END`.
12. El worker responde ACK final y entrega el `WORK_ASSIGNMENT` reconstruido a la capa superior.

### Calculo del worker

1. La capa superior deserializa el assignment.
2. Ejecuta backpropagation.
3. Serializa los gradientes como bytes.
4. Llama a `send_gradient(...)`.

### Envio worker -> master

1. El worker envia `START`:

```text
type = START
transfer_id = 2001
sender_id = 1
receiver_id = 0
object_type = GRADIENT_RESULT
```

2. El master responde `ACK_NONE`.
3. El worker envia los `DATA`.
4. El master confirma con ACK acumulativos.
5. El worker envia `END`.
6. El master reconstruye el `GRADIENT_RESULT`.
7. `MasterTransport::exchange(...)` devuelve:

```cpp
results[1] = gradient_result_bytes;
```

## 7. Relacion con Python

La integracion real con Python no necesita ejecutar `master.cpp` ni `worker.cpp`.

Python deberia llamar a una libreria C++ que exponga:

```cpp
API_RDT_UDP::MasterTransport
API_RDT_UDP::WorkerTransport
API_RDT_UDP::Bytes
```

Flujo esperado en Python:

```text
Python master:
    construye objetos WORK_ASSIGNMENT
    los serializa a bytes
    llama a MasterTransport.exchange(...)
    recibe GRADIENT_RESULT en bytes
    deserializa y promedia gradientes

Python worker:
    llama a WorkerTransport.receive_assignment()
    deserializa assignment
    ejecuta backpropagation
    serializa gradientes
    llama a WorkerTransport.send_gradient(...)
```

Los ejecutables `master` y `worker` siguen siendo utiles como pruebas manuales con archivos, pero no son la interfaz final para Python.

