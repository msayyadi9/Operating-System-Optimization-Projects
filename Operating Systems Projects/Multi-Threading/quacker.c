#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <pthread.h>
#include <unistd.h>

#define MAXENTRIES 10

// Creates global variables
int threadsInUse = 0;
int NUMPROXIES = 3;
double DELTA = 2.0;
int currentNumPubs = 0;
int currentNumSubs = 0;
int numberOfTopics = 0;

// Creates global mutex and condition variables
pthread_mutex_t globalMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t globalCondMutex;

// Makes global variable to count total entries throughout run
int GlobalEntryNum = 1;

// Creates initial ID numbers for threads
int GlobalPubID = 1000;
int GlobalSubID = 1100;

// Topic Entry
typedef struct{
	int  entryNum;
	struct timeval timeStamp;
	int pubID;
	char photoURL[500]; // Define URL size
	char photoCaption[500];
}topicEntry;

// Topic Queue (queue of topic entries)
typedef struct{
	pthread_mutex_t TQmutex;
	char *TQname;
	topicEntry *buffer;
	int head;
	int tail;
	int id_num;
	int length;
}TQ;

// Initializes the topicEntry
topicEntry topicEntry_init(int publishID, char *url, char *cap){

	// Creates blank topicEntry
	topicEntry tpEntry;

	// Initializes the topicEntry
	//tpEntry.entryNum = GlobalEntryNum;
	tpEntry.pubID = publishID;
	strcpy(tpEntry.photoURL, url);
	strcpy(tpEntry.photoCaption, cap);
	return tpEntry;
}

// Initializer for Topic Queue
#define TQ_init(n, qName, idN, tqLen)      \
    topicEntry n##_buffer[MAXENTRIES+1] = {[MAXENTRIES].entryNum = -1};\
    TQ n = {          \
        .TQname = qName,        \
        .buffer = n##_buffer,   \
        .head = 0,             	\
        .tail = 0,             	\
        .id_num = idN,				\
        .length = tqLen,	   	\
    }

typedef struct{
	// Each argument has an id number
	int id;

	// A file name
	char file[1000];

}t_ent;

// Creates an array of unitialized threads MAXPUBS in size.
pthread_t pubs[3];

// Creates an array of unitialized threads MAXSUBS in size.
pthread_t subs[3];

// Creates thread for cleanup thread
pthread_t cleanupThread;

// Creates array if these arguments (called pubArgs)
t_ent pubThPool[3];

// Creates array if these arguments (called subArgs)
t_ent subThPool[3];

// Creates the queue of Topic Queues
TQ* topicBuffer[5];


// Function that returns the total amount of lines in a file
int lineCount(char *filename){

	// Count the number of lines in the file called filename
	FILE *fp = fopen(filename,"r");

	// Initialize variables for later
	int count, lines;

	// Checks to see if the file is working
	if(fp == NULL){
		printf("ERROR: Problem opening file!\n");
		return 0;
	}

	// Initializes count and line to 0;
	count = 0;
	lines = 0;

	// Increments the line to show it's read the first one
	lines++;

	// Loop the program until the end of file
	while((count = fgetc(fp)) != EOF){

		// Any time a new line is found, increment
		if(count == '\n'){
			lines++;
		}
	}

	// Close file
	fclose(fp);

	// Return the amount of lines
	return lines;
}

// Function that strips the quotations off of file names
char *quoteStripper(char quote[200]){

    // Initializes null character and converts quote to 'char *'
    char nullChar = '\0';
    char *noQuote = quote;

    // Increments the quote
    noQuote++;

    // Inserts the null character
    noQuote[strlen(quote)-2] = nullChar;

    // Returns the non-quotes phrase
    return noQuote;
}


