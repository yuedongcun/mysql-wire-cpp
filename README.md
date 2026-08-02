# mysql-wire-cpp

`mysql-wire-cpp` is an embeddable C++17 MySQL wire-protocol frontend. It lets a
SQL engine accept connections from the standard `mysql` CLI by implementing a
small `SqlExecutor` interface.

The project implements the network and protocol boundary, not a SQL parser or
storage engine.

## Architecture

```text
mysql CLI
  -> TCP / MySQL packets
  -> handshake and session command loop
  -> SqlExecutor
  -> SqlQueryResult
  -> OK / ERR / text resultset packets
```

The protocol library does not depend on BusTub or another database engine.

## Build and test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Linux and other POSIX systems are currently supported. The socket layer uses
`recv`, `send`, `socket`, `bind`, `listen`, and `accept`.

## Run the demo

Start the server:

```bash
./build/mysql-wire-demo --host 127.0.0.1 --port 3307
```

Connect from another terminal with a MySQL 8.x client:

```bash
mysql --protocol=tcp -h127.0.0.1 -P3307 -uroot -Ddemo --ssl-mode=DISABLED
```

The demo executor supports:

```sql
SELECT 1;
SELECT 'hello';
SELECT DATABASE();
SELECT CONNECTION_ID();
```

Authentication fields are parsed, but credentials are not validated. Bind the
demo to `127.0.0.1`; it is not intended to be exposed to an untrusted network.

## Embed in an engine

Implement the execution boundary:

```cpp
class EngineExecutor : public mysql_wire::SqlExecutor {
 public:
  auto Execute(const std::string &sql,
               const mysql_wire::MysqlQueryContext &context)
      -> mysql_wire::SqlQueryResult override;

  auto DatabaseName() const -> std::string_view override;
};
```

Then pass a shared executor to `mysql_wire::MysqlServer`. One executor may be
shared by concurrent sessions, so the implementation must provide the required
thread safety.

## Supported protocol subset

- MySQL protocol v10 handshake
- HandshakeResponse41 parsing and capability negotiation
- `COM_QUERY`, `COM_INIT_DB`, `COM_PING`, and `COM_QUIT`
- OK, ERR, EOF, ColumnDefinition41, and text resultset rows
- connection ID and selected-database session state
- complete reads and writes for normal packets below 16 MiB

## Current limits

- anonymous authentication only; no account or privilege system
- no TLS
- no prepared statements or binary protocol
- no packet fragmentation for payloads at or above 16 MiB
- no query attributes, optional metadata, or deprecated-EOF mode
- one logical database per executor
- one detached worker thread per connection

Protocol field references are documented next to the corresponding encoders and
in [docs/protocol-scope.md](docs/protocol-scope.md).
