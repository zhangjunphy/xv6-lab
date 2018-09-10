#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

// Simplifed xv6 shell.

#define MAXARGS 10

// All commands have at least a type. Have looked at the type, the code
// typically casts the *cmd to some specific cmd type.
struct cmd {
  int type;          //  ' ' (exec), | (pipe), ';' (list), '<', '>' or '+' for redirection
};

struct execcmd {
  int type;              // ' '
  char *argv[MAXARGS];   // arguments to the command to be exec-ed
};

struct redircmd {
  int type;          // < or >
  struct cmd *cmd;   // the command to be run (e.g., an execcmd)
  char *file;        // the input/output file
  int flags;         // flags for open() indicating read or write
  int fd;            // the file descriptor number to use for the file
};

struct pipecmd {
  int type;          // |
  struct cmd *left;  // left side of pipe
  struct cmd *right; // right side of pipe
};

struct listcmd {
  int type;              // ;
  int back;
  struct cmd *first;     // first cmd
  struct cmd *rest;      // all the rest
};

int fork1(void);  // Fork but exits on failure.
struct cmd *parsecmd(char*);

// Execute cmd.  Never returns.
void
runcmd(struct cmd *cmd)
{
  int p[2], r;
  long pid;
  int backcount = 0;
  struct execcmd *ecmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;
  struct listcmd *lcmd;

  if(cmd == 0)
    _exit(0);

  switch(cmd->type){
  default:
    fprintf(stderr, "unknown runcmd\n");
    _exit(-1);

  case ' ':
    ecmd = (struct execcmd*)cmd;
    if(ecmd->argv[0] == 0)
      _exit(0);
    // Your code here ...
    r = execvp(ecmd->argv[0], ecmd->argv);
    if (r < 0)
      perror("execvp() failed");
    break;

  case '>':
  case '<':
  case '+':
    rcmd = (struct redircmd*)cmd;
    // Your code here ...
    r = close(rcmd->fd);
    int fd = open(rcmd->file, rcmd->flags);
    if (fd < 0)
      perror("open() failed");
    r = fchmod(fd, 0644);
    if (r < 0)
      perror("fchmod() failed");
    runcmd(rcmd->cmd);
    break;

  case '|':
    pcmd = (struct pipecmd*)cmd;
    // Your code here ...
    r = pipe(p);
    if (r < 0)
      perror("pipe() failed");
    if (fork1() == 0) {
      close(p[0]);
      close(fileno(stdout));
      dup(p[1]);
      runcmd(pcmd->left);
    } else {
      close(p[1]);
      close(fileno(stdin));
      dup(p[0]);
      runcmd(pcmd->right);
    }
    break;

  case ';':
    lcmd = (struct listcmd*)cmd;
    pid = fork1();
    if (pid == 0) {
      runcmd(lcmd->first);
      break;
    }
    wait(&r);
    runcmd(lcmd->rest);
    break;
  }
  _exit(0);
}

int
getcmd(char *buf, int nbuf)
{
  if (isatty(fileno(stdin)))
    fprintf(stdout, "6.828$ ");
  memset(buf, 0, nbuf);
  if(fgets(buf, nbuf, stdin) == 0)
    return -1; // EOF
  return 0;
}

int
main(void)
{
  static char buf[100];
  int fd, r;
  long pid;

  // Read and run input commands.
  while(getcmd(buf, sizeof(buf)) >= 0){
    if(buf[0] == 'c' && buf[1] == 'd' && buf[2] == ' '){
      // Clumsy but will have to do for now.
      // Chdir has no effect on the parent if run in the child.
      buf[strlen(buf)-1] = 0;  // chop \n
      if(chdir(buf+3) < 0)
	fprintf(stderr, "cannot cd %s\n", buf+3);
      continue;
    } else if (strncmp(buf, "wait", 4) == 0) {
      while ( (pid = wait(NULL)) ) {
	if (errno == ECHILD)
	  break;
      }
    }

    struct cmd* cmd = parsecmd(buf);
    if (fork1() == 0)
      runcmd(cmd);

    if (cmd->type == ';' && ((struct listcmd *) cmd)->back) {
      continue;
    }

    wait(&r);
  }
  exit(0);
}

int
fork1(void)
{
  int pid;

  pid = fork();
  if(pid == -1)
    perror("fork");
  return pid;
}

struct cmd*
execcmd(void)
{
  struct execcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = ' ';
  return (struct cmd*)cmd;
}

struct cmd*
redircmd(struct cmd *listcmd, char *file, int type)
{
  struct redircmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = type;
  cmd->cmd = listcmd;
  cmd->file = file;
  switch (type) {
  case '<':
    cmd->flags = O_RDONLY;
    break;
  case '>':
    cmd->flags = O_WRONLY|O_CREAT|O_TRUNC;
    break;
  case '+':
    cmd->flags = O_WRONLY|O_CREAT|O_APPEND;
  }
  cmd->fd = (type == '<') ? 0 : 1;
  return (struct cmd*)cmd;
}