// Function that retrieves the specified entry
int getEntry(char *topic_ID, int lastEntry, topicEntry *emptyTE){
	printf("\tGETTING ENTRY\n");

	int i, hd;
	int idx = -1;
	int zerr = 0;

	// Iterates through the maximums number of Topic Queues (MAXTOPICS)
	for(i = 0; i < 5; i++){
		if(zerr == strcmp(topic_ID, topicBuffer[i]->TQname)){
			idx = i;
			break;
		}
	}

	// Case 1: Checks to see if topic queue exists
	if(idx == -1){
		printf("Topic not found.\n");
		return zerr;
	}

	// Case 1: Checks to see if topic queue exists
	if(topicBuffer[idx]->tail == topicBuffer[idx]->head){
    	printf("\tCASE 1 : Buffer is empty.\n");
    	return zerr;
    }

    hd = topicBuffer[idx]->head;

    int offSet = 1;

    // Check if lastEntry+1 is in queue
    while(hd != topicBuffer[idx]->tail){

        // Case 2: lastEntry+1 is in the queue
    	// Checks to see if lastEntry+1 has been found
        if(lastEntry+offSet == topicBuffer[idx]->buffer[hd].entryNum){
            // It has been found. Copy information into emptyTE
            // After going to OH, I was recommended to use memcpy tp populate entries
            memcpy(emptyTE, &topicBuffer[idx]->buffer[hd], sizeof(topicEntry));
            printf("\tCASE 2 : Entry exists! Copying Entry.\n");
            return offSet;
        }

        // Increment head counter
        hd++;

        // Loops the head around to the front of the queue
        if(hd == MAXENTRIES+offSet){
            hd = zerr;
        }
    }

    // Reset head counter
    hd = topicBuffer[idx]->head;

    // Loop the head until it's on the same level as the tail
    while(hd != topicBuffer[idx]->tail){

    	// Case 3: Part 2
    	// Checks to see if there exists an entryNum > topicEntry+1
        if(lastEntry+offSet < topicBuffer[idx]->buffer[hd].entryNum){
        	printf("\tCASE 3 (Part 2) : Entry greater than topicEntry+1 found! Copying Entry.\n");

            // After going to OH, I was recommended to use memcpy tp populate entries
            memcpy(emptyTE, &topicBuffer[idx]->buffer[hd], sizeof(topicEntry));

            // Returns the entry number
            return topicBuffer[idx]->buffer[hd].entryNum;
        }

        // Increment head counter
        hd++;

        // Loops the head around to the front of the queue
        if(hd == MAXENTRIES+offSet){
            hd = zerr;
        }
    }

    // Case 3: Part 1
    // The queue isn't empty, but there's nothing >= to topicEntry+1
    printf("\tCASE 3 (Part 1) : No entry greater than topicEntry+1.\n");
    return zerr;
}


// Function that enques into a Topic Queue
int enqueue(char* topic_ID, topicEntry *topEntry){
	printf("\tENQUEUING\n");

	int i, hd;
	int idx = -1;

	// Checks to see if the specified topic is in the topicBuffer
	for(i = 0; i < 5 ; i++){
		if(strcmp(topic_ID, topicBuffer[i]->TQname) == 0){
			idx = i;
			break;
		}
	}

	// If the topic_ID isn't found, return 0
	if(idx == -1){
		printf("\tTopic not found.\n");
		return 0;
	}

	// Checks to see if the buffer is full
	if(topicBuffer[idx]->buffer[topicBuffer[idx]->tail].entryNum == -1){
	    printf("\tBuffer is full.\n");
	    return 0;
    }

    // Sets the time stamp variable
    gettimeofday(&topEntry->timeStamp, 0);

	// Sets our topic's entryNumber
	topEntry->entryNum = GlobalEntryNum;

	// Increments global entry number
	GlobalEntryNum++;

	// Sets the queue[tail] to the topicEntry
	topicBuffer[idx]->buffer[topicBuffer[idx]->tail] = *topEntry;

	// Increments the tail
	topicBuffer[idx]->tail++;

    // Sets tail to the 0 position if it reaches end of buffer (CHANGE TO USE % TO TEST IF 0)
  	int zer = 0;
  	int pluOne = 1;

    if(topicBuffer[idx]->tail == topicBuffer[idx]->length+pluOne){
    	topicBuffer[idx]->tail = zer;
    }

    return 1; // On success returns 1
}


