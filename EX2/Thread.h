#ifndef _THREAD_H_
#define _THREAD_H_

#include <setjmp.h>
#include <deque>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include <stdio.h>
#include "uthreads.h"

#ifdef __x86_64__
/* code for 64 bit Intel arch */

typedef unsigned long address_t;
#define JB_SP 6
#define JB_PC 7

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%fs:0x30,%0\n"
                 "rol    $0x11,%0\n"
            : "=g" (ret)
            : "0" (addr));
    return ret;
}

#else
/* code for 32 bit Intel arch */

typedef unsigned int address_t;
#define JB_SP 4
#define JB_PC 5


/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%gs:0x18,%0\n"
                 "rol    $0x9,%0\n"
    : "=g" (ret)
    : "0" (addr));
    return ret;
}
#endif

typedef void (*thread_entry_point)(void);

typedef enum State {
    READY,
    RUNNING,
    BLOCKED,
    SLEEPING,
    SLEEPING_AND_BLOCKED,
} State;

class Thread {
private:
    const int tid;
    int quantums;
    State state;
    char *t_stack;
    sigjmp_buf env;

public:
    static int id[MAX_THREAD_NUM];

    Thread() : tid(0), quantums(0), state(RUNNING), t_stack(nullptr) {
        sigsetjmp(env, 1);
        sigemptyset(&env->__saved_mask);
    }

    Thread(const int tid, thread_entry_point entry) :
            tid(tid), quantums(0), state(READY) {
        this->t_stack = new char[STACK_SIZE];
        address_t sp = (address_t) t_stack + STACK_SIZE - sizeof(address_t);
        address_t pc = (address_t) entry;
        sigsetjmp(env, 1);
        (env->__jmpbuf)[JB_SP] = translate_address(sp);
        (env->__jmpbuf)[JB_PC] = translate_address(pc);
        sigemptyset(&env->__saved_mask);
    }

    void set_quantums(int quantums) {
        this->quantums = quantums;
    }

    void reset_env() {

    }

    void incrament_quantums() {
        quantums++;
    }

    int get_quantums() {
        return quantums;
    }

    void set_state(State state) {
        this->state = state;
    }

    State get_state() {
        return state;
    }

    int get_tid() const {
        return tid;
    }

    sigjmp_buf* get_env() {
        return &env;
    }

    ~Thread() {
        delete[] this->t_stack;
    }
};

#endif //_THREAD_H_

