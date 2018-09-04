#include "mpc.h"

#ifdef _WIN32

static char buffer[2048];

char *readline(char *prompt)
{
  fputs(prompt, stdout);
  fgets(buffer, 2048, stdin);
  char *cpy = malloc(strlen(buffer) + 1);
  strcpy(cpy, buffer);
  cpy[strlen(cpy) - 1] = '\0';
  return cpy;
}

void add_history(char *unused) {}

#else
#include <editline/readline.h>
#include <editline/history.h>
#endif

#define LASSERT(args, cond, err) \
  if (!(cond))                   \
  {                              \
    lval_del(args);              \
    return lval_err(err);        \
  }

struct lval;
struct lenv;

typedef struct lval lval;
typedef struct lenv lenv;

/* Possible lval types, error or number */
enum
{
  LVAL_NUM,
  LVAL_ERR,
  LVAL_SYM,
  LVAL_SEXPR,
  LVAL_QEXPR,
  LVAL_FUN
};

typedef lval *(*lbuiltin)(lenv *, lval *);

struct lval
{
  int type;

  long num;
  char *err;
  char *sym;
  lbuiltin fun;

  int count;
  lval **cell;
};

lval *lval_num(long x)
{
  lval *v = malloc(sizeof(lval));
  v->type = LVAL_NUM;
  v->num = x;
  return v;
}

lval *lval_err(char *m)
{
  lval *v = malloc(sizeof(lval));
  v->type = LVAL_ERR;
  v->err = malloc(strlen(m) + 1);
  strcpy(v->err, m);
  return v;
}

lval *lval_sym(char *s)
{
  lval *v = malloc(sizeof(lval));
  v->type = LVAL_SYM;
  v->sym = malloc(strlen(s) + 1);
  strcpy(v->sym, s);
  return v;
}

lval *lval_fun(lbuiltin func)
{
  lval *v = malloc(sizeof(lval));
  v->type = LVAL_FUN;
  v->fun = func;
  return v;
}

lval *lval_sexpr(void)
{
  lval *v = malloc(sizeof(lval));
  v->type = LVAL_SEXPR;
  v->count = 0;
  v->cell = NULL;
  return v;
}

lval *lval_qexpr(void)
{
  lval *v = malloc(sizeof(lval));
  v->type = LVAL_QEXPR;
  v->cell = NULL;
  v->count = 0;
  return v;
}

void lval_del(lval *v)
{
  switch (v->type)
  {
  case LVAL_NUM:
    break;

  case LVAL_ERR:
    free(v->err);
    break;
  case LVAL_SYM:
    free(v->sym);
    break;

  /* If Sexpr then delete all elements inside */
  case LVAL_SEXPR:
  case LVAL_QEXPR:
    for (int i = 0; i < v->count; i++)
    {
      lval_del(v->cell[i]);
    }
    /* Also free the memory allocated to contain the pointers */
    free(v->cell);
    break;
  case LVAL_FUN:
    break;
  }

  /* Free the memory allocated for the "lval" struct itself */
  free(v);
}

lval *lval_copy(lval *v)
{
  lval *x = malloc(sizeof(lval));
  x->type = v->type;

  switch (v->type)
  {
  case LVAL_FUN:
    x->fun = v->fun;
    break;
  case LVAL_NUM:
    x->num = v->num;
    break;
  case LVAL_ERR:
    x->err = malloc(strlen(v->err) + 1);
    strcpy(x->err, v->err);
    break;
  case LVAL_SYM:
    x->sym = malloc(strlen(v->sym) + 1);
    strcpy(x->sym, v->sym);
    break;

  case LVAL_SEXPR:
  case LVAL_QEXPR:
    x->count = v->count;
    x->cell = malloc(sizeof(lval *) * x->count);
    for (int i = 0; i < x->count; i++)
    {
      x->cell[i] = lval_copy(v->cell[i]);
    }
    break;
  }
  return x;
}

lval *lval_add(lval *v, lval *x)
{
  v->count++;
  v->cell = realloc(v->cell, sizeof(lval *) * v->count);
  v->cell[v->count - 1] = x;
  return v;
}

lval *lval_pop(lval *v, int i)
{
  // Find item at index i
  lval *x = v->cell[i];

  // Shift memory
  memmove(&v->cell[i], &v->cell[i + 1], sizeof(lval *) * (v->count - i - 1));

  v->count--;

  v->cell = realloc(v->cell, sizeof(lval *) * v->count);
  return x;
}

lval *lval_join(lval *x, lval *y)
{
  while (y->count)
  {
    x = lval_add(x, lval_pop(y, 0));
  }

  /* Delete empty 'y' */
  lval_del(y);
  return x;
}