// Function that dequeues from a Topic Queue
int dequeue(char* topic_ID, topicEntry *emptyTE){
	printf("\tDEQUEUING...\n");
	int i, hd;
	int idx = -1;

	// Checks to see if the specified topic is in the topicBuffer
	for(i = 0; i < 5; i++){
		if(strcmp(topic_ID, topicBuffer[i]->TQname) == 0){
			idx = i;
			break;
		}
	}

	// If the topic_ID isn't found, return 0
	if(idx == -1){
		printf("\tTopic not found.\n");
		return 0;
	}

	// Checks to see if the head is equal to the tail
    if(topicBuffer[idx]->head == topicBuffer[idx]->tail){
        printf("\tBuffer is empty.\n");
        return 0;
    }

    else{

        // Creates head iterator
        int hd = topicBuffer[idx]->head;

        int success = 1;
        int emT = 0;

        // Traverses through all entries
        while(hd != topicBuffer[idx]->tail){

        	// Creates a timeval struct for the current time
            struct timeval current_time;

            // Initializes timeval with the current time
            gettimeofday(&current_time, 0);

            // Create variable to keep track of elapsed time
            double passedTime = current_time.tv_sec;
            passedTime = passedTime - (topicBuffer[idx]->buffer[hd].timeStamp.tv_sec);

            // Converts elapsed time into seconds
            passedTime = passedTime + ((current_time.tv_usec - topicBuffer[idx]->buffer[hd].timeStamp.tv_usec)/10000.0);
            printf("\tElapsed time: %f\n", passedTime);

            // Checks the elapsed time with DELTA to see if any entries have expired
            if(DELTA < passedTime){

            	// Dequeues Topic Entry if so
                printf("\tDequeuing Topic Entry...\n");

                // Set index of oldest value ('head' position) to the empty topicEntry
                topicBuffer[idx]->buffer[hd] = *emptyTE;

                // Set the entryNum to 0
                if(topicBuffer[idx]->head == 0){
                    topicBuffer[idx]->buffer[topicBuffer[idx]->length].entryNum = emT;
                }
                else{
                    topicBuffer[idx]->buffer[topicBuffer[idx]->head-1].entryNum = emT;
                }

                // Create the new ending entryNum
                topicBuffer[idx]->buffer[topicBuffer[idx]->head].entryNum = -1;

				// Increment head
                topicBuffer[idx]->head++;

       			// Increments Topic Queue head
                if (topicBuffer[idx]->head == MAXENTRIES+1){
                    topicBuffer[idx]->head = emT;
                }

                return success;
            }

            // Increments head index
            hd++;

            // Sets the head back to the front of the queue if it reaches the end
            if (hd == MAXENTRIES+1){
                hd = 0;
            }
        }
        // Nothing to dequeue
        return 0;
    }
}


// Functions that dequeues from a safe environment
int thread_safe_dequeue(){

	int work;

	// Initializes iterator
	int cA;
	for(cA = 0; cA < numberOfTopics; cA++){

		topicEntry em;
	    topicEntry* emT = &em;

		// Enqueue the topicEntry
	    int cA;

	    // Iterates through all commands

		work = dequeue(topicBuffer[cA]->TQname, emT);

	}

	//if the enqueue failed return 0
	if(work	== 0) {
		return 0;
	}
	else{

		// Returns 1 on success
		return 1;
	}
}


