#---------------------------------------------------------------------------------
# ryu_ldn_nx - Unit Tests Makefile
#---------------------------------------------------------------------------------

# Compiler settings (host, not Switch)
CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -g -O0
CXXFLAGS += -I../sysmodule/source
CXXFLAGS += -DTEST_BUILD

# Coverage flags (optional)
ifdef COVERAGE
CXXFLAGS += --coverage
LDFLAGS += --coverage
endif

#---------------------------------------------------------------------------------
# Source files
#---------------------------------------------------------------------------------
# Test sources
TEST_SOURCES := \
	protocol_tests.cpp \
	config_tests.cpp \
	socket_tests.cpp \
	tcp_client_tests.cpp \
	connection_state_tests.cpp \
	reconnect_tests.cpp \
	client_tests.cpp \
	ldn_types_tests.cpp \
	ldn_state_machine_tests.cpp \
	ldn_proxy_tests.cpp \
	ldn_error_handling_tests.cpp \
	ldn_integration_tests.cpp \
	overlay_tests.cpp

# Implementation sources needed for tests
IMPL_SOURCES := \
	../sysmodule/source/config/config.cpp \
	../sysmodule/source/network/socket.cpp \
	../sysmodule/source/network/tcp_client.cpp \
	../sysmodule/source/network/connection_state.cpp \
	../sysmodule/source/network/reconnect.cpp \
	../sysmodule/source/network/client.cpp

TEST_OBJECTS := $(TEST_SOURCES:.cpp=.o)
IMPL_OBJECTS := $(notdir $(IMPL_SOURCES:.cpp=.o))

# Targets
TARGET_PROTOCOL := run_protocol_tests
TARGET_CONFIG := run_config_tests
TARGET_SOCKET := run_socket_tests
TARGET_TCP_CLIENT := run_tcp_client_tests
TARGET_CONNECTION_STATE := run_connection_state_tests
TARGET_RECONNECT := run_reconnect_tests
TARGET_CLIENT := run_client_tests
TARGET_LDN_TYPES := run_ldn_types_tests
TARGET_LDN_STATE_MACHINE := run_ldn_state_machine_tests
TARGET_LDN_PROXY := run_ldn_proxy_tests
TARGET_LDN_ERROR := run_ldn_error_handling_tests
TARGET_LDN_INTEGRATION := run_ldn_integration_tests
TARGET_OVERLAY := run_overlay_tests
TARGET_ALL := run_all_tests

#---------------------------------------------------------------------------------
# Build rules
#---------------------------------------------------------------------------------
.PHONY: all clean test test-protocol test-config test-socket test-tcp-client test-connection-state test-reconnect test-client test-ldn-types test-ldn-state-machine test-ldn-proxy test-ldn-error test-ldn-integration test-overlay coverage

all: $(TARGET_PROTOCOL) $(TARGET_CONFIG) $(TARGET_SOCKET) $(TARGET_TCP_CLIENT) $(TARGET_CONNECTION_STATE) $(TARGET_RECONNECT) $(TARGET_CLIENT) $(TARGET_LDN_TYPES) $(TARGET_LDN_STATE_MACHINE) $(TARGET_LDN_PROXY) $(TARGET_LDN_ERROR) $(TARGET_LDN_INTEGRATION) $(TARGET_OVERLAY)

# Protocol tests (header-only, no impl needed)
$(TARGET_PROTOCOL): protocol_tests.o
	$(CXX) $(LDFLAGS) -o $@ $^

# Config tests (needs config.cpp impl)
$(TARGET_CONFIG): config_tests.o config.o
	$(CXX) $(LDFLAGS) -o $@ $^

# Socket tests (needs socket.cpp impl)
$(TARGET_SOCKET): socket_tests.o socket.o
	$(CXX) $(LDFLAGS) -o $@ $^

# TCP Client tests (needs socket.cpp and tcp_client.cpp)
$(TARGET_TCP_CLIENT): tcp_client_tests.o tcp_client.o socket.o
	$(CXX) $(LDFLAGS) -o $@ $^

# Connection State tests (needs connection_state.cpp)
$(TARGET_CONNECTION_STATE): connection_state_tests.o connection_state.o
	$(CXX) $(LDFLAGS) -o $@ $^

# Reconnect tests (needs reconnect.cpp)
$(TARGET_RECONNECT): reconnect_tests.o reconnect.o
	$(CXX) $(LDFLAGS) -o $@ $^

