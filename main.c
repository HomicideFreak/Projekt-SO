#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <semaphore.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/shm.h>
#include <sys/ipc.h>

#define BUFFER_MAX_LENGTH 1000

struct Node {
    char data[BUFFER_MAX_LENGTH];
    struct Node* next;
};

struct LinkedList {
    struct Node* head;
};

void addElement(struct LinkedList* list, const char* data) {
    struct Node* newNode = (struct Node*)malloc(sizeof(struct Node));
    strncpy(newNode->data, data, BUFFER_MAX_LENGTH - 1);
    newNode->data[BUFFER_MAX_LENGTH - 1] = '\0';
    newNode->next = NULL;

    if (list->head == NULL) {
        list->head = newNode;
    } else {
        struct Node* current = list->head;
        while (current->next != NULL) {
            current = current->next;
        }
        current->next = newNode;
    }
}

char* getFirstElement(struct LinkedList* list) {
    if (list->head == NULL) {
        return NULL;
    }

    char* firstElement = strdup(list->head->data);
    struct Node* temp = list->head;
    list->head = list->head->next;
    free(temp);

    return firstElement;
}

void freeLinkedList(struct LinkedList* list) {
    struct Node* current = list->head;
    while (current != NULL) {
        struct Node* temp = current;
        current = current->next;
        free(temp);
    }
    list->head = NULL;
}

int countElements(struct LinkedList* list) {
    int count = 0;
    struct Node* current = list->head;
    while (current != NULL) {
        count++;
        current = current->next;
    }
    return count;
}

int countChars(char line[BUFFER_MAX_LENGTH]){
    int counter = 0;
    while (line[counter] != '\0') {
        counter++;
    }
    return (counter - 1);
}

void getLines(struct LinkedList *list, pid_t process2, int pipep1p2[2], sem_t *semP1, sem_t *semP2) {

    FILE *file;
    close(pipep1p2[0]);

    char line[1000];
    file = fopen("we.txt", "r");
    if (file == NULL) {
        perror("Error opening file");
        return;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        addElement(list, line);

        if (countElements(list) > 1) {
            char *firstElement = getFirstElement(list);
            if (firstElement != NULL) {
                write(pipep1p2[1], firstElement, sizeof(line));
                sem_post(semP2);
                free(firstElement);
            }
            sem_wait(semP1);
        }
    }

    if (countElements(list) > 0) {
        char *lastElement = getFirstElement(list);
        if (lastElement != NULL) {
            write(pipep1p2[1], lastElement, sizeof(line));
            sem_post(semP2);
            free(lastElement);
        }
    }

    close(pipep1p2[1]);
    fclose(file);
}


int main() {
    struct LinkedList linkedList;
    linkedList.head = NULL;
    pid_t process1, process2, process3;
    sem_t *semP1 = sem_open("semP1", O_CREAT, 0666, 0);
    sem_t *semP2 = sem_open("semP2", O_CREAT, 0666, 0);
    sem_t *semP3 = sem_open("semP3", O_CREAT, 0666, 0);
    key_t memKey = 1234;
    int shmid = shmget(memKey, BUFFER_MAX_LENGTH, IPC_CREAT | 0666);
    int pipep1p2[2];
    if (pipe(pipep1p2) == -1) {
        perror("Błąd tworzenia potoku p1-p2");
        return 1;
    }
    if (shmid == -1)
    {
        perror("Błąd tworzenia pamięci dzielonej");
        return 1;
    }    
    process1 = fork();
    if (process1 == -1) {
        perror("Błąd tworzenia procesu 1");
        return 1;
    } else if (process1 == 0) {
        getLines(&linkedList, process2, pipep1p2, semP1, semP2);
        exit(EXIT_SUCCESS);
    } else {
        process2 = fork();
        if (process2 == -1) {
            perror("Błąd tworzenia procesu 2");
            return 1;
        } else if (process2 == 0) {
            sem_wait(semP2);
            close(pipep1p2[1]);
            char buffer[BUFFER_MAX_LENGTH];
            while (read(pipep1p2[0], buffer, BUFFER_MAX_LENGTH) > 0) {
                int numberOfCharsInLine = countChars(buffer);
                char message[BUFFER_MAX_LENGTH];
                snprintf(message, sizeof(message), "Otrzymano: %s Ilość znaków: %d\n", buffer, numberOfCharsInLine);
                char *sharedMessage = (char *)shmat(shmid, NULL, 0);
                if (sharedMessage == (char *)(-1))
                {
                    perror("Błąd pamięci dzielonej - pisanie");
                    return 1;
                }
                strcpy(sharedMessage, message);
                shmdt(sharedMessage);
                sem_post(semP3);
                sem_wait(semP2);
            }
            close(pipep1p2[0]);
            exit(EXIT_SUCCESS);
        } else {
            process3 = fork();
            if (process3 == -1)
            {
                perror("Błąd tworzenia procesu 3");
                return 1;
            }
            else if(process3 == 0){
                sem_wait(semP3);
                while (1)
                {
                    char *sharedMessage = (char *)shmat(shmid, NULL, 0);
                    if (sharedMessage == (char *)(-1))
                    {
                        perror("Błąd pamięci dzielonej - czytanie");
                        return 1;
                    }
                    printf("%s\n", sharedMessage);
                    shmdt(sharedMessage);
                    sem_post(semP1);
                    sem_wait(semP3);
                }
                exit(EXIT_SUCCESS);
                
            }
            else{

                waitpid(process1, NULL, 0);
                waitpid(process2, NULL, 0);
                waitpid(process3, NULL, 0);
                close(pipep1p2[0]);
                close(pipep1p2[1]);
                sem_close(semP1);
                sem_close(semP2);
                sem_close(semP3);
                sem_unlink("semP1");
                sem_unlink("semP2");
                sem_unlink("semP3");
                freeLinkedList(&linkedList);
            }
            
        }
    }
    return 0;
}
