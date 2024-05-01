#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <pthread.h>
#include <semaphore.h>
#include <limits.h>
#include <sys/shm.h>

#define MAX_AIRPORTS 10
#define MAX_RUNWAYS 10
#define BACKUP_LOAD_CAPACITY 15000
#define SHM_SIZE sizeof(int)
#define KEY_PATH "plane.c"

typedef struct {
    long messageType;
    int airportArrival;
    int airportDeparture;
    int planeID;
    int weightOfPlane;
    int planeType;
    int noOfPassengers;
    int flag;
} PlaneDetails;

typedef struct {
    int runwayNumber;
    int loadCapacity;
    int available;
} Runway;

typedef struct {
    PlaneDetails planeDetails;
    Runway* runways;
    int airportNumber;
    int numberOfRunways;
    sem_t* runwaySemaphores;
    sem_t semaphore;
} ThreadArgs;

pthread_mutex_t mutex,mutex2;

void* handleDeparture(void* threadArgs) {
    ThreadArgs* args = (ThreadArgs*)threadArgs;

    int planeWeight = args->planeDetails.weightOfPlane;
    int bestFitIndex = -1;
    int minDifference = INT_MAX;

    for (int i = 0; i < args->numberOfRunways; i++) {
        if (args->runways[i].available) {
            int difference = args->runways[i].loadCapacity - planeWeight;
            if (difference >= 0 && difference < minDifference) {
                minDifference = difference;
                bestFitIndex = i;
            }
        }
    }
    printf("hii am inside the thread!\n");

    if (bestFitIndex == -1) {
        pthread_mutex_lock(&mutex);
        bestFitIndex = args->numberOfRunways;  // Use the backup runway
        args->runways[bestFitIndex].available = 0;
        pthread_mutex_unlock(&mutex);
    } else {
        pthread_mutex_lock(&mutex);
        args->runways[bestFitIndex].available = 0;
        pthread_mutex_unlock(&mutex);
    }
    printf("i made it out of the mutex locks!\n");
    // Simulate boarding/loading
    sleep(3);

    printf("Plane %d has completed boarding/loading and taken off from Runway No. %d of Airport No. %d\n",
           args->planeDetails.planeID, args->runways[bestFitIndex].runwayNumber, args->airportNumber);

    // Set the runway as available
    args->runways[bestFitIndex].available = 1;

    // Signal the semaphore to release the runway availability
    sem_post(&(args->semaphore));

    pthread_exit(NULL);
}


void* handleArrival(void* threadArgs) {
    ThreadArgs* args = (ThreadArgs*)threadArgs;

    int planeWeight = args->planeDetails.weightOfPlane;
    int bestFitIndex = -1;
    int minDifference = INT_MAX;

    for (int i = 0; i < args->numberOfRunways; i++) {
        int difference = args->runways[i].loadCapacity - planeWeight;
        if (difference >= 0 && difference < minDifference && args->runways[i].available) {
            minDifference = difference;
            bestFitIndex = i;
        }
    }

    if (bestFitIndex == -1) {
        pthread_mutex_lock(&mutex);
        bestFitIndex = args->numberOfRunways;  // Use the backup runway
        pthread_mutex_unlock(&mutex);
    } else {
        pthread_mutex_lock(&mutex);
        args->runways[bestFitIndex].available = 0;  // Set the runway as unavailable
        pthread_mutex_unlock(&mutex);
    }

    // Simulate landing
    sleep(2);

    // Simulate deboarding/unloading
    sleep(3);

    printf("Plane %d has landed on Runway No. %d of Airport No. %d and has completed deboarding/unloading\n",
           args->planeDetails.planeID, args->runways[bestFitIndex].runwayNumber, args->airportNumber);

    // Set the runway as available
    args->runways[bestFitIndex].available = 1;

    // Signal the semaphore to release the runway availability
    sem_post(&(args->semaphore));

    pthread_exit(NULL);
}


