#include "DeadCode.hpp"
#include "Instruction.hpp"
#include "logging.hpp"
#include <memory>
#include <vector>

// 处理流程：两趟处理，mark 标记有用变量，sweep 删除无用指令
void DeadCode::run() {
    bool changed{};
    func_info->run();
    do {
        changed = false;
        for (auto &F : m_->get_functions()) {
            auto func = &F;
            changed |= clear_basic_blocks(func);
            mark(func);
            changed |= sweep(func);
        }
    } while (changed);
    LOG_INFO << "dead code pass erased " << ins_count << " instructions";
}

bool DeadCode::clear_basic_blocks(Function *func) {
    bool changed = 0;
    std::vector<BasicBlock *> to_erase;
    for (auto &bb1 : func->get_basic_blocks()) {
        auto bb = &bb1;
        if(bb->get_pre_basic_blocks().empty() && bb != func->get_entry_block()) {
            to_erase.push_back(bb);
            changed = 1;
        }
    }
    for (auto &bb : to_erase) {
        bb->erase_from_parent();
        delete bb;
    }
    return changed;
}

void DeadCode::mark(Function *func) {
    work_list.clear();
    marked.clear();
    int func_num = func->get_num_basic_blocks(); 
    // 1. 找到所有关键指令作为“活代码”的起点
    for (auto &bb : func->get_basic_blocks()) {
        for (auto &ins : bb.get_instructions()) {
            if (is_critical(&ins)) {
                if (!marked[&ins]) {
                    marked[&ins] = true;
                    work_list.push_back(&ins);
                }
            }
        }
    }

    // 2. 使用工作列表算法，传播“活”属性
    while (!work_list.empty()) {
        auto ins = work_list.front();
        work_list.pop_front();
        // 调用辅助函数来标记其操作数
        mark(ins);
    }
    
}

void DeadCode::mark(Instruction *ins) {
    for (auto op : ins->get_operands()) {
        auto def = dynamic_cast<Instruction *>(op);
        if (def == nullptr)
            continue;
        if (marked[def])
            continue;
        if (def->get_function() != ins->get_function())
            continue;
        marked[def] = true;
        work_list.push_back(def);
    }
}

bool DeadCode::sweep(Function *func) {
    // TODO: 删除无用指令
    // 提示：
    // 1. 遍历函数的基本块，删除所有标记为true的指令
    // 2. 删除指令后，可能会导致其他指令的操作数变为无用，因此需要再次遍历函数的基本块
    // 3. 如果删除了指令，返回true，否则返回false
    // 4. 注意：删除指令时，需要先删除操作数的引用，然后再删除指令本身
    // 5. 删除指令时，需要注意指令的顺序，不能删除正在遍历的指令
    std::unordered_set<Instruction *> wait_del{};

    // 1. 收集所有未被标记的指令
    for (auto &bb : func->get_basic_blocks()) {
        for (auto &ins : bb.get_instructions()) {
            if (!marked[&ins]) {
                wait_del.insert(&ins);
            }
        }
    }

    // 2. 执行删除
    for (auto ins : wait_del) {
        ins->get_parent()->erase_instr(ins); // 从基本块中删除指令
        ins_count++;
    }
    
    return not wait_del.empty(); // changed
}

bool DeadCode::is_critical(Instruction *ins) {
    // TODO: 判断指令是否是无用指令
    // 提示：
    // 1. 如果是函数调用，且函数是纯函数，则无用
    // 2. 如果是无用的分支指令，则无用
    // 3. 如果是无用的返回指令，则无用
    // 4. 如果是无用的存储指令，则无用
    if (ins->isTerminator()) {
        return true;
    }
    
    // 2. store 指令修改内存，是关键的
    if (ins->is_store()) {
        // 获取 store 指令的目标地址操作数（通常是第二个操作数）
        auto ptr_operand = ins->get_operand(1); // 假设 store 的格式是 store value, pointer

        // 如果写入的是全局变量或函数参数，则认为它有副作用，是关键指令
        if (dynamic_cast<GlobalVariable*>(ptr_operand) || dynamic_cast<Argument*>(ptr_operand)) {
            return true;
        }
        
        // 否则，它只是写入一个局部变量（例如由 alloca 创建），
        // 它本身不是关键指令。它的死活将由后续的 load 指令决定。
        return false;
    }

    // 3. call 指令可能有副作用（如IO），通常视为关键的
    if (auto func_call = dynamic_cast<CallInst*>(ins)) {
        return this->func_info->is_pure_function(func_call->get_function());
    }
    
    // 其他指令（如 add, sub, mul 等纯计算指令）默认不是关键的
    return false;
}

void DeadCode::sweep_globally() {
    std::vector<Function *> unused_funcs;
    std::vector<GlobalVariable *> unused_globals;
    for (auto &f_r : m_->get_functions()) {
        if (f_r.get_use_list().size() == 0 and f_r.get_name() != "main")
            unused_funcs.push_back(&f_r);
    }
    for (auto &glob_var_r : m_->get_global_variable()) {
        if (glob_var_r.get_use_list().size() == 0)
            unused_globals.push_back(&glob_var_r);
    }
    // changed |= unused_funcs.size() or unused_globals.size();
    for (auto func : unused_funcs)
        m_->get_functions().erase(func);
    for (auto glob : unused_globals)
        m_->get_global_variable().erase(glob);
}
