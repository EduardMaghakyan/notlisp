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

#define LASSERT(args, cond, fmt, ...)         \
  if (!(cond))                                \
  {                                           \
    lval *err = lval_err(fmt, ##__VA_ARGS__); \
    lval_del(args);                           \
    return err;                               \
  }

#define LASSERT_TYPE(func, args, index, expect)                                        \
  LASSERT(args, args->cell[index]->type == expect,                                     \
          "Function '%s' passed incorrect type for argument %i. Got %s, Expected %s.", \
          func, index, ltype_name(args->cell[index]->type), ltype_name(expect))

#define LASSERT_NOT_EMPTY(func, args, index)   \
  LASSERT(args, args->cell[index]->count != 0, \
          "Function '%s' passed {} for argument %i.", func, index);

#define LASSERT_COUNT(func, args, num)                                                \
  LASSERT(args, args->count == num,                                                   \
          "Function '%s' passed incorrect number of arguments. Got %i, Expected %i.", \
          func, args->count, num)

#define LASSERT_NUM(func, args, num)                             \
  LASSERT(args, args->count == num,                              \
          "Function '%s' passed incorrect number of arguments. " \
          "Got %i, Expected %i.",                                \
          func, args->count, num)

mpc_parser_t *Number;
mpc_parser_t *Boolean;
mpc_parser_t *Symbol;
mpc_parser_t *Sexpr;
mpc_parser_t *Qexpr;
mpc_parser_t *Expr;
mpc_parser_t *NotLispy;
mpc_parser_t *String;
mpc_parser_t *Comment;

struct lval;
struct lenv;
typedef struct lval lval;
typedef struct lenv lenv;

/* Possible lval types, error or number */
enum
{
  LVAL_ERR,
  LVAL_NUM,
  LVAL_BOOl,
  LVAL_SYM,
  LVAL_FUN,
  LVAL_STR,
  LVAL_SEXPR,
  LVAL_QEXPR
};

char *ltype_name(int t)
{
  switch (t)
  {
  case LVAL_FUN:
    return "Function";
  case LVAL_NUM:
    return "Number";
  case LVAL_BOOl:
    return "Boolean";
  case LVAL_STR:
    return "String";
  case LVAL_ERR:
    return "Error";
  case LVAL_SYM:
    return "Symbol";
  case LVAL_SEXPR:
    return "S-Expression";
  case LVAL_QEXPR:
    return "Q-Expression";
  default:
    return "Unknown";
  }
}

typedef lval *(*lbuiltin)(lenv *, lval *);
typedef enum
{
  false,
  true
} bool;

struct lval
{
  int type;
  long num;
  char *err;
  char *sym;
  char *str;

  lbuiltin builtin;
  lenv *env;
  lval *formals;
  lval *body;

  int count;
  lval **cell;
};

lval *lval_str(char *x)
{
  lval *v = malloc(sizeof(lval));
  v->type = LVAL_STR;
  v->str = malloc(strlen(x) + 1);
  strcpy(v->str, x);
  return v;
}

lval *lval_num(long x)
{
  lval *v = malloc(sizeof(lval));
  v->type = LVAL_NUM;
  v->num = x;
  return v;
}

lval *lval_bool(long x)
{
  lval *v = malloc(sizeof(lval));
  v->type = LVAL_BOOl;
  v->num = !!x;
  return v;
}

lval *lval_err(char *fmt, ...)
{
  lval *v = malloc(sizeof(lval));
  v->type = LVAL_ERR;

  /* Create and init list */
  va_list va;
  va_start(va, fmt);

  /* Allocate 512 bytes for error */
  v->err = malloc(512);

  vsnprintf(v->err, 511, fmt, va);

  v->err = realloc(v->err, strlen(v->err) + 1);

  /* Clean up list */
  va_end(va);

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
  v->builtin = func;
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

void lenv_del(lenv *e);
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

  case LVAL_STR:
    free(v->str);
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
    if (!v->builtin)
    {
      lenv_del(v->env);
      lval_del(v->formals);
      lval_del(v->body);
    }
    break;
  }

  /* Free the memory allocated for the "lval" struct itself */
  free(v);
}

