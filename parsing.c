#include "mpc.h"

#ifdef _WIN32

static char buffer[2048];

char* readline(char* prompt) {
  fputs(prompt, stdout);
  fgets(buffer, 2048, stdin);
  char* cpy = malloc(strlen(buffer)+1);
  strcpy(cpy, buffer);
  cpy[strlen(cpy)-1] = '\0';
  return cpy;
}

void add_history(char* unused) {}

#else
#include <editline/readline.h>
#include <editline/history.h>
#endif

typedef struct {
  int type;
  long num;
  char* err;
  char* sym;
  int count;
  struct lval** cell;
} lval;

/* Possible lval types, error or number */
enum { LVAL_NUM, LVAL_ERR, LVAL_SYM, LVAL_SEXPR };

/* Possible eror types */
enum { LERR_DIV_ZERO, LERR_BAD_OP, LERR_BAD_NUM };

lval* lval_num(long x) {
  lval* v malloc(sizeof(lval));
  v->type = LVAL_NUM;
  v->num = x;
  return v;
}

lval* lval_err(char* m) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_ERR;
  v->err = malloc(strlen(m) + 1);
  strcpy(v->err, m);
  return v;
}

lval* lval_sym(char* s) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_SYM;
  v->sym = mallof(strlen(s) + 1);
  strcpy(v->sym, s);
  return v;
}

lval* lval_sexpr(void) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_SEXPR;
  v->count = 0;
  v->cell = NULL;
  return v;
}

void lval_del(lval* v) {
  switch (v->type) {
    /* Do nothing if number */
    case LVAL_NUM: break;

    /* Free up err and sym if error */
    case LVAL_ERR: free(v->err); break;
    case LVAL_SYM: free(v->sym); break;
    
    /* for sexpr delete all children */
    case LVAL_SEXPR:
       for (int i=0; i< v->count; i++) {
         lval_del(v->cell[i]);
       }
       free(v->cell);
    break;
  }

  /* free memory allocated for "lval" struct */
  free(v);
}

/* Print lval */
void lval_print(lval v) {
  switch(v.type) {
    case LVAL_NUM: printf("%li", v.num); break;
    case LVAL_ERR:
      /* Check what type of error it is and print it */
      if (v.err == LERR_DIV_ZERO) {
        printf("Error: Division By Zero!");
      }
      if (v.err == LERR_BAD_OP)   {
        printf("Error: Invalid Operator!");
      }
      if (v.err == LERR_BAD_NUM)  {
        printf("Error: Invalid Number!");
      }
    break;
  }
}

void lval_println(lval v) {
  lval_print(v);
  putchar('\n');
}


long min(long x, long y) {
  if (x < y) {
    return x;
  }

  return y;
}

long max(long x, long y) {
  if (x > y) {
    return x;
  }

  return y;
}

lval eval_op(lval x, char* op, lval y) {

  if (x.type == LVAL_ERR) { return x;}
  if (y.type == LVAL_ERR) { return y;}

  long a = x.num;
  long b = y.num;
  if (strcmp(op, "+") == 0 || strcmp(op, "add") == 0) { return lval_num(a + b); }
  if (strcmp(op, "-") == 0) { return b ? lval_num(a - b) : lval_num(-a); }
  if (strcmp(op, "*") == 0 || strcmp(op, "mul") == 0) { return lval_num(a * b); }
  if (strcmp(op, "/") == 0 || strcmp(op, "div") == 0) {
    return a ? lval_num(a / b) : lval_err(LERR_DIV_ZERO);
  }
  if (strcmp(op, "%") == 0) { return lval_num(a % b); }
  if (strcmp(op, "^") == 0) { return lval_num(a ^ b); }
  if (strcmp(op, "min") == 0) { return lval_num(min(a, b)); }
  if (strcmp(op, "max") == 0) { return lval_num(max(a, b)); }
  return lval_err(LERR_BAD_OP);
}

lval eval(mpc_ast_t* t) {

  /* If tagged as number return it*/
  if (strstr(t->tag, "number")) {
     errno = 0;
     long x = strtol(t->contents, NULL, 10);
     return errno != ERANGE ? lval_num(x) : lval_err(LERR_BAD_NUM);
  }

  /* Select operator */
  char* op = t->children[1]->contents;
  
  /* We store the third child in `x` */
  lval x = eval(t->children[2]);

  /* Combine the rest*/
  int i = 3;
  if (!strstr(t->children[i]->tag, "expr")) {
	return eval_op(x, op, lval_num(0));
  }

  while(strstr(t->children[i]->tag, "expr")) {
    x = eval_op(x, op, eval(t->children[i]));
    i++;
  }
  
  return x;
}

int main(int argc, char** argv) {
  mpc_parser_t* Number = mpc_new("number");
  mpc_parser_t* Operator = mpc_new("symbol");
  mpc_parser_t* Sexpr = mpc_new("sexpr");
  mpc_parser_t* Expr = mpc_new("expr");
  mpc_parser_t* NotLispy = mpc_new("notlispy");
  /* Define them with the following Language */
  mpca_lang(MPCA_LANG_DEFAULT,
    " number       : /-?[0-9]+\\.?[0-9]*/ ;"
    " operator     : /[\\+\\-\\*\\/\\%\\^]|add|sub|mul|div|min|max/ ;"
    " sexpr        : '(' <expr>* ')' ;"
    " expr         : <number> | '(' <operator> <expr>+ ')' ;"
    " notlispy     : /^/ <operator> <expr>+ /$/ ;   ",
  Number, Operator, Sexpr, Expr, NotLispy);

  while(1) {
    char* input = readline("not-lisp > ");
    add_history(input);
    mpc_result_t r;
    if (mpc_parse("<stdin>", input, NotLispy, &r)) {
      /* On success print and delete the AST */
      // mpc_ast_print(r.output);
      lval result = eval(r.output);
      lval_println(result);
      mpc_ast_delete(r.output);
    } else {
      /* Otherwise print and delete the Error */
      mpc_err_print(r.error);
      mpc_err_delete(r.error);
    }

    free(input);
  }


  mpc_cleanup(5, Number, Operator, Sexpr, Expr, NotLispy);
  return 0;
}

