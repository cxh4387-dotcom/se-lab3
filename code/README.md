# 外卖点餐与配送系统 —— 实现（实验三）

对应实验二的 UML 设计，用 C++17 实现的核心业务原型。项目为无图形界面的
命令行程序，业务逻辑与输入输出分离，便于后续实验做白盒分析与测试。

## 1. 目录结构

```
code/
├── CMakeLists.txt          构建脚本
├── README.md               本文件
├── include/delivery/       公共头文件
│   ├── common.h            金额、错误码、时钟、ID 生成
│   ├── domain_types.h      实体与枚举（对应类图）
│   ├── domain_rules.h      状态机、权限矩阵、校验与口令哈希
│   ├── repositories.h      仓储接口（依赖倒置）
│   ├── memory_repository.h 内存仓储实现
│   └── services.h          应用服务声明
├── src/                    实现文件，按模块拆分
├── apps/main.cpp           命令行入口（演示脚本 + 交互菜单）
└── tests/                  GoogleTest 单元测试
```

## 2. 构建

依赖：CMake ≥ 3.16、支持 C++17 的编译器（GCC 8+ / Clang 7+ / MSVC 2019+）。

```bash
cmake -S . -B build
cmake --build build
```

生成 `build/delivery_cli`（Windows 下为 `delivery_cli.exe`）。

单元测试需要一个 GoogleTest，默认不编译；打开开关后 CMake 会用
`FetchContent` 拉取 v1.14.0：

```bash
cmake -S . -B build -DDELIVERY_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Windows 下如果项目路径包含中文，`MinGW Makefiles` 生成器可能报
"No rule to make target"，改用 `-G Ninja`，或把项目放到纯英文路径。

如果构建机不能联网，可先把 googletest 源码放到本地，再指定：

```bash
cmake -S . -B build -DDELIVERY_BUILD_TESTS=ON \
  -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=/path/to/googletest-1.14.0
```

## 3. 运行

```bash
./build/delivery_cli --demo          # 确定性演示：完整走一遍下单到完成、超时取消、退款重试
./build/delivery_cli --interactive   # 交互菜单
```

`--demo` 使用可控时钟，输出可复现，适合作为实验报告中的运行结果截图来源。

## 4. 代码风格

遵循 Google C++ Style，用 cpplint 检查，当前 0 个问题：

```bash
cpplint --recursive --root=include include
cpplint --recursive --root=. src apps tests
```

## 5. 分层与设计要点

- **interfaces → application → domain ← infrastructure**：`domain` 只声明仓储
  接口，`infrastructure` 给出内存实现，`apps` 只负责输入输出，符合实验二包图。
- **订单状态机集中实现**（`domain::OrderStateMachine`）：9 个状态、12 条合法
  转换，非法转换抛 `DomainError`，对应类图中 `Order::transitTo()` 的约束。
- **幂等提交**：`SubmitOrderRequest` 携带幂等键，重复提交返回同一订单；
  服务内用互斥量保证“查幂等键 + 建单”这一步的原子性。
- **库存条件扣减**：`StockRepository::TryDeduct` 在仓储层加锁完成“判断 + 扣减”，
  100 线程并发下单只卖出 10 份，单元测试覆盖。
- **退款一致性**：离线退款失败时退款单置“退款异常”，订单保持“已取消”，
  重试成功后订单才进入“已退款”。
- **审计日志**：所有关键动作写 `OperationLog`，管理员可查全量，其他角色
  只能看与自身相关的记录。

## 6. 与实验二设计的差异

- 设计报告把 `拒单 / 取消 / 配送失败 → 已退款` 合并计为 1 条规则，因此写的是
  “9 条白名单”；实现时把三条边分开，并显式建模 `配送失败 → 重新派单 → 配送中`，
  合法边共 12 条。测试 `OrderStateMachineTest` 按 12 条断言，并验证其余
  60 种组合全部被拒绝。
- 实验二设计使用 MySQL 与 Redis；本次实现为便于在课程环境运行，使用内存仓储
  （`MemoryDatabase`），仓储接口保持不变，替换为数据库实现不影响上层。

## 7. 规模

| 目录 | 文件数 | 有效代码行（去空行与注释） |
|---|---|---|
| include/ | 6 | 837 |
| src/ | 12 | 2222 |
| apps/ | 1 | 369 |
| **合计（不含测试）** | **19** | **3428** |
| tests/ | 6 | 609 |

## 8. 已知限制

- 数据仅保存在进程内存，退出即丢失；持久化留作后续 TODO。
- 短信验证码、地图距离计算、对象存储上传均以桩替代。
- 未实现图形界面（任务书不强制要求）。
