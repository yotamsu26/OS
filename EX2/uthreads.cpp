#include <signal.h>
#include "Thread.h"
#include "uthreads.h"
#include <iostream>
#include <deque>

////////////////// consts ////////////////////
#define MAIN_THREAD 0
#define TIME_SET 1000000

///////////////// errors code ////////////////
#define ERR_MSG "error"
#define ERR_CODE -1
#define ERR_EXIT 1

///////////////// errors /////////////////////
#define LIBRARY_ERR "thread library error: "
#define SYSTEM_ERR "system error: "
#define SIGPROCMASK_ERR "could not execute sigprocmask appropriately"
#define SETITIMER_ERR "could not execute setitimer appropriately"
#define SIGACTION_ERR "could not execute sigaction appropriately"
#define INVALID_THREAD_ERR "Thread Invalid"
#define NO_FREE_TID_ERR "No free TID"
#define NO_ENTRY_POINT_ERR "No entry poiny given"
#define INVALID_QUANTUM_ERR "Invalid quantum"
#define MAIN_SLEEP_ERR "cannot send main thread to sleep"

///////////////// global var /////////////////

Thread *thread_array[MAX_THREAD_NUM];
int sleeping_threads[MAX_THREAD_NUM];
struct sigaction sig_act;
struct itimerval itimer;
std::deque<Thread *> ready_threads;
Thread *current_thread = nullptr;
sigset_t signal_set;
int total_quantums = 0;

///////////////// Helper Functions /////////////////

/**
 * @brief Destroys all threads in the thread array.
 * 
 * This function iterates through the thread array and deletes each thread pointer.
 */
void destroy_threads() {
    for (auto thread: thread_array) {
        delete thread;
    }
}

/**
 * @brief Blocks the signal specified in the signal_set.
 */
void block_signal() {
    if (sigprocmask(SIG_BLOCK, &signal_set, NULL) < 0) {
        destroy_threads();
        std::cerr << SYSTEM_ERR << SIGPROCMASK_ERR << std::endl;
        exit(ERR_EXIT);
    }
}

/**
 * @brief Unblocks the signal specified in the signal_set.
 */
void unblock_signal() {
    if (sigprocmask(SIG_UNBLOCK, &signal_set, NULL) < 0) {
        destroy_threads();
        std::cerr << SYSTEM_ERR << SIGPROCMASK_ERR << std::endl;
        exit(ERR_CODE);
    }
}

/**
 * @brief Sets the timer for virtual time intervals.
 */
void set_timer() {
    if (setitimer(ITIMER_VIRTUAL, &itimer, NULL) < 0) {
        destroy_threads();
        std::cerr << SYSTEM_ERR << SETITIMER_ERR << std::endl;
        exit(ERR_CODE);
    }
}

/**
 * @brief Checks if the given thread ID is valid.
 * 
 * @param tid Thread ID to check.
 * @return true if the thread ID is valid, false otherwise.
 */
bool valid_thread(int tid) {
    return (tid >= 0 && tid < MAX_THREAD_NUM && thread_array[tid] != nullptr);
}

/**
 * @brief Finds the minimal thread ID that is currently unused.
 * 
 * @return The minimal thread ID that is unused, or -1 if all IDs are in use.
 */
