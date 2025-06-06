#include "cminusf_builder.hpp"
#include "BasicBlock.hpp"
#include "Constant.hpp"
#include "GlobalVariable.hpp"
#include "Instruction.hpp"
#include "Type.hpp"
#include "Value.hpp"
#include "ast.hpp"
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#define CONST_FP(num) ConstantFP::get((float)num, module.get())
#define CONST_INT(num) ConstantInt::get(num, module.get())

// types
Type *VOID_T;
Type *INT1_T;
Type *INT32_T;
Type *INT32PTR_T;
Type *FLOAT_T;
Type *FLOATPTR_T;

bool promote(IRBuilder *builder, Value **l_val_p, Value **r_val_p) {
    auto &l_val = *l_val_p;
    auto &r_val = *r_val_p;
    auto l_ty = l_val->get_type();
    auto r_ty = r_val->get_type();

    // 类型相同
    if (l_ty == r_ty) {
        if (l_ty->is_int1_type()) {
            l_val = builder->create_zext(l_val, INT32_T);
            r_val = builder->create_zext(r_val, INT32_T);
        }
        return !l_ty->is_float_type(); // true for int, false for float
    }

    // 不同类型：统一为 float 或 int32
    if (l_ty->is_float_type() || r_ty->is_float_type()) {
        if (!l_ty->is_float_type()) {
            if (l_ty->is_int1_type())
                l_val = builder->create_zext(l_val, INT32_T);
            l_val = builder->create_sitofp(l_val, FLOAT_T);
        }
        if (!r_ty->is_float_type()) {
            if (r_ty->is_int1_type())
                r_val = builder->create_zext(r_val, INT32_T);
            r_val = builder->create_sitofp(r_val, FLOAT_T);
        }
        return false;
    }

    // 其中一个是 int1，另一个是 int32，统一为 int32
    if (l_ty->is_int1_type())
        l_val = builder->create_zext(l_val, INT32_T);
    if (r_ty->is_int1_type())
        r_val = builder->create_zext(r_val, INT32_T);

    return true;
}

/*
 * use CMinusfBuilder::Scope to construct scopes
 * scope.enter: enter a new scope
 * scope.exit: exit current scope
 * scope.push: add a new binding to current scope
 * scope.find: find and return the value bound to the name
 */

Value* CminusfBuilder::visit(ASTProgram &node) {
    VOID_T = module->get_void_type();
    INT1_T = module->get_int1_type();
    INT32_T = module->get_int32_type();
    INT32PTR_T = module->get_int32_ptr_type();
    FLOAT_T = module->get_float_type();
    FLOATPTR_T = module->get_float_ptr_type();

    Value *ret_val = nullptr;
    for (auto &decl : node.declarations) {
        ret_val = decl->accept(*this);
    }
    return ret_val;
}

Value* CminusfBuilder::visit(ASTNum &node) {
    if (node.type == TYPE_FLOAT) {
        return CONST_FP(node.f_val);
    }
    return CONST_INT(node.i_val);
}

