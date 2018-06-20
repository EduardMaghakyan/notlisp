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

long eval_op(long x, char* op, long y) {
  if (strcmp(op, "+") == 0 || strcmp(op, "add") == 0) { return x + y; }
  if (strcmp(op, "-") == 0) { return y ? x - y : -x; }
  if (strcmp(op, "*") == 0 || strcmp(op, "mul") == 0) { return x * y; }
  if (strcmp(op, "/") == 0 || strcmp(op, "div") == 0) { return x / y; }
  if (strcmp(op, "%") == 0) { return x % y; }
  if (strcmp(op, "^") == 0) { return x ^ y; }
  if (strcmp(op, "min") == 0) { return min(x, y); }
  if (strcmp(op, "max") == 0) { return max(x, y); }
  return 0;
}

long eval(mpc_ast_t* t) {

  /* If tagged as number return it*/
  if (strstr(t->tag, "number")) {
	return atoi(t->contents);
  }

  /* Select operator */
  char* op = t->children[1]->contents;
  
  /* We store the third child in `x` */
  long x = eval(t->children[2]);

  /* Combine the rest*/
  int i = 3;
  if (!strstr(t->children[i]->tag, "expr")) {
	return eval_op(x, op, 0);
  }

  while(strstr(t->children[i]->tag, "expr")) {
    x = eval_op(x, op, eval(t->children[i]));
    i++;
  }
  
  return x;
}

int main(int argc, char** argv) {
  mpc_parser_t* Number = mpc_new("number");
  mpc_parser_t* Operator = mpc_new("operator");
  mpc_parser_t* Expr = mpc_new("expr");
  mpc_parser_t* NotLispy = mpc_new("notlispy");
  /* Define them with the following Language */
  mpca_lang(MPCA_LANG_DEFAULT,
    " number       : /-?[0-9]+\\.?[0-9]*/ ;"
    " operator     : /[\\+\\-\\*\\/\\%\\^]|add|sub|mul|div|min|max/ ;"
    " expr         : <number> | '(' <operator> <expr>+ ')' ;"
    " notlispy    : /^/ <operator> <expr>+ /$/ ;   ",
  Number, Operator, Expr, NotLispy);

  while(1) {
    char* input = readline("not-lisp > ");
    add_history(input);
    mpc_result_t r;
    if (mpc_parse("<stdin>", input, NotLispy, &r)) {
      /* On success print and delete the AST */
      // mpc_ast_print(r.output);
      long result = eval(r.output);
      printf("%li\n", result);
      mpc_ast_delete(r.output);
    } else {
      /* Otherwise print and delete the Error */
      mpc_err_print(r.error);
      mpc_err_delete(r.error);
    }

    free(input);
  }


  mpc_cleanup(4, Number, Operator, Expr, NotLispy);
  return 0;
}

