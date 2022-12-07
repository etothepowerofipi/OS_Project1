#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

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

typedef struct shared_memory
{
    sem_t sem1;
    sem_t sem2;
    int segment;
    int line;
    char data[LINE_SIZE];
}* Shm;

int getLines(char*);
void returnLine(char**,const int);
char* getSegment(const char* filename, const int segmentNo, const int LINES_PER_SEGMENT);
void copySegment(const char* filename, const int segmentNo, const int LINES_PER_SEGMENT, char *segment[]);

int main(int argc, char* argv[]){
    char sourceFile[100];
    int segLines;
    if (argc<=2){
        printf("Error: Not enough arguments given\n");
        exit(1);
    }
    else{
        strcpy(sourceFile,SOURCE_FOLDER);
        strcat(sourceFile,argv[1]);
        printf("sourcefile is %s\n",sourceFile);
        segLines = atoi(argv[2]);
    }
    const int LINES_PER_SEGMENT = segLines;
    int lines = getLines(sourceFile);
    if (lines == -1){
        exit(1);
    }
    else if (lines < MIN_LINES){
        printf("Error: %s does not have enough lines\n",sourceFile);
        exit(1);
    }
    printf("File %s has %d lines and %d lines per segment\n",sourceFile,lines,LINES_PER_SEGMENT);

    int temp = lines / LINES_PER_SEGMENT;

    const int SEGMENTS = lines / LINES_PER_SEGMENT;


    // char* temp2 = "Line 200\nLine 201\nLine 202\nLine 203\nLine 204\nLine 205\nLine 206\nLine 207\nLine 208\nLine 209\nLine 210\nLine 211\nLine 212\nLine 213\nLine 214\nLine 215\nLine 216\nLine 217\nLine 218\nLine 219\nLine 220\nLine 221\nLine 222\nLine 223\nLine 224\nLine 225\nLine 226\nLine 227\nLine 228\nLine 229\nLine 230\nLine 231\nLine 232\nLine 233\nLine 234\nLine 235\nLine 236\nLine 237\nLine 238\nLine 239\nLine 240\nLine 241\nLine 242\nLine 243\nLine 244\nLine 245\nLine 246\nLine 247\nLine 248\nLine 249\nLine 250\nLine 251\nLine 252\nLine 253\nLine 254\nLine 255\nLine 256\nLine 257\nLine 258\nLine 259\nLine 260\nLine 261\nLine 262\nLine 263\nLine 264\nLine 265\nLine 266\nLine 267\nLine 268\nLine 269\nLine 270\nLine 271\nLine 272\nLine 273\nLine 274\nLine 275\nLine 276\nLine 277\nLine 278\nLine 279\nLine 280\nLine 281\nLine 282\nLine 283\nLine 284\nLine 285\nLine 286\nLine 287\nLine 288\nLine 289\nLine 290\nLine 291\nLine 292\nLine 293\nLine 294\nLine 295\nLine 296\nLine 297\nLine 298\nLine 299";
    // printf("malloc successful\n");
    // copySegment(sourceFile,2,LINES_PER_SEGMENT,&temp2);
    // printf("segment is %s\n",temp2);



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
    
    if (sem_init(&shmp->sem1,1,0) == -1){
        perror("sem_init: sem1");
        exit(1);
    }
    if (sem_init(&shmp->sem2,1,1) == -1){
        perror("sem_init: sem2");
        exit(1);
    }

    int id = fork();
    if (id != 0){
        //in parent process
        if (sem_wait(&shmp->sem1) == -1){
            perror("sem_wait: sem1");
            exit(1);
        }
        
        //Κρίσιμη περιοχή
        printf("Parent copies <%d,%d>\n",shmp->segment,shmp->line);

        char* segment = getSegment(sourceFile,shmp->segment,LINES_PER_SEGMENT);
        char* line = (char*)malloc(LINE_SIZE*LINES_PER_SEGMENT);
        char* original_line = line;
        strcpy(line,segment);
        returnLine(&line,shmp->line);
        printf("Parent: line to be returned is %s\n",line);
        strcpy(shmp->data,line);
        printf("freeing original_line\n");
        free(original_line);
        printf("freeing segment\n");
        free(segment);

        if (sem_post(&shmp->sem1) == -1){
            perror("sem_post: sem1");
            exit(1);
        }

        if (sem_post(&shmp->sem2) == -1){
            perror("sem_post: sem2 (parent)");
        }
    }
    else{
        //in child process
        if (sem_wait(&shmp->sem2) == -1){
            perror("sem_wait: sem2");
            exit(1);
        }
        int segment = 2;
        int line = 37;

        printf("child %d requesting <%d,%d>\n",getpid(),segment,line);

        shmp->segment = segment;
        shmp->line = line;

        if (sem_post(&shmp->sem1) == -1){
            perror("sem_post: sem1");
            exit(1);
        }

        if (sem_wait(&shmp->sem1) == -1){
            perror("sem_wait: sem1 (from child)");
            exit(1);
        }
        printf("child received line: %s\n",shmp->data);

        if (sem_post(&shmp->sem1) == -2){
            perror("sem_post: sem2 (from child)");
            exit(1);
        }


    }
    shm_unlink(shmpath);
    printf("done\n");
    return 0;
}

char* getSegment(const char* filename, const int segmentNo, const int LINES_PER_SEGMENT){
    FILE *fp;
    fp = fopen(filename, "r");
    char line[LINE_SIZE];
    for(int i=0; i<LINES_PER_SEGMENT*(segmentNo-1); i++){
        fgets(line,LINE_SIZE,fp);
    }
    char* segment = (char*)malloc(LINE_SIZE*LINES_PER_SEGMENT*sizeof(char));
    fgets(line,LINE_SIZE,fp);
    strcpy(segment,line);
    for(int i=0; i<(LINES_PER_SEGMENT - 1); i++){
        fgets(line,LINE_SIZE,fp);
        strcat(segment,line);
    }
    fclose(fp);

    return segment;
}

void returnLine(char** segment, const int lineNumber){
    *segment = strtok(*segment,"\n");
    for (int i=0; i<(lineNumber-1); i++){
        *segment = strtok(NULL,"\n");
    }

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