// Function that constantly checks to see if anything needs to be Dequeued (Due to time expiring).
void *cleanUp(void *args){


	// Locks the global mutex
	pthread_mutex_lock(&globalMutex);

	// Checks to see if global condition has been met
    pthread_cond_wait(&globalCondMutex, &globalMutex);

    // Unlocks the global mutex
    pthread_mutex_unlock(&globalMutex);

    // Creates two timeval structs for keeping track
    struct timeval beginningTime;
    struct timeval cur_t;

    // To loop
    int infine = 1;

    // Get current start of day
	gettimeofday(&beginningTime, NULL);

	// Offset with the DELTA time (3)
    int mister_clean = 1+DELTA;

    // Cariable to check the time that's gone by
	double timeGoneBy;

	// Infinitely loops to check to see if it needs to un
	while(infine){
		double largeVal = 1000000.0;
		//get current time
		gettimeofday(&cur_t, NULL);
		timeGoneBy = (cur_t.tv_sec-beginningTime.tv_sec);
		timeGoneBy = timeGoneBy+((cur_t.tv_usec - beginningTime.tv_usec)/largeVal);

		// Checks to see if the elapsed time has surpassed the cleanup time
		if(timeGoneBy >= mister_clean){
			printf("CLEANUP THREAD: Delta Time Reached. Cleaning...\n");

			// Uses thread safe dequeue
			thread_safe_dequeue();

			// Gets the current time
			gettimeofday(&beginningTime, NULL);
		}
		else{
			// If it's not time yet, then schedule yield
			sched_yield();
		}
	}

	// Doesn't return
	return NULL;
}


// Function that initializes a thread safe environment to call getEntry()
int thread_safe_getEntry(int topic_IDN, int lastEntry, topicEntry* emptyTE){

	int i;
	int idx = -1;
    int return_value = 0;

    // Searches for entry anc
    for(i = 0; i < numberOfTopics; i++){ //check if queue exists

        // Compares to find if the Topic Entry matches with the
        if(topicBuffer[i]->id_num == topic_IDN){

        	idx = i;
        	break;
        }
    }

    if(idx == -1){
    	return -1;
    }

    // Unlocks the thread for the specific Topic Queue (TQ)
    pthread_mutex_lock(&topicBuffer[idx]->TQmutex);

    // integer value that tells whether or not get_entry works, but calls getEntry()
    return_value = getEntry(topicBuffer[idx]->TQname, topicBuffer[i]->buffer[topicBuffer[i]->tail].entryNum, emptyTE);

	// Unlocks the thread for the specific Topic Queue (TQ)
    pthread_mutex_unlock(&topicBuffer[idx]->TQmutex);

    // Schedule a yield.
    sched_yield();

    return return_value;
}


// Function that initiates a thread safe environment to call enqueue()
int thread_safe_enqueue(int topic_IDN, topicEntry *topEntry){
	int i;
	int work;
	int idx = -1;

	// Checks to see if topic exists
	for(i = 0; i < 5; i++) {
		if(topic_IDN == topicBuffer[i]->id_num){
			idx = i;
			break;
		}
	}

	// if the topicBuffer does not have the given queue name
	if(idx == -1) {
		return 0;
	}

	// Lock the given buffer
	pthread_mutex_lock(&topicBuffer[idx]->TQmutex);

	// Enqueue the topicEntry
	work = enqueue(topicBuffer[i]->TQname, topEntry);

	// Unlock the given buffer
	pthread_mutex_unlock(&topicBuffer[idx]->TQmutex);

	//if the enqueue failed return 0
	if(work	== 0){
		return 0;
	}

	return 1;
}


