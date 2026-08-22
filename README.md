# mysql-wire-cpp

`mysql-wire-cpp` 是一个可嵌入的 C++17 MySQL Wire Protocol 前端。SQL 引擎只需实现
一个小型 `SqlExecutor` 接口，就可以接受标准 `mysql` CLI 的 TCP 连接，并返回 OK、
ERR 或文本结果集。

这个项目负责网络与协议边界，不包含 SQL parser、optimizer、execution engine 或 storage
engine。它最初从 BusTub 的 MySQL 接入代码中拆出，目前协议核心不依赖 BusTub。

## 数据链路

```text
mysql CLI
  -> TCP / MySQL packet
  -> HandshakeV10 和 session command loop
  -> SqlExecutor::Execute(sql, context)
  -> SqlQueryResult
  -> OK / ERR / text resultset packet
```

协议层与数据库内核通过两个类型解耦：

- `SqlExecutor`：后端实现的 SQL 执行接口；
- `SqlQueryResult`：协议编码器消费的中间结果，包括列信息、行数据、影响行数和错误信息。

因此，packet 编解码、连接状态和 MySQL 兼容查询可以独立测试，数据库项目只保留一层
结果转换适配器。

## 已实现范围

- MySQL protocol v10 握手；
- HandshakeResponse41 解析与 capability 协商；
- `COM_QUERY`、`COM_INIT_DB`、`COM_PING`、`COM_QUIT`；
- OK、ERR、EOF、ColumnDefinition41 和 text resultset row；
- connection ID 与当前 database 的 session 状态；
- 小于 16 MiB 的普通 packet 完整收发；
- 可注入 SQL executor 和不依赖数据库内核的 socket session 测试。

协议字段以 MySQL 8.0.46 开发文档为参考，具体路径和 capability 选择见
[协议支持范围](docs/protocol-scope.md)。

## 构建与测试

要求：

- CMake 3.16 或更高版本；
- 支持 C++17 的编译器；
- POSIX socket 环境。目前在 Linux 上验证。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

可通过选项关闭示例或测试：

```bash
cmake -S . -B build \
  -DMYSQL_WIRE_BUILD_EXAMPLES=OFF \
  -DMYSQL_WIRE_BUILD_TESTS=OFF
```

## 运行示例

启动内置 demo executor：

```bash
./build/mysql-wire-demo --host 127.0.0.1 --port 3307
```

使用 MySQL 8.x 客户端连接：

```bash
mysql --protocol=tcp -h127.0.0.1 -P3307 -uroot -Ddemo --ssl-mode=DISABLED
```

示例后端支持：

```sql
SELECT 1;
SELECT 'hello';
SELECT DATABASE();
SELECT CONNECTION_ID();
```

其中 `DATABASE()` 与 `CONNECTION_ID()` 由协议前端处理，其余 SQL 委派给 demo executor。

## 嵌入 SQL 引擎

实现执行边界：

```cpp
#include "mysql_wire/mysql_wire.h"

class EngineExecutor final : public mysql_wire::SqlExecutor {
 public:
  auto Execute(const std::string &sql,
               const mysql_wire::MysqlQueryContext &context)
      -> mysql_wire::SqlQueryResult override;

  auto DatabaseName() const -> std::string_view override;
};
```

然后组装 executor 与 server：

```cpp
auto executor = std::make_shared<EngineExecutor>(/* engine */);
mysql_wire::MysqlServer server("127.0.0.1", 3307, std::move(executor));
return server.ServeForever();
```

同一个 executor 会被多个连接共享，后端必须自行保证所需的并发安全。通过 CMake
`add_subdirectory` 引入时，链接公开 target：

```cmake
add_subdirectory(third_party/mysql-wire-cpp)
target_link_libraries(your_server PRIVATE mysql-wire-cpp::mysql_wire)
```

BusTub 的完整依赖边界、结果转换和测试方式见
[BusTub 接入指南](docs/bustub-integration.md)。

## 当前限制

- 会解析用户名和认证响应，但不校验凭证，也没有用户与权限系统；
- 不支持 TLS，收到 SSLRequest 会拒绝连接；
- 不支持 prepared statement 和 binary protocol；
- 不支持大于等于 16 MiB payload 的 packet 分片；
- 不支持 query attributes、optional metadata 和 deprecated EOF；
- 每个 executor 只暴露一个逻辑 database；
- 当前网络模型为每连接一个 detached 工作线程，不提供优雅停机接口。

该实现用于协议学习、原型验证和受控环境中的数据库前端接入，不应直接暴露到不可信
网络。安全边界见 [SECURITY.md](SECURITY.md)。

## 目录结构

```text
include/mysql_wire/mysql_wire.h  唯一的公开 API
src/internal/                     内部协议组件声明
src/                              packet、session、server 和结果编码实现
tests/               packet 与 socket session 测试
examples/            可直接连接的 demo server
docs/                协议范围与后端接入文档
```

## 许可证

项目采用 MIT License。由 BusTub 代码演化而来的文件保留 Carnegie Mellon University
Database Group 的版权声明，新增代码同时标注 `mysql-wire-cpp contributors`。
