/* $Id: cmd_user.h,v 1.28 2005/05/24 21:27:50 kuhlmann Exp $ */

#ifndef MICQ_USER_H
#define MICQ_USER_H

#define END_MSG_STR    "."
#define CANCEL_MSG_STR "#"
#define W_SEPARATOR COLQUOTE, "============================================", COLNONE, "\n"

typedef int (jump_f)(const char *args, UDWORD data, int status);
#define JUMP_F(f) int f (const char *args, UDWORD data, int status)

struct jumpstr {
    jump_f *f;
    const char *name;
    int unidle;
    int data;
};

typedef struct jumpstr jump_t;

jump_t *CmdUserTable (void);
jump_t *CmdUserLookup (const char *command);

void CmdUser (const char *command);
void CmdUserInput (strc_t line);
void CmdUserInterrupt (void);
void CmdUserCallbackTodo (Event *event);

#define CMD_USER_HELP(syn,des) rl_printf ("%s" syn "%s\n\t" COLINDENT "%s" COLEXDENT "\n", COLQUOTE, COLNONE, des)
#define CMD_USER_HELP3(syn,d,e,f) rl_printf ("%s" syn "%s\n\t" COLINDENT "%s" COLEXDENT "\n", COLQUOTE, d, e, COLNONE, f)
#define CMD_USER_HELP7(syn,a,b,c,d,e,f,g) rl_printf ("%s" syn "%s\n\t" COLINDENT "%s\n%s\n%s\n%s\n%s\n%s\n%s" COLEXDENT "\n", COLQUOTE, COLNONE, a,b,c,d,e,f,g)

#endif /* MICQ_USER_H */
