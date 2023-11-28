# 指定编译器
CC = g++
# 源文件目录
SRC_DIR = src
# 头文件目录
INC_DIR = inc
# 目标文件目录
OBJ_DIR = obj
# 可执行文件目录
BIN_DIR = bin
# 指定编译选项
CFLAGS = -Iinc $(INC_DIR)

# 所有的源文件
SRCS = $(wildcard $(SRC_DIR)/*.cpp)
# 所有的目标文件
OBJS = $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(SRCS))
# 可执行文件名称
TARGET = $(BIN_DIR)/myprogram

# 默认目标
all: $(TARGET)

# 生成可执行程序
$(TARGET): $(OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $^ -o $@

# 生成目标文件
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# 清理目标文件和可执行文件
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

# 伪目标，防止与文件名冲突
.PHONY: all clean