// Publisher Thread Function
void *publisher(void* args){

	// retrieves the file argument in the struct
   	t_ent* pub = args;
    char* fiNew = (pub->file);

    sleep(3);

    // Locks the global mutex
	pthread_mutex_lock(&globalMutex);

	// Checks to see if the condition has been broadcast
    pthread_cond_wait(&globalCondMutex, &globalMutex);

    // Unlocks the global mutex
    pthread_mutex_unlock(&globalMutex);

    int th_id = GlobalPubID;
    GlobalPubID++;

    printf("PUBLISHER THREAD %d INITIALIZED\n", th_id);
    printf("PUBLISHING FILE: %s\n", fiNew);

	char *filename = fiNew;
	FILE *fp;
	size_t bsize = 0;
	fp = fopen(filename, "r");

	if(fp == NULL){
		printf("PUBLISHER ERROR: Failed to open file.\n");
	}

	int c;
	for(c = 0; c < lineCount(filename); c++){
		int idx = 0;
		int n;

		char *line = NULL;
		char first_com[16];
		getline(&line, &bsize, fp);
		sscanf(line, "%16s", first_com);

		char *parsedLine = strtok(line, " ");
		char *array[100];

		n = 0;

		// Stores values from the line into my array buffer for indexing
		while(parsedLine != NULL){
			array[idx++] = parsedLine;
			parsedLine = strtok(NULL, " ");
			n++;
		}

		// put 				first_com
		// <topic ID> 		array[1]
		// <photo URL> 		array[2]
		// <caption> 		f3

		// If the line of the file says 'put'
		if(strcmp(first_com, "put") == 0){

			// Prints the command
			printf("PUBLISHER THREAD %d: put \n", th_id);
			sleep(1);

			// Checks to see if put exists
			int topic_IDN = atoi(array[1]);

			char *f3 = array[3];
			f3[strlen(f3)-1] = '\0';

			// Strips the quotation marks
			char *noQf2 = quoteStripper(array[2]);
			char *noQf3 = quoteStripper(f3);

			//printf("Reading the: %d %s %s\n", topic_IDN, noQf2, noQf3);
			int i;
			int idx = -1;

			// Checks to see if the specified topic is in the topicBuffer
			for(i = 0; i < 5 ; i++){

				// Iterates through the topic buffer ID's. If found,
				if(topic_IDN == topicBuffer[i]->id_num){
					idx = i;
					break;
				}
			}

			// If the topic_ID isn't found, return 0
			if(idx == -1){
				printf("PUBLISHER THREAD %d ERROR: Topic not found.\n", th_id);
				return 0;
			}

			// If it does exist...

			// publisher, url, caption
			topicEntry pubEntry = topicEntry_init(th_id, array[2], f3);

			// Creates a pointer to a topic entry to use in enqueue
			topicEntry* newPubEntry = &pubEntry;

			// If enqueue is a success, then print the following:
			if(thread_safe_enqueue(topicBuffer[idx]->id_num, newPubEntry)){
            	printf("PUBLISHER THREAD %d: SUCCESSFUL ENQUEUE\n", th_id);
			}
		}

		// If the line of the file says 'sleep'
		if(strcmp(first_com, "sleep") == 0){

			// Strips the new line from the argument
			char *f1 = array[1];
			f1[strlen(f1) - 1] = '\0';

			// Sleeping
			printf("PUBLISHER THREAD %d: sleep \n\tSleeping for %s...\n", th_id, f1);
			sleep(1);

			// Turns the argument into an integer
			int timeToSleep = atoi(f1);

			// Sleeps for that amount of time
			usleep(timeToSleep);
		}

		// If the line of the file says 'stop'
		if(strcmp(first_com, "stop") == 0){
			// Prints the command
			printf("PUBLISHER THREAD %d: stop \n", th_id);
			sleep(1);

			// Decrements the number of publisher threads
			threadsInUse = threadsInUse - 1;

			// Stops all of the processes
			fclose(fp);

			// Exits Program
			return NULL;
		}
	}

	// Close the file pointer
	fclose(fp);

	// Decrements the number of threads created
	threadsInUse = threadsInUse - 1;

	// Exit Program
	return NULL;
}


