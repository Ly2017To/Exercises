#!/bin/sh

# 检查参数数量是否为 2 个（进程名 + 输出文件）
if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <process_name> <output_file>"
    exit 1
fi

# 正确赋值：去掉反斜杠，获取实际的参数值
PROCESS_NAME="$1"
OUTPUT_FILE="$2"

# 打印调试信息：确认要监控的进程名
echo "Attempting to find process: $PROCESS_NAME"

# 获取进程的 PID（-x 精确匹配进程名，-o 只输出 PID）
PID=$(pgrep -x -o "$PROCESS_NAME")

# 打印 PID 调试信息
echo "Found PID: $PID"

# 检查 PID 是否为空（进程不存在）
if [ -z "$PID" ]; then
    echo "Error: Process '$PROCESS_NAME' not found."
    exit 1
fi

# 创建/清空输出文件，并写入监控开始信息
echo "=== Thread Monitor Started at $(date +"%Y-%m-%d %H:%M:%S") ===" > "$OUTPUT_FILE"
echo "Monitoring threads for PID $PID (Process: $PROCESS_NAME)..." >> "$OUTPUT_FILE"
echo "" >> "$OUTPUT_FILE"

# 无限循环监控线程（直到进程退出）
while true; do
    # 检查进程是否仍存活（通过 /proc/$PID 目录是否存在判断）
    if [ ! -d "/proc/$PID" ]; then
        echo "Process $PID ($PROCESS_NAME) has exited. Stopping monitor." >> "$OUTPUT_FILE"
        echo "Monitor stopped at $(date +"%Y-%m-%d %H:%M:%S")" >> "$OUTPUT_FILE"
        exit 0
    fi

    # 获取当前时间戳
    TIMESTAMP=$(date +"%Y-%m-%d %H:%M:%S")

    # 安全获取线程列表（2>/dev/null 抑制目录不存在的报错）
    THREADS=$(ls /proc/$PID/task/ 2>/dev/null)

    # 写入当前时间的线程信息到输出文件
    echo "[$TIMESTAMP] Active threads (TID):" >> "$OUTPUT_FILE"
    if [ -z "$THREADS" ]; then
        echo "  No active threads found (process may be exiting)..." >> "$OUTPUT_FILE"
    else
        for TID in $THREADS; do
            echo "  Thread $TID is active." >> "$OUTPUT_FILE"
        done
        # 汇总当前线程 ID
        echo "  All TIDs: $THREADS" >> "$OUTPUT_FILE"
    fi

    # 空行分隔，提升日志可读性
    echo "" >> "$OUTPUT_FILE"

    # 每 60 秒监控一次
    sleep 60
done