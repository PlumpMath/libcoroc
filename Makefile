all: app

OS := $(shell uname)

TSC_HEADERS:= clock.h channel.h context.h lock.h queue.h support.h thread.h vpu.h 
TSC_CFILES := boot.c channel.c clock.c context.c thread.c vpu.c 

ifeq (${OS}, Darwin)
TSC_HEADERS += pthread_barrier.h amd64-ucontext.h 386-ucontext.h power-ucontext.h
TSC_CFILES += pthread_barrier.c ucontext.c
TSC_OBJS := asm.o
endif

TSC_OBJS += $(subst .c,.o,$(TSC_CFILES))

CFLAGS := -g3
ifeq (${enable_timer}, 1)
	CFLAGS += -DENABLE_TIMER
endif

%.o:%.S
	gcc -c -g3 $<

%.o:%.c
	gcc -c ${CFLAGS} $<

app: app.o libTSC.a
	gcc app.o ${CFLAGS} -L. -lTSC -lpthread -o $@

libTSC.a: $(TSC_OBJS)
	ar r $@ $(TSC_OBJS)

.PHONY:clean
clean:
	rm -f *.o libTSC.a app 
