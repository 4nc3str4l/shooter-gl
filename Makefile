CXX      := g++
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra -Wno-unused-parameter
LDFLAGS_CLIENT := -lglfw -lGLEW -lGL -lm -lpthread
LDFLAGS_SERVER := -lm -lpthread

COMMON_SRC := network.cpp game.cpp

all: fps_server fps_client

fps_server: server_main.cpp $(COMMON_SRC) common.h game.h network.h
	$(CXX) $(CXXFLAGS) -DFPS_SERVER server_main.cpp $(COMMON_SRC) -o $@ $(LDFLAGS_SERVER)

fps_client: client_main.cpp renderer.cpp $(COMMON_SRC) common.h game.h network.h renderer.h
	$(CXX) $(CXXFLAGS) -DFPS_CLIENT client_main.cpp renderer.cpp $(COMMON_SRC) -o $@ $(LDFLAGS_CLIENT)

clean:
	rm -f fps_server fps_client

.PHONY: all clean
