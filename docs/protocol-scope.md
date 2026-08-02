# 协议支持范围

当前实现以 MySQL 8.0.46 Wire Protocol 开发文档为参考，只实现标准 CLI 完成连接、
基础命令和文本结果集所需的子集。本页说明实现选择，而不是完整 MySQL 协议规范。

## Packet framing

普通 MySQL packet 由 4 字节 header 和 payload 组成：

```text
int<3> payload_length  小端序，最大值 0xffffff
int<1> sequence_id
string payload
```

`PacketReader` 和 `PacketWriter` 会处理短 `recv` / `send`，但当前拒绝大于等于 16 MiB
的逻辑 payload，没有实现多个 `0xffffff` packet 的分片与重组。

参考：

- https://dev.mysql.com/doc/dev/mysql-server/8.0.46/page_protocol_basic_packets.html

## Connection Phase

```text
server -> HandshakeV10, sequence 0
client -> HandshakeResponse41, sequence 1
server -> OK 或 ERR, sequence 2
```

服务端声明 protocol 4.1、secure-connection framing、plugin-auth metadata、transaction
标记，以及连接时选择 database 的能力。握手响应会解析 capability、用户名、认证响应、
初始 database 和 auth plugin 名称。

当前以 `mysql_native_password` 作为握手 plugin。该 plugin 使用 20 字节 challenge；
packet 中发送 20 字节 scramble 和一个尾部 `0x00`，所以 `auth_plugin_data_len` 为 21。
尾部零用于把 plugin 输入表示为 NUL-terminated message，不属于 20 字节随机 challenge。

当前不会验证认证响应。由于没有 TLS，实现不声明 `CLIENT_SSL`，收到 SSLRequest 时也会
返回错误并断开连接。

参考：

- https://dev.mysql.com/doc/dev/mysql-server/8.0.46/page_protocol_connection_phase_packets.html
- https://dev.mysql.com/doc/dev/mysql-server/8.0.46/page_protocol_connection_phase_packets_protocol_handshake_v10.html
- https://dev.mysql.com/doc/dev/mysql-server/8.0.46/page_protocol_connection_phase_packets_protocol_handshake_response.html
- https://dev.mysql.com/doc/dev/mysql-server/8.0.46/page_protocol_connection_phase_authentication_methods.html

## Command Phase

每条 command 开始新的 packet sequence。客户端 command packet 使用 sequence 0，服务端
响应从 sequence 1 开始。

| Command | 值 | 当前行为 |
| --- | ---: | --- |
| `COM_QUIT` | `0x01` | 结束当前 session |
| `COM_INIT_DB` | `0x02` | 更新连接内选中的逻辑 database |
| `COM_QUERY` | `0x03` | 处理兼容查询或调用 `SqlExecutor` |
| `COM_PING` | `0x0e` | 返回 OK |

没有声明 `CLIENT_QUERY_ATTRIBUTES`，因此 `COM_QUERY` payload 在 command byte 之后直接
按 `string<EOF>` 解释为 SQL 文本。

参考：

- https://dev.mysql.com/doc/dev/mysql-server/8.0.46/page_protocol_command_phase.html
- https://dev.mysql.com/doc/dev/mysql-server/8.0.46/page_protocol_com_query.html
- https://dev.mysql.com/doc/dev/mysql-server/8.0.46/page_protocol_com_init_db.html
- https://dev.mysql.com/doc/dev/mysql-server/8.0.46/page_protocol_com_quit.html
- https://dev.mysql.com/doc/dev/mysql-server/8.0.46/page_protocol_com_ping.html

## Text Resultset

查询成功并返回行时，使用以下 packet 序列：

```text
column count
ColumnDefinition41 x N
EOF
Text Resultset Row x M
EOF
```

实现没有声明 `CLIENT_DEPRECATE_EOF` 或 `CLIENT_OPTIONAL_RESULTSET_METADATA`，因此列定义
不会省略，metadata 和 row data 后都使用 EOF packet 终止。SQL `NULL` 编码为 `0xfb`，
非 NULL cell 编码为 length-encoded string。

当前中间结果支持的列类型为：

- `LONG` (`0x03`)；
- `LONGLONG` (`0x08`)；
- `VAR_STRING` (`0xfd`)。

参考：

- https://dev.mysql.com/doc/dev/mysql-server/8.0.46/page_protocol_com_query_response_text_resultset.html
- https://dev.mysql.com/doc/dev/mysql-server/8.0.46/page_protocol_com_query_response_text_resultset_column_definition.html
- https://dev.mysql.com/doc/dev/mysql-server/8.0.46/page_protocol_com_query_response_text_resultset_row.html

## 前端兼容查询

`ExecuteQuery` 会在委派给后端前处理少量 CLI 探测语句：

- `SELECT DATABASE()`；
- `SELECT CONNECTION_ID()`；
- `SELECT @@version`；
- `SELECT @@version_comment`；
- `SHOW DATABASES`；
- `SET ...`。

这些逻辑只用于让 MySQL CLI 完成基础会话交互，不代表实现了对应的 MySQL server
变量、warning 系统或 session 配置语义。
