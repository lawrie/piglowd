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
#include <stdarg.h>

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

static int pattern = -1, fd, lt, np=0, is_daemon = FALSE, verbose=FALSE, stepping=FALSE;
static char**tokens;

// Test for all digits
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

// Open the input fifo
static int open_fifo(void) 
{
  char fifo_name[] = "/tmp/piglowfifo";
  struct stat st;

  // If created as a normal file, delete it
  if (stat(fifo_name, &st) == 0 && !S_ISFIFO(st.st_mode)) 
    remove(fifo_name);

  if (stat(fifo_name, &st) != 0)
    mkfifo(fifo_name, 0666);

  fd= open(fifo_name, O_RDONLY | O_NONBLOCK);

  return fd;
}

// Read a single character command or a pattern number from the FIFO
static int read_fifo(void) {
  char buf[1];

  if (read(fd, &buf, 1) > 0)
  {
    if (buf[0] < 32) return -1;
    if (buf[0] >= '0' && buf[0] <= '9') return buf[0] - '0';
    else if (buf[0] >= 'a' && buf[0] <= 'f') return (buf[0] - 'a' + 10);
    else return buf[0];
  }
  else return -1;
}

// Send an error message to the daemon log or console
static void error(char *msg, ...)
{
  char str[100];
  va_list args;
  va_start( args, msg );

  vsprintf( str, msg, args );

  va_end( args );

  if (is_daemon) syslog(LOG_NOTICE, str);
  else printf(str);
}

// Log an information message to the daemoon log or console
static void log_msg(char *msg, ...)
{ 
  char str[100];
  va_list args;
  va_start( args, msg );

  vsprintf( str, msg, args );

  va_end( args );

  if (is_daemon) syslog(LOG_NOTICE, str);
  else printf(str);
}

// Become a daemon
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
  
  is_daemon = TRUE;

  /* Ensure only one copy */
  pidFilehandle = open(pidfile, O_RDWR|O_CREAT, 0600);
 
  if (pidFilehandle == -1 )
  {
    /* Couldn't open lock file */
    log_msg("Could not open PID lock file %s, exiting", pidfile);
    exit(EXIT_FAILURE);
  }
  
  /* Try to lock file */
  if (lockf(pidFilehandle,F_TLOCK,0) == -1)
  {
    /* Couldn't get lock on lock file */
    log_msg("Could not lock PID lock file %s, exiting", pidfile);
    exit(EXIT_FAILURE);
  }
 
  /* Get and format PID */
  sprintf(str,"%d\n",getpid());
 
  /* write pid to lockfile */
  write(pidFilehandle, str, strlen(str));
}

// Read the patterns from the config file
static char** read_config(void)
{
  int lines_allocated = 5;
  int max_line_len = 200;

  /* Allocate lines of text */
  char **words = (char **)malloc(sizeof(char*)*lines_allocated);
  if (words==NULL)
  {
    fprintf(stderr,"Out of memory (1).\n");
    exit(EXIT_FAILURE);
  }

  FILE *fp = fopen("/etc/piglowd/piglowd.conf", "r");
  if (fp == NULL)
  {
    fprintf(stderr,"Error opening file.\n");
    exit(EXIT_FAILURE);
  }

  int i;
  for (i=0;1;i++)
  {
    int j;
    char *r = NULL;

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
        exit(EXIT_FAILURE);
      }
      lines_allocated = new_size;
    }
	
    /* Allocate space for the next line */
    words[i] = malloc(max_line_len);
	
    if (words[i]==NULL)
    {
      fprintf(stderr,"Out of memory (3).\n");
      exit(EXIT_FAILURE);
    }
    for(;;) 
    {
      r = fgets(words[i],max_line_len-1,fp);
      if (r == NULL) break;
      // Ignore comments
      if (r[0] != '#') break;
    }
    if (r == NULL) break;

    /* Get rid of CR or LF at end of line */
    for (j=strlen(words[i])-1;j>=0 && (words[i][j]=='\n' || words[i][j]=='\r');j    --);
    words[i][j+1]='\0';
  }

  if (verbose && !is_daemon) 
  {
    printf("There are %d patterns:\n", i);

    int j;
    for(j = 0; j < i; j++)
      printf("%d: %s\n", j, words[j]);
  }

  fclose(fp); 
  np = i;
  return words;
}

// Split a line into tokens
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

// Switch off all the LEDs
static void clear(void) 
{
  int i;
  // Switches all LEDS off
  for(i=0;i<3;i++) piGlowLeg(i,0);
}