# Client tests (needs all network modules)
$(TARGET_CLIENT): client_tests.o client.o tcp_client.o socket.o connection_state.o reconnect.o config.o
	$(CXX) $(LDFLAGS) -o $@ $^

# LDN Types tests (header-only, no impl needed)
$(TARGET_LDN_TYPES): ldn_types_tests.o
	$(CXX) $(LDFLAGS) -o $@ $^

# LDN State Machine tests (standalone test implementation)
$(TARGET_LDN_STATE_MACHINE): ldn_state_machine_tests.o
	$(CXX) $(LDFLAGS) -o $@ $^

# LDN Proxy tests (header-only, no impl needed)
$(TARGET_LDN_PROXY): ldn_proxy_tests.o
	$(CXX) $(LDFLAGS) -o $@ $^

# LDN Error Handling tests (standalone test implementation)
$(TARGET_LDN_ERROR): ldn_error_handling_tests.o
	$(CXX) $(LDFLAGS) -o $@ $^

# LDN Integration tests (standalone end-to-end tests)
$(TARGET_LDN_INTEGRATION): ldn_integration_tests.o
	$(CXX) $(LDFLAGS) -o $@ $^

# Overlay tests (mocked Switch environment)
$(TARGET_OVERLAY): overlay_tests.o
	$(CXX) $(LDFLAGS) -o $@ $^

# Compile test sources
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Compile implementation sources
config.o: ../sysmodule/source/config/config.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

socket.o: ../sysmodule/source/network/socket.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

tcp_client.o: ../sysmodule/source/network/tcp_client.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

connection_state.o: ../sysmodule/source/network/connection_state.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

reconnect.o: ../sysmodule/source/network/reconnect.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

client.o: ../sysmodule/source/network/client.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Run all tests
test: $(TARGET_PROTOCOL) $(TARGET_CONFIG) $(TARGET_SOCKET) $(TARGET_TCP_CLIENT) $(TARGET_CONNECTION_STATE) $(TARGET_RECONNECT) $(TARGET_CLIENT) $(TARGET_LDN_TYPES) $(TARGET_LDN_STATE_MACHINE) $(TARGET_LDN_PROXY) $(TARGET_LDN_ERROR) $(TARGET_LDN_INTEGRATION) $(TARGET_OVERLAY)
	@echo "=== Running Protocol Tests ==="
	./$(TARGET_PROTOCOL)
	@echo ""
	@echo "=== Running Config Tests ==="
	./$(TARGET_CONFIG)
	@echo ""
	@echo "=== Running Socket Tests ==="
	./$(TARGET_SOCKET)
	@echo ""
	@echo "=== Running TcpClient Tests ==="
	./$(TARGET_TCP_CLIENT)
	@echo ""
	@echo "=== Running Connection State Tests ==="
	./$(TARGET_CONNECTION_STATE)
	@echo ""
	@echo "=== Running Reconnect Tests ==="
	./$(TARGET_RECONNECT)
	@echo ""
	@echo "=== Running Client Tests ==="
	./$(TARGET_CLIENT)
	@echo ""
	@echo "=== Running LDN Types Tests ==="
	./$(TARGET_LDN_TYPES)
	@echo ""
	@echo "=== Running LDN State Machine Tests ==="
	./$(TARGET_LDN_STATE_MACHINE)
	@echo ""
	@echo "=== Running LDN Proxy Tests ==="
	./$(TARGET_LDN_PROXY)
	@echo ""
	@echo "=== Running LDN Error Handling Tests ==="
	./$(TARGET_LDN_ERROR)
	@echo ""
	@echo "=== Running LDN Integration Tests ==="
	./$(TARGET_LDN_INTEGRATION)
	@echo ""
	@echo "=== Running Overlay Tests ==="
	./$(TARGET_OVERLAY)

test-protocol: $(TARGET_PROTOCOL)
	./$(TARGET_PROTOCOL)

test-config: $(TARGET_CONFIG)
	./$(TARGET_CONFIG)

test-socket: $(TARGET_SOCKET)
	./$(TARGET_SOCKET)

test-tcp-client: $(TARGET_TCP_CLIENT)
	./$(TARGET_TCP_CLIENT)

test-connection-state: $(TARGET_CONNECTION_STATE)
	./$(TARGET_CONNECTION_STATE)

test-reconnect: $(TARGET_RECONNECT)
	./$(TARGET_RECONNECT)

