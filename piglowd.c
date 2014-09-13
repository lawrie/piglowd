#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <syslog.h>
#include <fcntl.h>

#include <wiringPi.h>
#include <piGlow.h>

#define MAX_INDEX 3

#define OPCODE_FOR 0
#define OPCODE_RING 1
#define OPCODE_LEG 2
#define OPCODE_LED 3
#define OPCODE_DELAY 4

struct instruction {
  unsigned char opcode;
  int p1;
  int p2;
  int p3;
};

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void become_daemon()
{
    pid_t pid;
    int pidFilehandle;
    char* pidfile = "/tmp/pilogd.pid";
    char str[10];

    /* Fork off the parent process */
    pid = fork();

    /* An error occurred */
    if (pid < 0)
        exit(EXIT_FAILURE);

    /* Success: Let the parent terminate */
    if (pid > 0)
        exit(EXIT_SUCCESS);

    /* On success: The child process becomes session leader */
    if (setsid() < 0)
        exit(EXIT_FAILURE);

    /* Catch, ignore and handle signals */
    //TODO: Implement a working signal handler */
    signal(SIGCHLD, SIG_IGN);
    signal(SIGHUP, SIG_IGN);

    /* Fork off for the second time*/
    pid = fork();

    /* An error occurred */
    if (pid < 0)
        exit(EXIT_FAILURE);

    /* Success: Let the parent terminate */
    if (pid > 0)
        exit(EXIT_SUCCESS);

    /* Set new file permissions */
    umask(0);

    /* Change the working directory to the root directory */
    /* or another appropriated directory */
    chdir("/");

    /* Close all open file descriptors */
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    /* Open the log file */
    openlog ("pilogd", LOG_PID, LOG_DAEMON);

    /* Ensure only one copy */
    pidFilehandle = open(pidfile, O_RDWR|O_CREAT, 0600);
 
    if (pidFilehandle == -1 )
    {
      /* Couldn't open lock file */
      syslog(LOG_INFO, "Could not open PID lock file %s, exiting", pidfile);
      exit(EXIT_FAILURE);
    }
    /* Try to lock file */
    if (lockf(pidFilehandle,F_TLOCK,0) == -1)
    {
      /* Couldn't get lock on lock file */
      syslog(LOG_INFO, "Could not lock PID lock file %s, exiting", pidfile);
      exit(EXIT_FAILURE);
    }
 
    /* Get and format PID */
    sprintf(str,"%d\n",getpid());
 
    /* write pid to lockfile */
    write(pidFilehandle, str, strlen(str));
}

char** read_config(void)
{
  int lines_allocated = 5;
  int max_line_len = 200;

  /* Allocate lines of text */
  char **words = (char **)malloc(sizeof(char*)*lines_allocated);
  if (words==NULL)
  {
    fprintf(stderr,"Out of memory (1).\n");
    exit(1);
  }

  FILE *fp = fopen("/etc/piglod/piglod.conf", "r");
  if (fp == NULL)
  {
    fprintf(stderr,"Error opening file.\n");
    exit(2);
  }

  int i;
  for (i=0;1;i++)
  {
    int j;

    /* Have we gone over our line allocation? */
    if (i >= lines_allocated)
    {
      int new_size;

      /* Double our allocation and re-allocate */
      new_size = lines_allocated*2;
      words = (char **)realloc(words,sizeof(char*)*new_size);
      if (words==NULL)
      {
        fprintf(stderr,"Out of memory.\n");
        exit(3);
      }
      lines_allocated = new_size;
    }
    /* Allocate space for the next line */
    words[i] = malloc(max_line_len);
    if (words[i]==NULL)
    {
      fprintf(stderr,"Out of memory (3).\n");
      exit(4);
    }
    if (fgets(words[i],max_line_len-1,fp)==NULL)
      break;

    /* Get rid of CR or LF at end of line */
    for (j=strlen(words[i])-1;j>=0 && (words[i][j]=='\n' || words[i][j]=='\r');j    --);
    words[i][j+1]='\0';
  }

  printf("There are %d patterns\n", i);

  int j;
  for(j = 0; j < i; j++)
    printf("%s\n", words[j]);

  return words;
}


char** str_split(char* a_str, const char a_delim)
{
    char** result    = 0;
    size_t count     = 0;
    char* tmp        = a_str;
    char* last_comma = 0;
    char delim[2];
    delim[0] = a_delim;
    delim[1] = 0;

    /* Count how many elements will be extracted. */
    while (*tmp)
    {
        if (a_delim == *tmp)
        {
            count++;
            last_comma = tmp;
        }
        tmp++;
    }

    /* Add space for trailing token. */
    count += last_comma < (a_str + strlen(a_str) - 1);

    /* Add space for terminating null string so caller
       knows where the list of returned strings ends. */
    count++;

    result = malloc(sizeof(char*) * count);

    if (result)
    {
        size_t idx  = 0;
        char* token = strtok(a_str, delim);

        while (token)
        {
            assert(idx < count);
            *(result + idx++) = strdup(token);
            token = strtok(0, delim);
        }
        assert(idx == count - 1);
        *(result + idx) = 0;
    }

    return result;
}

