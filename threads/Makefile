TARGET = thread-example
SRCS = thread.c mythread.c

CC=gcc
RM=rm
CFLAGS= -g -Wall
INCLUDE_DIR="."

all: ${TARGET}

${TARGET}: ${SRCS}
	${CC} ${CFLAGS} -I${INCLUDE_DIR} ${SRCS} -o ${TARGET}

clean:
	${RM} -f *.o ${TARGET}