// Execute compiled instructions
static int execute(struct instruction *instructions, int level, int start, int end, int* index){
  int i, j, e;

  //log_msg("Executing from %d to %d at level %d with index = %d\n", start,end, level, index[level]);

  for(i=start;i<end;i++)
  {
    struct instruction inst = instructions[i];
    int opcode = inst.opcode;
    int p1 = inst.p1;
    int p2 = inst.p2;
    int p3 = inst.p3;
    int p4 = inst.p4;

    pattern = read_fifo();
    if (stepping) 
    {
      if (pattern == 'g') 
      {
        stepping = FALSE;
      } 
      else if (pattern < 0) {
       i--;
       continue;
      }
      else if (pattern != 'n') return FALSE;
      log_msg("Stepping mode, command = %c, token=%s, i=%d, j=%d, k=%d\n",pattern, tokens[i], index[0], (level > 0 ? index[1] : 0), (level > 1 ? index[2] : 0));
    }
    else if (pattern >= 0) return FALSE;

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

// Compile a pattern
struct instruction *compile(char * pattern) 
{
  char* pattern_copy = strdup(pattern); 
  tokens = str_split(pattern_copy,' ');
  struct instruction * instructions = NULL;
  int level=0;

  if (tokens)
  {
    int i;
    int ring, leg ;
    char *token;
    char **args;
    char *left, *right;
    char * start, *end, *increment;
    char *token_copy;
    char indent[5];

    FILE *fp = fopen("/tmp/pattern.c", "w");
    fprintf(fp,"#include <wiringPi.h\n");
    fprintf(fp,"#include <piGlow.h\n");
    fprintf(fp, "\nint main(void)\n{\n  int i,j,k;\n\n  wiringPiSetupSys();\n  piGlowSetup(1);\n\n");

    strcpy(indent,(level == 0 ? "" : (level == 1 ? "  " : "    "))); 

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
	    
      if (*token == 'l') // Leg
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
      else if (*token == 'r') // Ring
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
        if(strlen(left) == 2) // Ring only
        {
       	  instructions[i].opcode = OPCODE_RING;
          instructions[i].p1 = ring;
	      instructions[i].p2 = ((right[0] >= 'i'  && right[0] <= 'k') ? (right[0] - 'h') * -1 : atoi(right));
          if (instructions[i].p2 < -3 || instructions[i].p2 > 255) 
          {
            error("Invalid intensity value: %s\n", token);
            return NULL;
          }
          if (ring < 0) fprintf(fp, "%s  PiGlowRing(%c %c 6,%d)\n", indent, token[1], '%',instructions[i].p2);
          else fprintf(fp,"    piGlowRing(%d,%d)\n", ring, instructions[i].p2);
        }
        else if (strlen(left) == 4) // Ring and leg
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
      else if (*token >= 'i' && *token <= 'k') // Variable
      {
        if (token[1] == '?') // Random value
        {
          instructions[i].opcode = OPCODE_RANDOM;
          instructions[i].p1 =  *token - 'i';
          instructions[i].p2 = atoi(start);
          instructions[i].p3 = atoi(end);
        } 
        else if (token[1] == ':') // Assignment (subtraction)
        {
          instructions[i].opcode = OPCODE_ASSIGN;
          instructions[i].p1 =  *token - 'i';
          instructions[i].p2 = ((start[0] >= 'i'  && start[0] <= 'k') ? (start[0] - 'h') * -1 : atoi(start));
          instructions[i].p3 = ((end[0] >= 'i'  && end[0] <= 'k') ? (end[0] - 'h') * -1 : atoi(end));
        } 
        else
        {
          if (token[1] != '=') // For loop
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
          level = *token - 'i';
          fprintf(fp,"%s  for(%c=%d;%c<=%d;i++)\n",indent,*token,instructions[i].p2,*token,instructions[i].p3);
          fprintf(fp,"%s  {\n",indent);
        }
      }
      else if (*token == 'd') // Delay
      {
        if (!is_numeric(&token[1]) && (strlen(token) > 2 || token[1] < 'i' || token[1] > 'k')) 
        {
          error("Invalid delay token: %s\n", token);
          return NULL;
        }
        instructions[i].opcode = OPCODE_DELAY;
        instructions[i].p1 = ((token[1] >= 'i' && token[1] <= 'k') ? (token[1] - 'h') * -1 : atoi(&token[1]));
        fprintf(fp,"%s    delay(%d)\n", indent, instructions[i].p1);
      }
      else 
      {
         error("Invalid token: %s\n", token);
         return NULL;
      }
      free(token_copy);
    }
    free(pattern_copy);
    fprintf(fp,"}\n");
    fclose(fp);
  }
  return instructions;
}

// Main entry point
int main (int argc, char *argv[])
{
  char** patterns;
  struct instruction * instructions;
  int index[MAX_INDEX];
  int i;
  int make_daemon = TRUE;

  index[0] = 0;

  // Process args
  for (i = 1; i < argc; i++)  /* Skip argv[0] (program name). */
  {
    if (strcmp(argv[i], "-v") == 0) 
    {
      verbose = TRUE;
    }
     else if (strcmp(argv[i], "-p") == 0) 
    {
      make_daemon = FALSE;
      pattern = atoi(argv[++i]);
    }
  }

  srand((unsigned int) time(NULL));

  wiringPiSetupSys () ;

  piGlowSetup (1) ;

  patterns = read_config();

  fd = open_fifo();

  if (make_daemon)
  {
    become_daemon();
    log_msg("Starting piglowd daemon");
  }

  for(;;)
  { 
    if (pattern < 0) pattern = read_fifo();

    if (pattern  < 0) 
    {
      delay(500);
      continue;
    }

    if (pattern == 'x') break;
    else if (pattern == 'q')
    {
      clear();
      pattern = -1;
      continue;
    } else if (pattern== 'r') {
      clear();
      pattern = -1;
      patterns = read_config();
      continue;
    } else if (pattern == 's') {
      stepping = TRUE;
      pattern = -1;
      continue;
    }

    if (pattern >= np) {
      log_msg("Invalid pattern number: %d\n", pattern);
      pattern = -1;
      continue;
    }

    instructions = compile(patterns[pattern]);
    if (instructions == NULL)
    {
      pattern = -1;
      continue;
    }

    log_msg("Executing pattern %d : %s\n", pattern, patterns[pattern]);
    pattern = -1;
    clear();
    execute(instructions,0,0,lt,index);
    log_msg("Finished pattern\n");
    clear();
    free(instructions);
    for(i=0;*(tokens +i);i++) free(tokens[i]);
    free(tokens);
    if (!is_daemon) break;
  }

  clear();
  close(fd);
  remove("/tmp/piglowfifo");
  if (is_daemon) 
  {
    log_msg("piglowd terminated.\n");
    closelog();
  } 
  return 0;
}