int find_minimal_tid() {
    for (int i = 1; i < MAX_THREAD_NUM; i++) {
        if (thread_array[i] == nullptr) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief Removes the thread with the given ID from the ready list.
 * 
 * @param tid Thread ID to remove.
 * @return 1 if the thread was removed, -1 if the thread was not found.
 */
int remove_thread_from_ready(int tid) {
    for (auto thread = ready_threads.begin(); thread != ready_threads.end(); ++thread) {
        if ((*thread)->get_tid() == tid) {
            ready_threads.erase(thread);
            return 1;
        }
    }
    return -1;
}

/**
 * @brief Updates the sleeping state of all threads.
 * 
 * Decrements the sleep counter for each thread. 
 */
void update_sleeping() {
    for (int i = 0; i < MAX_THREAD_NUM; i++) {
        if (sleeping_threads[i] > 0) {
            sleeping_threads[i]--;
        }
        if (sleeping_threads[i] == 0) {
            State prev = thread_array[i]->get_state();
            thread_array[i]->set_state(BLOCKED);
            if (prev == SLEEPING) {
                uthread_resume(i);
            }
        }
    }
}

/**
 * @brief Moves to the next thread in the ready list.
 */
void move_to_next_thread() {
    // get the first ready thread
    Thread *thread = ready_threads.front();
    ready_threads.pop_front();

    // set it to the current thread
    thread->set_state(RUNNING);
    thread->incrament_quantums();
    current_thread = thread;

    unblock_signal();
    siglongjmp(*(thread->get_env()), 1);
}

/**
 * @brief handle err, print it, return err_code and unblock the signal.
 */
int library_error_handler(std::string err){
    std::cout << LIBRARY_ERR << err << std::endl;
    unblock_signal();
    return ERR_CODE;
}

/**
 * @brief checks if the state is neither block or sleep.
 */
bool not_block_or_sleep(State state){
    return state != BLOCKED && state != SLEEPING && state != SLEEPING_AND_BLOCKED;
}

/**
 * @brief Updates the quantum timer and schedules the next thread.
 * 
 * Blocks signals, updates the sleep state of threads, and increments the total quantum count.
 * 
 * @param unused Unused parameter to match the expected function signature for signal handlers.
 */
void quantum_update_func(int) {
    block_signal();
    update_sleeping();
    total_quantums++;

    if (ready_threads.empty()) {
        current_thread->incrament_quantums();
        unblock_signal();
        return;
    } else if (current_thread == nullptr) {
        set_timer();
        move_to_next_thread();
    } else if (sigsetjmp(*(current_thread->get_env()), 1) == 0) {
        if (not_block_or_sleep(current_thread->get_state())) {
            current_thread->set_state(READY);
            ready_threads.push_back(current_thread);
        }
        set_timer();
        move_to_next_thread();
    }
}

///////////////// library api /////////////////

int uthread_init(int quantum_usecs) {
    if (quantum_usecs <= 0) {
        std::cout << LIBRARY_ERR << INVALID_QUANTUM_ERR << std::endl;
        return ERR_CODE;
    }
    sig_act.sa_handler = &quantum_update_func;
    if (sigaction(SIGVTALRM, &sig_act, NULL) < 0) {
        std::cerr << SYSTEM_ERR << SIGACTION_ERR << std::endl;
        exit(ERR_EXIT);
    }
    // set signal set
    sigemptyset(&signal_set);
    sigaddset(&signal_set, SIGVTALRM);
    // set timer
    itimer = {{quantum_usecs / TIME_SET, quantum_usecs % TIME_SET},
              {quantum_usecs / TIME_SET, quantum_usecs % TIME_SET}};
    // set main thread
    Thread *main_thread = new Thread();
    thread_array[0] = main_thread;
    current_thread = main_thread;
    // init sleeping thread
    for (int i = 0; i < MAX_THREAD_NUM; ++i) {
    sleeping_threads[i] = -1;
    }
    // quantum update
    main_thread->incrament_quantums();
    total_quantums++;
    set_timer();
    return EXIT_SUCCESS;
}

/**
 * @brief Creates a new thread, whose entry point is the function entry_point with the signature
 * void entry_point(void).
 *
 * The thread is added to the end of the READY threads list.
 * The uthread_spawn function should fail if it would cause the number of concurrent threads to exceed the
 * limit (MAX_THREAD_NUM).
 * Each thread should be allocated with a stack of size STACK_SIZE bytes.
 * It is an error to call this function with a null entry_point.
 *
 * @return On success, return the ID of the created thread. On failure, return -1.
*/
int uthread_spawn(thread_entry_point entry_point){
    block_signal();
    if (entry_point == nullptr) {
        return library_error_handler(NO_ENTRY_POINT_ERR);
    }
    int tid = find_minimal_tid();
    if (tid == -1) {
        return library_error_handler(NO_FREE_TID_ERR);
    }
    Thread *new_thread = new Thread(tid, entry_point);
    thread_array[tid] = new_thread;
    ready_threads.push_back(new_thread);
    unblock_signal();
    return tid;
}

/**
 * @brief Terminates the thread with ID tid and deletes it from all relevant control structures.
 *
 * All the resources allocated by the library for this thread should be released. If no thread with ID tid exists it
 * is considered an error. Terminating the main thread (tid == 0) will result in the termination of the entire
 * process using exit(0) (after releasing the assigned library memory).
 *
 * @return The function returns 0 if the thread was successfully terminated and -1 otherwise. If a thread terminates
 * itself or the main thread is terminated, the function does not return.
*/
int uthread_terminate(int tid){
    block_signal();
    if(!valid_thread(tid)){
        return library_error_handler(INVALID_THREAD_ERR);
    }
    // terminate the main thread
    if(tid == MAIN_THREAD) {
        destroy_threads();
        exit(EXIT_SUCCESS);
    }
    // terminate itself
    if (tid == current_thread->get_tid()) {
        delete thread_array[tid];
        thread_array[tid] = nullptr;
        current_thread = nullptr;
        sleeping_threads[tid] = -1;
        quantum_update_func(0);
    }
    else {
        remove_thread_from_ready(tid);
        delete thread_array[tid];
        thread_array[tid] = nullptr;
    }
    unblock_signal();
    return EXIT_SUCCESS;
}

/**
 * @brief Blocks the thread with ID tid. The thread may be resumed later using uthread_resume.
 *
 * If no thread with ID tid exists it is considered as an error. In addition, it is an error to try blocking the
 * main thread (tid == 0). If a thread blocks itself, a scheduling decision should be made. Blocking a thread in
 * BLOCKED state has no effect and is not considered an error.
 *
 * @return On success, return 0. On failure, return -1.
*/
int uthread_block(int tid){
    block_signal();
    if(tid == 0 || !valid_thread(tid)){
        return library_error_handler(INVALID_THREAD_ERR);
    }
    State thread_state = thread_array[tid]->get_state();
    if(thread_state != BLOCKED && thread_state != SLEEPING_AND_BLOCKED) {
        if (thread_state == SLEEPING) {
            thread_array[tid]->set_state(SLEEPING_AND_BLOCKED);
        } else {
            thread_array[tid]->set_state(BLOCKED);
        }
    }
    if (tid == current_thread->get_tid()) {
        quantum_update_func(0);
    }
    remove_thread_from_ready(tid);
    unblock_signal();
    return 0;
}

/**
 * @brief Resumes a blocked thread with ID tid and moves it to the READY `state.`
 *
 * Resuming a thread in a RUNNING or READY state has no effect and is not considered as an error. If no thread with
 * ID tid exists it is considered an error.
 *
 * @return On success, return 0. On failure, return -1.
*/
int uthread_resume(int tid){
    block_signal();
    if(!valid_thread(tid)){
        return library_error_handler(INVALID_THREAD_ERR);
    }
    State thread_state = thread_array[tid]->get_state();
    if(thread_state == SLEEPING_AND_BLOCKED){
        thread_array[tid]->set_state(SLEEPING);
    } 
    if(thread_state == BLOCKED){
        thread_array[tid]->set_state(READY);
        ready_threads.push_back(thread_array[tid]);
    }
    unblock_signal();
    return EXIT_SUCCESS;
}

/**
 * @brief Blocks the RUNNING thread for num_quantums quantums.
 *
 * Immediately after the RUNNING thread transitions to the BLOCKED state a scheduling decision should be made.
 * After the sleeping time is over, the thread should go back to the end of the READY queue.
 * If the thread which was just RUNNING should also be added to the READY queue, or if multiple threads wake up 
 * at the same time, the order in which they're added to the end of the READY queue doesn't matter.
 * The number of quantums refers to the number of times a new quantum starts, regardless of the reason. Specifically,
 * the quantum of the thread which has made the call to uthread_sleep isn’t counted.
 * It is considered an error if the main thread (tid == 0) calls this function.
 *
 * @return On success, return 0. On failure, return -1.
*/
int uthread_sleep(int num_quantums) {
    block_signal();
    if (current_thread == thread_array[0]) {
        return library_error_handler(MAIN_SLEEP_ERR);
    }
    current_thread->set_state(SLEEPING);
    sleeping_threads[current_thread->get_tid()] = num_quantums;
    quantum_update_func(0);
    unblock_signal();
    return EXIT_SUCCESS;
}

/**
 * @brief Returns the thread ID of the calling thread.
 *
 * @return The ID of the calling thread.
*/
int uthread_get_tid(){
    return current_thread->get_tid();
}

/**
 * @brief Returns the total number of quantums since the library was initialized, including the current quantum.
 *
 * Right after the call to uthread_init, the value should be 1.
 * Each time a new quantum starts, regardless of the reason, this number should be increased by 1.
 *
 * @return The total number of quantums.
*/
int uthread_get_total_quantums(){
    return total_quantums;
}

/**
 * @brief Returns the number of quantums the thread with ID tid was in RUNNING state.
 *
 * On the first time a thread runs, the function should return 1. Every additional quantum that the thread starts should
 * increase this value by 1 (so if the thread with ID tid is in RUNNING state when this function is called, include
 * also the current quantum). If no thread with ID tid exists it is considered an error.
 *
 * @return On success, return the number of quantums of the thread with ID tid. On failure, return -1.
*/
int uthread_get_quantums(int tid){
    if(valid_thread(tid)){
        return thread_array[tid]->get_quantums();
    }

    std::cout << LIBRARY_ERR <<INVALID_THREAD_ERR << std::endl;
    return ERR_CODE;
}
