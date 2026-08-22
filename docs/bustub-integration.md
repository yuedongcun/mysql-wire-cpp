# BusTub 接入指南

本项目从 BusTub 的 MySQL 前端中拆分而来。拆分后的目标是让 BusTub 依赖一个通用协议库，
而不是让协议库包含任何 BusTub 类型或头文件。

## 接入边界

```text
mysql CLI
  -> mysql_wire::MysqlServer
  -> mysql_wire::MysqlSession
  -> mysql_wire::SqlExecutor
  -> BusTubSqlExecutor
  -> BusTubInstance::ExecuteSql
  -> BusTub ResultWriter callback
  -> mysql_wire::SqlQueryResult
  -> MySQL text resultset
```

`mysql-wire-cpp` 负责 TCP、packet、握手、session 状态和结果编码。BusTub 仓库只保留
`BusTubSqlExecutor`，负责调用内核并转换结果。这个方向保证依赖单向：

```text
BusTub -> mysql-wire-cpp
mysql-wire-cpp -X-> BusTub
```

## 1. 作为 submodule 引入

GitHub 仓库创建后，在 BusTub 根目录执行：

```bash
git submodule add <mysql-wire-cpp-repository-url> third_party/mysql-wire-cpp
git submodule update --init --recursive
```

在 BusTub 的 `third_party/CMakeLists.txt` 中关闭独立示例和测试，再加入子目录：

```cmake
set(MYSQL_WIRE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(MYSQL_WIRE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(mysql-wire-cpp)
```

协议库向父项目提供稳定 target：

```text
mysql-wire-cpp::mysql_wire
```

## 2. 实现 BusTub adapter

适配器继承公开接口，并持有 BusTub 实例的引用：

```cpp
#include "mysql_wire/mysql_wire.h"

class BusTubSqlExecutor final : public mysql_wire::SqlExecutor {
 public:
  explicit BusTubSqlExecutor(BusTubInstance &bustub) : bustub_(bustub) {}

  auto Execute(const std::string &sql,
               const mysql_wire::MysqlQueryContext &context)
      -> mysql_wire::SqlQueryResult override;

  auto DatabaseName() const -> std::string_view override;

 private:
  BusTubInstance &bustub_;
};
```

`Execute` 的职责只有三项：

1. 调用 `BusTubInstance::ExecuteSql(sql, writer)`；
2. 通过一个 `ResultWriter` 实现捕获列名、每行 cell 和受影响行数；
3. 转换为 `SqlQueryResult::Ok`、`Rows` 或 `Error`。

BusTub 当前的 `ResultWriter` 接口只暴露字符串形式的 header 和 cell，不传递 `Schema`、
`TypeId` 或独立的 NULL 标记。因此当前 adapter 将结果列统一声明为 `VAR_STRING`，并按
文本发送 cell；`SqlQueryResult` 本身支持 `std::nullopt`，但这个 adapter 暂时无法从现有
回调中可靠恢复 SQL NULL。若后续给 BusTub 增加带类型的结果输出接口，类型映射和 NULL
保真仍应在 adapter 中完成，协议库不需要依赖 BusTub 的 `Value` 或 `TypeId`。

## 3. 链接 adapter

```cmake
add_library(bustub_mysql_adapter STATIC bustub_sql_executor.cpp)
target_link_libraries(
  bustub_mysql_adapter
  PUBLIC mysql-wire-cpp::mysql_wire bustub)
```

server 入口只负责组装对象：

```cpp
auto bustub = std::make_unique<bustub::BusTubInstance>(db_file, bpm_size);
auto executor =
    std::make_shared<bustub::mysql::BusTubSqlExecutor>(*bustub);
mysql_wire::MysqlServer server(host, port, std::move(executor));
return server.ServeForever();
```

对象声明顺序很重要：`BusTubInstance` 必须比持有其引用的 executor 和 server 活得更久。

## 4. 测试分层

协议库测试不启动 BusTub：

- packet 编解码与 length-encoded value；
- 基于 `socketpair` 和 Fake Executor 的握手、查询、结果集及退出链路。

BusTub 仓库只增加 adapter 集成测试：

- 创建内存 `BusTubInstance`；
- 通过 `BusTubSqlExecutor` 执行建表、插入和查询；
- 校验 `affected_rows`、列信息和行数据已转换为 `SqlQueryResult`。

最终再使用真实 MySQL 8.x CLI 验证：

```text
TCP -> handshake -> COM_QUERY -> BusTub ExecuteSql -> text resultset
```

这样可以分别定位协议错误、适配错误和数据库执行错误，且公开协议仓库不需要包含 BusTub
课程实现。
