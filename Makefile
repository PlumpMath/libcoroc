
APPS := app.run findmax.run prio.run

OS := $(shell uname)

TSC_HEADERS:= clock.h channel.h context.h lock.h queue.h support.h thread.h vpu.h 
TSC_CFILES := boot.c channel.c clock.c context.c thread.c vpu.c 

ifeq (${OS}, Darwin)
TSC_HEADERS += pthread_barrier.h amd64-ucontext.h 386-ucontext.h power-ucontext.h
TSC_CFILES += pthread_barrier.c ucontext.c
TSC_OBJS := asm.o
endif

TSC_OBJS += $(subst .c,.o,$(TSC_CFILES))

CFLAGS += -DENABLE_QUICK_RESPONSE
CFLAGS += -DENABLE_WORKSTEALING

ifeq (${enable_timer}, 1)
	CFLAGS += -DENABLE_TIMER
endif

ifeq (${enable_optimize}, 1)
	CFLAGS += -O2
else
	CFLAGS += -g3
endif

all: ${APPS}

libTSC.a: $(TSC_OBJS)
	ar r $@ $(TSC_OBJS)

%.o:%.S
	gcc -c ${CFLAGS} $<

%.o:%.c
	gcc -c ${CFLAGS} $<

%.run:%.o libTSC.a
	gcc $< ${CFLAGS} -L. -lTSC -lpthread -o $@

.PHONY:clean
clean:
	rm -f *.o libTSC.a ${APPS}
