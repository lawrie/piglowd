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
#include <ctype.h>
#include <time.h>

#include <wiringPi.h>
#include <piGlow.h>

#define MAX_INDEX 3
#define TRUE 1
#define FALSE 0

#define OPCODE_FOR 0
#define OPCODE_RING 1
#define OPCODE_LEG 2
#define OPCODE_LED 3
#define OPCODE_DELAY 4
#define OPCODE_RANDOM 5
#define OPCODE_ASSIGN 6

struct instruction {
  unsigned char opcode;
  int p1;
  int p2;
  int p3;
  int p4;
};

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int pattern = -1, fd, lt, is_daemon = FALSE;

static int is_numeric(char *str)
{
  while(*str)
  {
    if(!isdigit(*str))
      return 0;
    str++;
  }

  return 1;
}
static int open_fifo(void) 
{
  char fifo_name[] = "/tmp/piglowfifo";
  struct stat st;

  if (stat(fifo_name, &st) != 0)
    mkfifo(fifo_name, 0666);

  fd= open(fifo_name, O_RDONLY | O_NONBLOCK);

  return fd;
}

static int read_fifo(void) {
  char buf[1];

  if (read(fd, &buf, sizeof(char)*1) > 0)
  {
    return buf[0] - '0';
  }
  else return -1;
}

static void become_daemon()
{
    pid_t pid;
    int pidFilehandle;
    char* pidfile = "/tmp/piglowd.pid";
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

    is_daemon = TRUE;
}

static void error(char *msg, char* token)
{
  if (is_daemon) syslog(LOG_NOTICE, msg, token);
  else printf(msg, token);
}

static char** read_config(void)
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

  FILE *fp = fopen("/etc/piglowd/piglowd.conf", "r");
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

  printf("There are %d patterns:\n", i);

  int j;
  for(j = 0; j < i; j++)
    printf("%d: %s\n", j, words[j]);

  fclose(fp); 

  return words;
}

static char** str_split(char* a_str, const char a_delim)
{
    char** result    = 0;
    size_t count     = 0;
    char* tmp        = a_str;
    char* last_delim = 0;
    char delim[2];
    delim[0] = a_delim;
    delim[1] = 0;
    char *save_ptr;

    /* Count how many elements will be extracted. */
    while (*tmp)
    {
        if (a_delim == *tmp)
        {
            count++;
            last_delim = tmp;
        }
        tmp++;
    }

    /* Add space for trailing token. */
    count += last_delim < (a_str + strlen(a_str) - 1);

    /* Add space for terminating null string so caller
       knows where the list of returned strings ends. */
    count++;

    result = malloc(sizeof(char*) * count);

    if (result)
    {
        size_t idx  = 0;
        char* token = strtok_r(a_str, delim, &save_ptr);

        while (token)
        {
            assert(idx < count);
            *(result + idx++) = strdup(token);
            token = strtok_r(0, delim, &save_ptr);
        }
        assert(idx == count - 1);
        *(result + idx) = 0;
    }
    return result;
}

static void clear(void) 
{
  int i;
  // Switches all LEDS off
  for(i=0;i<3;i++) piGlowLeg(i,0);
}

static int execute(struct instruction *instructions, int level, int start, int end, int* index){
  int i, j, e;

  //syslog(LOG_NOTICE,"Executing from %d to %d at level %d with index = %d\n", start,end, level, index[level]);

  for(i=start;i<end;i++)
  {
    struct instruction inst = instructions[i];
    int opcode = inst.opcode;
    int p1 = inst.p1;
    int p2 = inst.p2;
    int p3 = inst.p3;
    int p4 = inst.p4;

    pattern = read_fifo();
    if (pattern >= 0) return FALSE;

    //syslog(LOG_NOTICE, "Opcode is %d, p1 is %d, p2 is %d, p3 is %d\n", opcode, p1, p2, p3);
    switch(opcode) 
    {
      case OPCODE_FOR:
        for(e=i+1;e<end && (instructions[e].opcode != OPCODE_FOR || instructions[e].p1 > p1);e++);
        for(j=p2;(p2 <= p3 ? j<=p3 : j>=p3);) 
        {
          index[p1] = j;
          if (!execute(instructions, p1,i+1, e, index)) return FALSE;
          if (p2 <= p3) j += p4; else j-= p4;
        }
        i = e-1;
        break; 
      case OPCODE_RING:
        piGlowRing((p1 < 0 ? index[p1*-1 - 1] :p1) % 6,(p2 < 0 ? index[p2*-1 - 1] :p2));
        break;
      case OPCODE_LEG:
        piGlowLeg((p1 < 0 ? index[p1*-1 -1] :p1) % 3, (p2 < 0 ? index[p2*-1 - 1] :p2));
        break;
      case OPCODE_LED:
        piGlow1((p1 < 0 ? index[p1*-1 -1] :p1) % 3, (p2 < 0 ? index[p2*-1 -1] :p2) % 6, (p3 < 0 ? index[p3*-1 - 1] :p3));
        break;
      case OPCODE_DELAY:
        delay((p1 < 0 ? index[p1*-1 - 1] :p1));
        break; 
      case OPCODE_RANDOM:
        index[p1] = p2 + rand() % (p3 + 1 - p2);
        break;
      case OPCODE_ASSIGN:
        index[p1] = (p2 < 0 ? index[p2*-1 -1] :p2) - (p3 < 0 ? index[p3*-1 -1] :p3);
        break;
    }
  }
  return TRUE;
}

