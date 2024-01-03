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
#include <sys/time.h>

#define BUFFER_MAX_LENGTH 1000

struct Node {
    char data[BUFFER_MAX_LENGTH];
    struct Node* next;
};

struct LinkedList {
    struct Node* head;
};

struct Flags
{
    int exitFlag1;
    int exitFlag2;
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

void getLines(struct LinkedList *list, pid_t process2, int pipep1p2[2], sem_t *semP1, sem_t *semP2, const char* fileName) {
    //printf("Start of getline()\n");
    FILE *file;
    close(pipep1p2[0]);

    char line[1000];
    file = fopen(fileName, "r");
    if (file == NULL) {
        perror("Error opening file");
        return;
    }
    //printf("Before first getline() loop\n");
    while (fgets(line, sizeof(line), file) != NULL) {
        addElement(list, line);
        //printf("Added element\n");
        if (countElements(list) > 1) {
            char *firstElement = getFirstElement(list);
            if (firstElement != NULL) {
                write(pipep1p2[1], firstElement, sizeof(line));
                //printf("Before sem2 post getline()\n");
                sem_post(semP2);
                free(firstElement);
            }
            //printf("Before semp1 wait getline()\n");
            sem_wait(semP1);
            //printf("After semp1 wait getline()\n");
        }
    }
    //printf("End of first loop in getline()\n");
    if (countElements(list) > 0) {
        char *lastElement = getFirstElement(list);
        if (lastElement != NULL) {
            write(pipep1p2[1], lastElement, sizeof(line));
            free(lastElement);
        }
    }
    //printf("End of getline()\n");
    close(pipep1p2[1]);
    fclose(file);
}

struct ProcessTimes {
    double timeP1;
    double timeP2;
    double timeP3;
};

int main() {
    struct timeval start, end;

    int menuOption, result;
    char fileName[BUFFER_MAX_LENGTH];
    FILE *file;

    do {
        printf("Menu:\n");
        printf("1. Podaj nazwę pliku do przetworzenia\n");
        printf("2. Zakończ działanie programu\n");
        printf("Wybierz opcję: ");
        if (scanf("%d", &menuOption) != 1) {
            printf("Błąd: Wprowadzono nieprawidłowe dane. Proszę użyć liczby całkowitej.\n");

            // Czyszczenie bufora wejściowego
            int c;
            while ((c = getchar()) != '\n' && c != EOF) { }

            continue; // Powrót do początku pętli
        }

        switch (menuOption) {
            case 1:
                printf("Podaj nazwę pliku: ");
                scanf("%s", fileName);

                file = fopen(fileName, "r");
                if (file == NULL) {
                    perror("Wystąpił błąd. Nie można otworzyć pliku");
                    break; // Wyjście z case, powrót do menu
                }
                fclose(file);

                gettimeofday(&start, NULL);  // Rozpoczęcie pomiaru czasu
                struct LinkedList linkedList;
                linkedList.head = NULL;
                pid_t process1, process2, process3;
                sem_t *semP1 = sem_open("semP1", O_CREAT, 0666, 0);
                sem_t *semP2 = sem_open("semP2", O_CREAT, 0666, 0);
                sem_t *semP3 = sem_open("semP3", O_CREAT, 0666, 0);
                key_t memKey = 1234;
                key_t flagMemKey = 5678;
                int shmid = shmget(memKey, BUFFER_MAX_LENGTH, IPC_CREAT | 0666);
                int shmid2 = shmget(flagMemKey, sizeof(struct Flags), IPC_CREAT | 0666);
                struct Flags *exitFlags = (struct Flags*)shmat(shmid2, NULL, 0);
                if (exitFlags == (void*)(-1))
                {
                    perror("Flags shmat error");
                    return 1;
                }
                exitFlags->exitFlag1 = 0;
                exitFlags->exitFlag2 = 0;


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
                if (shmid2 == -1)
                {
                    perror("Błąd tworzenia pamięci dzielonej 2");
                    return 1;
                }

                struct ProcessTimes *times;
                int shmIdTimes = shmget(IPC_PRIVATE, sizeof(struct ProcessTimes), IPC_CREAT | 0666);
                times = (struct ProcessTimes *)shmat(shmIdTimes, NULL, 0);
                if (times == (void *)(-1)) {
                    perror("Błąd przyłączania segmentu pamięci dla czasów");
                    return 1;
                }

                process1 = fork();

                if (process1 == -1) {
                    perror("Błąd tworzenia procesu 1");
                    return 1;
                } else if (process1 == 0) {
                    struct Flags *exitFlags = (struct Flags*)shmat(shmid2, NULL, 0);
                    if (exitFlags == (void*)(-1))
                    {
                        perror("Flags shmat error");
                        return 1;
                    }
                    getLines(&linkedList, process2, pipep1p2, semP1, semP2, fileName);
                    //printf("Przed ustawieniem flagi i przed semp2 wait w proces1\n");
                    exitFlags->exitFlag1 = 1;
                    //printf("Flaga ustawiona koniec 1\n");
                    sem_post(semP2);
                    //printf("Flaga ustawiona sem2 post proces1\n");
                    shmdt(exitFlags);
                    exit(EXIT_SUCCESS);
                } else {
                    process2 = fork();
                    if (process2 == -1) {
                        perror("Błąd tworzenia procesu 2");
                        return 1;
                    } else if (process2 == 0) {
                        //printf("Process 2 beginning\n");
                        struct Flags *exitFlags = (struct Flags*)shmat(shmid2, NULL, 0);
                        if (exitFlags == (void*)(-1))
                        {
                            perror("Flags shmat error");
                            return 1;
                        }
                        //printf("Before first wait in process2\n");
                        sem_wait(semP2);
                        close(pipep1p2[1]);
                        char buffer[BUFFER_MAX_LENGTH];
                        int counter = 1;
                        while (read(pipep1p2[0], buffer, BUFFER_MAX_LENGTH) > 0) {
                            int numberOfCharsInLine = countChars(buffer);
                            char message[BUFFER_MAX_LENGTH];
                            snprintf(message, sizeof(message), "%d. %sLiczba znaków: %d", counter, buffer, numberOfCharsInLine);
                            char *sharedMessage = (char *)shmat(shmid, NULL, 0);
                            if (sharedMessage == (char *)(-1))
                            {
                                perror("Błąd pamięci dzielonej - pisanie");
                                return 1;
                            }
                            strcpy(sharedMessage, message);
                            shmdt(sharedMessage);
                            counter++;
                            //printf("Before post sem3\n");
                            if(exitFlags->exitFlag1 == 1){
                                exitFlags->exitFlag2 = 1;
                                sem_post(semP3);
                                shmdt(exitFlags);
                                break;
                            }
                            //printf("exitFlag1: %d\n", exitFlags->exitFlag1);
                            sem_post(semP3);
                            //printf("Before wait sem2\n");
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
                            //printf("Beginning of process3\n");
                            struct Flags *exitFlags = (struct Flags*)shmat(shmid2, NULL, 0);
                            if (exitFlags == (void*)(-1))
                            {
                                perror("Flags shmat error");
                                return 1;
                            }
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
                                if (exitFlags->exitFlag1 == 1 && exitFlags->exitFlag2 == 1)
                                {
                                    shmdt(exitFlags);
                                    break;
                                }

                                //printf("Before semp1 post\n");
                                sem_post(semP1);
                                //printf("Before semp3 wait\n");
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
                            shmdt(exitFlags);
                            freeLinkedList(&linkedList);
                        }

                    }
                }
                gettimeofday(&end, NULL); // Zakończenie pomiaru czasu
                double time_taken = end.tv_sec - start.tv_sec + (end.tv_usec - start.tv_usec) / 1000000.0;
                printf("Całkowity czas działania programu: %f sekund\n", time_taken);
                break;
            case 2:
                printf("Zakończenie programu.\n");
                break;

            default:
                printf("Niepoprawny wybór, spróbuj ponownie.\n");
                break;
        }
    } while (menuOption != 2);

    return 0;
}
