/*
Copyright (c) 2018 Sebastian Ullrich. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.

Authors: Sebastian Ullrich

TEMPORARY code for the old runtime
*/

#include <string>
#include <iostream>
#include "util/timeit.h"
#include "library/trace.h"
#include "library/vm/vm.h"
#include "library/vm/vm_string.h"
#include "library/vm/vm_option.h"
#include "library/vm/vm_nat.h"
#include "frontends/lean/elaborator.h"
#include "frontends/lean/parser.h"

namespace lean {
environment elab_attribute_cmd(environment env, expr const & cmd) {
    auto const & data = mdata_data(cmd);
    bool local = *get_bool(data, "local");
    decl_attributes attributes(!local);
    buffer<expr> args, eattrs, eids;
    get_app_args(mdata_expr(cmd), args);
    get_app_args(args[0], eattrs);
    get_app_args(args[1], eids);
    for (auto const & e : eattrs)
        attributes.set_attribute(env, const_name(e));
    for (auto const & e : eids)
        env = attributes.apply(env, get_dummy_ios(), const_name(e));
    return env;
}

void elaborate_command(parser & p, expr const & cmd) {
    auto const & data = mdata_data(cmd);
    if (auto const & cmd_name = get_name(data, "command")) {
        if (*cmd_name == "attribute") {
            p.set_env(elab_attribute_cmd(p.env(), cmd));
            return;
        }
    }
    throw elaborator_exception(cmd, "unexpected input to 'elaborate_command'");
}


struct vm_env : public vm_external {
    environment m_env;

    explicit vm_env(environment const & env) : m_env(env) {}

    virtual ~vm_env() {}

    virtual void dealloc() override { delete this; }

    virtual vm_external *ts_clone(vm_clone_fn const &) {lean_unreachable()}

