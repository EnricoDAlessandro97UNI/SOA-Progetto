#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include "user_header.h"

#define NTHREADS 16
#define DEFAULT_BUFFER_SIZE 128

pthread_barrier_t barrier;

void *test_put_syscall(void *arg) {

    int ret;
    char source[DEFAULT_BUFFER_SIZE];
    size_t size;
    pthread_t tid;

    tid = *(pthread_t *)arg;
    printf("[THREAD %ld]: funzione test_put_syscall()\n", tid);
    fflush(stdout);

    sprintf(source, "Ha scritto il thread %ld\n", tid);
    size = strlen(source);

    pthread_barrier_wait(&barrier);

    do {    
        ret = syscall(PUT_DATA, source, size);
    } while(errno == EAGAIN);

    if(ret >= 0) {
        printf("[THREAD %ld]: esecuzione test_put_syscall() terminata con successo e scritto il blocco %d\n", tid, ret);
        fflush(stdout);
    }
    else {
        printf("[THREAD %ld]: esecuzione test_put_syscall() fallita\n", tid);
        fflush(stdout);
    }

    pthread_exit(NULL);
}

void *test_get_syscall(void *arg) {

    int ret, offset;
    char destination[DEFAULT_BUFFER_SIZE];
    size_t size;
    pthread_t tid;

    tid = *(pthread_t *)arg;
    printf("[THREAD %ld]: funzione test_get_syscall()\n", tid);
    fflush(stdout);

    size = (size_t)DEFAULT_BUFFER_SIZE;
    offset = (int)(tid % NBLOCKS);

    pthread_barrier_wait(&barrier);

    ret = syscall(GET_DATA, offset, destination, DEFAULT_BUFFER_SIZE);
    
    if(ret >= 0) {
        printf("[THREAD %ld]: esecuzione test_get_syscall() terminata con successo sul blocco %d - read: %s\n", tid, offset, destination);
        fflush(stdout);
    }
    else {
        printf("[THREAD %ld]: esecuzione test_get_syscall() fallita\n", tid);
        fflush(stdout);
    }

    pthread_exit(NULL);
}

void *test_invalidate_syscall(void *arg) {

    int ret, offset;
    pthread_t tid;

    tid = *(pthread_t *)arg;
    printf("[THREAD %ld]: funzione test_invalidate_syscall()\n", tid);
    fflush(stdout);

    offset = (int)(tid % NBLOCKS);

    pthread_barrier_wait(&barrier);

    ret = syscall(INVALIDATE_DATA, offset);
    
    if(ret >= 0) {
        printf("[THREAD %ld]: esecuzione test_invalidate_syscall() terminata con successo e invalidato il blocco %d\n", tid, ret);
        fflush(stdout);
    }
    else {
        printf("[THREAD %ld]: esecuzione test_invalidate_syscall() fallita\n", tid);
        fflush(stdout);
    }

    pthread_exit(NULL);
}


int main(int argc, char *argv[]) {
    
    int ret, i, thread;
    long r;
    pthread_t tids[NTHREADS];

    pthread_barrier_init(&barrier, NULL, NTHREADS);
    
    for(i = 0; i < NTHREADS; i++) {
        r = random();
        thread = r % 3;
        if (thread == 0) ret = pthread_create(&tids[i], NULL, test_put_syscall, &tids[i]);
        else if (thread == 1) ret = pthread_create(&tids[i], NULL, test_get_syscall, &tids[i]);
        else if (thread == 2) ret = pthread_create(&tids[i], NULL, test_invalidate_syscall, &tids[i]);
        else goto error;

        if(ret != 0) 
            goto error;
    }

    for(i = 0; i < NTHREADS; i++) {
        pthread_join(tids[i], NULL);
    }

    pthread_barrier_destroy(&barrier);
    return 0;

error:
    printf("\n[Errore]: qualcosa è andato storto durante la creazione dei thread\n");
    fflush(stdout);
    pthread_barrier_destroy(&barrier);
    return -1;
}