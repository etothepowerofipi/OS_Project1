#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#include <semaphore.h>
#include <unistd.h>

// #include <sys/memory.h>
#include <sys/ipc.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/sem.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <fcntl.h> 
#include <ctype.h>

//Ένα πολύ απλό macro για error handling
#define ASSERT(call) ({                           \
    int code = call; \
    if (code == -1) {      \
        perror("Error");    \
        exit(code);             \
    }                         \
})

// #define CALL_BF(call)       \
// {                           \
//   BF_ErrorCode code = call; \
//   if (code != BF_OK) {      \
//     BF_PrintError(code);    \
//     return BF_ERROR;        \
//   }                         \
// }



#define LINE_SIZE 10
#define MIN_LINES 1000
#define SOURCE_FOLDER "source/"

int getLines(char*);
char* getSegment(char*,int,const int);
FILE* createFile(const int childNumber);
int generateRequest(int lastRequest, int SEGMENTS);
void recordRequest(FILE* fp, char* segment, int lineNumber);

int main(int argc, char* argv[]){
    if (argc < 3) { 
        fprintf(stderr, "Usage: %s <sourceFile> <segment_size>\n", argv[0]); 
        return 1; 
    }
    char sourceFile[100];
    strcpy(sourceFile,SOURCE_FOLDER);
    strcat(sourceFile,argv[1]);
    printf("sourcefile is %s\n",sourceFile);

    const int LINES_PER_SEGMENT = atoi(argv[2]);
    const int REQUESTS_PER_CHILD = 10;
    const int TOTAL_LINES = getLines(sourceFile);
    const int SEGMENTS = TOTAL_LINES / LINES_PER_SEGMENT;
    const int CHILDREN = 2;

    if (TOTAL_LINES == -1){
        exit(1);
    }
    else if (TOTAL_LINES < MIN_LINES){
        fprintf(stderr,"Error: %s does not have enough lines",sourceFile);
        exit(1);
    }

    const char* mempath = "/shared_memory";
    int memory_fileDesc = shm_open(mempath,O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
    if (memory_fileDesc == -1){
        perror("shmopen");
        exit(1);
    }

    unsigned size = 0;
    /*Read mutex, Write mutex*/
    size += 2*sizeof(sem_t);

    /*readCount, currentSegment, requestedSegment*/
    size += 3*sizeof(int);

    /*SemaphoreArray*/
    size += SEGMENTS*sizeof(sem_t);

    /*segment*/
    size += LINES_PER_SEGMENT*LINE_SIZE*sizeof(char*);

    ASSERT(ftruncate(memory_fileDesc,size));

    char* mem = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, memory_fileDesc, 0);
    if (mem == MAP_FAILED){
        perror("mmap failed");
        exit(1);
    }

    char* segment = mem;
    sem_t* semArray = (sem_t*)(mem + LINES_PER_SEGMENT*LINE_SIZE*sizeof(char*));
    sem_t* read = (sem_t*)(mem + LINES_PER_SEGMENT*LINE_SIZE*sizeof(char*) + SEGMENTS*sizeof(sem_t));
    sem_t* write = (sem_t*)(mem + LINES_PER_SEGMENT*LINE_SIZE*sizeof(char*) + SEGMENTS*sizeof(sem_t) + sizeof(sem_t));
    int* readCount = (int*)(mem + LINES_PER_SEGMENT*LINE_SIZE*sizeof(char*) + SEGMENTS*sizeof(sem_t) + 2*sizeof(sem_t));
    int* currentSegment = (int*)(mem + LINES_PER_SEGMENT*LINE_SIZE*sizeof(char*) + SEGMENTS*sizeof(sem_t) + 2*sizeof(sem_t) + sizeof(int));
    int* requestedSegment = (int*)(mem + LINES_PER_SEGMENT*LINE_SIZE*sizeof(char*) + SEGMENTS*sizeof(sem_t) + 2*sizeof(sem_t) + 2*sizeof(int));



    // int* readCount = (int*)(mem + 2*sizeof(sem_t));
    // int* currentSegment = (int*)(mem + 2*sizeof(sem_t) + sizeof(int));
    // int* requestedSegment = (int*)(mem + 2*sizeof(sem_t) + 2*sizeof(int));
    // sem_t* semArray = (sem_t*)(mem + 2*sizeof(sem_t) + 3*sizeof(int));
    // char* segment = (char*)(mem + 2*sizeof(sem_t) + 3*sizeof(int) + SEGMENTS*sizeof(sem_t));

    /*Αρχικοποίηση των σημαφόρων*/
    ASSERT(sem_init(write,1,0));
    ASSERT(sem_init(read,1,1));
    for (int i=0; i<SEGMENTS; i++){
        printf("init #%d\n",i+1);
        ASSERT(sem_init(&semArray[i],1,0));
    }

    /*Αρχικοποίηση των άλλων μεταβλητών*/
    *readCount = 0;
    *currentSegment = 0;
    *requestedSegment = 0;



    /*Δημιουργία παιδιών*/
    int i=1;
    int id = fork();
    if (id==0) printf("Created new child with pid=%ld\n",(long)getpid());
    while (i < CHILDREN && id!=0){
        id = fork();
        if (id==0) printf("Created new child with pid=%ld\n",(long)getpid());
        i++;
    }


    if (id != 0){
        /*Στη γονική διεργασία*/

        // /*Περιμένει να ελευθερωθεί ο σημαφόρος*/
        // ASSERT(sem_wait(write));
        // printf("In parent process\n");

        // /*Άντληση segment που ζητήθηκε*/
        // char* newSegment = getSegment(*requestedSegment);

        // /*Αποθήκευση του νέου segment στην κοινή μνήμη*/
        // strcpy(segment,newSegment);
        // free(newSegment);

        // *currentSegment = *requestedSegment;

        // /*Απελευθέρωση του σημαφόρου εγγραφής*/
        // ASSERT(sem_post(write));

        // /*Απελευθερώνει όλους τους άλλους σημαφόρους*/
        // for (int i=0; i<SEGMENTS; i++){
        //     ASSERT(sem_post(&semArray[i]));
        // }
        int uniqueRequests = 0;

        for (int i=0; i<CHILDREN*REQUESTS_PER_CHILD; i++){
            ASSERT(sem_wait(write));
            printf("\nIn parent process\n");
            printf("requested segment is %d, current segment is %d\n",*requestedSegment,*currentSegment);
            printf("parent readCount = %d\n",*readCount);
            if (*readCount == 0){

                
                if (uniqueRequests>0)   {
                    printf("sem_waiting %d\n",*currentSegment);
                    ASSERT(sem_wait(&semArray[*currentSegment - 1]));
                    printf("sem_waited %d\n",*currentSegment);
                }

                /*Άντληση segment που ζητήθηκε*/
                char* newSegment = getSegment(sourceFile,*requestedSegment,LINES_PER_SEGMENT);
                uniqueRequests++;
                
                /*Αποθήκευση του νέου segment στην κοινή μνήμη*/
                strcpy(segment,newSegment);
                free(newSegment);

                *currentSegment = *requestedSegment;
            }
            printf("all is well, posting %d\n",*currentSegment);
            ASSERT(sem_post(&semArray[*currentSegment - 1]));
        }

    }
    else{
        /*Σε διεργασία-παιδί*/

        /*Δημιουργία αρχείου καταγραφής για το παιδί*/
        /*Χρησιμοποιείται ο αριθμός από τη while που χρησιμοποιήθηκε παραπάνω για τη δημιουργία του παιδιού*/
        FILE* out = createFile(i);
        if (out == NULL){
            printf("Child number %d couldn't create file\n",i);
            exit(1);
        }

        /*Μπλοκάρισμα του σημαφόρου, χρήσιμο αν είναι το πρώτο παιδί που κάνει request*/
        ASSERT(sem_wait(read));

        /*Έλεγχος για το αν έχει αρχικοποιηθεί η κοινή μνήμη με request*/
        bool initiated = *currentSegment != 0;
        int lastRequest = 0;
        int start = 0;
        if (!initiated){
            start = 1;
            int request = rand() % SEGMENTS + 1;
            int requestLine = rand() % LINES_PER_SEGMENT + 1;
            printf("shared memory not initiated. Child #%ld requesting segment #%d\n",(long)getpid(),request);

            /*Ο read και ο write είναι και οι δύο κλειδωμένοι, δεν υπάρχει κίνδυνος*/
            *requestedSegment = request;
            lastRequest = request;

            /*Ξεκλείδωμα του write για να διαχειριστεί το request*/
            ASSERT(sem_post(write));

            /*Μετά την εκτέλεση του αιτήματος, ο γονιός θα ξεκλειδώσει τον σημαφόρο του αντίστοιχου request.*/
            /*Έτσι μπλοκάρονται τα άλλα παιδιά του αντίστοιχου request, ενώ τα υπόλοιπα είναι ήδη μπλοκαρισμένα από τη δικιά τους sem_wait*/
            /*Ο γονιός είναι μπλοκαρισμένος από τη sem_wait(write) με την οποία ξεκινά η επανάληψή του*/
            ASSERT(sem_wait(&semArray[request - 1]));
            printf("child %d waited for %d\n",getpid(),request);
            *readCount = *readCount+1;
            /*Ξεκλείδωμα του σημαφόρου ώστε να χρησιμοποιηθεί από τα άλλα παιδιά που τον περιμένουν*/
            ASSERT(sem_post(&semArray[request - 1]));
            recordRequest(out,segment,requestLine);


        }
        ASSERT(sem_post(read));
        for (int i=start; i<REQUESTS_PER_CHILD; i++){
            int newRequest = generateRequest(lastRequest,SEGMENTS);
            if (lastRequest==2)printf("\tin FAULTY REQUEST\tin FAULTY REQUEST\tin FAULTY REQUEST\n");
            int requestLine = rand() % LINES_PER_SEGMENT + 1;
            printf("child %d waiting for read\n",getpid());
            ASSERT(sem_wait(read));
            printf("child:%d new request:%d last request:%d current segment:%d requested segment:%d\n",getpid(),newRequest,lastRequest,*currentSegment,*requestedSegment);
            if (*requestedSegment == *currentSegment){
                // printf("requestedSegment == currentSegment\n");
                *requestedSegment = newRequest;
            }
            if (newRequest != *currentSegment && lastRequest == *currentSegment){
                *readCount = *readCount-1;
                printf("\t\t\tDECREASED\t\t\t, readCount is now %d\n",*readCount);
            }
            ASSERT(sem_post(read));
            printf("child %d requesting segment #%d\n",getpid(),newRequest);
            ASSERT(sem_post(write));

            printf("child %d posted write\n",getpid());
            ASSERT(sem_wait(&semArray[newRequest-1]));
            printf("child %d waited for %d\n",getpid(),newRequest);
            if (lastRequest != *currentSegment && newRequest == *currentSegment) {
                *readCount = *readCount+1;
                printf("\t\t\tINCREASED\t\t\t, new readCount is %d from kid %d\n",*readCount,getpid());
            }
            if (newRequest != *currentSegment) printf("they are different\n");
            // *readCount += 1;
            lastRequest = newRequest;
            printf("child %d posting %d\n",getpid(),newRequest);
            ASSERT(sem_post(&semArray[newRequest-1]));
            printf("right before recordRequest\n");
            recordRequest(out,segment,requestLine);
            printf("right after recordRequest\n");
            
        }
        fclose(out);
    }
    if (id!=0) {
        printf("parent exiting\n");
    }
}


