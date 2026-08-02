# Protocol scope

The implementation follows the MySQL 8.0.46 wire-protocol documentation for
the supported connection and command paths.

## Connection phase

```text
server -> HandshakeV10, sequence 0
client -> HandshakeResponse41, sequence 1
server -> OK or ERR, sequence 2
```

The frontend advertises protocol 4.1, secure-connection framing, plugin-auth
metadata, transactions, and optional initial database selection. It rejects an
SSL request because TLS is not implemented. Authentication response bytes are
parsed but are not validated.

## Command phase

Each command starts a new packet sequence. The client command packet uses
sequence 0 and the server response begins with sequence 1.

Supported commands:

- `COM_QUERY` (`0x03`)
- `COM_INIT_DB` (`0x02`)
- `COM_PING` (`0x0e`)
- `COM_QUIT` (`0x01`)

Text query results use:

```text
column count
ColumnDefinition41 x N
EOF
text row x M
EOF
```

The server intentionally does not advertise `CLIENT_DEPRECATE_EOF` or
`CLIENT_OPTIONAL_RESULTSET_METADATA`.

## References

- https://dev.mysql.com/doc/dev/mysql-server/8.0.46/page_protocol_basic_packets.html
- https://dev.mysql.com/doc/dev/mysql-server/8.0.46/page_protocol_connection_phase_packets.html
- https://dev.mysql.com/doc/dev/mysql-server/8.0.46/page_protocol_command_phase.html
- https://dev.mysql.com/doc/dev/mysql-server/8.0.46/page_protocol_com_query_response_text_resultset.html
