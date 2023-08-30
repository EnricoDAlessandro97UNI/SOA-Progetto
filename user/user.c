#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>

#include "user_header.h"


void put_data() {

    int ret;
    ssize_t size;
    char *source;

    source = (char *) malloc(sizeof(char) * 4096);
    if (source == NULL) {
        printf("malloc error\n");
        exit(-1);
    }

    printf("\nInserire il messaggio: ");
    fgets(source, 4096, stdin);

    if (strlen(source) == 0) {
        printf("\nMessaggio inserito vuoto");
        return;
    }

    if ((strlen(source) > 0) && (source[strlen(source)-1] == '\n'))
        source[strlen(source)-1] = '\0';
    size = strlen(source);
    printf("\nHai inserito un messaggio lungo %ld caratteri\n", size);
    printf("Messaggio acquisito: %s", source);

    printf("\nInvocazione della syscall put_data...\n");
    
    do {
        ret = syscall(PUT_DATA, source, size);
    } while (errno == EAGAIN);
    
    print_put_ret(ret);
}

void get_data() {

    int ret, offset;
    ssize_t size = 2048; // 2048 è un semplice valore per il testing
    char *destination;

    destination = (char *) malloc(sizeof(char) * 4096);
    if (destination == NULL) {
        printf("malloc error\n");
        exit(-1);
    } 

    printf("Inserire l'offset (un blocco utente da 0 a N-1): ");
    scanf("%d", &offset);
    flush(stdin);

    printf("Inserire il numero di byte da leggere dal blocco: ");
    scanf("%ld", &size);
    flush(stdin);

    ret = syscall(GET_DATA, offset, destination, size);
    print_get_ret(ret, offset, destination);

    free(destination);
}

void invalidate_data() {

    int offset, ret;

    printf("Inserire l'offset (un blocco da 0 a N-1): ");
    scanf("%d", &offset);
    flush(stdin);

    do {
        ret = syscall(INVALIDATE_DATA, offset);
    } while (errno == EAGAIN);

    print_invalidate_ret(ret, offset);
}

int main(int argc, char *argv[]) {
    
    int op, ret;
    char mount_command[1024];

    system("clear");

    while (1) {
        printf("%s\nScegli un operazione: ", menu);
        scanf("%d", &op);
        flush(stdin);
        switch (op) {
            case 1:
                put_data();
                printf("\nPremere invio per tornare al menu...");
                while(getchar() != '\n');
                break;
            case 2:
                get_data();
                printf("\nPremere invio per tornare al menu...");
                while(getchar() != '\n');
                break;
            case 3:
                invalidate_data();
                printf("\nPremere invio per tornare al menu...");
                while(getchar() != '\n');
                break;
            case 4:
                sprintf(mount_command, "mount -o loop -t singlefilefs %s ../mount/", IMAGE_PATH);
                ret = system(mount_command);
                if (ret == 0) printf("\nMontaggio avvenuto con successo\n");
                else printf("\nErrore durante il montaggio\n");
                printf("\nPremere invio per tornare al menu...");
                while(getchar() != '\n');
                break;
            case 5:
                sprintf(mount_command,"umount ../mount/");
                ret = system(mount_command);
                if (ret == 0) printf("\nSmontaggio avvenuto con successo\n");
                else printf("\nErrore durante lo smontaggio\n");
                printf("\nPremere invio per tornare al menu...");
                while(getchar() != '\n');
                break;
            case 6:
                goto exit;
            default:
                printf("\nScelta non valida...\n");
                break;
        }
    }
exit:
    return 0;
}