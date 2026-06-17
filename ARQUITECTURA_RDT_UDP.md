# Arquitectura RDT UDP

El sistema transporta bytes opacos entre un master y diez workers. La capa RDT
no interpreta el contenido de los payloads.

```text
Master -> Worker: WORKER_PAYLOAD
Worker -> Master: MASTER_PAYLOAD
```

## Capas

```text
protocolo/API_RDT_UDP.hpp / protocolo/API_RDT_UDP.cpp
    API publica y sesiones master-worker.

protocolo/protocol.hpp / protocolo/protocol.cpp
    Envio y recepcion confiable de objetos sobre UDP.

protocolo/datagram.hpp
    Formato binario, serializacion y CRC32.
```

## Datagramas

Cada datagrama tiene un header de `28 bytes` y hasta `472 bytes` de payload,
para un maximo total de `500 bytes`. Los datagramas tienen longitud variable:
los mensajes sin payload ocupan solo los `28 bytes` del header.

Tipos de datagrama:

- `START`: anuncia una transferencia.
- `DATA`: transporta un fragmento.
- `ACK`: confirma acumulativamente los fragmentos recibidos.
- `END`: finaliza la transferencia.

Tipos de objeto:

- `WorkerPayload`: payload enviado hacia un worker.
- `MasterPayload`: payload enviado hacia el master.
- `None`: usado por ACK.

`serialize_datagram(...)` convierte un `Datagram` a bytes y agrega CRC32.
`parse_datagram(...)` valida formato, longitud y CRC32 antes de aceptar los
datos.

## RDT

`protocol.cpp` implementa Go-Back-N:

1. El emisor envia `START` y espera `ACK_NONE`.
2. Envia hasta `8` fragmentos por ventana.
3. El receptor acepta solamente el siguiente fragmento esperado.
4. Cada ACK confirma todos los fragmentos hasta su numero.
5. Tras `500 ms` sin ACK, el emisor retransmite desde `base`.
6. Despues de `5` reintentos fallidos, el envio falla.
7. El emisor envia `END` y espera el ACK final.

IDs:

```text
Master:  0
Workers: 1..10

WORKER_PAYLOAD transfer ID = 1000 + worker ID
MASTER_PAYLOAD transfer ID = 2000 + worker ID
```

## Master

`MasterTransport::exchange(worker_payloads)` crea diez threads. Cada thread:

1. Crea su propio socket UDP.
2. Envia el `WORKER_PAYLOAD` de un solo worker.
3. Procesa sus ACK y retransmisiones.
4. Recibe y confirma el `MASTER_PAYLOAD` del mismo worker.
5. Guarda el payload recibido o el error de la sesion.

Los threads no comparten sockets ni buffers. Al finalizar, `exchange()` devuelve
un mapa de `MASTER_PAYLOAD` por worker. Si alguna sesion falla, espera las demas
y reporta todos los errores juntos.

Despues de enviar completamente el `WORKER_PAYLOAD`, la sesion espera hasta
`60 segundos` para recibir el `START` del `MASTER_PAYLOAD`. Una vez recibido,
espera hasta reconstruirlo completamente.

## Worker

`WorkerTransport(worker_id)` abre el puerto `9000 + worker_id`.

```cpp
Bytes worker_payload = worker.receive_worker_payload();
worker.send_master_payload(master_payload);
```

El worker recuerda la direccion del master al recibir el primer payload. Por
eso debe recibir el `WORKER_PAYLOAD` antes de enviar el `MASTER_PAYLOAD`.

## API

```cpp
using Bytes = std::vector<uint8_t>;

std::map<uint16_t, Bytes> MasterTransport::exchange(
    const std::map<uint16_t, Bytes>& worker_payloads
);

Bytes WorkerTransport::receive_worker_payload();
void WorkerTransport::send_master_payload(const Bytes& master_payload);
```

La API trabaja exclusivamente con bytes. La capa superior decide como crear e
interpretar cada payload.