    virtual vm_external *clone(vm_clone_fn const &) {lean_unreachable()}
};

vm_obj vm_environment_empty() {
    return mk_vm_external(new vm_env(environment()));
}

name to_name(vm_obj const & o) {
    switch (cidx(o)) {
        case 0: return name();
        case 1: {
            std::string str = to_string(cfield(o, 1));
            return name(to_name(cfield(o, 0)), str.c_str());
        }
        case 2:
            return name(to_name(cfield(o, 0)), nat(vm_nat_to_mpz1(cfield(o, 1))));
        default: lean_unreachable();
    }
}

vm_obj to_obj(name const & n) {
    if (n.is_anonymous()) {
        return mk_vm_simple(0);
    } else if (n.is_string()) {
        return mk_vm_constructor(1, to_obj(n.get_prefix()), to_obj(n.get_string().to_std_string()));
    } else {
        return mk_vm_constructor(1, to_obj(n.get_prefix()), mk_vm_nat(n.get_numeral().to_mpz()));
    }
}

level to_level(vm_obj const & o) {
    switch (cidx(o)) {
        case 0:
            return mk_level_zero();
        case 1:
            return mk_succ(to_level(cfield(o, 0)));
        case 2:
            return mk_max(to_level(cfield(o, 0)), to_level(cfield(o, 1)));
        case 3:
            return mk_imax(to_level(cfield(o, 0)), to_level(cfield(o, 1)));
        case 4:
            return mk_univ_param(to_name(cfield(o, 0)));
        case 5:
            return mk_univ_mvar(to_name(cfield(o, 0)));
        default: lean_unreachable();
    }
}

levels to_levels(vm_obj const & o) {
    switch (cidx(o)) {
        case 0:
            return levels();
        case 1:
            return levels(to_level(cfield(o, 0)), to_levels(cfield(o, 1)));
        default: lean_unreachable();
    }
}

binder_info to_binder_info(vm_obj const & o) {
    lean_assert(is_simple(o));
    return static_cast<binder_info>(cidx(o));
}

kvmap to_kvmap(vm_obj const & o) {
    switch (cidx(o)) {
        case 0:
            return kvmap();
        case 1: {
            auto vm_k = cfield(cfield(o, 0), 0);
            auto vm_v = cfield(cfield(o, 0), 1);
            auto vm_d = cfield(vm_v, 0);
            data_value v;
            switch (cidx(vm_v)) {
                case 0:
                    v = data_value(to_string(vm_d));
                    break;
                case 1:
                    v = data_value(nat(vm_nat_to_mpz1(vm_d)));
                    break;
                case 2:
                    v = data_value(to_bool(vm_d));
                    break;
                case 3:
                    v = data_value(to_name(vm_d));
                    break;
                default: lean_unreachable();
            }
            return kvmap({to_name(vm_k), v}, to_kvmap(cfield(o, 1)));
        }
        default: lean_unreachable();
    }
}

expr to_expr(vm_obj const & o) {
    switch (cidx(o)) {
        case 0:
            return mk_bvar(nat(vm_nat_to_mpz1(cfield(o, 0))));
        case 1:
            return mk_local(to_name(cfield(o, 0)), to_name(cfield(o, 0)), expr(), mk_binder_info());
        case 2:
            return mk_metavar(to_name(cfield(o, 0)), to_expr(cfield(o, 1)));
        case 3:
            return mk_sort(to_level(cfield(o, 0)));
        case 4:
            return mk_constant(to_name(cfield(o, 0)), to_levels(cfield(o, 1)));
        case 5:
            return mk_app(to_expr(cfield(o, 0)), to_expr(cfield(o, 1)));
        case 6:
            return mk_lambda(to_name(cfield(o, 0)), to_expr(cfield(o, 1)), to_expr(cfield(o, 2)),
                             to_binder_info(cfield(o, 3)));
        case 7:
            return mk_pi(to_name(cfield(o, 0)), to_expr(cfield(o, 1)), to_expr(cfield(o, 2)),
                         to_binder_info(cfield(o, 3)));
        case 8:
            return mk_let(to_name(cfield(o, 0)), to_expr(cfield(o, 1)), to_expr(cfield(o, 2)), to_expr(cfield(o, 3)));
        case 9: {
            auto l = cfield(o, 0);
            switch (cidx(l)) {
                case 0:
                    return mk_lit(literal(to_string(cfield(l, 0)).c_str()));
                case 1:
                    return mk_lit(literal(vm_nat_to_mpz1(cfield(l, 0))));
                default: lean_unreachable();
            }
        }
        case 10:
            return mk_mdata(to_kvmap(cfield(o, 0)), to_expr(cfield(o, 1)));
        case 11:
            return mk_proj(to_name(cfield(o, 0)), nat(vm_nat_to_mpz1(cfield(o, 1))), to_expr(cfield(o, 2)));
        default: lean_unreachable();
    }
}

options to_options(vm_obj o) {
    options opts;
    kvmap m = to_kvmap(o);
    for (auto const & kv : m) {
        switch (kv.snd().kind()) {
            case data_value_kind::Bool:
                opts.update(kv.fst(), kv.snd().get_bool());
                break;
            case data_value_kind::Name:
                opts.update(kv.fst(), kv.snd().get_name());
                break;
            case data_value_kind::Nat:
                opts.update(kv.fst(), kv.snd().get_nat().get_small_value());
                break;
            case data_value_kind::String:
                opts.update(kv.fst(), kv.snd().get_string());
                break;
        }
    }
    return opts;
}

name_set to_name_set(vm_obj o) {
    name_set ns;
    while (o.is_ptr()) {
        ns.insert(to_name(cfield(o, 0)));
        o = cfield(o, 1);
    }
    return ns;
}

name_generator to_name_generator(vm_obj const & o) {
    name_generator ngen;
    ngen.m_prefix = to_name(cfield(o, 0));
    ngen.m_next_idx = to_unsigned(cfield(o, 1));
    return ngen;
}

vm_obj to_obj(name_generator const & ngen) {
    return mk_vm_constructor(0, to_obj(ngen.m_prefix), mk_vm_nat(ngen.m_next_idx));
}

/* elaborate_command : string -> expr -> old_elaborator_state -> except string old_elaborator_state */
// TODO(Sebastian): replace `string` with `message` in the new runtime
vm_obj vm_elaborate_command(vm_obj const & vm_filename, vm_obj const & vm_cmd, vm_obj const & vm_st) {
    auto vm_e = cfield(vm_st, 0);
    lean_vm_check(dynamic_cast<vm_env *>(to_external(vm_e)));
    auto env = static_cast<vm_env *>(to_external(vm_e))->m_env;
    io_state const & ios = get_dummy_ios();
    std::stringstream in;
    parser p(env, ios, in, to_string(vm_filename));
    auto s = p.mk_snapshot();
    auto ngen = to_name_generator(cfield(vm_st, 1));
    auto vm_lds = cfield(vm_st, 2);
    local_level_decls lds;
    name_set lvars;
    while (vm_lds.is_ptr()) {
        auto it = cfield(vm_lds, 0);
        auto n = to_name(cfield(it, 0));
        lds.insert(n, to_level(cfield(it, 1)));
        // all local decls are variables in Lean 4
        lvars.insert(n);
        vm_lds = cfield(vm_lds, 1);
    }
    auto vm_eds = cfield(vm_st, 3);
    local_expr_decls eds;
    name_set vars;
    while (vm_eds.is_ptr()) {
        auto it = cfield(vm_eds, 0);
        auto n = to_name(cfield(it, 0));
        eds.insert(n, to_expr(cfield(it, 1)));
        // all local decls are variables in Lean 4
        vars.insert(n);
        vm_eds = cfield(vm_eds, 1);
    }
    auto includes = to_name_set(cfield(vm_st, 4));
    auto options = to_options(cfield(vm_st, 5));
    p.reset(snapshot(p.env(), ngen, lds, eds, lvars, vars, includes, options, true, false,
            parser_scope_stack(), to_unsigned(cfield(vm_st, 6)), pos_info {1, 0}));

    try {
        elaborate_command(p, to_expr(vm_cmd));
        s = p.mk_snapshot();
        auto vm_st2 = mk_vm_constructor(0, {
            mk_vm_external(new vm_env(env)),
            to_obj(s->m_ngen),
            // TODO(Sebastian): support commands that change the local context
            cfield(vm_st, 2),
            cfield(vm_st, 3),
            cfield(vm_st, 4),
            cfield(vm_st, 5),
            cfield(vm_st, 6)
        });
        return mk_vm_constructor(1, vm_st);
    } catch (exception & e) {
        return mk_vm_constructor(0, to_obj(std::string(e.what())));
    }
}

void initialize_vm_elaborator() {
    DECLARE_VM_BUILTIN(name({"lean", "environment", "empty"}), vm_environment_empty);
    DECLARE_VM_BUILTIN(name({"lean", "elaborator", "elaborate_command"}), vm_elaborate_command);
}

void finalize_vm_elaborator() {
}
}
