#include <linux/slab.h>

#include "../utils_header.h"

// Funzione per la ricerca di un nodo
int list_search(struct Node *head, unsigned int block_num) {

    struct Node *p = head;

    while (p != NULL) {
        if (p->block_num == block_num)
            return 1;
        p = p->next;
    }

    return 0;
}

// Funzione per inserire un nodo in coda alla lista
struct Node *list_insert(struct Node **head, unsigned int block_num) {

    struct Node *newNode;
    struct Node *curr = *head;

    newNode = (struct Node *) kmalloc(sizeof(struct Node), GFP_KERNEL);
    if (!newNode) return NULL;

    newNode->block_num = block_num;
    newNode->next = NULL;

    if (*head == NULL) {
        *head = newNode;
        asm volatile("mfence"); // make it visible to the readers
    }
    else {
        while (curr->next != NULL) {
            curr = curr->next;
        }
        curr->next = newNode;
        asm volatile("mfence"); // make it visible to the readers
    }

    return newNode;
}

// Funzione per eliminare un nodo dalla lista
struct Node *list_remove(struct Node **head, unsigned int block_num) {

    struct Node *curr = *head;
    struct Node *prev = *head;

    if (curr == NULL) // lista vuota
        return NULL;

    if (curr->block_num == block_num) { // elimina la testa
        *head = curr->next;
        asm volatile("mfence");
        return curr;
    }
    
    while (curr != NULL) { // elimina un nodo non all'inizio
        if (curr->block_num == block_num) {
            prev->next = curr->next;
            asm volatile("mfence");
            return curr;
        }
        prev = curr;
        curr = curr->next;
    }

    return NULL;
}

// Funzione per ottenere il contenuto del nodo precedente ad un nodo dato
unsigned int list_get_prev(struct Node *head, unsigned int block_num) {

    struct Node *curr = head;
    struct Node *prev = NULL;

    while (curr != NULL) {
        if (curr->block_num == block_num) {
            if (prev != NULL)
                return prev->block_num;
            else
                return -1;
        }
        prev = curr;
        curr = curr->next;
    }

    return -1;
}

// Funzione per ottenere il contenuto del nodo successivo ad un nodo dato
unsigned int list_get_next(struct Node *head, unsigned int block_num) {

    struct Node *curr = head;
    
    while (curr != NULL) {
        if (curr->block_num == block_num) {
            if (curr->next != NULL)
                return curr->next->block_num;
            else
                return -1;
        }
        curr = curr->next;
    }

    return -1;
}

// Funzione per ottenere un nodo della lista dato il suo contenuto
struct Node *list_get_node(struct Node *head, unsigned int block_num) {
    struct Node *p = head;

    while (p != NULL) {
        if (p->block_num == block_num)
            return p;
        p = p->next;
    }

    return NULL;
}

// Funzione per svuotare la lista
void list_clear(struct Node **head) {

    struct Node *p = *head;
    struct Node *nextNode;

    while (p != NULL) {
        nextNode = p->next;
        kfree(p);
        p = nextNode;
    }

    // Set the head pointer to NULL to indicate an empty list
    *head = NULL;
}