lval *lval_take(lval *v, int i)
{
  lval *x = lval_pop(v, i);
  lval_del(v);
  return x;
}

/* Print lval */
void lval_print(lval *v);
void lval_expr_print(lval *v, char open, char close)
{
  putchar(open);
  for (int i = 0; i < v->count; i++)
  {

    /* Print Value contained within */
    lval_print(v->cell[i]);

    /* Don't print trailing space if last element */
    if (i != (v->count - 1))
    {
      putchar(' ');
    }
  }
  putchar(close);
}

void lval_print(lval *v)
{
  switch (v->type)
  {
  case LVAL_NUM:
    printf("%li", v->num);
    break;
  case LVAL_ERR:
    printf("Error: %s", v->err);
    break;
  case LVAL_SYM:
    printf("%s", v->sym);
    break;
  case LVAL_SEXPR:
    lval_expr_print(v, '(', ')');
    break;
  case LVAL_QEXPR:
    lval_expr_print(v, '{', '}');
    break;
  case LVAL_FUN:
    printf("<function>");
    break;
  }
}

void lval_println(lval *v)
{
  lval_print(v);
  putchar('\n');
}

struct lenv
{
  int count;
  char **syms;
  lval **vals;
};

lenv *lenv_new(void)
{
  lenv *e = malloc(sizeof(lenv));
  e->count = 0;
  e->syms = NULL;
  e->vals = NULL;
  return e;
}

lval *lenv_get(lenv *e, lval *k)
{
  for (int i = 0; i < e->count; i++)
  {
    // Check if the stored string matches the symbol string
    // If it does, return a copy of the value
    if (strcmp(e->syms[i], k->sym) == 0)
    {
      return lval_copy(e->vals[i]);
    }
  }

  return lval_err("unbound symbol!");
}

void lenv_put(lenv *e, lval *k, lval *v)
{
  for (int i = 0; i < e->count; i++)
  {
    if (strcmp(e->syms[i], k->sym) == 0)
    {
      lval_del(e->vals[i]);
      e->vals[i] = lval_copy(v);
      return;
    }
  }

  /* New entry, make space */
  e->count++;
  e->vals = realloc(e->vals, sizeof(lval *) * e->count);
  e->syms = realloc(e->syms, sizeof(char *) * e->count);

  e->vals[e->count - 1] = lval_copy(v);
  e->syms[e->count - 1] = malloc(strlen(k->sym) + 1);
  strcpy(e->syms[e->count - 1], k->sym);
}

void lenv_del(lenv *e)
{
  for (int i = 0; i < e->count; i++)
  {
    free(e->syms[i]);
    lval_del(e->vals[i]);
  }
  free(e->syms);
  free(e->vals);
  free(e);
}

lval *lval_eval(lenv *e, lval *v);

lval *builtin_op(lenv *e, lval *a, char *op)
{
  for (int i = 0; i < a->count; i++)
  {
    if (a->cell[i]->type != LVAL_NUM)
    {
      lval_del(a);
      return lval_err("Cannot operate on non-number!");
    }
  }

  // Get frist element;
  lval *x = lval_pop(a, 0);

  /* If no arguments and sub then perform unary negation */
  if ((strcmp(op, "-") == 0) && a->count == 0)
  {
    x->num = -x->num;
  }

  if (strcmp(op, "incr") == 0 && a->count == 0)
  {
    x->num = x->num + 1;
  }

  if (strcmp(op, "decr") == 0 && a->count == 0)
  {
    x->num = x->num - 1;
  }

  while (a->count > 0)
  {
    // pop next element
    lval *y = lval_pop(a, 0);
    if (strcmp(op, "+") == 0 || strcmp(op, "add") == 0)
    {
      x->num += y->num;
    }
    if (strcmp(op, "-") == 0 || strcmp(op, "sub") == 0)
    {
      x->num -= y->num;
    }
    if (strcmp(op, "*") == 0 || strcmp(op, "mul") == 0)
    {
      x->num *= y->num;
    }
    if (strcmp(op, "%") == 0 || strcmp(op, "mod") == 0)
    {
      x->num = fmod(x->num, y->num);
    }
    if (strcmp(op, "/") == 0 || strcmp(op, "div") == 0)
    {
      if (y->num == 0)
      {
        lval_del(x);
        lval_del(y);
        x = lval_err("Division By Zero!");
        break;
      }
      x->num /= y->num;
    }
    lval_del(y);
  }

  lval_del(a);
  return x;
}

