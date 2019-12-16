/******************************************************************************
 *
 *  File Name........: main.c
 *
 *  Description......: Simple driver program for ush's parser
 *
 *  Author...........: Vincent W. Freeh
 *
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include "parse.h"
#include <sys/stat.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>

#define NO_FILE 0

int fd;
int util_pipe[2][2];
int flag;
char host[20];

extern char **environ;

static char* is_exists_cmd(Cmd command) {
  char *str = malloc(100);
  struct stat st;
  char *result = malloc(100);
  char *cmd = malloc(100), *cm = malloc(100);
  char env[100];
  strcpy(cmd, "/");
  strcpy(env, getenv("PATH"));
  strcat(cmd, command->args[0]);
  result = strtok( env, ":");
  while ( result != NULL )
  {
    strcpy(str, result);
    strcat(str, cmd);
    if (stat(str, &st) == 0)
      return str;
    
    result = strtok( NULL, ":" );
  }
  return NULL;
}

static void quit_handler(int signum) {
  signal(signum, SIG_IGN);
  printf("\n%s%% ", host);
  fflush(STDIN_FILENO);
  signal(signum, quit_handler);
}

static void int_handler(int signum) {
  printf("\r\n");
  printf("%s%% ", host);
  fflush(STDIN_FILENO);
}

static void term_handler(int signum) {
  signal(signum, SIG_IGN);
  signal(SIGTERM, SIG_IGN);
  killpg(getpgrp(), SIGTERM);
  signal(signum, term_handler);
  exit(0);
}

static void parent_handler(Cmd command) {
  if (command->next != NULL) {
    if (command->in == Tpipe || command->in == TpipeErr) {
      if (flag == 0) {
        close(util_pipe[1][1]);
        flag  = 1;
      }
      else {
        close(util_pipe[0][1]);
        flag = 0;
      }
    }
    else if (command->in == Tnil) {
      if (command->next->in == Tpipe || command->next->in == TpipeErr) {
        close(util_pipe[1][1]);
        flag = 1;
      }    
    }
    
    if ((command->in == Tpipe || command->in == TpipeErr) && (command->next->in == Tpipe || command->next->in == TpipeErr)) {
      if (flag == 0)
        pipe(util_pipe[1]);
      else
        pipe(util_pipe[0]);
    }
  }
}

static void process(Cmd command, int *input_file) {
  if (command->in == Tpipe) {
    if (flag == 1) {
      close(util_pipe[1][1]);
      dup2(util_pipe[1][0], 0);
    }
    else {
      close(util_pipe[0][1]);
      dup2(util_pipe[0][0], 0);
    }
  }
  else if (command->in == Tin) {
    *input_file = open(command->infile, O_RDONLY, 0660);
    dup2(*input_file, 0);
  }
  else if (command->in == TpipeErr) {
    if (flag == 1) {
      close(util_pipe[1][1]);
      dup2(util_pipe[1][0], 0);
    }
    else {
      close(util_pipe[0][1]);
      dup2(util_pipe[0][0], 0);
    }
  }
  
  if (command->next != NULL && (command->out == Tpipe || command->out == TpipeErr)) {
    if (command->next->in == Tpipe) {
      if (flag == 0) {
        close(util_pipe[1][0]);
        dup2(util_pipe[1][1], 1);
      }
      else {
        close(util_pipe[0][0]);
        dup2(util_pipe[0][1], 1);
      }
    }
    
    if (command->next->in == TpipeErr) {
      dup2(1, 2);
      if (flag == 0) {
        close(util_pipe[1][0]);
        dup2(util_pipe[1][1], 1);
      }
      else {
        close(util_pipe[0][0]);
        dup2(util_pipe[0][1], 1);
      }
    }
  }
  else if (command->out == ToutErr) {
    dup2(1, 2);
    fflush(stdout);
    fd = creat(command->outfile, 0660);
    dup2(fd, 1);
  }
  else if (command->out == TappErr) {
    dup2(1, 2);
    fflush(stdout);
    fd = open(command->outfile, O_WRONLY | O_APPEND | O_CREAT, 0660);
    dup2(fd, 1);
  }
  else if (command->out == Tout) {
    fflush(stdout);
    fd = creat(command->outfile, 0660);
    dup2(fd, 1);
  }
  else if (command->out == Tapp) {
    fflush(stdout);
    fd = open(command->outfile, O_WRONLY | O_APPEND | O_CREAT, 0660);
    dup2(fd, 1);
  }
}

static void run_command(Cmd command)
{
  int input_file = NO_FILE;
  fd = 0;
  char buffer[10];
  char *c_path = is_exists_cmd(command);
  pid_t pid = fork();
  if (pid == 0) {
    if (c_path != NULL) {
      process(command, &input_file);
      int executed = execv(c_path, command->args);
      if (executed < 0)
        perror("execv: ");
      
      if (input_file > 0) {
        close(input_file);
        close(0);
      }
    }
    else {
      if (command->out == TappErr) {
        dup2(1, 2);
        fd = open(command->outfile, O_WRONLY | O_APPEND | O_CREAT, 0660);
        dup2(fd, 1);
      }
      else if (command->out == ToutErr) {
        dup2(1, 2);
        fd = open(command->outfile, O_WRONLY | O_TRUNC | O_CREAT, 0660);
        dup2(fd, 1);
      }
      
      printf("Command not found\n");
      fflush(stdout);
      clearerr(stdout);
    }
    
    if (fd > 0) {
      close(fd);
      close(1);
      close(2);
    }
    exit(0);
  }
  else {
    wait();
    parent_handler(command);
  }
}

static void exec_cd(Cmd command) {
  char directory[100] = "";
  char buff;
  fd;
  int input_file = NO_FILE;
  int i = 0;
  
  if (command->next != NULL) {
    pid_t pid = fork();
    if (pid == 0)
    {
      process(command, &input_file);
      if (command->in == Tin)
      {
        fd = open(command->infile, O_RDONLY, 0660);
        while (read(fd, &buff, 1) > 0 && buff != '\n' && buff != '\0')
        {
          directory[i] = buff;
          i++;
        }
        
        close(fd);
        
        if (chdir(directory) != 0)
          perror("-ush: cd: failed: ");
      }
      else if (command->args[1] != NULL)
      {
        
        if (chdir(command->args[1]) != 0)
          perror("-ush: cd: failed: ");
      }
      else {
        chdir(getenv("HOME"));
      }
      exit(0);
    }
    else {
      wait();
      parent_handler(command);
    }
  }
  else {
    if (command->in == Tin) {
      fd = open(command->infile, O_RDONLY, 0660);
      while (read(fd, &buff, 1) > 0 && buff != '\0' && buff != '\n')
      {
        directory[i] = buff;
        i++;
      }
      
      close(fd);
      
      if (chdir(directory) != 0)
        perror("-ush: cd: failed: ");
    }
    else if (command->args[1] != NULL)
    {
      if (chdir(command->args[1]) != 0)
        perror("-ush: cd: failed: ");
    }
    else
    {
      chdir(getenv("HOME"));
    }
  }
}

static void exec_echo(Cmd c) {
  int i=1;
  if (c->args[1] != NULL){
    if (c->args[1][0] == '$'){
      char *temp = strtok(c->args[1],"$");
      char *result = getenv(temp);
      if (result != NULL)
        printf("%s", result);
      else
        printf("No such environment variable found");
    }
    else if (!strcmp(c->args[1],"~")){
      char *homeDirPath = (char *)malloc(1000);
      strcpy(homeDirPath, getenv("HOME"));
      printf("%s",homeDirPath);
    }
    else{
      while(c->args[i]!=NULL)
      {
        printf("%s ",c->args[i]);
        i++;  
      }
    }
  } 
  printf("\n");
}

static void exec_pwd(Cmd command) {
  char wd[1024];
  getcwd(wd, sizeof(wd));
  printf("%s\n", wd);
}

static void exec_where(Cmd command) {
  
}

static void exec_setenv(Cmd command) {
  
}

static void exec_unsetenv(Cmd command) {
  
}

static void exec_nice(Cmd command) {
  
  char *cmd;
  int value;
  int input_file = 0;
  long int priority;  
  pid_t parent = getpid();
  int child_pid = 0;
  int test;
  
  process(command, &input_file);
  
  if (command->nargs == 1) {
    int success = setpriority(PRIO_PROCESS, parent, 4);
    if (success < 0)
      perror("Unable to set priority: ");
  } else if (command->nargs == 2) {
    priority = atoi(command->args[1]);
    if (priority > 19 || priority < -20) {
      printf("Out of range");
      return;
    }
    else if (priority == 0) {
      if (command->args[1][0] == '+')
        setpriority(PRIO_PROCESS, parent, priority);
      else if (command->args[1][0] == '-')
        setpriority(PRIO_PROCESS, parent, priority);
      else {
        child_pid = fork();
        if (child_pid == 0)
        {
          if (setpriority(PRIO_PROCESS, getpid(), 4) < 0)
          {
            perror("Nice error: ");
            exit(0);
          }
          test = execvp(command->args[1], &command->args[1]);
          if (test != 0) {
            perror("Nice error: ");
            exit(0);
          }
          exit(0);
        } else {
          priority = 4;
          wait();
        }
      }
    } else {
      if (setpriority(PRIO_PROCESS, parent, priority) != 0) {
        perror("Nice error: ");
        exit(0);
      }
    }
  } else if (command->nargs == 3) {
    priority = atoi(command->args[1]);
    if (priority > 19 || priority < -20) {
      printf("Out of range");
      return;
    } else if (priority == 0) {
      if (command->args[1][0] == '-') {
        child_pid = fork();
        if (child_pid == 0) {
          if (setpriority(PRIO_PROCESS, getpid(), 4) < 0)
          {
            perror("Nice error: ");
            exit(0);
          }
          execvp(command->args[1], &command->args[1]);
          exit(0);
        }
        else {
          priority = 4;
          wait();
        }
      } else if (command->args[1][0] == '+') {
        child_pid = fork();
        if (child_pid == 0)
        {
          if (setpriority(PRIO_PROCESS, getpid(), 4) < 0)
          {
            perror("Nice error: ");
            exit(0);
          }
          execvp(command->args[1], &command->args[1]);
          exit(0);
        } else {
          priority = 4;
          wait();
        }
      }
    } else {
      child_pid = fork();
      if (child_pid == 0) {
        if (setpriority(PRIO_PROCESS, getpid(), 4) < 0)
        {
          perror("Nice error: ");
          exit(0);
        }
        execvp(command->args[1], &command->args[1]);
      } else {
        priority = 4;
        wait();
      }
    }
  }
  
  fflush(stdout);
  parent_handler(command);
  
  if (fd > 0) {
    close(fd);
    close(1);
    close(2);
  }
  
  if (input_file > 0) {
    close(input_file);
    close(0);
  }
}

static void exec_cmd(Cmd command) {
  
  char *cmd = command->args[0];
  
  if (strcmp(cmd, "logout") == 0)
    exit(0);
  else if (strcmp(cmd, "cd") == 0)
    exec_cd(command);
  else if (strcmp(cmd, "pwd") == 0)
    exec_pwd(command);
  else if (strcmp(cmd, "echo") == 0)
    exec_echo(command);
  else if (strcmp(cmd, "setenv") == 0)
    exec_setenv(command);
  else if (strcmp(cmd, "unsetenv") == 0)
    exec_unsetenv(command);
  else if (strcmp(cmd, "where") == 0)
    exec_where(command);
  else if (strcmp(cmd, "nice") == 0)
    exec_nice(command);  
  else
    run_command(command);
}

static void prPipe(Pipe p)
{
  int i = 0;
  Cmd c;
  
  if ( p == NULL )
    return;
  for ( c = p->head; c != NULL; c = c->next ) {
    exec_cmd(c);
  }
  flag = 0;
  pipe(util_pipe[0]);
  pipe(util_pipe[1]);
  prPipe(p->next);
}

int main(int argc, char *argv[])
{
  Pipe p;
  gethostname(host, 20);
  char *ush_path = malloc(sizeof(char) * 50);
  char *old_home;
  int fd;
  
  signal (SIGINT, int_handler);
  signal (SIGQUIT, quit_handler);
  signal (SIGTERM, term_handler);
  
  old_home = getenv("HOME");
  
  strcpy(ush_path, old_home);
  
  strcat(ush_path, "/.ushrc");
  
  if ((fd = open(ush_path, O_RDONLY)) != -1) {
    
    int oldStdIn = dup(STDIN_FILENO);
    dup2(fd, STDIN_FILENO);
    close(fd);
    flag = 0;
    pipe(util_pipe[0]);
    pipe(util_pipe[1]);
    
    p = parse();
    prPipe(p);
    freePipe(p);
    dup2(oldStdIn, STDIN_FILENO);
    close(oldStdIn);
  }
  
  while ( 1 ) {
    flag = 0;
    pipe(util_pipe[0]);
    pipe(util_pipe[1]);
    printf("%s%% ", host);
    fflush(stdout);
    p = parse();
    prPipe(p);
    freePipe(p);
  }
}

/*........................ end of main.c ....................................*/