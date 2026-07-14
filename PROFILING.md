<!-- @format -->

# Rime 性能分析指南

## 概述

通过编译 librime 的 profiling 版本并配合 fcitx5 前端，
可以精确测量每个 Rime 组件（处理器、翻译器、过滤器等）的单次调用耗时，
从而定位 C++ 或 Lua 插件的性能瓶颈。

## 1. 编译 profiling 版本

```sh
# 在 librime 仓库根目录
cmake . -Bbuild-profile \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_MERGED_PLUGINS=OFF \
    -DENABLE_EXTERNAL_PLUGINS=ON \
    -DENABLE_PROFILING=ON
cmake --build build-profile -j$(nproc)
```

编译产物位于 `build-profile/lib/librime.so` 及其插件目录。

## 2. 启动 fcitx5 并采集日志

```sh
# 启动 profiling 版本并采集日志到文件
LD_PRELOAD=$HOME/librime/build-profile/lib/librime.so \
    fcitx5 --verbose=rime=5 -r 2>&1 \
    | grep -E '\[PROFILE\]|receive key' \
    | tee /tmp/rime-profile.log
```

> **注意**: 此命令会占用终端，快捷键 `Ctrl+C` 停止。

| 参数                                 | 说明                                           |
| ------------------------------------ | ---------------------------------------------- |
| `LD_PRELOAD`                         | 覆盖系统 librime.so，使用 profiling 版本。     |
| `-r`                                 | 替换当前的 fcitx5 实例。                       |
| `--verbose=rime=5`                   | 启用 fcitx5 的 Rime 调试日志（输出按键信息）。 |
| `grep -E '\[PROFILE\]\|receive key'` | 只保留 profiling 数据和按键日志。              |

## 3. 操作流程

1. 运行启动命令，终端开始输出日志
2. 切换到目标应用（Kate、浏览器等）
3. 正常打字（建议录一段完整对话或文章截图）
4. 打字完毕后 `Ctrl+C` 停止采集
5. 日志文件保存在 `/tmp/rime-profile.log`

## 4. 生成分析报告

```sh
# 生成 Markdown 报告
python3 profile_report.py /tmp/rime-profile.log > report.md

# 同时导出 CSV 数据（可用 Excel / Python 进一步分析）
python3 profile_report.py --csv data.csv /tmp/rime-profile.log
```

报告包含：

- 按键延迟分布 (P50 / P95 / P99)
- 每个组件的 count / avg / max / total
- 翻译器按 segment 和输入长度分组
- Lua vs C++ 占比
- 尖峰自动检测（超过阈值的组件）
- 最慢按键 Top 10（含具体按键名）

## 5. 环境变量参考

| 变量               | 说明                                                     |
| ------------------ | -------------------------------------------------------- |
| `RIME_PROFILE_LOG` | 指定 profiling 日志输出文件路径。不设则仅输出到 stderr。 |
| `LD_PRELOAD`       | 指定预加载的 librime.so 路径。                           |

## 6. 注意事项

- profiling 版本仅在编译时启用了 `ENABLE_PROFILING=ON` 才有效。
  非 profiling 版本中所有 `RIME_PROFILE_SCOPE` 宏被编译器优化为空操作，无任何性能开销。
- `fcitx5 --verbose=rime=5` 会将所有 Rime 日志（包括按键）输出到 stderr，
  `grep` 过滤后只保留 profiling 数据和按键行。
- 日志文件可能较大（61k 行的典型会话约 3MB），建议采集 30s-2min 的数据即可。
- 按键信息来自 `rimeengine.cpp:447] Rime receive key:` 行，
  需要 `--verbose=rime=5` 才会输出。