struct instruction * compile(char * pattern) 
{
  char* pattern_copy = strdup(pattern); 
  char** tokens = str_split(pattern_copy,' ');
  struct instruction * instructions = NULL;

  if (tokens)
  {
    int i;
    int ring, leg ;
    char *token;
    char **args;
    char *left, *right;
    char * start, *end, *increment;
    char *token_copy;

    for(lt=0;*(tokens + lt);lt++);

    instructions = (struct instruction *) malloc(sizeof(struct instruction) * lt);
    for (i = 0; *(tokens + i); i++)
    {
      token = *(tokens + i);

      token_copy = strdup(token);
      args = str_split(token_copy,(token[1] == '?' ? '?' : (token[1] == ':' ? ':' : '=')));
      left = args[0];
      right = args[1];

      start = NULL;
      end = NULL;
      increment = NULL;

      if (right != NULL) 
      {
        args = str_split(right,',');
        right = args[0];
        increment = args[1];
        args = str_split(right,'-');
        start=args[0];
        end = args[1];
      }
	    
      if (*token == 'l') 
      {
        if (strlen(token) < 4 || token[2] != '=') 
        {
          error("Invalid leg token: %s\n",token);
          return NULL;
        }
        leg = ((token[1] >= 'i' && token[1] <= 'k') ? (token[1] - 'h') * -1 : token[1] - '0');
        if (leg < -3 || leg > 2) 
        {
          error("Invalid leg value: %s\n",token);
          return NULL;
        }
	instructions[i].opcode = OPCODE_LEG;
	instructions[i].p1 = leg;
	instructions[i].p2 =  ((right[0] >= 'i'  && right[0] <= 'k') ? (right[0] - 'h') * -1 : atoi(right));
        if (instructions[i].p2 < -3 || instructions[i].p2 > 255) 
        {
          error("Invalid intensity value: %s\n",token);
          return NULL;
        }
      }
      else if (*token == 'r')
      {
        if (strlen(token) < 4 || (token[2] != '=' && token[2] != 'l')) 
        {
          error("Invalid ring token: %s\n",token);
          return NULL;
        }
	ring = ((token[1] >= 'i'  && token[1] <= 'k') ? (token[1] - 'h') * -1 : token[1] - '0');
        if (ring < -3 || ring > 5) 
        {
          error("Invalid ring value: %s\n", token);
          return NULL;
        }
	if(strlen(left) == 2)
	{
       	  instructions[i].opcode = OPCODE_RING;
          instructions[i].p1 = ring;
	  instructions[i].p2 = ((right[0] >= 'i'  && right[0] <= 'k') ? (right[0] - 'h') * -1 : atoi(right));
          if (instructions[i].p2 < -3 || instructions[i].p2 > 255) 
          {
            error("Invalid intensity value: %s\n", token);
            return NULL;
          }
	}
	else if (strlen(left) == 4)
        {
	  leg = ((token[3] >= 'i' && token[3] <= 'k') ? (token[3] - 'h') * -1 : token[3] - '0');
          if (leg < -3 || leg > 2) 
          {
            error("Invalid leg value: %s\n", token);
            return NULL;
          }
          instructions[i].opcode = OPCODE_LED;
	  instructions[i].p1 = leg;
	  instructions[i].p2 = ring;
	  instructions[i].p3 = ((right[0] >= 'i'  && right[0] <= 'k') ? (right[0] - 'h') * -1 : atoi(right));
          if (instructions[i].p3 < -3 || instructions[i].p3 > 255) 
          {
            error("Invalid intensity value: %s\n", token);
            return NULL;
          }
	}
        else 
        {
          error("Invalid ring token (left length): %s\n", token);
          return NULL;
        }
      } 
      else if (*token >= 'i' && *token <= 'k')
      {
        if (token[1] == '?')
        {
          instructions[i].opcode = OPCODE_RANDOM;
          instructions[i].p1 =  *token - 'i';
          instructions[i].p2 = atoi(start);
          instructions[i].p3 = atoi(end);
        } 
        else if (token[1] == ':')
        {
          instructions[i].opcode = OPCODE_ASSIGN;
          instructions[i].p1 =  *token - 'i';
          instructions[i].p2 = ((start[0] >= 'i'  && start[0] <= 'k') ? (start[0] - 'h') * -1 : atoi(start));
          instructions[i].p3 = ((end[0] >= 'i'  && end[0] <= 'k') ? (end[0] - 'h') * -1 : atoi(end));
        } 
        else
        {
          if (token[1] != '=') 
          {
            error("Invalid loop token: %s\n", token);
            return NULL;
          }
          if (start != NULL && !is_numeric(start)) 
          {
            error("Invalid start loop value: %s\n", token);
            return NULL;
          }
        
          if (end != NULL && !is_numeric(end)) 
          {
            error("Invalid end loop value: %s\n", token);
            return NULL;
          }
          if (increment != NULL && !is_numeric(increment)) 
          {
            error("Invalid increment loop value: %s\n", token);
            return NULL;
          }
          instructions[i].opcode = OPCODE_FOR;
          instructions[i].p1 = *token - 'i';
          instructions[i].p2 = (start == NULL ? 0 : atoi(start));
          instructions[i].p3 = (end == NULL ? 1000000 : atoi(end));
          instructions[i].p4 = (increment == NULL ? 1 : atoi(increment));
        }
      }
      else if (*token == 'd')
      {
        if (!is_numeric(&token[1]) && (strlen(token) > 2 || token[1] < 'i' || token[1] > 'k')) 
        {
          error("Invalid delay token: %s\n", token);
          return NULL;
        }
        instructions[i].opcode = OPCODE_DELAY;
        instructions[i].p1 = ((token[1] >= 'i' && token[1] <= 'k') ? (token[1] - 'h') * -1 : atoi(&token[1]));
      }
      else 
      {
         error("Invalid token: %s\n", token);
         return NULL;
      }
      free(token_copy);
    }
    for(i=0;*(tokens +i);i++) free(tokens[i]);
    free(tokens);
    free(pattern_copy);
  }
  return instructions;
}