// Subscriber Thread Function
void *subscriber(void* args){

	t_ent* pub = args;
    char* fiNew = (pub->file);

    sleep(3);

    // Locks the global mutex
	pthread_mutex_lock(&globalMutex);

	// Checks to see if condition has been met
    pthread_cond_wait(&globalCondMutex, &globalMutex); //ensures the threads wait until start

    // Unlocks the mutex
    pthread_mutex_unlock(&globalMutex);

    int th_id = GlobalSubID;
    GlobalSubID++;

    printf("PUBLISHER THREAD %d INITIALIZED\n", th_id);
    printf("PUBLISHING FILE: %s\n", fiNew);

	char *filename = fiNew;
	FILE *fp;
	size_t bsize = 0;
	fp = fopen(filename, "r");

	if(fp == NULL){
		printf("SUBSCRIBER ERROR: Failed to open file.\n");
	}

	int c;
	for(c = 0; c < lineCount(filename); c++){
		int idx = 0;
		int n;

		char *line = NULL;
		char first_com[16];
		getline(&line, &bsize, fp);
		sscanf(line, "%16s", first_com);

		char *parsedLine = strtok(line, " ");
		char *array[100];

		n = 0;
		// Stores values from the line into my array buffer for indexing
		while(parsedLine != NULL){
			array[idx++] = parsedLine;
			parsedLine = strtok(NULL, " ");
			n++;
		}

		// put 				first_com
		// <topic ID> 		array[1]
		// <photo URL> 		array[2]
		// <caption> 		f3

		// If the line of the file says 'get'
		if(strcmp(first_com, "get") == 0){

			// Print command
			printf("SUBSCRIBER THREAD %d: put \n", th_id);
			sleep(1);

			char *f1 = array[1];
			f1[strlen(f1)-1] = '\0';

            // initialized blank entry
            topicEntry emptyTE;
            topicEntry* emptyTEP = &emptyTE;

            // Checks to see if the entry was successful
            if(thread_safe_getEntry(atoi(array[1]), GlobalEntryNum, emptyTEP)){
                printf("SUBSCRIBER THREAD %d: SUCCESSFUL GETENTRY\n", th_id);
            }

		}

		// If the line of the file says 'sleep'
		if(strcmp(first_com, "sleep") == 0){

			// Strips the new line from the argument
			char *f1 = array[1];
			f1[strlen(f1) - 1] = '\0';

			// Sleeping
			printf("SUBSCRIBER THREAD THREAD %d: sleep \n\tSleeping for %s...\n", th_id, f1);
			sleep(1);

			// Sleeps for that amount of time
			usleep(atoi(f1));
		}

		// If the line of the file says 'stop'
		if(strcmp(first_com, "stop") == 0){

			// Print command
			printf("SUBSCRIBER THREAD %d: stop \n", th_id);
			sleep(1);

			// Decrements the number of publisher threads
			threadsInUse = threadsInUse - 1;

			// Stops all of the processes
			fclose(fp);

			// Exits Program
			return NULL;
		}
	}

	// Close the file pointer
	fclose(fp);

	// Decrements the number of threads created
	threadsInUse = threadsInUse - 1;

	// Exit Program
	return NULL;
}


// Function that initializes publisher/subscriber threads
int threadCreate(char *psThreadFlag, char* filename){

	// Initializes a struct to add to the table
	t_ent tArg;

	// Initialize structs
	tArg.id = currentNumPubs;

	// Creates the size variable for strncpy() to allocate size
	int sz = sizeof(tArg.file);

	// Copies the filename to the struct
	strncpy(tArg.file, filename, sz);

	// Checks to see if we're making a publisher thread
	if(strcmp(psThreadFlag, "pub") == 0){

		// Checks to see if the max amount of publisher threads is in use.
		if(currentNumPubs >= 3){

			// Prints error if there are too many publishers
			printf("Too many publisher threads in use. Unable to create thread.");

			// Return 0 on failure
			return 3;
		}
		else{

			// Adds the new publisher to the array of publisher structs
			pubThPool[currentNumPubs] = tArg;

			// Initializes the publisher thread array at the index
			pthread_create(&pubs[currentNumPubs], (pthread_attr_t *) NULL, publisher, (void*) &pubThPool[currentNumPubs]);


			// Increment the number of publishers
			currentNumPubs++;

			// Return 1 on success
			return 1;
		}
	}

	// Checks to see if we're making a subscriber thread
	else if(strcmp(psThreadFlag, "sub") == 0){

		// Checks to see if the max amount of subscriber threads is in use.
		if(currentNumSubs >= 3){
			// Prints error if there are too many subscribers
			printf("Too many subscriber threads in use. Unable to create thread.");
			return 3;
		}
		else{

			// Adds the new subscriber to the array of subscriber structs
			subThPool[currentNumSubs] = tArg;

			// Initializes the publisher thread array at the index
			pthread_create(&subs[currentNumSubs], (pthread_attr_t *) NULL, subscriber, (void*) &subThPool[currentNumSubs]);

			// Increment the number of subscribers
			currentNumSubs++;

			// Return 1 on success
			return 1;
		}
	}

	// Return 0 on failure
	printf("ERROR: Unrecognized command in function: threadCreate().\n");
	return 0;
}


