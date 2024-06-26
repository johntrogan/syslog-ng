/*
 * Copyright (c) 2023 Balázs Scheidler <balazs.scheidler@axoflow.com>
 * Copyright (c) 2023 shifter
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */

#include "filterx/expr-function.h"
#include "filterx/filterx-grammar.h"
#include "filterx/filterx-globals.h"
#include "filterx/filterx-eval.h"
#include "plugin.h"
#include "cfg.h"

GQuark
filterx_function_error_quark(void)
{
  return g_quark_from_static_string("filterx-function-error-quark");
}

typedef struct _FilterXSimpleFunction
{
  FilterXFunction super;
  GList *argument_expressions;
  FilterXSimpleFunctionProto function_proto;
} FilterXSimpleFunction;

static GPtrArray *
_simple_function_eval_expressions(GList *expressions)
{
  if (expressions == NULL)
    {
      return NULL;
    }
  GPtrArray *res = g_ptr_array_sized_new(g_list_length(expressions));
  g_ptr_array_set_free_func(res, (GDestroyNotify) filterx_object_unref);
  for (GList *head = expressions; head; head = head->next)
    {
      FilterXObject *fxo = filterx_expr_eval(head->data);

      if (fxo == NULL)
        goto error;
      g_ptr_array_add(res, fxo);
    }
  return res;
error:
  g_ptr_array_free(res, TRUE);
  return NULL;
}

static FilterXObject *
_simple_eval(FilterXExpr *s)
{
  FilterXSimpleFunction *self = (FilterXSimpleFunction *) s;

  GPtrArray *args = _simple_function_eval_expressions(self->argument_expressions);

  FilterXSimpleFunctionProto f = self->function_proto;

  g_assert(f != NULL);
  FilterXObject *res = f(args);

  if (args != NULL)
    g_ptr_array_free(args, TRUE);
  if (!res)
    filterx_eval_push_error("Function call failed", s, NULL);

  return res;
}

static void
_simple_free(FilterXExpr *s)
{
  FilterXSimpleFunction *self = (FilterXSimpleFunction *) s;
  g_list_free_full(self->argument_expressions, (GDestroyNotify) filterx_expr_unref);
  filterx_function_free_method(&self->super);
}

FilterXExpr *
filterx_simple_function_new(const gchar *function_name, GList *arguments, FilterXSimpleFunctionProto function_proto)
{
  FilterXSimpleFunction *self = g_new0(FilterXSimpleFunction, 1);

  filterx_function_init_instance(&self->super, function_name);
  self->super.super.eval = _simple_eval;
  self->super.super.free_fn = _simple_free;
  self->argument_expressions = arguments;
  self->function_proto = function_proto;

  return &self->super.super;
}

void
filterx_function_free_method(FilterXFunction *s)
{
  g_free(s->function_name);
  filterx_expr_free_method(&s->super);
}

static void
_function_free(FilterXExpr *s)
{
  FilterXFunction *self = (FilterXFunction *) s;
  filterx_function_free_method(self);
}

void
filterx_function_init_instance(FilterXFunction *s, const gchar *function_name)
{
  filterx_expr_init_instance(&s->super);
  s->function_name = g_strdup(function_name);
  s->super.free_fn = _function_free;
}

static FilterXExpr *
_lookup_simple_function(GlobalConfig *cfg, const gchar *function_name, GList *arguments)
{
  // Checking filterx builtin functions first
  FilterXSimpleFunctionProto f = filterx_builtin_simple_function_lookup(function_name);
  if (f != NULL)
    {
      return filterx_simple_function_new(function_name, arguments, f);
    }

  // fallback to plugin lookup
  Plugin *p = cfg_find_plugin(cfg, LL_CONTEXT_FILTERX_SIMPLE_FUNC, function_name);

  if (p == NULL)
    return NULL;

  f = plugin_construct(p);
  if (f == NULL)
    return NULL;

  return filterx_simple_function_new(function_name, arguments, f);
}

static FilterXExpr *
_lookup_function(GlobalConfig *cfg, const gchar *function_name, GList *arguments, GError **error)
{
  // Checking filterx builtin functions first
  FilterXFunctionCtor ctor = filterx_builtin_function_ctor_lookup(function_name);

  if (!ctor)
    {
      // fallback to plugin lookup
      Plugin *p = cfg_find_plugin(cfg, LL_CONTEXT_FILTERX_FUNC, function_name);
      if (!p)
        return NULL;
      ctor = plugin_construct(p);
    }

  if (!ctor)
    return NULL;

  FilterXFunction *func_expr = ctor(function_name, arguments, error);
  if (!func_expr)
    return NULL;
  return &func_expr->super;
}

/* NOTE: takes the references of objects passed in "arguments" */
FilterXExpr *
filterx_function_lookup(GlobalConfig *cfg, const gchar *function_name, GList *arguments, GError **error)
{
  FilterXExpr *expr = _lookup_simple_function(cfg, function_name, arguments);
  if (expr)
    return expr;

  expr = _lookup_function(cfg, function_name, arguments, error);
  if (expr)
    return expr;

  if (!(*error))
    g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_FUNCTION_NOT_FOUND, "function not found");
  return NULL;
}
