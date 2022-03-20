# define variable
SRC := $(wildcard ./*.cpp)
OBJS := $(patsubst %.cpp, %.o, $(SRC))

TARGET := corn_server
LIBS   := -lpthread

$(TARGET):$(OBJS)
	$(CXX) $^ -o $@ $(LIBS)

.PHONY:clean clean_all
clean:
	rm $(OBJS) -f

clean_all:
	find . -name '*.o' | xargs rm -r
	find . -name $(TARGET) |  xargs rm -r