Value* CminusfBuilder::visit(ASTVarDeclaration &node) {
    // TODO: This function is empty now.
    // Add some code here.
    
    Type* type = nullptr;
    if(node.num) {
        auto ele_num = node.num->accept(*this);
        int ele_num_val = ele_num->as<ConstantInt>()->get_value();

        if (node.type == TYPE_INT) {
            type = ArrayType::get(INT32_T, ele_num_val);
        } else if (node.type == TYPE_FLOAT) {
            type = ArrayType::get(FLOAT_T, ele_num_val);
        }
    } else {
        if (node.type == TYPE_INT) {
            type = INT32_T;
        } else if (node.type == TYPE_FLOAT) {
            type = FLOAT_T;
        } 
    }

    if(context.func == nullptr) {
        // global var
        Constant *init_val = nullptr;
        if(node.num) {
            // global array
            auto ele_num = node.num->accept(*this);
            int ele_num_val = ele_num->as<ConstantInt>()->get_value();
            std::vector<Constant*> vals(ele_num_val, CONST_INT(0));
            Type* inner_type = nullptr;
            if(node.type == TYPE_INT) {
                inner_type = INT32_T;
            } else if(node.type == TYPE_FLOAT) {
                inner_type = FLOAT_T;
            }
            init_val = ConstantZero::get(
                ArrayType::get(inner_type, ele_num_val),
                builder->get_module()
            );
            type = ArrayType::get(inner_type, ele_num_val);
        } else {
            // global var
            if (node.type == TYPE_INT) {
                init_val = CONST_INT(0);
            } else if (node.type == TYPE_FLOAT) {
                init_val = CONST_FP(0.);
            }
        }
        
        auto var = GlobalVariable::create(
                node.id, builder->get_module(), type,false,init_val);
        scope.push(node.id, var);
        return nullptr;
    }

    auto allocx = builder->create_alloca(type);
    // if(type->is_array_type()){
    //     return nullptr;
    // }
    // if(type->is_pointer_type()){
    //     return  nullptr;
    // }
    std::string name = node.id;
    this->scope.push(name,allocx);
    
    return nullptr;
}

Value* CminusfBuilder::visit(ASTFunDeclaration &node) {
    FunctionType *fun_type;
    Type *ret_type;
    std::vector<Type *> param_types;
    if (node.type == TYPE_INT)
        ret_type = INT32_T;
    else if (node.type == TYPE_FLOAT)
        ret_type = FLOAT_T;
    else
        ret_type = VOID_T;

    for (auto &param : node.params) {
        if (param->type == TYPE_INT) {
            if (param->isarray) {
                param_types.push_back(INT32PTR_T);
            } else {
                param_types.push_back(INT32_T);
            }
        } else {
            if (param->isarray) {
                param_types.push_back(FLOATPTR_T);
            } else {
                param_types.push_back(FLOAT_T);
            }
        }
    }
    
    fun_type = FunctionType::get(ret_type, param_types);
    auto func = Function::create(fun_type, node.id, module.get());
    scope.push(node.id, func);
    context.func = func;
    auto funBB = BasicBlock::create(module.get(), "entry", func);
    builder->set_insert_point(funBB);
    scope.enter();
    context.pre_enter_scope = true;
    std::vector<Value *> args;
    for (auto &arg : func->get_args()) {
        args.push_back(&arg);
    }
    for (unsigned int i = 0; i < node.params.size(); ++i) {
        // TODO: You need to deal with params and store them in the scope.
        auto* param_i = node.params[i]->accept(*this);
        args[i]->set_name(node.params[i]->id);
        builder->create_store(args[i], param_i);
        scope.push(args[i]->get_name(), param_i);
    }
    node.compound_stmt->accept(*this);
    if (!builder->get_insert_block()->is_terminated()) 
    {
        if (context.func->get_return_type()->is_void_type())
            builder->create_void_ret();
        else if (context.func->get_return_type()->is_float_type())
            builder->create_ret(CONST_FP(0.));
        else
            builder->create_ret(CONST_INT(0));
    }
    scope.exit();
    return nullptr;
}

Value* CminusfBuilder::visit(ASTParam &node) {
    Type* type = nullptr;
    if(node.isarray) {
        if (node.type == TYPE_INT) {
            type = PointerType::get(INT32_T);
        } else if (node.type == TYPE_FLOAT) {
            type = PointerType::get(FLOAT_T);
        }
    } else {
        if (node.type == TYPE_INT) {
            type = INT32_T;
        } else if (node.type == TYPE_FLOAT) {
            type = FLOAT_T;
        
        }
    }    
    return  builder->create_alloca(type);
    
}

