#ifndef _USER_HEADER_H
#define _USER_HEADER_H

#include "../common_header.h"

// SYSTEM CALLS
#define PUT_DATA         134
#define GET_DATA         156
#define INVALIDATE_DATA  174

#define flush(stdin) while(getchar() != '\n') // pulizia del buffer stdin

// MENU UTENTE
char menu[] = {"\n\n**************************************************************************** \
		\n\t       BLOCK-LEVEL DATA MANAGEMENT SERVICE\n\n\
  1) PUT DATA 		     - Scrivi un messaggio in un blocco libero\n\
  2) GET DATA		     - Recupera un messaggio in un blocco\n\
  3) INVALIDATE DATA	     - Elimina il contenuto di un blocco\n\
  4) MOUNT FILESYSTEM  	     - Monta il FS\n\
  5) UNMOUNT FILESYSTEM      - Smonta il FS\n\
  6) EXIT\n\
		\n****************************************************************************\n"};


// MACRO PER LA STAMPA ERRORI DELLE TRE SYSTEM CALL
#define print_put_ret(ret) \
    if (ret >= 0) { \
        printf("Messaggio correttamente inserito nel blocco %d\n", ret); \
    } else { \
        switch (errno) { \
            case ENOMEM: \
                printf("[Errore]: device pieno oppure problemi di allocazione di memoria\n"); \
                break; \
            case ENODEV: \
                printf("[Errore]: filesystem non montato\n"); \
                break; \
            case EINVAL: \
                printf("[Errore]: parametri non validi\n"); \
                break; \
            case EBUSY: \
                printf("[Errore]: try lock occupato\n"); \
                break; \
            default: \
                printf("[Errore]: generico\n"); \
                break; \
        } \
    }

#define print_get_ret(ret, offset, destination) \
    if (ret >= 0) { \
        printf("%d byte letti dal blocco %d: '%s'\n", ret, offset, destination); \
    } else { \
        switch (errno) { \
            case EINVAL: \
                printf("[Errore]: parametri non validi\n"); \
                break; \
            case ENODATA: \
                printf("[Errore]: blocco richiesto non valido\n"); \
                break; \
            case ENODEV: \
                printf("[Errore]: filesystem non montato\n"); \
                break; \
            default: \
                printf("[Errore]: generico\n"); \
                break; \
        } \
    }

#define print_invalidate_ret(ret, offset) \
    if (ret >= 0) { \
        printf("Blocco %d invalidato con successo\n", offset); \
    } else { \
        switch (errno) { \
            case EINVAL: \
                printf("[Errore]: parametri non validi\n"); \
                break; \
            case ENODATA: \
                printf("[Errore]: blocco già invalidato, nulla da fare\n"); \
                break; \
            case ENODEV: \
                printf("[Errore]: filesystem non montato\n"); \
                break; \
            default: \
                printf("[Errore]: generico\n"); \
                break; \
        } \
    }

#endif