int main (int argc, char *argv[])
{
  char** patterns;
  struct instruction * instructions;
  int index[MAX_INDEX];

  srand((unsigned int) time(NULL));

  wiringPiSetupSys () ;

  piGlowSetup (1) ;

  patterns = read_config();

  fd = open_fifo();

  if (argc > 1) 
  {
    pattern = atoi(argv[1]);
    printf("pattern:%d\n",pattern);
  }
        
  become_daemon();
  syslog(LOG_NOTICE, "Starting piglowd daemon");

  for(;;)
  { 
    if (pattern < 0) pattern = read_fifo();

    if (pattern  < 0) 
    {
      delay(500);
      continue;
    }

    if (pattern == ('x' - '0')) break;
    else if (pattern == ('c' - '0'))
    {
      clear();
      pattern = -1;
      continue;
    } else if (pattern== ('r' - '0')) {
      clear();
      pattern = -1;
      patterns = read_config();
      continue;
    }

    instructions = compile(patterns[pattern]);
    if (instructions == NULL)
    {
      pattern = -1;
      continue;
    }

    syslog(LOG_NOTICE, "Executing pattern %d\n", pattern);
    pattern = -1;
    clear();
    execute(instructions,-1,0,lt,index);
    syslog(LOG_NOTICE, "Finishing pattern");
    clear();
    free(instructions);
  }

  clear();
  syslog(LOG_NOTICE, "piglowd terminated.");
  closelog();
  return 0;
}