lval *builtin_head(lval *e, lval *a)
{
  LASSERT(a, a->count == 1,
          "Function 'head' passed too many arguments!");
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
          "Function 'head' passed incorrect type!");
  LASSERT(a, a->cell[0]->count != 0,
          "Function 'head' passed {}!");

  /* Valid usage take first argument */
  lval *v = lval_take(a, 0);

  /* Clear the rest */
  while (v->count > 1)
  {
    lval_del(lval_pop(v, 1));
  }

  return v;
}

lval *builtin_tail(lval *e, lval *a)
{
  LASSERT(a, a->count == 1,
          "Function 'tail' passed too many arguments!");
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
          "Function 'tail' passed incorrect type!");
  LASSERT(a, a->cell[0]->count != 0,
          "Function 'tail' passed {}!");

  /* Valid usage take first argument */
  lval *v = lval_take(a, 0);

  /* Delete the first element and return */
  lval_del(lval_pop(v, 0));
  return v;
}

lval *builtin_list(lval *e, lval *a)
{
  a->type = LVAL_QEXPR;
  return a;
}

lval *builtin_eval(lenv *e, lval *a)
{
  LASSERT(a, a->count == 1, "Function 'eval' too many arguments");
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR, "Function 'eval' passed incorrect type");

  lval *x = lval_take(a, 0);
  x->type = LVAL_SEXPR;
  return lval_eval(e, x);
}

lval *builtin_join(lval *e, lval *a)
{
  for (int i = 0; i < a->count; i++)
  {
    LASSERT(a, a->cell[i]->type == LVAL_QEXPR,
            "Function 'join', passed incorrect type");
  }
  lval *x = lval_pop(a, 0);
  while (a->count)
  {
    x = lval_join(x, lval_pop(a, 0));
  }

  lval_del(a);
  return x;
}

lval *builtin_len(lval *a)
{
  LASSERT(a, a->count == 1,
          "Function 'len' passed too many arguments!");
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
          "Function 'len' passed incorrect type!");

  long count = 0;
  lval *v = lval_take(a, 0);

  /* Clear the rest */
  while (v->count)
  {
    lval *tmp = lval_pop(v, 0);
    if (tmp->type == LVAL_QEXPR)
    {
      lval *tmpcount = builtin_len(tmp);
      count += tmpcount->count;
    }
    else
    {
      count += 1;
    }
  }
  return lval_num(count);
}

lval *builtin_cons(lval *a)
{
  LASSERT(a, a->count == 2,
          "Function 'cons' passed too many arguments!");
  LASSERT(a, a->cell[1]->type == LVAL_QEXPR,
          "Function 'const' passed incorrect type!");
  lval *result = lval_qexpr();

  lval *v = lval_pop(a, 0);
  lval_add(result, v);
  result = lval_join(result, lval_pop(a, 0));
  return result;
}

lval *builtin_add(lenv *e, lval *a)
{
  return builtin_op(e, a, "+");
}

lval *builtin_sub(lenv *e, lval *a)
{
  return builtin_op(e, a, "-");
}

lval *builtin_mul(lenv *e, lval *a)
{
  return builtin_op(e, a, "*");
}

lval *builtin_div(lenv *e, lval *a)
{
  return builtin_op(e, a, "/");
}

void lenv_add_builtin(lenv *e, char *name, lbuiltin func)
{
  lval *k = lval_sym(name);
  lval *v = lval_fun(func);
  lenv_put(e, k, v);
  lval_del(k);
  lval_del(v);
}

void lenv_add_builtins(lenv *e)
{
  /* List Functions */
  lenv_add_builtin(e, "list", builtin_list);
  // lenv_add_builtin(e, "head", builtin_head);
  // lenv_add_builtin(e, "tail", builtin_tail);
  // lenv_add_builtin(e, "eval", builtin_eval);
  // lenv_add_builtin(e, "join", builtin_join);
  // lenv_add_builtin(e, "cons", builtin_cons);
  // lenv_add_builtin(e, "len", builtin_len);

  // /* Mathematical Functions */
  // lenv_add_builtin(e, "+", builtin_add);
  // lenv_add_builtin(e, "-", builtin_sub);
  // lenv_add_builtin(e, "*", builtin_mul);
  // lenv_add_builtin(e, "/", builtin_div);
  // lenv_add_builtin(e, "add", builtin_add);
  // lenv_add_builtin(e, "sub", builtin_sub);
  // lenv_add_builtin(e, "mul", builtin_mul);
  // lenv_add_builtin(e, "div", builtin_div);
}

// Evaluation