void createAirportProcess(int airportNumber, int numberOfRunways, int* loadCapacities, int msgQueue) {
    PlaneDetails planeDetails;
    Runway runways[MAX_RUNWAYS + 1];

    for (int i = 0; i < numberOfRunways; i++) {
        runways[i].runwayNumber = i+1;
        runways[i].loadCapacity = loadCapacities[i];
        runways[i].available = 1;
    }

    runways[numberOfRunways].runwayNumber = numberOfRunways+1;
    runways[numberOfRunways].loadCapacity = BACKUP_LOAD_CAPACITY;
    runways[numberOfRunways].available = 1;

    pthread_t threadDeparture, threadArrival;
    sem_t semaphore;
    pthread_mutex_init(&mutex2, NULL);
    pthread_mutex_init(&mutex,NULL);
    sem_init(&semaphore, 0, numberOfRunways);
    pthread_mutex_lock(&mutex2);

    ThreadArgs departureArgs = { .runways = runways, .airportNumber = airportNumber, .numberOfRunways = numberOfRunways, .semaphore = semaphore };
    ThreadArgs arrivalArgs = { .runways = runways, .airportNumber = airportNumber, .numberOfRunways = numberOfRunways, .semaphore = semaphore };

    while(1){
        if(msgrcv(msgQueue, &planeDetails, sizeof(PlaneDetails) - sizeof(long), 2, 0) != -1){
            printf("message of type 2 recd!\n");
        }
        //departure airport :
        if(planeDetails.airportDeparture == airportNumber){
            printf("hey there!\n");
            departureArgs.planeDetails = planeDetails;

            if (pthread_create(&threadDeparture, NULL, handleDeparture, (void*)&departureArgs) != 0) {
                perror("Failed to create departure thread");
                exit(1);
            }

            // Wait for the departure thread to finish
            if (pthread_join(threadDeparture, NULL) != 0) {
                perror("Failed to join departure thread");
                exit(1);
            }

            planeDetails.messageType = 3;
            if (msgsnd(msgQueue, &planeDetails, sizeof(PlaneDetails) - sizeof(long), 0) == -1) {
                perror("Failed to receive the message");
                exit(1);
            }
            printf("message of type 3 sent!\n");

            //sleep
            sleep(2);
        }

        //arrival airport :
        if(planeDetails.airportArrival == airportNumber){
            if(msgrcv(msgQueue, &planeDetails, sizeof(PlaneDetails) - sizeof(long), 4, 0) != -1){
                printf("message of type 4 recd!\n");
            }
            sleep(30);
            planeDetails.messageType = 5;
            if (msgsnd(msgQueue, &planeDetails, sizeof(PlaneDetails) - sizeof(long), 0) == -1) {
                perror("Failed to send the message");
                exit(1);
            }
            printf("message of type 5 sent!\n");

            arrivalArgs.planeDetails = planeDetails;

            if (pthread_create(&threadArrival, NULL, handleArrival, (void*)&arrivalArgs) != 0) {
                perror("Failed to create arrival thread");
                exit(1);
            }

            if (pthread_join(threadArrival, NULL) != 0) {
                perror("Failed to join arrival thread");
                exit(1);
            }
        }

    }
    
    pthread_mutex_unlock(&mutex2);
    pthread_mutex_destroy(&mutex);
    pthread_mutex_destroy(&mutex2);
    sem_destroy(&semaphore);
}

int main() {
    PlaneDetails planeDetails;
    int airportNumber, numberOfRunways;
    int loadCapacities[MAX_RUNWAYS];

    printf("Enter Airport Number (between 1 and 10): ");
    scanf("%d", &airportNumber);

    printf("Enter number of Runways (must be an even number): ");
    scanf("%d", &numberOfRunways);

    if (numberOfRunways % 2 != 0) {
        fprintf(stderr, "Number of runways must be an even number\n");
        exit(1);
    }

    if (numberOfRunways < 1 || numberOfRunways > MAX_RUNWAYS) {
        fprintf(stderr, "Number of runways must be between 1 and %d\n", MAX_RUNWAYS);
        exit(1);
    }

    if (airportNumber < 1 || airportNumber > MAX_AIRPORTS) {
        fprintf(stderr, "Airport number must be between 1 and %d\n", MAX_AIRPORTS);
        exit(1);
    }

    printf("Enter loadCapacity of Runways (give as a space-separated list in a single line): ");
    for (int i = 0; i < numberOfRunways; i++) {
        scanf("%d", &loadCapacities[i]);
        if (loadCapacities[i] < 1000 || loadCapacities[i] > 12000) {
            fprintf(stderr, "Load capacity must be between 1000 and 12000\n");
            exit(1);
        }
    }

    // Get the message queue
    key_t key = ftok(KEY_PATH, 'Z');
    if (key == -1) {
        perror("Failed to generate IPC key");
        exit(1);
    }

    int msgQueue = msgget(key, 0666);
    if (msgQueue == -1) {
        perror("Failed to get the message queue");
        exit(1);
    }

    while(1){
        if (msgrcv(msgQueue, &planeDetails, sizeof(PlaneDetails) - sizeof(long), 997, IPC_NOWAIT) != -1) {
            printf("termination signal recd!");
            break;
        }
        createAirportProcess(airportNumber, numberOfRunways, loadCapacities, msgQueue);
    }

    return 0;
}