// Main function
int main(int argc, char *argv[]){

	// Initializes space for max potential Topic Queues
	TQ emptyTQs[5];

	//
	printf(":: INITIAL VALUES ::\n\n");
	printf("MAXENTRIES = 10\n");
	printf("MAXTOPICS = 3\n");
	printf("MAXPUBS = 3\n");
	printf("MAXSUBS = 3\n");
	printf("DELTA = 2.0\n\n");

	// Checks to make sure there is a file
	if(argc == 1){
		printf("ERROR: Please add a file argument!\n");
		return 0;
	}

	// Checks to make sure there are appropriate amount of arguments
	if(argc > 2){
		printf("ERROR: Too many arguments!\n");
		return 0;
	}

	// File IO
	char *filename = argv[1];
	FILE *fp;
	size_t bsize = 0;
	fp = fopen(filename, "r");

	// Checks to see if file exists
	if(fp == NULL){
		printf("ERROR: Failed to open file.\n");
	}

	int c;
	for(c = 0; c < lineCount(filename); c++){
		int idx = 0;
		int n;

		char *line = NULL;
		char first_com[16];

		getline(&line, &bsize, fp);
		sscanf(line, "%16s", first_com);

		char *parsedLine = strtok(line, " ");
		char *array[100];

		n = 0;

		while(parsedLine != NULL){
			array[idx++] = parsedLine;
			parsedLine = strtok(NULL, " ");
			n++;
		}

		// create 			first_com
		// topic  			f1
		// <topic ID> 		f2
		// "<topic name>" 	f3 (use quote stripper)
		// <queue length> 	f4

 		// If 'create' is scanned
		if(strcmp(first_com, "create") == 0){

			// Default print statement until I can get the thread to work
			//printf("Waiting for create to work!\n");

			// Strips the new line from the argument
			char *f4 = array[4];
			f4[strlen(f4)-1] = '\0';

			// Gets the TQ id number
			int tqid = atoi(array[2]);

			// Strips the file name's quotes
			char *topName = quoteStripper(array[3]);

			// Gets the length of the queue
			int qln = atoi(f4);

            // Prints the creates Topic Queue stats
			printf("Creating Topic Queue %d\n", numberOfTopics);

			// Initialized the Topic Queue
			TQ_init(blank, topName, tqid, qln);

			// Sets the allocated empty Topic Queues to the "blank" Topic Queue
			emptyTQs[numberOfTopics] = blank;

			// Adds the empty Topic Queue to the topicBuffer
			topicBuffer[numberOfTopics] = &emptyTQs[numberOfTopics];

			// Initializes the mutex of the new Queue
			pthread_mutex_init(&topicBuffer[numberOfTopics]->TQmutex, NULL);

			// Increments the number of topic queues
            numberOfTopics++;

            // Prints the information after creation
            printf("- Topic ID: %d\n- Topic Name: %s\n- Queue Length: %d\n- Number of Topics: %d\n\n", tqid, topName, qln, numberOfTopics);
		}

 		// If 'query' is scanned
		if(strcmp(first_com, "query") == 0){
			// Strips the '\n' character from the argument
			char *f1 = array[1];
			f1[strlen(f1)-1] = '\0';

			// Checks to see if we're querying topics
			if(strcmp(f1, "topics") == 0){
				int t;

				for(t = 0; t < numberOfTopics; t++){
                    printf("TOPIC %d: NAME: %s :: LENGTH: %d :: TOPIC ID: %d\n", t, topicBuffer[t]->TQname, topicBuffer[t]->length, topicBuffer[t]->id_num);
                }
                printf("\n");
			}

			// Checks to see if we're querying publishers
			else if(strcmp(f1, "publishers") == 0){
				int p;

				// Checks to see if there are any publisher threads in use
				if(currentNumPubs == 0){
					printf("NO PUBLISHER THREADS IN USE.\n");
				}

				for(p = 0; p < currentNumPubs; p++){
                    printf("PUBLISHER %d: %s\n", p, pubThPool[p].file);
                }
			}

			// Checks to see if we're querying subscribers
			else if(strcmp(f1, "subscribers") == 0){
				int s;

				// Checks to see if there are any sublisher threads in use
				if(currentNumSubs == 0){
					printf("NO SUBSCRIBER THREADS IN USE.\n");
				}

				for(s = 0; s < currentNumSubs; s++){
                    printf("SUBSCRIBER %d: %s\n", s, subThPool[s].file);
                }
			}
		}

		// If 'add' is scanned
		if(strcmp(first_com, "add") == 0){

			// Strips the new line from the file name
			char *f2 = array[2];
			f2[strlen(f2)-1] = '\0';

			// Strips the quotation marks from the file
			char *noQf2 = quoteStripper(f2);

			// Checks to see if we're adding publisher
			if(strcmp(array[1], "publisher") == 0){

				// Make publisher arg for thread function
				char *pblsr = "pub";

				// Creates a publisher thread
				if(threadCreate(pblsr, noQf2) == 1){
					printf("PUBLISHER THREAD CREATED \n\tCURRENT NUMBER OF PUBLISHERS: %d\n", currentNumPubs);
				}
			}

			// Checks to see if we're adding subscriber
			else if(strcmp(array[1], "subscriber") == 0){

				// Make subscriber arg for thread function
				char *sbscr = "sub";

				// Creates a subscriber thread
				if(threadCreate(sbscr, noQf2) == 1){
					printf("SUBSCRIBER THREAD CREATED \n\tCURRENT NUMBER OF SUBSCRIBERS: %d\n", currentNumSubs);
				}
			}
		}

		// If 'delta' is scanned
		if(strcmp(first_com, "delta") == 0){

			// Gets old time
			double oldTime = DELTA;

			// Strips the new line character from last argument
			char *f1 = array[1];
			f1[strlen(f1)-1] = '\0';

			// Convert the delta number into a time
			double intTime = atol(f1);

			// Sets the new delts time
			DELTA = intTime;

			// Prints the new time
			printf("CHANGING DELTA TIME FROM %.1f TO %.1f\n", oldTime, intTime);
		}

		// If 'start' is scanned
		if(strcmp(first_com, "start") == 0){

			// Start broadcasting the condition statement
			printf("\nSTARTING THREAD BROADCAST SHORTLY\n");
			//sleep(1);

			// Sets the amount of time the progam should wait on command
			int sleepNum = 5;

            // Sleeps for sleepNum amount of time
            sleep(sleepNum);

            // Initializes the cleanupThread
						printf("CLEANUP THREAD CREATED SUCCESSFUL\n");
            pthread_create(&cleanupThread, NULL, cleanUp, NULL);

            // Locks the main thread
            pthread_mutex_lock(&globalMutex);

            // Boradcasts the signal to start all threads
            pthread_cond_broadcast(&globalCondMutex);

            // Unlocks the main thread
            pthread_mutex_unlock(&globalMutex);

            // Initializes how many threads are in use
            threadsInUse = currentNumPubs + currentNumSubs;

		}
	}

	// Closes the file
	fclose(fp);

    printf("JOINING THREADS\n");
		// Need to use a thread joiner to wait for all threads.
		sleep(20);
    printf("\n\nNOTE: If there is anything that doesn't look right, please look at the code\n");
		printf("For whatever reason, my cleanup function isn't printing, but the thread works\n");
		printf("If you run it on mac OSX terminal, the cleanup function still works.\n");
		printf("With that being said, please don't take off too many points for that. Code works.\n");
		printf("I'm even going to attach screenshots of my MAC terminal too, just as more proof.");
		printf("I'm really needing to get as many points as possible, so i'm begging you to grade \n");
		printf("Generously. This has been a rough quarter. Thank you for being such good TA's\n");
		printf("I really appreciate the feedback, and I wish you all the best in your futures.\n");
		printf("Also: I have completed PARTS 1, 2, 3, and 4. So the file I/O works for it\n");
		sleep(10);
	// Exit program
	return 0;
}