FILE* createFile(const int childNumber){
    char filename[100];
    char childID[10];
    sprintf(childID,"%d",childNumber);
    strcpy(filename,"output/record_");
    strcat(filename,childID);
    strcat(filename,".txt");
    return fopen(filename,"w");
}

int generateRequest(int lastRequest, int SEGMENTS){
    if (lastRequest == 0)
        return (rand() % SEGMENTS + 1);
    int newRequest = lastRequest;
    int chance = rand() % 10 + 1;
    bool isSame = chance <= 7;
    if (!isSame){
        newRequest = rand() % SEGMENTS + 1;
        while (newRequest==lastRequest)
            newRequest = rand() % SEGMENTS + 1;        
    }
    return newRequest;
}

void recordRequest(FILE* out, char* segment, int lineNumber){
    char line[LINE_SIZE];
    char* temp = malloc(strlen(segment)+1);
    strcpy(temp,segment);
    char* token = strtok(temp,"\n");

    for (int i=0; i<(lineNumber-1); i++){
        token = strtok(token,"\n");
    }
    strcpy(line,token);
    printf("child %d recording line %s\n",getpid(),line);
    fprintf(out,"%s\n",line);
    free(temp);
}

char* getSegment(char* sourceFile,int segmentNo,const int LINES_PER_SEGMENT){
    FILE* fp;
    fp = fopen(sourceFile,"r");
    /*Εύρεση του σωστού segment*/
    char line[LINE_SIZE];
    printf("in getsegment\n");
    for(int i=0; i<LINES_PER_SEGMENT*(segmentNo-1); i++){
        fgets(line,LINE_SIZE,fp);
    }

    /*Οι επόμενες LINES_PER_SEGMENT γραμμές θα επιστραφούν*/
    char* segment = malloc(LINE_SIZE*LINES_PER_SEGMENT*sizeof(char));
    fgets(line,LINE_SIZE,fp);
    strcpy(segment,line);
    for (int i=0; i<(LINES_PER_SEGMENT-1); i++){
        fgets(line,LINE_SIZE,fp);
        strcat(segment,line);
    }
    fclose(fp);

    return segment;
}

int getLines(char* filename){
    FILE *fp;
    int count = 0;  // Line counter (result)
    char c;  // To store a character read from file
 
    // Open the file
    fp = fopen(filename, "r");
 
    // Check if file exists
    if (fp == NULL)
    {
        printf("Error: Could not open %s\n",filename);
        return -1;
    }
 
    // Extract characters from file and store in character c
    for (c = getc(fp); c != EOF; c = getc(fp))
        if (c == '\n') // Increment count if this character is newline
            count = count + 1;
 
    // Close the file
    fclose(fp);
 
    return count;
}
