CC=gcc
CFLAGS=-O2 -std=c11 -pthread -Iinclude -Wall -Wextra
LDFLAGS=-lcurl -lsqlite3 -lm -pthread
SRC=src/server.c src/db.c src/data_gov.c src/geo.c src/parser.c
BIN=build/server

all: $(BIN)

$(BIN): $(SRC)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $(BIN) $(SRC) $(LDFLAGS)

clean:
	rm -rf build *.o

run: all
	./$(BIN)

# Windows MinGW: mingw32-make  (install libcurl + sqlite3 dev via MSYS2: pacman -S mingw-w64-x86_64-curl mingw-w64-x86_64-sqlite3)
