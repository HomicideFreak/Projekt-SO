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


#define MAX_LINE_SIZE 1000


struct Flags
{
    int exitFlag0;
    int exitFlag1;
    int exitFlag2;
};

struct Flags *exitFlags;
pid_t process1, process2, process3;
sem_t *semP1, *semP2, *semP3;
int shmid, shmid2, shmid3;


int main(){

    int menuOption;
    char fileName[MAX_LINE_SIZE];
    FILE *file;
    do
    {
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
        switch (menuOption)
        {
        case 1:
             int pipep1p2[2];
            if (pipe(pipep1p2) == -1) {
                perror("Błąd tworzenia potoku p1-p2");
                return 1;
            }

            key_t memKey = 1234;
            key_t flagMemKey = 5678;
            key_t rdFlagMemKey = 9012;
            shmid = shmget(memKey, MAX_LINE_SIZE, IPC_CREAT | 0666);
            shmid2 = shmget(flagMemKey, sizeof(struct Flags), IPC_CREAT | 0666);

            exitFlags = (struct Flags*)shmat(shmid2, NULL, 0);
            if (exitFlags == (void*)(-1))
            {
                perror("Flags shmat error");
                return 1;
            }

            exitFlags->exitFlag0 = 0;
            exitFlags->exitFlag1 = 0;
            exitFlags->exitFlag2 = 0;

            semP1 = sem_open("semP1", O_CREAT, 0666, 0);
            semP2 = sem_open("semP2", O_CREAT, 0666, 0);
            semP3 = sem_open("semP3", O_CREAT, 0666, 0);
            printf("Podaj nazwę pliku: ");
            scanf("%s", fileName);

            file = fopen(fileName, "r");
            if (file == NULL) {
                perror("Wystąpił błąd. Nie można otworzyć pliku");
                break; // Wyjście z case, powrót do menu
            }
            fclose(file);
            process1 = fork();
            if (process1 == -1)
            {
                perror("Error creating process");
                return 1;
            }
            else if(process1 == 0){
                exitFlags = (struct Flags*)shmat(shmid2, NULL, 0);
                if (exitFlags == (void*)(-1))
                {
                    perror("Flags shmat error");
                    return 1;
                }
                char line[MAX_LINE_SIZE];
                FILE *file;
                file = fopen(fileName, "r");
                if (file == NULL)
                {
                    perror("Error opening the file");
                    return 1;
                }
                
                while (fgets(line, sizeof(line), file) != NULL){
                    
                    write(pipep1p2[1], line, sizeof(line));
                    sem_post(semP2);
                    sem_wait(semP1);
                }
                fclose(file);
                exitFlags->exitFlag1 = 1;
                exitFlags->exitFlag0 = 1;
                sem_post(semP2);
                exit(EXIT_SUCCESS);

            }
            else{
                process2 = fork();
                if (process2 == -1)
                {
                    perror("Error creating process");
                    return 1;
                }
                else if(process2 == 0){
                    exitFlags = (struct Flags*)shmat(shmid2, NULL, 0);
                    if (exitFlags == (void*)(-1))
                    {
                        perror("Flags shmat error");
                        return 1;
                    }
                    sem_wait(semP2);
                    char line[MAX_LINE_SIZE];
                    int counter;
                    int messageCounter = 1;
                    while (exitFlags->exitFlag0 == 0)
                    {       
                        read(pipep1p2[0], &line, sizeof(line));         
                        int numberOfChars = 0;
                        counter = 0; 
                        while (line[counter] != '\0')
                        {
                            counter++;
                        }
                        numberOfChars = counter - 1;
                        char message[MAX_LINE_SIZE + 18];

                        snprintf(message, sizeof(message), "%d. %sLiczba znaków: %d\n", messageCounter, line, numberOfChars);
                        messageCounter++;
                        char *sharedMessage = (char *)shmat(shmid, NULL, 0);
                        if (sharedMessage == (char *)(-1))
                        {
                            perror("Błąd pamięci dzielonej - pisanie");
                            return 1;
                        }
                        strcpy(sharedMessage, message);
                        shmdt(sharedMessage);
                        if (exitFlags->exitFlag1 == 1)
                        {
                            exitFlags->exitFlag2 = 1;
                            sem_post(semP3);
                            exit(EXIT_SUCCESS);
                        }
                        sem_post(semP3);
                        sem_post(semP1);
                        sem_wait(semP2);
                    }
                    exitFlags->exitFlag2 = 1;
                    sem_post(semP3);
                    exit(EXIT_SUCCESS);

                }
                else{
                    process3 = fork();
                    if (process3 == -1)
                    {
                        perror("Error creating process");
                        return 1;
                    }
                    else if(process3 == 0){
                        exitFlags = (struct Flags*)shmat(shmid2, NULL, 0);
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
                                exit(EXIT_SUCCESS);
                            }
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
                    }
                    
                }
                
            }
            break;
        case 2:
            printf("Zakończenie programu.\n");
            break;
        default:
            break;
        }
    } while (menuOption != 2);
    
    return 0;
}
