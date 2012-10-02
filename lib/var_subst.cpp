/*++
Copyright (c) 2007 Microsoft Corporation

Module Name:

    var_subst.cpp

Abstract:

    Variable substitution.

Author:

    Leonardo (leonardo) 2008-01-10

Notes:

--*/
#include"var_subst.h"
#include"used_vars.h"
#include"ast_ll_pp.h"
#include"ast_pp.h"
#include"ast_smt2_pp.h"
#include"well_sorted.h"
#include"for_each_expr.h"

void var_subst::operator()(expr * n, unsigned num_args, expr * const * args, expr_ref & result) {
    SASSERT(is_well_sorted(result.m(), n));
    m_reducer.reset();
    if (m_std_order)
	m_reducer.set_inv_bindings(num_args, args);
    else
        m_reducer.set_bindings(num_args, args);
    m_reducer(n, result);
    SASSERT(is_well_sorted(m_reducer.m(), result));
    TRACE("var_subst_bug",
          tout << "m_std_order: " << m_std_order << "\n" << mk_ismt2_pp(n, m_reducer.m()) << "\nusing\n";
          for (unsigned i = 0; i < num_args; i++) tout << mk_ismt2_pp(args[i], m_reducer.m()) << "\n";
          tout << "\n------>\n";
          tout << mk_ismt2_pp(result, m_reducer.m()) << "\n";);
}

void elim_unused_vars(ast_manager & m, quantifier * q, expr_ref & result) {
    SASSERT(is_well_sorted(m, q));
    if (is_ground(q->get_expr())) {
        // ignore patterns if the body is a ground formula.
        result = q->get_expr();
        return;
    }
    if (!q->may_have_unused_vars()) {
        result = q;
        return;
    }
    used_vars used;
    used.process(q->get_expr());
    unsigned num_patterns = q->get_num_patterns();
    for (unsigned i = 0; i < num_patterns; i++)
        used.process(q->get_pattern(i));
    unsigned num_no_patterns = q->get_num_no_patterns();
    for (unsigned i = 0; i < num_no_patterns; i++)
        used.process(q->get_no_pattern(i));

    unsigned num_decls = q->get_num_decls();
    if (used.uses_all_vars(num_decls)) {
        q->set_no_unused_vars();
        result = q;
        return;
    }

    ptr_buffer<sort>  used_decl_sorts; 
    buffer<symbol>    used_decl_names;
    for (unsigned i = 0; i < num_decls; ++i) {
        if (used.contains(num_decls - i - 1)) {
            used_decl_sorts.push_back(q->get_decl_sort(i));
            used_decl_names.push_back(q->get_decl_name(i));
        }
    }
    
    unsigned         num_removed = 0;
    expr_ref_buffer  var_mapping(m);
    int              next_idx = 0;
    unsigned         sz = used.get_max_found_var_idx_plus_1();

    for (unsigned i = 0; i < num_decls; ++i) {
        sort * s = used.contains(i);
        if (s) {
            var_mapping.push_back(m.mk_var(next_idx, s));
            next_idx++;
        }
        else {
            num_removed++;
            var_mapping.push_back(0);
        }
    }
    // (VAR 0) is in the first position of var_mapping.

    for (unsigned i = num_decls; i < sz; i++) {
        sort * s = used.contains(i);
        if (s)
            var_mapping.push_back(m.mk_var(i - num_removed, s));
        else
            var_mapping.push_back(0);
    }
    
    
    // Remark: 
    // (VAR 0) should be in the last position of var_mapping.
    // ...
    // (VAR (var_mapping.size() - 1)) should be in the first position.
    std::reverse(var_mapping.c_ptr(), var_mapping.c_ptr() + var_mapping.size());

    expr_ref  new_expr(m);
    var_subst subst(m);
    
    subst(q->get_expr(), var_mapping.size(), var_mapping.c_ptr(), new_expr);
    
    if (num_removed == num_decls) {
        result = new_expr;
        return;
    }

    expr_ref tmp(m);
    expr_ref_buffer new_patterns(m);
    expr_ref_buffer new_no_patterns(m);
    
    for (unsigned i = 0; i < num_patterns; i++) {
        subst(q->get_pattern(i), var_mapping.size(), var_mapping.c_ptr(), tmp);
        new_patterns.push_back(tmp);
    }
    for (unsigned i = 0; i < num_no_patterns; i++) {
        subst(q->get_no_pattern(i), var_mapping.size(), var_mapping.c_ptr(), tmp);
        new_no_patterns.push_back(tmp);
    }
    
    result = m.mk_quantifier(q->is_forall(),
                             used_decl_sorts.size(),
                             used_decl_sorts.c_ptr(),
                             used_decl_names.c_ptr(),
                             new_expr,
                             q->get_weight(),
                             q->get_qid(),
                             q->get_skid(),
                             num_patterns,
                             new_patterns.c_ptr(),
                             num_no_patterns,
                             new_no_patterns.c_ptr());
    to_quantifier(result)->set_no_unused_vars();
    SASSERT(is_well_sorted(m, result));
}

void instantiate(ast_manager & m, quantifier * q, expr * const * exprs, expr_ref & result) {
    var_subst subst(m);
    expr_ref new_expr(m);
    subst(q->get_expr(), q->get_num_decls(), exprs, new_expr);
    TRACE("var_subst", tout << mk_pp(q, m) << "\n" << mk_pp(new_expr, m) << "\n";);
    inv_var_shifter shift(m);
    shift(new_expr, q->get_num_decls(), result);
    SASSERT(is_well_sorted(m, result));
    TRACE("instantiate_bug", tout << mk_ismt2_pp(q, m) << "\nusing\n";
          for (unsigned i = 0; i < q->get_num_decls(); i++) tout << mk_ismt2_pp(exprs[i], m) << "\n";
          tout << "\n----->\n" << mk_ismt2_pp(result, m) << "\n";);
}

static void get_free_vars_offset(expr* e, unsigned offset, ptr_vector<sort>& sorts) {
    ast_mark mark;
    ptr_vector<expr> todo;
    todo.push_back(e);
    while (!todo.empty()) {
        e = todo.back();
        todo.pop_back();
        if (mark.is_marked(e)) {
            continue;
        }
        mark.mark(e, true);
        switch(e->get_kind()) {
        case AST_QUANTIFIER: {
            quantifier* q = to_quantifier(e);
            get_free_vars_offset(q->get_expr(), offset+q->get_num_decls(), sorts);
            break;
        }
        case AST_VAR: {
            var* v = to_var(e);
            if (v->get_idx() >= offset) {
                unsigned idx = v->get_idx()-offset;
                if (sorts.size() <= idx) {
                    sorts.resize(idx+1);
                }
                if (!sorts[idx]) {
                    sorts[idx] = v->get_sort();
                }
                SASSERT(sorts[idx] == v->get_sort());
            }
            break;
        }
        case AST_APP: {
            app* a = to_app(e);
            for (unsigned i = 0; i < a->get_num_args(); ++i) {
                todo.push_back(a->get_arg(i));
            }
            break;
        }
        default:
            UNREACHABLE();
        }
    }
}


void get_free_vars(expr* e, ptr_vector<sort>& sorts) {
    get_free_vars_offset(e, 0, sorts);
}