test-client: $(TARGET_CLIENT)
	./$(TARGET_CLIENT)

test-ldn-types: $(TARGET_LDN_TYPES)
	./$(TARGET_LDN_TYPES)

test-ldn-state-machine: $(TARGET_LDN_STATE_MACHINE)
	./$(TARGET_LDN_STATE_MACHINE)

test-ldn-proxy: $(TARGET_LDN_PROXY)
	./$(TARGET_LDN_PROXY)

test-ldn-error: $(TARGET_LDN_ERROR)
	./$(TARGET_LDN_ERROR)

test-ldn-integration: $(TARGET_LDN_INTEGRATION)
	./$(TARGET_LDN_INTEGRATION)

test-overlay: $(TARGET_OVERLAY)
	./$(TARGET_OVERLAY)

coverage: clean
	$(MAKE) COVERAGE=1 test
	gcov $(TEST_SOURCES)
	@echo "Coverage report generated"

clean:
	rm -f *.o $(TARGET_PROTOCOL) $(TARGET_CONFIG) $(TARGET_SOCKET) $(TARGET_TCP_CLIENT) $(TARGET_CONNECTION_STATE) $(TARGET_RECONNECT) $(TARGET_CLIENT) $(TARGET_LDN_TYPES) $(TARGET_LDN_STATE_MACHINE) $(TARGET_LDN_PROXY) $(TARGET_LDN_ERROR) $(TARGET_LDN_INTEGRATION) $(TARGET_OVERLAY)
	rm -f *.gcno *.gcda *.gcov

#---------------------------------------------------------------------------------
# Dependencies
#---------------------------------------------------------------------------------
protocol_tests.o: protocol_tests.cpp \
	../sysmodule/source/protocol/types.hpp \
	../sysmodule/source/protocol/ryu_protocol.hpp \
	../sysmodule/source/protocol/packet_buffer.hpp

config_tests.o: config_tests.cpp \
	../sysmodule/source/config/config.hpp

config.o: ../sysmodule/source/config/config.cpp \
	../sysmodule/source/config/config.hpp

socket_tests.o: socket_tests.cpp \
	../sysmodule/source/network/socket.hpp

socket.o: ../sysmodule/source/network/socket.cpp \
	../sysmodule/source/network/socket.hpp

tcp_client_tests.o: tcp_client_tests.cpp \
	../sysmodule/source/network/tcp_client.hpp \
	../sysmodule/source/network/socket.hpp \
	../sysmodule/source/protocol/ryu_protocol.hpp \
	../sysmodule/source/protocol/packet_buffer.hpp

tcp_client.o: ../sysmodule/source/network/tcp_client.cpp \
	../sysmodule/source/network/tcp_client.hpp \
	../sysmodule/source/network/socket.hpp \
	../sysmodule/source/protocol/ryu_protocol.hpp \
	../sysmodule/source/protocol/packet_buffer.hpp

connection_state_tests.o: connection_state_tests.cpp \
	../sysmodule/source/network/connection_state.hpp

connection_state.o: ../sysmodule/source/network/connection_state.cpp \
	../sysmodule/source/network/connection_state.hpp

reconnect_tests.o: reconnect_tests.cpp \
	../sysmodule/source/network/reconnect.hpp

reconnect.o: ../sysmodule/source/network/reconnect.cpp \
	../sysmodule/source/network/reconnect.hpp

client_tests.o: client_tests.cpp \
	../sysmodule/source/network/client.hpp \
	../sysmodule/source/network/tcp_client.hpp \
	../sysmodule/source/network/connection_state.hpp \
	../sysmodule/source/network/reconnect.hpp

client.o: ../sysmodule/source/network/client.cpp \
	../sysmodule/source/network/client.hpp \
	../sysmodule/source/network/tcp_client.hpp \
	../sysmodule/source/network/connection_state.hpp \
	../sysmodule/source/network/reconnect.hpp

ldn_types_tests.o: ldn_types_tests.cpp \
	../sysmodule/source/protocol/types.hpp

ldn_state_machine_tests.o: ldn_state_machine_tests.cpp

ldn_proxy_tests.o: ldn_proxy_tests.cpp \
	../sysmodule/source/protocol/types.hpp

ldn_error_handling_tests.o: ldn_error_handling_tests.cpp

ldn_integration_tests.o: ldn_integration_tests.cpp

overlay_tests.o: overlay_tests.cpp