lval *lval_eval_sexpr(lenv *e, lval *v)
{
  for (int i = 0; i < v->count; i++)
  {
    v->cell[i] = lval_eval(e, v->cell[i]);
  }

  for (int i = 0; i < v->count; i++)
  {
    if (v->cell[i]->type == LVAL_ERR)
    {
      return lval_take(v, i);
    }
  }

  if (v->count == 0)
  {
    return v;
  }
  if (v->count == 1)
  {
    return lval_take(v, 0);
  }

  /* Ensure first element is a function after evaluation */
  lval *f = lval_pop(v, 0);
  if (f->type != LVAL_FUN)
  {
    lval_del(v);
    lval_del(f);
    return lval_err("first element is not a function");
  }

  /* If so call function to get result */
  lval *result = f->fun(e, v);
  lval_del(f);
  return result;
}

lval *lval_eval(lenv *e, lval *v)
{
  if (v->type == LVAL_SYM)
  {
    lval *x = lenv_get(e, v);
    lval_del(v);
    return x;
  }

  if (v->type == LVAL_SEXPR)
  {
    return lval_eval_sexpr(e, v);
  }
  return v;
}

// Reading
lval *lval_read_num(mpc_ast_t *t)
{
  errno = 0;
  long x = strtol(t->contents, NULL, 10);
  return errno != ERANGE ? lval_num(x) : lval_err("invalid number");
}

lval *lval_read(mpc_ast_t *t)
{

  /* If Symbol or Number return conversion to that type */
  if (strstr(t->tag, "number"))
  {
    return lval_read_num(t);
  }
  if (strstr(t->tag, "symbol"))
  {
    return lval_sym(t->contents);
  }

  /* If root (>) or sexpr then create empty list */
  lval *x = NULL;
  if (strcmp(t->tag, ">") == 0)
  {
    x = lval_sexpr();
  }

  if (strstr(t->tag, "sexpr"))
  {
    x = lval_sexpr();
  }

  if (strstr(t->tag, "qexpr"))
  {
    x = lval_qexpr();
  }

  /* Fill this list with any valid expression contained within */
  for (int i = 0; i < t->children_num; i++)
  {
    if (strcmp(t->children[i]->contents, "(") == 0)
    {
      continue;
    }
    if (strcmp(t->children[i]->contents, ")") == 0)
    {
      continue;
    }
    if (strcmp(t->children[i]->contents, "{") == 0)
    {
      continue;
    }
    if (strcmp(t->children[i]->contents, "}") == 0)
    {
      continue;
    }
    if (strcmp(t->children[i]->tag, "regex") == 0)
    {
      continue;
    }
    x = lval_add(x, lval_read(t->children[i]));
  }

  return x;
}

int main(int argc, char **argv)
{
  mpc_parser_t *Number = mpc_new("number");
  mpc_parser_t *Symbol = mpc_new("symbol");
  mpc_parser_t *Sexpr = mpc_new("sexpr");
  mpc_parser_t *Qexpr = mpc_new("qexpr");
  mpc_parser_t *Expr = mpc_new("expr");
  mpc_parser_t *NotLispy = mpc_new("notlispy");
  /* Define them with the following Language */
  mpca_lang(MPCA_LANG_DEFAULT,
            " number       : /[+-]?([0-9]*[.])?[0-9]+/ ;"
            " symbol       : /[a-zA-Z0-9_+\\-*\\/\\\\=<>!&]+/ ;"
            " sexpr        : '(' <expr>* ')' ;"
            " qexpr        : '{' <expr>* '}' ;"
            " expr         : <number> | <symbol> | <sexpr> | <qexpr>;"
            " notlispy     : /^/ <expr>* /$/ ;   ",
            Number, Symbol, Sexpr, Qexpr, Expr, NotLispy);

  lenv *e = lenv_new();
  lenv_add_builtins(e);
  while (1)
  {
    char *input = readline("not-lisp > ");
    add_history(input);
    mpc_result_t r;
    if (mpc_parse("<stdin>", input, NotLispy, &r))
    {
      /* On success print and delete the AST */
      // mpc_ast_print(r.output);
      // lval result = eval(r.output);
      lval *x = lval_eval(e, lval_read(r.output));
      lval_println(x);
      lval_del(x);
      mpc_ast_delete(r.output);
    }
    else
    {
      /* Otherwise print and delete the Error */
      mpc_err_print(r.error);
      mpc_err_delete(r.error);
    }

    free(input);
  }
  lenv_del(e);
  mpc_cleanup(6, Number, Symbol, Sexpr, Qexpr, Expr, NotLispy);
  return 0;
}