Value* CminusfBuilder::visit(ASTCompoundStmt &node) {
    // TODO: This function is not complete.
    // You may need to add some code here
    // to deal with complex statements. 
    bool scope_entered = context.pre_enter_scope;

    if(not scope_entered) {
        scope.enter();
    } else {
        context.pre_enter_scope = false;
    }
    

    for (auto &decl : node.local_declarations) {
        decl->accept(*this);
    }

    for (auto &stmt : node.statement_list) {
        stmt->accept(*this);
        // if(!builder->get_insert_block()->is_terminated()) {
            
        // }
    }
    if(not scope_entered){
        scope.exit();
    }
    return nullptr;
}

Value* CminusfBuilder::visit(ASTExpressionStmt &node) {
    if (node.expression != nullptr) {
        return node.expression->accept(*this);
    }
    return nullptr;
}

Value* CminusfBuilder::visit(ASTSelectionStmt &node) {
    auto *ret_val = node.expression->accept(*this);
    auto *trueBB = BasicBlock::create(module.get(), "", context.func);
    BasicBlock *falseBB{};
    auto *contBB = BasicBlock::create(module.get(), "", context.func);
    Value *cond_val = nullptr;
    if (ret_val->get_type()->is_int32_type()) {
        cond_val = builder->create_icmp_ne(ret_val, CONST_INT(0));
    } else if (ret_val->get_type()->is_float_type()) {
        cond_val = builder->create_fcmp_ne(ret_val, CONST_FP(0.));
    } else if (ret_val->get_type()->is_int1_type()) {
        cond_val = ret_val;
    }

    if (node.else_statement == nullptr) {
        builder->create_cond_br(cond_val, trueBB, contBB);
    } else {
        falseBB = BasicBlock::create(module.get(), "", context.func);
        builder->create_cond_br(cond_val, trueBB, falseBB);
    }
    builder->set_insert_point(trueBB);
    node.if_statement->accept(*this);

    if (not builder->get_insert_block()->is_terminated()) {
        builder->create_br(contBB);
    }

    if (node.else_statement == nullptr) {
        // falseBB->erase_from_parent(); // did not clean up memory
    } else {
        builder->set_insert_point(falseBB);
        node.else_statement->accept(*this);
        if (not builder->get_insert_block()->is_terminated()) {
            builder->create_br(contBB);
        }
    }

    builder->set_insert_point(contBB);
    return nullptr;
}

Value* CminusfBuilder::visit(ASTIterationStmt &node) {
    // TODO: This function is empty now.
    // Add some code here.
    auto cond_bb = BasicBlock::create(builder->get_module(), "", context.func);
    builder->create_br(cond_bb);
    builder->set_insert_point(cond_bb);
    auto cond = node.expression->accept(*this);

    auto body = BasicBlock::create(builder->get_module(), "", context.func);
    auto end = BasicBlock::create(builder->get_module(), "", context.func);

    if (cond->get_type()->is_int32_type()) {
        cond = builder->create_icmp_ne(cond, CONST_INT(0));
    } else if(cond->get_type()->is_float_type()) {
        cond = builder->create_fcmp_ne(cond, CONST_FP(0.));
    }
    builder->create_cond_br(cond, body, end);
    builder->set_insert_point(body);
    node.statement->accept(*this);
    if(not builder->get_insert_block()->is_terminated()) {
        builder->create_br(cond_bb);
    }
    builder->set_insert_point(end);
    return nullptr;
}

Value* CminusfBuilder::visit(ASTReturnStmt &node) {
    if (node.expression == nullptr) {
        builder->create_void_ret();
    } else {
        auto *fun_ret_type =
            context.func->get_function_type()->get_return_type();
        auto *ret_val = node.expression->accept(*this);
        if (fun_ret_type != ret_val->get_type()) {
            if (fun_ret_type->is_integer_type()) {
                ret_val = builder->create_fptosi(ret_val, INT32_T);
            } else {
                ret_val = builder->create_sitofp(ret_val, FLOAT_T);
            }
        }

        builder->create_ret(ret_val);
    }

    return nullptr;
}