lenv *lenv_copy(lenv *e);
lval *lval_copy(lval *v)
{
  lval *x = malloc(sizeof(lval));
  x->type = v->type;

  switch (v->type)
  {
  case LVAL_FUN:
    if (v->builtin)
    {
      x->builtin = v->builtin;
    }
    else
    {
      x->builtin = NULL;
      x->env = lenv_copy(v->env);
      x->formals = lval_copy(v->formals);
      x->body = lval_copy(v->body);
    }
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

  case LVAL_STR:
    x->str = malloc(strlen(v->str) + 1);
    strcpy(x->str, v->str);
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

lval *lval_join(lval *x, lval *y)
{
  for (int i = 0; i < y->count; i++)
  {
    x = lval_add(x, y->cell[i]);
  }
  free(y->cell);
  free(y);
  return x;
}

lval *lval_pop(lval *v, int i)
{
  lval *x = v->cell[i];
  memmove(&v->cell[i],
          &v->cell[i + 1], sizeof(lval *) * (v->count - i - 1));
  v->count--;
  v->cell = realloc(v->cell, sizeof(lval *) * v->count);
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

void lval_print_str(lval *v)
{
  char *escaped = malloc(strlen(v->str) + 1);
  strcpy(escaped, v->str);
  escaped = mpcf_escape(escaped);
  printf("\"%s\"", escaped);
  free(escaped);
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
    printf("<%s>", v->sym);
    break;
  case LVAL_BOOl:
    if (v->num)
    {
      printf("%s", "true");
    }
    else
    {
      printf("%s", "false");
    }
    break;
  case LVAL_STR:
    lval_print_str(v);
    break;
  }
}

void lval_println(lval *v)
{
  lval_print(v);
  putchar('\n');
}

/* Environment */
struct lenv
{
  lenv *parent;
  int count;
  char **syms;
  lval **vals;
};

lenv *lenv_new(void)
{
  lenv *e = malloc(sizeof(lenv));
  e->parent = NULL;
  e->count = 0;
  e->syms = NULL;
  e->vals = NULL;
  return e;
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

lenv *lenv_copy(lenv *e)
{
  lenv *n = malloc(sizeof(lenv));
  n->parent = e->parent;
  n->count = e->count;
  n->syms = malloc(sizeof(char *) * n->count);
  n->vals = malloc(sizeof(lval *) * n->count);
  for (int i = 0; i < e->count; i++)
  {
    n->syms[i] = malloc(strlen(e->syms[i]) + 1);
    strcpy(n->syms[i], e->syms[i]);
    n->vals[i] = lval_copy(e->vals[i]);
  }
  return n;
}

lval *lval_lambda(lval *formals, lval *body)
{
  lval *v = malloc(sizeof(lval));
  v->type = LVAL_FUN;

  // Indicate that it's not builtin function
  v->builtin = NULL;

  // Build env for the lambda.
  v->env = lenv_new();

  v->formals = formals;
  v->body = body;

  return v;
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

  if (e->parent)
  {
    return lenv_get(e->parent, k);
  }

  return lval_err("unbound symbol '%s'!", k->sym);
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

void lenv_def(lenv *e, lval *k, lval *v)
{
  while (e->parent)
  {
    e = e->parent;
  }

  lenv_put(e, k, v);
}

lval *lval_eval(lenv *e, lval *v);

lval *builtin_list(lenv *e, lval *a)
{
  a->type = LVAL_QEXPR;
  return a;
}

lval *builtin_head(lenv *e, lval *a)
{
  LASSERT_COUNT("head", a, 1);
  LASSERT_TYPE("head", a, 0, LVAL_QEXPR);
  LASSERT_NOT_EMPTY("head", a, 0);

  /* Valid usage take first argument */
  lval *v = lval_take(a, 0);

  /* Clear the rest */
  while (v->count > 1)
  {
    lval_del(lval_pop(v, 1));
  }

  return v;
}

lval *builtin_tail(lenv *e, lval *a)
{
  LASSERT_COUNT("tail", a, 1);
  LASSERT_TYPE("tail", a, 0, LVAL_QEXPR);
  LASSERT_NOT_EMPTY("tail", a, 0);

  /* Valid usage take first argument */
  lval *v = lval_take(a, 0);

  /* Delete the first element and return */
  lval_del(lval_pop(v, 0));
  return v;
}

lval *builtin_eval(lenv *e, lval *a)
{
  LASSERT_COUNT("eval", a, 1);
  LASSERT_TYPE("eval", a, 0, LVAL_QEXPR);
  LASSERT_NOT_EMPTY("eval", a, 0);

  lval *x = lval_take(a, 0);
  x->type = LVAL_SEXPR;
  return lval_eval(e, x);
}

lval *builtin_join(lenv *e, lval *a)
{
  for (int i = 0; i < a->count; i++)
  {
    LASSERT_TYPE("eval", a, i, LVAL_QEXPR);
  }
  lval *x = lval_pop(a, 0);
  while (a->count)
  {
    x = lval_join(x, lval_pop(a, 0));
  }

  lval_del(a);
  return x;
}

lval *builtin_len(lenv *e, lval *a)
{
  LASSERT_COUNT("len", a, 1);
  LASSERT_TYPE("len", a, 0, LVAL_QEXPR);
  LASSERT_NOT_EMPTY("len", a, 0);

  long count = 0;
  lval *v = lval_take(a, 0);

  /* Clear the rest */
  while (v->count)
  {
    lval *tmp = lval_pop(v, 0);
    if (tmp->type == LVAL_QEXPR)
    {
      lval *tmpcount = builtin_len(e, tmp);
      count += tmpcount->count;
    }
    else
    {
      count += 1;
    }
  }
  return lval_num(count);
}

lval *builtin_cons(lenv *e, lval *a)
{
  LASSERT_COUNT("cons", a, 1);
  LASSERT_TYPE("cons", a, 0, LVAL_QEXPR);
  LASSERT_NOT_EMPTY("cons", a, 0);

  lval *result = lval_qexpr();

  lval *v = lval_pop(a, 0);
  lval_add(result, v);
  result = lval_join(result, lval_pop(a, 0));
  return result;
}

lval *builtin_lambda(lenv *e, lval *a)
{
  LASSERT_NUM("\\", a, 2);
  LASSERT_TYPE("\\", a, 0, LVAL_QEXPR);
  LASSERT_TYPE("\\", a, 1, LVAL_QEXPR);

  for (int i = 0; i < a->cell[0]->count; i++)
  {
    LASSERT(a, (a->cell[0]->cell[i]->type == LVAL_SYM),
            "Cannot define non-symbol. Got %s, Expected %s.",
            ltype_name(a->cell[0]->cell[i]->type), ltype_name(LVAL_SYM));
  }

  lval *formals = lval_pop(a, 0);
  lval *body = lval_pop(a, 0);
  lval_del(a);

  return lval_lambda(formals, body);
}

lval *builtin_op(lenv *e, lval *a, char *op)
{
  for (int i = 0; i < a->count; i++)
  {
    LASSERT_TYPE(op, a, i, LVAL_NUM);
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

lval *builtin_ord(lenv *e, lval *a, char *op)
{
  LASSERT_NUM(op, a, 2);
  LASSERT_TYPE(op, a, 0, LVAL_NUM);
  LASSERT_TYPE(op, a, 1, LVAL_NUM);

  int r;
  if (strcmp(op, ">") == 0)
  {
    r = (a->cell[0]->num > a->cell[1]->num);
  }

  if (strcmp(op, "<") == 0)
  {
    r = (a->cell[0]->num < a->cell[1]->num);
  }

  if (strcmp(op, ">=") == 0)
  {
    r = (a->cell[0]->num >= a->cell[1]->num);
  }

  if (strcmp(op, "<=") == 0)
  {
    r = (a->cell[0]->num <= a->cell[1]->num);
  }

  lval_del(a);
  return lval_num(r);
}

lval *builtin_gt(lenv *e, lval *a)
{
  return builtin_ord(e, a, ">");
}

lval *builtin_gte(lenv *e, lval *a)
{
  return builtin_ord(e, a, ">=");
}

lval *builtin_lt(lenv *e, lval *a)
{
  return builtin_ord(e, a, "<");
}

lval *builtin_lte(lenv *e, lval *a)
{
  return builtin_ord(e, a, "<=");
}

lval *lval_read(mpc_ast_t *t);
lval *builtin_load(lenv *e, lval *a)
{
  LASSERT_NUM("load", a, 1);
  LASSERT_TYPE("load", a, 0, LVAL_STR);

  mpc_result_t r;
  if (mpc_parse_contents(a->cell[0]->str, NotLispy, &r))
  {

    lval *expr = lval_read(r.output);
    mpc_ast_delete(r.output);

    while (expr->count)
    {
      lval *x = lval_eval(e, lval_pop(expr, 0));
      if (x->type == LVAL_ERR)
      {
        lval_println(x);
      }
      lval_del(x);
    }
    lval_del(expr);
    lval_del(a);
    return lval_sexpr();
  }
  else
  {
    char *err_msg = mpc_err_string(r.error);
    mpc_err_delete(r.error);
    lval *err = lval_err("Could not load Library %s", err_msg);
    free(err_msg);
    lval_del(a);

    return err;
  }
}

lval *builtin_print(lenv *e, lval *a)
{

  /* Print each argument followed by a space */
  for (int i = 0; i < a->count; i++)
  {
    lval_print(a->cell[i]);
    putchar(' ');
  }

  /* Print a newline and delete arguments */
  putchar('\n');
  lval_del(a);

  return lval_sexpr();
}

lval *builtin_error(lenv *e, lval *a)
{
  LASSERT_NUM("error", a, 1);
  LASSERT_TYPE("error", a, 0, LVAL_STR);

  /* Construct Error from first argument */
  lval *err = lval_err(a->cell[0]->str);

  /* Delete arguments and return */
  lval_del(a);
  return err;
}

int lval_eq(lval *x, lval *y)
{
  if (x->type != y->type)
  {
    return 0;
  }

  switch (x->type)
  {
  case LVAL_NUM:
  case LVAL_BOOl:
    return (x->num == y->num);
  case LVAL_STR:
    return (strcmp(x->str, y->str) == 0);
  case LVAL_ERR:
    return (strcmp(x->err, y->err) == 0);
  case LVAL_SYM:
    return (strcmp(x->sym, y->sym) == 0);
  case LVAL_FUN:
    if (x->builtin || y->builtin)
    {
      return x->builtin == y->builtin;
    }
    else
    {
      return lval_eq(x->formals, y->formals) && lval_eq(x->body, y->body);
    }
  case LVAL_SEXPR:
  case LVAL_QEXPR:
    if (x->count != y->count)
    {
      return 0;
    }
    for (int i = 0; i < x->count; i++)
    {
      if (!lval_eq(x->cell[i], y->cell[i]))
      {
        return 0;
      }
    }
    return 1;
    break;
  }
  return 0;
}

lval *builtin_cmp(lenv *e, lval *a, char *op)
{
  int r;
  if (strcmp(op, "==") == 0)
  {
    r = lval_eq(a->cell[0], a->cell[1]);
  }
  if (strcmp(op, "!=") == 0)
  {
    r = !lval_eq(a->cell[0], a->cell[1]);
  }
  lval_del(a);
  return lval_num(r);
}

lval *builtin_eq(lenv *e, lval *a)
{
  return builtin_cmp(e, a, "==");
}

lval *builtin_ne(lenv *e, lval *a)
{
  return builtin_cmp(e, a, "!=");
}

lval *builtin_if(lenv *e, lval *a)
{
  LASSERT_NUM("if", a, 3);
  LASSERT_TYPE("if", a, 1, LVAL_QEXPR);
  LASSERT_TYPE("if", a, 2, LVAL_QEXPR);

  /* Mark Both Expressions as evaluable */
  lval *x;
  a->cell[1]->type = LVAL_SEXPR;
  a->cell[2]->type = LVAL_SEXPR;

  if (a->cell[0]->num)
  {
    x = lval_eval(e, lval_pop(a, 1));
  }
  else
  {
    x = lval_eval(e, lval_pop(a, 2));
  }

  lval_del(a);
  return x;
}

lval *builtin_and(lenv *e, lval *a)
{
  LASSERT_NUM("&&", a, 2);

  int x;
  if (a->cell[0]->type == LVAL_QEXPR)
  {
    a->cell[0]->type = LVAL_SEXPR;
  }

  if (a->cell[1]->type == LVAL_QEXPR)
  {
    a->cell[1]->type = LVAL_SEXPR;
  }

  lval *b = lval_eval(e, lval_pop(a, 0));
  lval *c = lval_eval(e, lval_pop(a, 0));
  x = (b->num && c->num);
  return lval_bool(x);
}

lval *builtin_or(lenv *e, lval *a)
{
  LASSERT_NUM("||", a, 2);

  int x;
  if (a->cell[0]->type == LVAL_QEXPR)
  {
    a->cell[0]->type = LVAL_SEXPR;
  }

  if (a->cell[1]->type == LVAL_QEXPR)
  {
    a->cell[1]->type = LVAL_SEXPR;
  }

  lval *b = lval_eval(e, lval_pop(a, 0));
  lval *c = lval_eval(e, lval_pop(a, 0));
  x = (b->num || c->num);
  return lval_bool(x);
}

lval *builtin_not(lenv *e, lval *a)
{
  LASSERT_NUM("!", a, 1);
  int x;
  if (a->cell[0]->type == LVAL_QEXPR)
  {
    a->cell[0]->type = LVAL_SEXPR;
  }

  lval *b = lval_eval(e, lval_pop(a, 0));
  x = !b->num;
  return lval_bool(x);
}

lval *builtin_var(lenv *e, lval *a, char *func)
{
  LASSERT_TYPE(func, a, 0, LVAL_QEXPR);

  lval *syms = a->cell[0];
  for (int i = 0; i < syms->count; i++)
  {
    LASSERT(a, (syms->cell[i]->type == LVAL_SYM),
            "Function '%s' cannot define non-symbol. "
            "Got %s, Expected %s.",
            func,
            ltype_name(syms->cell[i]->type), ltype_name(LVAL_SYM));
  }

  LASSERT(a, (syms->count == a->count - 1),
          "Function '%s' passed too many arguments for symbols. "
          "Got %i, Expected %i.",
          func, syms->count, a->count - 1);

  for (int i = 0; i < syms->count; i++)
  {
    /* If 'def' define in globally. If 'put' define in locally */
    if (strcmp(func, "def") == 0)
    {
      lenv_def(e, syms->cell[i], a->cell[i + 1]);
    }

    if (strcmp(func, "=") == 0)
    {
      lenv_put(e, syms->cell[i], a->cell[i + 1]);
    }
  }

  lval_del(a);
  return lval_sexpr();
}

lval *builtin_def(lenv *e, lval *a)
{
  return builtin_var(e, a, "def");
}

lval *builtin_fun(lenv *e, lval *a)
{
  lval *sym = lval_pop(a->cell[0], 0);
  lenv_put(e, sym, lval_lambda(a->cell[0], a->cell[1]));
  lval_del(a);
  lval_del(sym);
  return lval_sexpr();
}

lval *builtin_put(lenv *e, lval *a)
{
  return builtin_var(e, a, "=");
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
  /* Function on Variables */
  lenv_add_builtin(e, "def", builtin_def);
  lenv_add_builtin(e, "fun", builtin_fun);

  /* Functions on Lists */
  lenv_add_builtin(e, "list", builtin_list);
  lenv_add_builtin(e, "head", builtin_head);
  lenv_add_builtin(e, "tail", builtin_tail);
  lenv_add_builtin(e, "eval", builtin_eval);
  lenv_add_builtin(e, "join", builtin_join);
  lenv_add_builtin(e, "cons", builtin_cons);
  lenv_add_builtin(e, "len", builtin_len);

  /* Mathematical Functions */
  lenv_add_builtin(e, "+", builtin_add);
  lenv_add_builtin(e, "-", builtin_sub);
  lenv_add_builtin(e, "*", builtin_mul);
  lenv_add_builtin(e, "/", builtin_div);
  lenv_add_builtin(e, "add", builtin_add);
  lenv_add_builtin(e, "sub", builtin_sub);
  lenv_add_builtin(e, "mul", builtin_mul);
  lenv_add_builtin(e, "div", builtin_div);
  lenv_add_builtin(e, "\\", builtin_lambda);

  /* Define global  variables. */
  lenv_add_builtin(e, "=", builtin_put);

  /* Conditionals */
  lenv_add_builtin(e, ">", builtin_gt);
  lenv_add_builtin(e, "<", builtin_lt);
  lenv_add_builtin(e, ">=", builtin_gte);
  lenv_add_builtin(e, "<=", builtin_lte);
  lenv_add_builtin(e, "==", builtin_eq);
  lenv_add_builtin(e, "!=", builtin_ne);
  lenv_add_builtin(e, "if", builtin_if);

  /* Boolean logic */
  lenv_add_builtin(e, "&&", builtin_and);
  lenv_add_builtin(e, "||", builtin_or);
  lenv_add_builtin(e, "!", builtin_not);

  /* String Functions */
  lenv_add_builtin(e, "load", builtin_load);
  lenv_add_builtin(e, "error", builtin_error);
  lenv_add_builtin(e, "print", builtin_print);
}

// Evaluation
lval *lval_call(lenv *e, lval *f, lval *a)
{
  if (f->builtin)
  {
    return f->builtin(e, a);
  }
  int given = a->count;
  int total = f->formals->count;

  while (a->count)
  {
    if (f->formals->count == 0)
    {
      lval_del(a);
      return lval_err("Function passed too many arguments. "
                      "Got %i, Expected %i.",
                      given, total);
    }

    lval *sym = lval_pop(f->formals, 0);

    if (strcmp(sym->sym, "&") == 0)
    {
      if (f->formals->count != 1)
      {
        lval_del(a);
        return lval_err("Function format invalid. "
                        "Symbol '&' not followed by single symbol.");
      }
      lval *nsym = lval_pop(f->formals, 0);
      lenv_put(f->env, nsym, builtin_list(e, a));
      lval_del(sym);
      lval_del(nsym);
      break;
    }

    lval *val = lval_pop(a, 0);
    lenv_put(f->env, sym, val);
    lval_del(sym);
    lval_del(val);
  }
  lval_del(a);

  if (f->formals->count > 0 &&
      strcmp(f->formals->cell[0]->sym, "&") == 0)
  {

    /* Check to ensure that & is not passed invalidly. */
    if (f->formals->count != 2)
    {
      return lval_err("Function format invalid. "
                      "Symbol '&' not followed by single symbol.");
    }

    lval_del(lval_pop(f->formals, 0));
    lval *sym = lval_pop(f->formals, 0);
    lval *val = lval_qexpr();
    lenv_put(f->env, sym, val);
    lval_del(sym);
    lval_del(val);
  }

  if (f->formals->count == 0)
  {

    f->env->parent = e;
    return builtin_eval(f->env,
                        lval_add(lval_sexpr(), lval_copy(f->body)));
  }
  else
  {
    return lval_copy(f);
  }
}

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
    return lval_eval(e, lval_take(v, 0));
  }

  /* Ensure first element is a function after evaluation */
  lval *f = lval_pop(v, 0);
  if (f->type != LVAL_FUN)
  {
    lval *err = lval_err(
        "S-Expression starts with incorrect type. "
        "Got %s, Expected %s.",
        ltype_name(f->type), ltype_name(LVAL_FUN));
    lval_del(f);
    lval_del(v);
    return err;
  }

  /* If so call function to get result */
  lval *result = lval_call(e, f, v);
  lval_del(f);
  return result;
}

lval *lval_eval(lenv *e, lval *v)
{
  if (v->type == LVAL_SYM)
  {
    lval *x = lenv_get(e, v);
    x->sym = malloc(strlen(v->sym) + 1);
    strcpy(x->sym, v->sym);
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

lval *lval_read_bool(mpc_ast_t *t)
{
  errno = 0;
  int x = (strcmp(t->contents, "true") == 0);
  return errno != ERANGE ? lval_bool(x) : lval_err("invalid number");
}

lval *lval_read_str(mpc_ast_t *t)
{
  t->contents[strlen(t->contents) - 1] = '\0';
  char *unescaped = malloc(strlen(t->contents + 1) + 1);
  strcpy(unescaped, t->contents + 1);
  unescaped = mpcf_unescape(unescaped);
  lval *str = lval_str(unescaped);
  free(unescaped);
  return str;
}

lval *lval_read(mpc_ast_t *t)
{

  /* If Symbol or Number return conversion to that type */
  if (strstr(t->tag, "number"))
  {
    return lval_read_num(t);
  }
  if (strstr(t->tag, "boolean"))
  {
    return lval_read_bool(t);
  }
  if (strstr(t->tag, "symbol"))
  {
    return lval_sym(t->contents);
  }

  if (strstr(t->tag, "string"))
  {
    return lval_read_str(t);
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

    if (strstr(t->children[i]->tag, "comment"))
    {
      continue;
    }
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
  Number = mpc_new("number");
  Boolean = mpc_new("boolean");
  Symbol = mpc_new("symbol");
  Sexpr = mpc_new("sexpr");
  Qexpr = mpc_new("qexpr");
  Expr = mpc_new("expr");
  NotLispy = mpc_new("notlispy");
  String = mpc_new("string");
  Comment = mpc_new("comment");

  /* Define them with the following Language */
  mpca_lang(MPCA_LANG_DEFAULT,
            " boolean      : /true|false/ ;"
            " string       : /\"(\\\\.|[^\"])*\"/ ;"
            " comment      : /;[^\\r\\n]*/ ;"
            " number       : /[+-]?([0-9]*[.])?[0-9]+/ ;"
            " symbol       : /[a-zA-Z0-9_+\\-*\\/\\\\=<>!&|]+/ ;"
            " sexpr        : '(' <expr>* ')' ;"
            " qexpr        : '{' <expr>* '}' ;"
            " expr         : <comment> | <string> | <boolean> | <number> | <symbol> | <sexpr> | <qexpr>;"
            " notlispy     : /^/ <expr>* /$/ ;   ",
            Comment, String, Boolean, Number, Symbol, Sexpr, Qexpr, Expr, NotLispy);

  lenv *e = lenv_new();
  lenv_add_builtins(e);

  if (argc == 1)
  {
    puts("Not Lispy Version 0.0.0.1.0");
    puts("Press Ctrl+c to Exit\n");

    while (1)
    {
      char *input = readline("not-lisp > ");
      if (strlen(input) > 0)
      {
        add_history(input);
      }
      mpc_result_t r;
      if (mpc_parse("<stdin>", input, NotLispy, &r))
      {
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
  }

  if (argc >= 2)
  {

    /* loop over each supplied filename (starting from 1) */
    for (int i = 1; i < argc; i++)
    {

      /* Argument list with a single argument, the filename */
      lval *args = lval_add(lval_sexpr(), lval_str(argv[i]));

      /* Pass to builtin load and get the result */
      lval *x = builtin_load(e, args);

      /* If the result is an error be sure to print it */
      if (x->type == LVAL_ERR)
      {
        lval_println(x);
      }
      lval_del(x);
    }
  }

  lenv_del(e);
  mpc_cleanup(9, String, Comment, Boolean, Number, Symbol, Sexpr, Qexpr, Expr, NotLispy);
  return 0;
}
