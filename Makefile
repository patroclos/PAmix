CC=g++
CFLAGS=-Wall -O2 -lncurses -lpulse -std=c++11 -Iinclude -g

SRCFOLDER=src
INCFOLDER=include
OBJFOLDER=build
BINFOLDER=bin

SOURCES := $(shell find $(SRCFOLDER) -name '*.cpp')
OBJECTS := $(patsubst $(SRCFOLDER)/%.cpp,$(OBJFOLDER)/%.o,$(SOURCES))

NAME=pamix
BINARY=$(BINFOLDER)/$(NAME)

.PHONY: all clean
all: $(BINARY)

$(BINARY): $(OBJECTS)
	@if [ ! -d $(dir $@) ]; then mkdir -p $(dir $@) ; fi
	$(CC) $(CFLAGS) $(OBJECTS) -o $(BINARY)
	
$(OBJFOLDER)/%.o: $(SRCFOLDER)/%.cpp $(wildcard $(INCFOLDER)/%.h $(INCFOLDER)/%.hpp)
	@if [ ! -d $(dir $@) ]; then mkdir -p $(dir $@) ; fi
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	@-rm -rf $(BINARY) $(OBJECTS)