Value* CminusfBuilder::visit(ASTVar &node) {
    // TODO: This function is empty now.
    Value* baseAddr = this->scope.find(node.id);
    Type* alloctype = nullptr;
    
    if(baseAddr->is<AllocaInst>()) {
        alloctype = baseAddr->as<AllocaInst>()->get_alloca_type();
    } else {
        alloctype = baseAddr->as<GlobalVariable>()->get_type()->get_pointer_element_type();
    }

    if(node.expression) {
        bool original_require_lvalue = context.require_lvalue;
        context.require_lvalue = false;
        auto idx = node.expression->accept(*this);
        context.require_lvalue = original_require_lvalue;

        if (idx->get_type()->is_float_type()) {
            idx = builder->create_fptosi(idx, INT32_T);
        } else if(idx->get_type()->is_int1_type()){
            idx = builder->create_zext(idx, INT32_T);
        }
        auto right_bb = BasicBlock::create(module.get(), "", context.func);
        auto wrong_bb = BasicBlock::create(module.get(), "", context.func);
        
        auto cond_neg = builder->create_icmp_ge(idx, CONST_INT(0));
        builder->create_cond_br(cond_neg,right_bb, wrong_bb);

        auto wrong = scope.find("neg_idx_except");
        builder->set_insert_point(wrong_bb);
        builder->create_call(wrong, {});
        builder->create_br(right_bb);
        builder->set_insert_point(right_bb);
        
        if(context.require_lvalue) {
            if(alloctype->is_pointer_type()) {
                baseAddr = builder->create_load(baseAddr);
                baseAddr = builder->create_gep(baseAddr,{idx});
            } else if(alloctype->is_array_type()){ 
                baseAddr = builder->create_gep(baseAddr,{CONST_INT(0),idx});
            }
            context.require_lvalue = false;
            return baseAddr;
        } else {
            if(alloctype->is_pointer_type()){
                baseAddr = builder->create_load(baseAddr);
                baseAddr = builder->create_gep(baseAddr,{idx});
            } else if(alloctype->is_array_type()){ 
                baseAddr = builder->create_gep(baseAddr,{CONST_INT(0),idx});
            }
            baseAddr = builder->create_load(baseAddr);
            return baseAddr;
        }
    } else {
        if (context.require_lvalue) {
            context.require_lvalue = false;
            return baseAddr;
            // return builder->create_gep(baseAddr, {CONST_INT(0)});
        } else {
            if(alloctype->is_array_type()){
                return builder->create_gep(baseAddr, {CONST_INT(0),CONST_INT(0)});
            } else {
                return builder->create_load(baseAddr);
            }
            
        }
    }


}

Value* CminusfBuilder::visit(ASTAssignExpression &node) {
    auto *expr_result = node.expression->accept(*this);
    context.require_lvalue = true;
    auto *var_addr = node.var->accept(*this);
    if (var_addr->get_type()->get_pointer_element_type() !=
        expr_result->get_type()) {
        if (expr_result->get_type() == INT32_T) {
            expr_result = builder->create_sitofp(expr_result, FLOAT_T);
        } else if (expr_result->get_type() == FLOAT_T) {
            expr_result = builder->create_fptosi(expr_result, INT32_T);
        } else if(expr_result->get_type() == INT1_T){
            expr_result = builder->create_zext(expr_result, INT32_T);
        }
    }
    builder->create_store(expr_result, var_addr);
    return expr_result;
}

