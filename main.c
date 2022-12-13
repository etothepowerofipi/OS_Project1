#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#include <semaphore.h>
#include <unistd.h>

#include <sys/shm.h>
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



#define LINE_SIZE 10
#define MIN_LINES 1000
#define SOURCE_FOLDER "source/"


//Ένα πολύ απλό macro για error handling
#define CALL_OR_DIE(call)     \
  {                           \
    int code = call; \
    if (code == -1) {      \
      perror("Error");    \
      exit(code);             \
    }                         \
  }


typedef struct shared_memory
{
    sem_t wrt;
    sem_t mutex;
    sem_t** segmentSemaphores;
    int readCount; //Πόσες διεργασίες-παιδιά διαβάζουν το τρέχον segment
    int currentSegment;
    int requestedSegment;
    char* data;
}* Shm;

void childProcess(const int childNumber, Shm shmp, const int SEGMENTS, const int LINES_PER_SEGMENT, const int REQUESTS);
void recordRequest(FILE* output, char* segment, int lineNumber);
int getLines(char*);
char* returnLine(char*,const int);
char* getSegment(const char* filename, const int segmentNo, const int LINES_PER_SEGMENT);

int main(int argc, char* argv[]){
    char sourceFile[100];
    int segLines;
    if (argc<=2){
        perror("Error: Not enough arguments given");
        exit(1);
    }
    else{
        strcpy(sourceFile,SOURCE_FOLDER);
        strcat(sourceFile,argv[1]);
        printf("sourcefile is %s\n",sourceFile);
        segLines = atoi(argv[2]);
    }
    const int LINES_PER_SEGMENT = segLines;
    const int REQUESTS_PER_CHILD = 20;

    const int lines = getLines(sourceFile);
    if (lines == -1){
        exit(1);
    }
    else if (lines < MIN_LINES){
        printf("Error: %s does not have enough lines",sourceFile);
        exit(1);
    }
    printf("File %s has %d lines and %d lines per segment\n",sourceFile,lines,LINES_PER_SEGMENT);
    const int SEGMENTS = lines / LINES_PER_SEGMENT;

    const char* shmpath = "/shared211";
    int fd = shm_open(shmpath,O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
    if (fd == -1){
        perror("shmget");
        exit(1);
    }
    
    if (ftruncate(fd,sizeof(struct shared_memory)) == -1){
        perror("ftruncate");
        exit(1);
    }

    Shm shmp = mmap(NULL, sizeof(*shmp), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);    
    if (shmp == MAP_FAILED){
        perror("mmap");
        exit(1);
    }
    shmp->segmentSemaphores = malloc(SEGMENTS*sizeof(sem_t*));
    for (int i=0; i<SEGMENTS; i++){
        shmp->segmentSemaphores[i] = malloc(sizeof(sem_t));
        CALL_OR_DIE(sem_init(shmp->segmentSemaphores[i],1,0));
    }

    shmp->readCount = 0;
    shmp->currentSegment = 0;
    shmp->data = malloc(LINE_SIZE*LINES_PER_SEGMENT*sizeof(char));

    //Γίνεται init με 0, διότι πρώτα πρέπει ένα παιδί να κάνει request, και μετά να καλέσει τη sem_post(wrt) με το αντίστοιχο request
    //ώστε ο γονέας να αποθηκεύσει το αντίστοιχο segment.
    CALL_OR_DIE(sem_init(&shmp->wrt,1,0));
    CALL_OR_DIE(sem_init(&shmp->mutex,1,1));



    srand(time(0));
    const int CHILDREN = 2;
    int i=0;
    int id = fork();
    if (id==0) printf("Created new child with pid=%ld\n",(long)getpid());

    //Δημιουργία παιδιών
    while (i<CHILDREN && id!=0){
        id = fork();
        if (id==0) printf("Created new child with pid=%ld\n",(long)getpid());
        i++;
    }

    if (id != 0){
        //Στη γονική διεργασία
        int rep = 0;
        while(rep < CHILDREN*REQUESTS_PER_CHILD){
            printf("In parent process with pid=%ld\n",(long)getpid());

            bool isInit = shmp->currentSegment != 0;
            int previousSegment = shmp->currentSegment;
            CALL_OR_DIE(sem_wait(&shmp->wrt));

            if (isInit)
                CALL_OR_DIE(sem_wait(shmp->segmentSemaphores[shmp->currentSegment - 1]));

            //
            //Κρίσιμη περιοχή
            //

            printf("parent took wrt\n");
            printf("current segment is %d, requested is %d\n",shmp->currentSegment,shmp->requestedSegment);
            shmp->currentSegment = shmp->requestedSegment; 
            free(shmp->data); 
            shmp->data = getSegment(sourceFile,shmp->currentSegment,LINES_PER_SEGMENT);
            CALL_OR_DIE(sem_post(&shmp->wrt));
            //
            //Τέλος κρίσιμης περιοχής
            //

            CALL_OR_DIE(sem_post(shmp->segmentSemaphores[shmp->currentSegment-1]));

            rep++;
        }

    }
    else{
        childProcess(i,shmp,SEGMENTS,LINES_PER_SEGMENT,REQUESTS_PER_CHILD);

    }
    for(int i=0; i<SEGMENTS; i++){
        free(shmp->segmentSemaphores[i]);
    }
    free(shmp->segmentSemaphores);
    free(shmp->data);
    shm_unlink(shmpath);
    printf("%ld exiting\n",(long)getpid());
    if (id != 0) printf("PARENT EXITING\n");
    return 0;
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

//Παράγει ένα τυχαίο segment number με πιθανότητα 7/10 να είναι ίδιο με το τωρινό.
//Αν είναι ίδιο, απλά αντιγράφει μια γραμμή του, την καταγράφει μαζί με μερικά μετα-δεδομένα, περιμένει 20 ms, και ξαναξεκινά.
//Αλλιώς, καλεί τη sem_wait() του αντίστοιχου σημαφόρου του segment, και όταν ελευθερωθεί κάνει την καταγραφή.
void childProcess(const int childNumber, Shm shmp, const int SEGMENTS, const int LINES_PER_SEGMENT, const int REQUESTS){
    FILE* output = createFile(childNumber);
    CALL_OR_DIE(sem_wait(&shmp->mutex));
    printf("In child process with pid=%ld.\n",(long)getpid());
    //Έλεγχος για το αν έχει αρχικοποιηθεί η κοινή μνήμη με request
    bool initiated = shmp->currentSegment != 0;
    int start;
    if (initiated){
        start = 0;
        CALL_OR_DIE(sem_post(&shmp->mutex));
    }
    else{
        int value;
        CALL_OR_DIE(sem_getvalue(&shmp->mutex, &value));
        CALL_OR_DIE(sem_getvalue(&shmp->wrt, &value));
        start = 1;
        int segmentToRequest = rand() % SEGMENTS + 1;
        int arrayPosition = segmentToRequest - 1;
        printf("shared memory not initiated. Child #%ld requesting segment #%d\n",(long)getpid(),segmentToRequest);
        //Εγγραφή στην κοινή μνήμη της αίτησης. Δεν υπάρχει πρόβλημα καθώς ήδη έχει κληθεί η sem_wait(mutex).
        shmp->requestedSegment = segmentToRequest;
        CALL_OR_DIE(sem_post(&shmp->wrt));

        //Ο σημαφόρος mutex είναι ακόμα 0, λόγω της sem_wait(mutex) πιο πάνω, συνεπώς μετά τη sem_post(wrt) από τον γονιό,
        //θα κληθεί αυτή εδώ η sem_wait(wrt) και όχι κάποια από άλλο παιδί.
        CALL_OR_DIE(sem_wait(&shmp->wrt));
        printf("first child took wrt\n");



        int lineNumber = rand() % LINES_PER_SEGMENT + 1;
        recordRequest(output, shmp->data,lineNumber);

        CALL_OR_DIE(sem_post(&shmp->mutex));
        CALL_OR_DIE(sem_post(shmp->segmentSemaphores[arrayPosition]));
    }
    for (int i=start; i<REQUESTS; i++){
        printf("in request #%d for child %d\n",i+1,getpid());
        // CALL_OR_DIE(sem_wait(&shmp->mutex));
        int currentSemaphore = shmp->currentSegment - 1;

        int chance = rand() % 10 + 1;
        bool sameSegment = chance <= 7;

        if (!sameSegment){
            printf("diff segment\n");
            CALL_OR_DIE(sem_wait(&shmp->mutex));
            printf("sem_waited mutex\n");
            shmp->readCount--;
            if (shmp->readCount == 0){
                printf("readcount = 0");
                //Δεν διαβάζει κανείς απ' το τρέχον segment, άρα πρέπει να αλλαχθεί
                CALL_OR_DIE(sem_post(&shmp->wrt));

            }
            CALL_OR_DIE(sem_post(&shmp->mutex));

            //Το segmentToRequest πρέπει να είναι ανάμεσα στο 1 και το SEGMENTS
            int segmentToRequest = rand() % SEGMENTS + 1;

            //Το segmentToRequest πρέπει να είναι διαφορετικό από το τωρινό
            while (segmentToRequest == shmp->currentSegment)
                segmentToRequest = rand() % SEGMENTS + 1;

            int arrayPosition = segmentToRequest - 1;
    
            // CALL_OR_DIE(sem_wait(shmp->segmentSemaphores[arrayPosition]));
            int lineNumber = rand() % LINES_PER_SEGMENT + 1;
            recordRequest(output, shmp->data,lineNumber);
            int value;
            CALL_OR_DIE(sem_getvalue(&shmp->mutex, &value));
            CALL_OR_DIE(sem_getvalue(&shmp->wrt, &value));
            //Η παρακάτω sem_wait() σταματά την εκτέλεση της διεργασίας, μέχρι να εισαχθεί το αντίστοιχο (=arrayPosition+1) segment στη μνήμη.

            //Γίνεται αλλαγή που αφορά μόνο το συγκεκριμένο κελί της κοινής μνήμης, οπότε υπάρχει χωριστός σημαφόρος γι' αυτό.
            //Άλλες διεργασίες δεν μπλοκάρονται, εκτός αν πρόκειται να γράψουν στο readCount του συγκεκριμένου κελιού SemInfo.
            //gettimeofday()
            CALL_OR_DIE(sem_wait(shmp->segmentSemaphores[arrayPosition]));
            CALL_OR_DIE(sem_post(shmp->segmentSemaphores[arrayPosition]));
            CALL_OR_DIE(sem_wait(&shmp->mutex));
            shmp->readCount++;
            if (shmp->readCount == 1){
                //Μπλοκάρει τον σημαφόρο του γονιού, γιατί πρόκειται να γίνει ανάγνωση από την κοινή μνήμη οπότε δεν πρέπει να γίνονται ταυτόχρονα αλλαγές
                //Αν είναι πάνω από 1, η παρακάτω sem_wait() έχει ήδη κληθεί
                CALL_OR_DIE(sem_wait(&shmp->wrt));
                printf("child %d took wrt\n",getpid());
                shmp->requestedSegment = segmentToRequest;                
            }
            CALL_OR_DIE(sem_post(&shmp->mutex));



    
        }
        else{
            //gettimeofday()
            sleep(1);            
            int lineNumber = rand() % LINES_PER_SEGMENT + 1;
            recordRequest(output,shmp->data,lineNumber);       
        }
    }
    fclose(output);
}

void recordRequest(FILE* output, char* segment, int lineNumber){
    char* line = returnLine(segment,lineNumber);
    fprintf(output,"%s",line);
    free(line);
    line = NULL;
}

char* getSegment(const char* filename, const int segmentNo, const int LINES_PER_SEGMENT){
    FILE *fp;
    fp = fopen(filename, "r");
    char line[LINE_SIZE];
    for(int i=0; i<LINES_PER_SEGMENT*(segmentNo-1); i++){
        fgets(line,LINE_SIZE,fp);
    }
    char* segment = malloc(LINE_SIZE*LINES_PER_SEGMENT*sizeof(char));
    fgets(line,LINE_SIZE,fp);
    strcpy(segment,line);
    for(int i=0; i<(LINES_PER_SEGMENT - 1); i++){
        fgets(line,LINE_SIZE,fp);
        strcat(segment,line);
    }
    fclose(fp);

    return segment;
}

char* returnLine(char* segment, const int lineNumber){
    if (segment == NULL){
        perror("segment is NULL\n");
        exit(1);
    }
    char* token = strtok(segment,"\n");

    for (int i=0; i<(lineNumber-1); i++){
        token = strtok(token,"\n");
    }
    char* line = malloc(LINE_SIZE);
    strcpy(line,token);
    return line;
}


int getLines(char* filename)
{
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