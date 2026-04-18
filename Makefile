CC=g++
CFLAGS=-g -Iinclude
OBJDIR = obj
#DEPS = HTTP_Server.h
TARGET = server
SOURCES = $(wildcard src/*.cc)
OBJECTS = $(patsubst src/%.cc, $(OBJDIR)/%.o, $(SOURCES))
flags = -g -Wall -lm -ldl -fPIC -rdynamic -I./include
# flags = -I./include

#$(exec): $(objects)
#	$(CC) $(objects) $(flags) -o $(exec)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) $(CFLAGS) -o $@

$(OBJDIR)/%.o: src/%.cc | $(OBJDIR)
	$(CC) -c $(CFLAGS) $< -o $@

$(OBJDIR):
	mkdir -p $@

clean:
	-rm -rf obj/

.PHONY: clean