Value* CminusfBuilder::visit(ASTSimpleExpression &node) {
    // TODO: This function is empty now.
    // Add some code here.
    Value* l_val = nullptr;
    Value* r_val = nullptr;
    if (node.additive_expression_l)
        l_val = node.additive_expression_l->accept(*this);
    if (node.additive_expression_r)
        r_val =node.additive_expression_r->accept(*this);

    if(!l_val) return r_val;
    if(!r_val) return l_val;

    bool is_int = promote(&*builder, &l_val, &r_val);
    Value* ret = nullptr;
    switch (node.op) {
    case OP_EQ:
        if(is_int) {
            ret = builder->create_icmp_eq(l_val, r_val);
        } else {
            ret = builder->create_fcmp_eq(l_val, r_val);
        }
        break;
    case OP_GE:
        if(is_int) {
            ret = builder->create_icmp_ge(l_val, r_val);
        } else {
            ret = builder->create_fcmp_ge(l_val, r_val);
        }
        break;
    case OP_GT:
        if(is_int) {
            ret = builder->create_icmp_gt(l_val, r_val);
        } else {
            ret = builder->create_fcmp_gt(l_val, r_val);
        }
        break;
    case OP_LE:
        if(is_int) {
            ret = builder->create_icmp_le(l_val, r_val);
        } else {
            ret = builder->create_fcmp_le(l_val, r_val);
        }
        break;
    case OP_LT:
        if(is_int) {
            ret = builder->create_icmp_lt(l_val, r_val);
        } else {
            ret = builder->create_fcmp_lt(l_val, r_val);
        }
        break;
    case OP_NEQ:
        if(is_int) {
            ret = builder->create_icmp_ne(l_val, r_val);
        } else {
            ret = builder->create_fcmp_ne(l_val, r_val);
        }
        break;
    }
    
    return ret;
}

Value* CminusfBuilder::visit(ASTAdditiveExpression &node) {
    if (node.additive_expression == nullptr) {
        return node.term->accept(*this);
    }

    auto *l_val = node.additive_expression->accept(*this);
    auto *r_val = node.term->accept(*this);
    bool is_int = promote(&*builder, &l_val, &r_val);
    Value *ret_val = nullptr;
    switch (node.op) {
    case OP_PLUS:
        if (is_int) {
            ret_val = builder->create_iadd(l_val, r_val);
        } else {
            ret_val = builder->create_fadd(l_val, r_val);
        }
        break;
    case OP_MINUS:
        if (is_int) {
            ret_val = builder->create_isub(l_val, r_val);
        } else {
            ret_val = builder->create_fsub(l_val, r_val);
        }
        break;
    }
    return ret_val;
}

Value* CminusfBuilder::visit(ASTTerm &node) {
    if (node.term == nullptr) {
        return node.factor->accept(*this);
    }

    auto *l_val = node.term->accept(*this);
    auto *r_val = node.factor->accept(*this);
    bool is_int = promote(&*builder, &l_val, &r_val);

    Value *ret_val = nullptr;
    switch (node.op) {
    case OP_MUL:
        if (is_int) {
            ret_val = builder->create_imul(l_val, r_val);
        } else {
            ret_val = builder->create_fmul(l_val, r_val);
        }
        break;
    case OP_DIV:
        if (is_int) {
            ret_val = builder->create_isdiv(l_val, r_val);
        } else {
            ret_val = builder->create_fdiv(l_val, r_val);
        }
        break;
    }
    return ret_val;
}

Value* CminusfBuilder::visit(ASTCall &node) {
    auto *func = dynamic_cast<Function *>(scope.find(node.id));
    std::vector<Value *> args;
    auto param_type = func->get_function_type()->param_begin();
    for(auto& arg : node.args) {
        auto *arg_val = arg->accept(*this);
        auto arg_val_type = arg_val->get_type();
        if (!arg_val_type->is_pointer_type() &&
            *param_type != arg_val_type) {
            if (arg_val->get_type()->is_int32_type()) {
                arg_val = builder->create_sitofp(arg_val, FLOAT_T);
            } else if(arg_val->get_type()->is_int1_type()) {
                arg_val = builder->create_zext(arg_val, INT32_T);
            } else {
                arg_val = builder->create_fptosi(arg_val, INT32_T);
            }
        } 
        args.push_back(arg_val);
        param_type++;
    }

    return builder->create_call(static_cast<Function *>(func), args);
}