void execute(struct instruction *instructions, int level, int start, int end, int* index){
  int i, j, e;

  //printf("Executing from %d to %d at level %d with index = %d\n", start,end, level, index[level]);

  for(i=start;i<end;i++)
  {
    struct instruction inst = instructions[i];
    int opcode = inst.opcode;
    int p1 = inst.p1;
    int p2 = inst.p2;
    int p3 = inst.p3;

    //printf("Opcode is %d, p1 is %d, p2 is %d, p3 is %d\n", opcode, p1, p2, p3);

    switch(opcode) 
    {
      case OPCODE_FOR:
        for(e=i+1;e<end && (instructions[e].opcode != OPCODE_FOR || instructions[e].p1 > p1);e++);
        //printf("Skipping to %d\n", e);
        for(j=p2;(p2 <= p3 ? j<=p3 : j>=p3);(p2 <= p3 ? j++ : j--)) 
        {
          index[p1] = j;
          execute(instructions, p1,i+1, e, index);
        }
        //printf("Setting i to %d\n", e-1);
        i = e-1;
        break; 
      case OPCODE_RING:
        piGlowRing((p1 < 0 ? index[p1*-1 - 1] :p1) % 6, p2);
        break;
      case OPCODE_LEG:
        piGlowLeg((p1 < 0 ? index[p1*-1 -1] :p1) % 3, p2);
        break;
      case OPCODE_LED:
        piGlow1((p1 < 0 ? index[p1*-1 -1] :p1) % 3, (p2 < 0 ? index[p2*-1 -1] :p2) % 6, p3);
        break;
      case OPCODE_DELAY:
        delay(p1);
        break; 
    }
  }
}

int main (int argc, char *argv[])
{
  int i;
  int ring, leg ;
  char **tokens;
  char *token;
  char **args;
  char *left, *right;
  char **params;
  char * start, *end;
  char **patterns;
  
  wiringPiSetupSys () ;

  piGlowSetup (1) ;

  patterns = read_config();

  if (argc > 0) 
  {
    printf("pattern:%s\n",patterns[atoi(argv[1])]);
    tokens = str_split(patterns[atoi(argv[1])],' ');

    if (tokens)
    {
        int index[MAX_INDEX];
        int lt;
        struct instruction * instructions;
        char msg[80];

        become_daemon();

        sprintf(msg,"pilogd executing pattern %s\n", argv[1]);
        syslog(LOG_NOTICE, msg);

        for(lt=0;*(tokens + lt);lt++);
        //printf("Length of tokens is %d\n", lt);

        instructions = (struct instruction *) malloc(sizeof(struct instruction) * lt);
        //printf("level = %d, index = %d\n", level, index[level]); 
        for (i = 0; *(tokens + i); i++)
        {
          token = *(tokens + i);
	  //printf("token %d = %s\n", i,  token);

	  args = str_split(strdup(token),'=');
	  left = args[0];
	  right = args[1];

	  //printf("token %d = %s\n", i,  token);

	  start = NULL;
	  end = NULL;

	  if (right != NULL) 
	  {
	    params = str_split(right,'-');
	    start = params[0];
	    end = params[1];

	    //printf("start = %s, end = %s\n", start, end);
	  }

	  //printf("left = %s, right = %s\n", left, right);
	    
	  if (*token == 'l') 
	  {
	    leg = ((token[1] >= 'i' && token[1] <= 'k') ? (token[1] - 'h') * -1 : token[1] - '0');
	    instructions[i].opcode = OPCODE_LEG;
	    instructions[i].p1 = leg;
	    instructions[i].p2 = atoi(right);
	  }
	  else if (*token == 'r')
	  {
	    //printf("Length is %d\n", strlen(left));
	    ring = ((token[1] >= 'i'  && token[1] <= 'k') ? (token[1] - 'h') * -1 : token[1] - '0');
	    if(strlen(left) == 2)
	    {
       	      instructions[i].opcode = OPCODE_RING;
              instructions[i].p1 = ring;
	      instructions[i].p2 = atoi(right);
	    }
	    else 
            {
	      leg = ((token[3] >= 'i' && token[3] <= 'k') ? (token[3] - 'h') * -1 : token[3] - '0');
              instructions[i].opcode = OPCODE_LED;
	      instructions[i].p1 = leg;
	      instructions[i].p2 = ring;
	      instructions[i].p3 = atoi(right);
	    }
	  } 
	  else if (*token >= 'i' && *token <= 'k')
	  {
	    instructions[i].opcode = OPCODE_FOR;
	    instructions[i].p1 = *token - 'i';
	    instructions[i].p2 = (start == NULL ? 0 : atoi(start));
	    instructions[i].p3 = (end == NULL ? 1000000 : atoi(end));
	  }
	  else if (*token == 'd')
	  {
	    instructions[i].opcode = OPCODE_DELAY;
	    instructions[i].p1 = atoi(&token[1]);
	  }
	  else 
          {
            printf("Invalid token: %s\n", token);
          }
        }

        execute(instructions,-1,0,lt,index);


        syslog(LOG_NOTICE, "pilogd terminated.");
        closelog();

    }
  }
  return 0;
}