struct cmd*
pipecmd(struct cmd *left, struct cmd *right)
{
  struct pipecmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = '|';
  cmd->left = left;
  cmd->right = right;
  return (struct cmd*)cmd;
}

struct cmd*
listcmd(struct cmd *first, struct cmd *rest, int back) {
  struct listcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = ';';
  cmd->first = first;
  cmd->rest = rest;
  cmd->back = back;

  return (struct cmd*) cmd;
}

// Parsing

char whitespace[] = " \t\r\n\v";
char symbols[] = "<|>;()&";

int
gettoken(char **ps, char *es, char **q, char **eq)
{
  char *s;
  int ret;

  s = *ps;
  while(s < es && strchr(whitespace, *s))
    s++;
  if(q)
    *q = s;
  ret = *s;
  switch(*s){
  case 0:
    break;
  case '|':
  case '<':
  case ';':
  case '(':
  case ')':
  case '&':
    s++;
    break;
  case '>':
    if (s + 1 < es && *(s+1) == '>') {
      ret = '+';
      s++;
    }
    s++;
    break;
  default:
    ret = 'a';
    while(s < es && !strchr(whitespace, *s) && !strchr(symbols, *s))
      s++;
    break;
  }
  if(eq)
    *eq = s;

  while(s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return ret;
}

int
peek(char **ps, char *es, char *toks)
{
  char *s;

  s = *ps;
  while(s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return *s && strchr(toks, *s);
}

struct cmd *parseline(char**, char*);
struct cmd *parsepipe(char**, char*);
struct cmd *parseexec(char**, char*);

// make a copy of the characters in the input buffer, starting from s through es.
// null-terminate the copy to make it a string.
char
*mkcopy(char *s, char *es)
{
  int n = es - s;
  char *c = malloc(n+1);
  assert(c);
  strncpy(c, s, n);
  c[n] = 0;
  return c;
}

struct cmd*
parsecmd(char *s)
{
  char *es;
  struct cmd *cmd;

  es = s + strlen(s);
  cmd = parseline(&s, es);
  peek(&s, es, "");
  if(s != es){
    fprintf(stderr, "leftovers: %s\n", s);
    exit(-1);
  }
  return cmd;
}

struct cmd*
parseline(char **ps, char *es)
{
  struct cmd *cmd;
  cmd = parsepipe(ps, es);
  if (peek(ps, es, ";") || peek(ps, es, "&")) {
    int tok = gettoken(ps, es, 0, 0);
    cmd = listcmd(cmd, parseline(ps, es), tok == '&');
  }
  return cmd;
}

struct cmd*
parsepipe(char **ps, char *es)
{
  struct cmd *cmd;

  cmd = parseexec(ps, es);

  if(peek(ps, es, "|")) {
    gettoken(ps, es, 0, 0);
    cmd = pipecmd(cmd, parsepipe(ps, es));
  }
  return cmd;
}

struct cmd*
parseredirs(struct cmd *cmd, char **ps, char *es)
{
  int tok;
  char *q, *eq;

  while(peek(ps, es, "<>")) {
    tok = gettoken(ps, es, 0, 0);
    if(gettoken(ps, es, &q, &eq) != 'a') {
      fprintf(stderr, "missing file for redirection\n");
      exit(-1);
    }
    cmd = redircmd(cmd, mkcopy(q, eq), tok);
  }
  return cmd;
}

struct cmd*
parseblock(char **ps, char *es)
{
  struct cmd *cmd;
  if(!peek(ps, es, "(")) {
    fprintf(stderr, "syntax error - parseblock");
    exit(-1);
  }
  gettoken(ps, es, 0, 0);
  cmd = parseline(ps, es);
  if(!peek(ps, es, ")")) {
    fprintf(stderr, "syntax error - missing )");
    exit(-1);
  }
  gettoken(ps, es, 0, 0);
  cmd = parseredirs(cmd, ps, es);

  return cmd;
}

struct cmd*
parseexec(char **ps, char *es)
{
  char *q, *eq;
  int tok, argc;
  struct execcmd *cmd;
  struct cmd *ret;

  ret = execcmd();
  cmd = (struct execcmd*) ret;

  if (peek(ps, es, "(")) {
    return parseblock(ps, es);
  }

  argc = 0;
  ret = parseredirs(ret, ps, es);
  while(!peek(ps, es, "|;&()")) {
    if((tok=gettoken(ps, es, &q, &eq)) == 0)
      break;
    if(tok != 'a') {
      fprintf(stderr, "syntax error\n");
      exit(-1);
    }
    cmd->argv[argc] = mkcopy(q, eq);
    argc++;
    if(argc >= MAXARGS) {
      fprintf(stderr, "too many args\n");
      exit(-1);
    }
    ret = parseredirs(ret, ps, es);
  }
  cmd->argv[argc] = 0;
  return ret;
}
