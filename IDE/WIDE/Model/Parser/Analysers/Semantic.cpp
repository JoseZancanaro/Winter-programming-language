#include "Semantic.hpp"
#include "Constants.hpp"
#include "ActionDefinitions.hpp"
#include "OperatorDefinitions.hpp"

#include <iostream>
#include <algorithm>
#include <numeric>
#include <sstream>
#include <tuple>

namespace wpl {

Semantic::Semantic(std::unique_ptr<Logger_Base> && p_logger)
    : logger(std::move(p_logger)) {

    // Push STD lib
    this->name_table.push_back({ 0, "print", { Type_Name::VOID }, { Type_Name::VOID }, true, true, true, 0, false, true });
    this->name_table.push_back({ 0, "read", { Type_Name::INTEGER }, { Type_Name::INTEGER }, true, true, true, 0, false, true });

    this->gc.binary_second_operand.push(false);
}

auto Semantic::execute_action(int action, Token const* token) -> void {
    //@TODO enum class

    auto [ prefix, suffix ] = decode_action(action);

    std::cout << "[" << prefix << ", " << suffix << "]" << std::endl;

    switch (prefix) {
        case 0: {
            this->do_scope_action(suffix, token);
            break;
        }
        case 1: {
            this->do_type_action(suffix, token);
            break;
        }
        case 2: {
            this->do_declare_action(suffix, token);
            break;
        }
        case 3: {
            this->do_function_action(suffix, token);
            break;
        }
        case 4: {
            this->do_name_provider_action(suffix, token);
            break;
        }
        case 7: {
            this->do_value_provider_action(suffix, token);
            break;
        }
        case 5: {
            this->do_value_access_action(suffix, token);
            break;
        }
        case 6: {
            this->do_assignment_action(suffix, token);
            break;
        }
        case 8: {
            this->do_expression_handling_action(suffix, token);
            break;
        }
        case 9: {
            this->do_vector_constructor_action(suffix, token);
            break;
        }
        case 10: {
            this->do_flow_control_action(suffix, token);
            break;
        }
        default: {
            std::cout << "Uncaught action with type "
                      << "Prefix" << prefix
                      << "Suffix" << suffix << std::endl;
        }
    }

    std::cout << "[end]" << std::endl;
}

auto Semantic::do_scope_action(int suffix, [[maybe_unused]] Token const* token) -> void {
    switch (Scope_Suffix(suffix)) {
        case Scope_Suffix::PUSH_GLOBAL: {
            this->scopes.push(this->scope_count++);
            this->bip_asm_text("JMP", this->bip_asm_hash_function_name("main"));
            break;
        }
        case Scope_Suffix::POP_GLOBAL: {
            this->verify_scope_lifetime(this->scopes.top());
            this->scopes.pop();

            this->bip_asm_text_pop_back();
            this->bip_asm_text("HLT", "0");

            break;
        }
        case Scope_Suffix::PUSH_QUALIFIED: {
            this->scopes.push(this->scope_count++);
            break;
        }
        case Scope_Suffix::POP_QUALIFIED: {
            this->verify_scope_lifetime(this->scopes.top());
            this->scopes.pop();
            break;
        }
    }
}

auto Semantic::do_type_action(int suffix, Token const* token) -> void {
    switch (Type_Suffix(suffix)) {
        case Type_Suffix::PUSH_TYPE_NAME: {
            this->types.push({ get_type_name(token->get_lexeme()) });
            break;
        }
        case Type_Suffix::SET_VEC_MODIFIER: {
            this->issue_error(
                detail::to_string("Array size must be specified.")
            ); // @TODO Enhance Logger Class
            break;
        }
        case Type_Suffix::ACK_VEC_SIZE_MODIFIER: {
            this->types.top().array = true;
            break;
        }
        case Type_Suffix::SET_VEC_SIZE_MODIFIER: {
            auto size = std::stoi(token->get_lexeme());

            if (size <= 0) {
                this->issue_error(
                    detail::to_string("Array size must be greater than 0.")
                ); // @TODO Enhance Logger Class
            } else {
                this->types.top().length = size;
            }

            break;
        }
        case Type_Suffix::SET_CONST_MODIFIER: {
            this->types.top().constant = true;
            break;
        }
        case Type_Suffix::SET_REF_MODIFIER: {
            this->types.top().ref = true;
            break;
        }
        case Type_Suffix::SET_POINTER_MODIFIER: {
            this->types.top().pointer = true;
            break;
        }
    }
}

auto Semantic::do_declare_action(int suffix, Token const* token) -> void {
    switch (Declare_Suffix(suffix)) {
        case Declare_Suffix::PUSH_NAME_ID: {
            this->names.push({ this->scopes.top(), token->get_lexeme(), this->types.top(), this->types.top() });
            this->gc.name_ids.push(token->get_lexeme());

            this->sanitize_type(this->types.top());

            break;
        }
        case Declare_Suffix::PUSH_INITIALIZED: {
            auto name = this->names.top(); this->names.pop();
            name.initialized = true;

            auto expression_node = this->expression_nodes.top();
            this->expression_nodes.pop();

            if (name.type.name == Type_Name::ANY) {
                name.inferred = this->infer_any_type(name.type, expression_node);
            }

            this->sanitize_type_compatibility(name, expression_node.type);

            this->try_put_name(name);

            if (!name.type.array) {
                this->bip_asm_text("STO", this->bip_asm_hash_name(&name));
            }

            break;
        }
        case Declare_Suffix::PUSH_UNINITIALIZED: {
            auto name = this->names.top(); this->names.pop();

            if (name.type.name == Type_Name::ANY) {
                this->issue_error(
                    detail::to_string("Declaration of name '", name.id, "' with deduced type Any requires an initializer.")
                ); // @TODO Enhance Logger Class
            }

            this->try_put_name(name);

            break;
        }
        case Declare_Suffix::POP_TYPE: {
            this->types.pop();
            break;
        }
    }
}

auto Semantic::do_function_action(int suffix, Token const* token) -> void {
    switch (Function_Suffix(suffix)) {
        case Function_Suffix::NAME_DISCOVER: {
            this->names.push({ this->scopes.top(), token->get_lexeme(),
                              { Type_Name::UNKNOWN },
                              { Type_Name::UNKNOWN }
                            });
            break;
        }
        case Function_Suffix::NAME_FUNC_PUSH: {
            auto function_name = this->names.top();
            this->names.pop();

            auto function_type = this->types.top();
            this->types.pop();

            this->sanitize_type(function_type, false, true);

            function_name.type = function_type;
            function_name.inferred = function_type;
            function_name.initialized = true;
            function_name.function = true;

            this->try_put_name(function_name);
            this->current_function_scope = { function_name, false };

            this->parameter_count = 0;

            break;
        }
        case Function_Suffix::NAME_PARAM_PUSH: {
            auto param_name = token->get_lexeme();

            auto param_type = this->types.top();
            this->types.pop();

            this->sanitize_type(param_type, true);

            Name name { this->scopes.top(), param_name, param_type, param_type };
            name.param_pos = this->parameter_count++;
            name.param = true;
            name.initialized = true;
            name.parent = { this->names.top().scope, this->names.top().id };

            this->try_put_name(name);

            break;
        }
        case Function_Suffix::NAME_FUNC_POP: {
            if (this->current_function_scope.has_value()) {
                auto const& [ name, returned ] = this->current_function_scope.value();

                if (name.inferred.name != Type_Name::VOID && !returned) {
                    this->issue_warning(
                        detail::to_string("Function name '", name.id, "' doesn't have a return statement.")
                    ); // @TODO Enhance Logger class
                }
            }

            this->current_function_scope = std::nullopt;

            break;
        }
    }
}

auto Semantic::do_name_provider_action(int suffix, Token const* token) -> void {
    switch (Name_Provider_Suffix(suffix)) {
        case Name_Provider_Suffix::SET_NAME_VAR_ID: {
            this->name_providers.push({ token->get_lexeme() });
            this->gc.name_ids.push(token->get_lexeme());

            break;
        }
        case Name_Provider_Suffix::SET_SUBSCRIPT_ACCESS: {
            this->name_providers.top().subscript_access = true;

            if (this->gc.binary_second_operand.top()) {
                this->bip_asm_text("STO", "1001");
            }

            this->gc.binary_second_operand.push(false);

            break;
        }
        case Name_Provider_Suffix::SET_SUBSCRIPT_INDEX: {
            this->gc.binary_second_operand.pop();
            break;
        }
        case Name_Provider_Suffix::SET_NAME_FUNCTION_ID: {
            this->name_providers.push({ token->get_lexeme(), true });

            if (auto name_opt = this->try_get_name(token->get_lexeme()); name_opt.has_value()) {
                auto name = name_opt.value();
                auto params = this->fetch_function_params(*name);

                this->call_contexts.push({ name, params });
            }

            if (this->gc.binary_second_operand.top()) {
                this->bip_asm_text("STO", "1001");
            }

            this->gc.binary_second_operand.push(false);

            break;
        }
        case Name_Provider_Suffix::SET_CALL_ARG: {
            auto & context = this->call_contexts.top();

            auto expression_node = this->expression_nodes.top();
            this->expression_nodes.pop();

            if (expression_node.type.name == Type_Name::VOID) {
                this->issue_error(
                    detail::to_string("Void in argument ", context.args.size() + 1, " when calling '", context.name->id, "'.")
                ); // @TODO enhance Logger Class
            }

            if (!context.name->reserved && context.args.size() < context.params.size()) {
                this->bip_asm_text("STO", this->bip_asm_hash_name(&context.params.at(context.args.size())));
            }

            context.args.push_back(expression_node);

            break;
        }
        case Name_Provider_Suffix::SET_CALL_END: {
            auto context = this->call_contexts.top();
            this->call_contexts.pop();

            if (!context.name->reserved) {
                if (this->mismatch_params(context.params, context.args)) {
                    this->issue_error(
                        detail::to_string("Call to '", context.name->id, "' mismatched function signature. ",
                                          "Expected ", context.params.size(), " as ", this->format_function_signature(context.params),
                                          " and saw ", context.args.size(), " as ", this->format_function_signature(context.args),
                                          " .")
                    ); // @TODO Enhance Logger Class
                }
                else {
                    this->bip_asm_text("CALL", this->bip_asm_hash_function_name(context.name));
                }
            }

            this->gc.binary_second_operand.pop();

            break;
        }
    }
}

auto Semantic::do_value_provider_action(int suffix, [[maybe_unused]] Token const* token) -> void {
    switch (Value_Provider_Suffix(suffix)) {
        case Value_Provider_Suffix::PUSH_INT: {
            this->value_providers.push({ Type_Name::INTEGER });
            break;
        }
        case Value_Provider_Suffix::PUSH_DOUBLE: {
            this->value_providers.push({ Type_Name::DOUBLE });
            break;
        }
        case Value_Provider_Suffix::PUSH_BOOL: {
            this->value_providers.push({ Type_Name::BOOL });
            break;
        }
        case Value_Provider_Suffix::PUSH_CHAR: {
            this->value_providers.push({ Type_Name::CHAR });
            break;
        }
        case Value_Provider_Suffix::PUSH_STRING: {
            this->value_providers.push({ Type_Name::STRING });
            break;
        }
        case Value_Provider_Suffix::PUSH_INT_BIN: {
            this->value_providers.push({ Type_Name::INTEGER });
            break;
        }
        case Value_Provider_Suffix::PUSH_INT_HEX: {
            this->value_providers.push({ Type_Name::INTEGER });
            break;
        }
    }
}

auto get_operator_instruction(std::string const& op_sign) -> std::string {
    if (op_sign == "+") {
        return "ADD";
    } else if (op_sign == "-") {
        return "SUB";
    } else if (op_sign == "&") {
        return "AND";
    } else if (op_sign == "|") {
        return "OR";
    } else if (op_sign == "~") {
        return "NOT";
    } else if (op_sign == "<<") {
        return "SLL";
    } else if (op_sign == ">>") {
        return "SRL";
    } else if (op_sign == "^") {
        return "XOR";
    }
    return op_sign;
}

auto get_operator_imediate_candidate(std::string const& op) -> std::string {
    if (op == "ADD" || op == "SUB" || op == "AND" || op == "OR" || op == "LD" || op == "XOR") {
        return op + "I";
    } else {
        return op;
    }
}

auto Semantic::do_value_access_action(int suffix, [[maybe_unused]] Token const* token) -> void {
    switch (Value_Access_Suffix(suffix)) {
        case Value_Access_Suffix::NAME_ACCESS: {
            auto provider = this->name_providers.top();
            this->name_providers.pop();

            if (auto name_opt = this->try_get_name(provider.name_id); name_opt.has_value()) {
                auto name = name_opt.value();
                name->read = true;

                if (name->function && !provider.function_call) {
                    this->issue_error(
                        detail::to_string("Name '", name->id, "' is a function.")
                    );
                }

                if (!name->initialized) {
                    this->issue_warning(
                        detail::to_string("Name '", name->id, "' is unitialized when used.")
                    ); // @TODO Enhance Logger class
                }

                if (provider.subscript_access) {
                    auto child_type = name->inferred;
                    child_type.array = false;

                    this->expression_nodes.push({ child_type });
                } else {
                    this->expression_nodes.push({ name->inferred });
                }

                if (!this->gc.binary_second_operand.top()) {
                    if (name->function && provider.function_call) {    
                        if (provider.name_id == "read") {
                            this->bip_asm_text("LD", "$in_port");
                        } else if (provider.name_id == "print") {
                            this->bip_asm_text("STO", "$out_port");
                        }
                    } else if (provider.subscript_access) {
                        this->bip_asm_text("STO", "$indr");
                        this->bip_asm_text("LDV", this->bip_asm_hash_name(name));
                    } else {
                        this->bip_asm_text("LD", this->bip_asm_hash_name(name));
                    }     
                } else {
                    bool rel = is_relational(this->gc.operators.top());

                    if (name->function && provider.function_call) {
                        if (provider.name_id == "read") {
                            this->bip_asm_text("STO", "1001");
                            this->bip_asm_text("LD", "$in_port");

                            if (rel) {
                                this->bip_asm_text("STO", "1002");
                                this->bip_asm_text("LD", "1001");
                                this->bip_asm_text("SUB", "1002");
                            } else {
                                this->bip_asm_text(this->gc.operators.top(), "1001");
                            }
                        } else {
                            this->bip_asm_text("STO", "1002");
                            this->bip_asm_text("LD", "1001");
                            this->bip_asm_text(this->gc.operators.top(), "1002");
                        }
                    } else if (provider.subscript_access) {
                        this->bip_asm_text("STO", "$indr");
                        this->bip_asm_text("LDV", this->bip_asm_hash_name(name));
                        this->bip_asm_text("STO", "1002");
                        this->bip_asm_text("LD", "1001");

                        if (rel) {
                            this->bip_asm_text("STO", "1002");
                            this->bip_asm_text("LD", "1001");
                            this->bip_asm_text("SUB", "1002");
                        } else {
                            this->bip_asm_text(this->gc.operators.top(), "1002");
                        }
                    } else {
                        if (rel) {
                            this->bip_asm_text("LD", this->bip_asm_hash_name(name));
                            this->bip_asm_text("STO", "1002");
                            this->bip_asm_text("LD", "1001");
                            this->bip_asm_text("SUB", "1002");
                        } else {
                            this->bip_asm_text(this->gc.operators.top(), this->bip_asm_hash_name(name));
                        }
                    }
                    this->gc.binary_second_operand.top() = false;
                    this->gc.operators.pop();
                }
            }

            break;
        }
        case Value_Access_Suffix::LITERAL_ACCESS: {
            if (this->gc.vector_constructed) {
                this->gc.vector_constructed = false;
            } else {
                auto provider = this->value_providers.top();
                this->value_providers.pop();

                this->expression_nodes.push({ provider.type });

                if (!this->gc.binary_second_operand.top()) {
                    this->bip_asm_text("LDI", token->get_lexeme());
                } else {
                    if (is_relational(this->gc.operators.top())) {
                        this->bip_asm_text("LDI", token->get_lexeme());
                        this->bip_asm_text("STO", "1002");
                        this->bip_asm_text("LD", "1001");
                        this->bip_asm_text("SUB", "1002");
                    } else {
                        this->bip_asm_text(get_operator_imediate_candidate(this->gc.operators.top()), token->get_lexeme());
                    }

                    this->gc.binary_second_operand.top() = false;
                    this->gc.operators.pop();
                }
            }


            break;
        }
        case Value_Access_Suffix::ACK_BINARY_SECOND_OPERAND: {
            this->gc.binary_second_operand.top() = true;
            this->gc.operators.push(get_operator_instruction(token->get_lexeme()));

            if (this->gc.is_flow_control) {
                this->gc.flow_control_operators.push(token->get_lexeme());
            }

            if (is_relational(this->gc.operators.top())) {
                this->bip_asm_text("STO", "1001");
            }

            break;
        }
    }
}

auto Semantic::do_assignment_action(int suffix, [[maybe_unused]] Token const* token) -> void {
    switch (Assignment_Suffix(suffix)) {
        case Assignment_Suffix::FETCH_ADDRESS: {
            auto provider = this->name_providers.top();

            if (auto name_opt = this->try_get_name(provider.name_id); name_opt.has_value()) {
                if (provider.subscript_access) {
                    this->bip_asm_text("STO", "1000");
                }
            }

            break;
        }
        case Assignment_Suffix::ASSIGN: {
            auto provider = this->name_providers.top();
            this->name_providers.pop();

            auto expression_node = this->expression_nodes.top();
            this->expression_nodes.pop();

            if (auto name_opt = this->try_get_name(provider.name_id); name_opt.has_value()) {
                auto name = name_opt.value();
                name->initialized = true;

                this->sanitize_type_compatibility(name, expression_node.type);

                if (provider.subscript_access) {
                    // Save RVALUE
                    this->bip_asm_text("STO", "1001");

                    // Restore subscript index and store it in $indr
                    this->bip_asm_text("LD", "1000");
                    this->bip_asm_text("STO", "$indr");

                    // Restore RVALUE
                    this->bip_asm_text("LD", "1001");

                    // Store acc in name at $indr
                    this->bip_asm_text("STOV", this->bip_asm_hash_name(name));
                } else {
                    this->bip_asm_text("STO", this->bip_asm_hash_name(name));
                }
            }

            break;
        }
    }
}

auto Semantic::do_expression_handling_action(int suffix, [[maybe_unused]] Token const* token) -> void {
    auto push_result_type = [&](Expression_Node && node) {
        if (node.type.name != Type_Name::INVALID) {
            this->expression_nodes.push(std::move(node));
        } else {
            this->issue_error(
                detail::to_string("Invalid result type caught in expression.")
            ); // @TODO Move to class scope, enhance Logger class
        }
    };

    if (is_unary_expression_suffix(suffix)) {
        auto node = this->expression_nodes.top();
        this->expression_nodes.pop();

        if (!type_traits::is_primitive(node.type)) {
            this->issue_error(
                detail::to_string("Array or pointer expression is not supported.")
            ); // @TODO enhance Logger class
        }

        if (node.type.name == Type_Name::VOID) {
            this->issue_error(
                detail::to_string("Invalid operand in unary expression (", get_type_description(node.type.name), ").")
            ); // @TODO enhance Logger class
        }

        switch (Unary_Expression_Handling_Suffix(suffix)) {
            case Unary_Expression_Handling_Suffix::ARITHMETIC_ADD:
            case Unary_Expression_Handling_Suffix::ARITHMETIC_SUB: {
                if (type_traits::is_numeric(node.type)) {
                    push_result_type({ node.type });
                } else {
                    this->issue_error(detail::to_string("Unary arithmetic operator on non numeric type."));
                    // @TODO enhance Logger class
                }
                break;
            }
            case Unary_Expression_Handling_Suffix::LOGICAL_NOT: {
                if (type_traits::is_truthy_type(node.type)) {
                    push_result_type({{ Type_Name::BOOL }});
                } else {
                    this->issue_error(detail::to_string("Unary negate operator on non truthy type."));
                    // @TODO enhance Logger class
                }
                break;
            }
            case Unary_Expression_Handling_Suffix::BITWISE_COMP: {
                if (type_traits::is_integral(node.type)) {
                    push_result_type({ node.type });
                } else {
                    this->issue_error(detail::to_string("Unary complement operator on non integral type."));
                    // @TODO enhance Logger class
                }
                break;
            }
            case Unary_Expression_Handling_Suffix::ATTRIBUTION_PREFIX_INC:
            case Unary_Expression_Handling_Suffix::ATTRIBUTION_PREFIX_DEC:
            case Unary_Expression_Handling_Suffix::ATTRIBUTION_POSTFIX_INC:
            case Unary_Expression_Handling_Suffix::ATTRIBUTION_POSTFIX_DEC: {
                if (type_traits::is_numeric(node.type)) {
                    push_result_type({ node.type });
                } else {
                    this->issue_error(detail::to_string("Unary arithmetic operator on non numeric type."));
                    // @TODO enhance Logger class
                }
                break;
            }
        }
    } else {
        auto left = this->expression_nodes.top();
        this->expression_nodes.pop();

        auto right = this->expression_nodes.top();
        this->expression_nodes.pop();

        if (!type_traits::is_primitive(left.type) || !type_traits::is_primitive(left.type)) {
            this->issue_error(
                detail::to_string("Array or pointer expression is not supported.")
            ); // @TODO enhance Logger class
        }

        if (left.type.name == Type_Name::VOID || right.type.name == Type_Name::VOID) {
            this->issue_error(
                detail::to_string("Invalid operand in binary expression (", get_type_description(left.type.name),
                                    " and ", get_type_description(right.type.name), ").")
            ); // @TODO enhance Logger class
        }

        switch (Binary_Expression_Handling_Suffix(suffix)) {
            case Binary_Expression_Handling_Suffix::LOGICAL_OR:
            case Binary_Expression_Handling_Suffix::LOGICAL_AND: {
                push_result_type({
                    { Semantic_Table::get_result_type(left.type.name, right.type.name, Operator::LOGICAL) }
                });
                break;
            }
            case Binary_Expression_Handling_Suffix::BITWISE_OR:
            case Binary_Expression_Handling_Suffix::BITWISE_XOR:
            case Binary_Expression_Handling_Suffix::BITWISE_AND:
            case Binary_Expression_Handling_Suffix::BITWISE_SR:
            case Binary_Expression_Handling_Suffix::BITWISE_SL: {
                push_result_type({
                    { Semantic_Table::get_result_type(left.type.name, right.type.name, Operator::BITWISE) }
                });
                break;
            }
            case Binary_Expression_Handling_Suffix::RELATIONAL_EQ:
            case Binary_Expression_Handling_Suffix::RELATIONAL_NEQ:
            case Binary_Expression_Handling_Suffix::RELATIONAL_GT:
            case Binary_Expression_Handling_Suffix::RELATIONAL_LT:
            case Binary_Expression_Handling_Suffix::RELATIONAL_GTE:
            case Binary_Expression_Handling_Suffix::RELATIONAL_LTE: {
                push_result_type({
                    { Semantic_Table::get_result_type(left.type.name, right.type.name, Operator::RELATIONAL) }
                });
                break;
            }
            case Binary_Expression_Handling_Suffix::ARITHMETIC_ADD: {
                push_result_type({
                    { Semantic_Table::get_result_type(left.type.name, right.type.name, Operator::ADD) }
                });
                break;
            }
            case Binary_Expression_Handling_Suffix::ARITHMETIC_SUB: {
                push_result_type({
                    { Semantic_Table::get_result_type(left.type.name, right.type.name, Operator::SUB) }
                });
                break;
            }
            case Binary_Expression_Handling_Suffix::ARITHMETIC_MULT: {
                push_result_type({
                    { Semantic_Table::get_result_type(left.type.name, right.type.name, Operator::MULT) }
                });
                break;
            }
            case Binary_Expression_Handling_Suffix::ARITHMETIC_DIV: {
                push_result_type({
                    { Semantic_Table::get_result_type(left.type.name, right.type.name, Operator::DIV) }
                });
                break;
            }
            case Binary_Expression_Handling_Suffix::ARITHMETIC_REM: {
                push_result_type({
                    { Semantic_Table::get_result_type(left.type.name, right.type.name, Operator::REM) }
                });
                break;
            }
            case Binary_Expression_Handling_Suffix::ARITHMETIC_EXP: {
                push_result_type({
                    { Semantic_Table::get_result_type(left.type.name, right.type.name, Operator::EXP) }
                });
                break;
            }
            default: {
                std::cerr << "Uncaught operator handling " << suffix << std::endl;
            }
        }
    }
}

auto Semantic::do_vector_constructor_action(int suffix, [[maybe_unused]] Token const* token) -> void {
    switch (Vector_Constructor_Suffix(suffix)) {
        case Vector_Constructor_Suffix::BEGIN_FILLED_DECL: {
            this->gc.vector_index = 0;
            break;
        }
        case Vector_Constructor_Suffix::END_FILLED_DECL: {
            this->gc.name_ids.pop();
            this->gc.vector_constructed = true;

            break;
        }
        case Vector_Constructor_Suffix::PUSH_EMPTY: {
            this->issue_error(
                detail::to_string("Can't initialize an empty array.")
            ); // @TODO Move to class scope, enhance Logger class

            break;
        }
        case Vector_Constructor_Suffix::PUSH_VALUE: {
            // Save RVALUE
            this->bip_asm_text("STO", "1001");

            // Restore subscript index and store it in $indr
            this->bip_asm_text("LDI", std::to_string(this->gc.vector_index++));
            this->bip_asm_text("STO", "$indr");

            // Restore RVALUE
            this->bip_asm_text("LD", "1001");

            // Store acc in name at $indr
            this->bip_asm_text("STOV", this->gc.name_ids.top());

            break;
        }
    }
}

auto Semantic::do_flow_control_action(int suffix, [[maybe_unused]] Token const* token) -> void {
    auto matching_relational_opposite = [](std::string const& op) -> std::string {
        if (op == ">") {
            return "BLE";
        } else if (op == "<") {
            return "BGE";
        } else if (op == ">=") {
            return "BLT";
        } else if (op == "<=") {
            return "BGT";
        } else if (op == "==") {
            return "BNE";
        } else if (op == "!=") {
            return "BEQ";
        }
        return "HLT";
    };

    auto matching_relational = [](std::string const& op) -> std::string {
        if (op == ">") {
            return "BGT";
        } else if (op == "<") {
            return "BLT";
        } else if (op == ">=") {
            return "BGE";
        } else if (op == "<=") {
            return "BLE";
        } else if (op == "==") {
            return "BEQ";
        } else if (op == "!=") {
            return "BNE";
        }
        return "HLT";
    };

    switch (Flow_Control_Suffix(suffix)) {
        case Flow_Control_Suffix::ACK_IF_CONDITIONAL: {
            this->gc.is_flow_control = true;

            break;
        }
        case Flow_Control_Suffix::BEGIN_TRUE_BLOCK: {
            auto label = this->bip_asm_create_label();
            this->gc.labels.push(label);

            auto op = this->gc.flow_control_operators.top();
            this->gc.flow_control_operators.pop();

            this->bip_asm_text(matching_relational_opposite(op), label);

            this->gc.is_flow_control = false;

            break;
        }
        case Flow_Control_Suffix::BEGIN_FALSE_BLOCK: {
            auto label = this->gc.labels.top();
            this->gc.labels.pop();

            auto end_label = this->bip_asm_create_label();

            this->bip_asm_text("JMP", end_label);
            this->gc.labels.push(end_label);

            this->bip_asm_text(label, ":");

            break;
        }
        case Flow_Control_Suffix::ACK_IF_END: {
            auto label = this->gc.labels.top();
            this->gc.labels.pop();
            this->bip_asm_text(label, ":");

            break;
        }
        case Flow_Control_Suffix::ACK_WHILE_CONDITIONAL: {
            auto label = this->bip_asm_create_label();
            this->gc.labels.push(label);
            this->bip_asm_text(label, ":");

            this->gc.is_flow_control = true;

            break;
        }
        case Flow_Control_Suffix::BEGIN_WHILE_LOOP: {
            auto label = this->bip_asm_create_label();
            this->gc.labels.push(label);

            auto op = this->gc.flow_control_operators.top();
            this->gc.flow_control_operators.pop();

            this->bip_asm_text(matching_relational_opposite(op), label);

            this->gc.is_flow_control = false;

            break;
        }
        case Flow_Control_Suffix::ACK_WHILE_END: {
            auto end_label = this->gc.labels.top();
            this->gc.labels.pop();

            auto begin_label = this->gc.labels.top();
            this->gc.labels.pop();

            this->bip_asm_text("JMP", begin_label);
            this->bip_asm_text(end_label, ":");

            break;
        }
        case Flow_Control_Suffix::ACK_DO_WHILE_CONDITIONAL: {
            auto label = this->bip_asm_create_label();
            this->gc.labels.push(label);
            this->bip_asm_text(label, ":");

            break;
        }
        case Flow_Control_Suffix::ACK_DO_WHILE_EXPRESSION: {
            this->gc.is_flow_control = true;

            break;
        }
        case Flow_Control_Suffix::ACK_DO_WHILE_END: {
            auto label = this->gc.labels.top();
            this->gc.labels.pop();

            auto op = this->gc.flow_control_operators.top();
            this->gc.flow_control_operators.pop();

            this->bip_asm_text(matching_relational(op), label);

            this->gc.is_flow_control = false;

            break;
        }
        case Flow_Control_Suffix::ACK_FOR_CONDITIONAL: {
            auto label = this->bip_asm_create_label();
            this->gc.labels.push(label);
            this->bip_asm_text(label, ":");

            this->gc.is_flow_control = true;

            break;
        }
        case Flow_Control_Suffix::ACK_FOR_MUTATION: {
            auto label = this->bip_asm_create_label();
            this->gc.labels.push(label);

            auto op = this->gc.flow_control_operators.top();
            this->gc.flow_control_operators.pop();

            this->bip_asm_text(matching_relational_opposite(op), label);

            this->gc.redirected_output = true;
            this->gc.redirection.push({});

            break;
        }
        case Flow_Control_Suffix::ACK_FOR_MUTATION_END: {
            this->gc.redirected_output = false;

            break;
        }
        case Flow_Control_Suffix::ACK_FOR_END: {
            auto mutation = this->gc.redirection.top();
            this->gc.redirection.pop();

            for (auto const& [ op, operand ] : mutation) {
                this->bip_asm_text(op, operand);
            }

            auto end_label = this->gc.labels.top();
            this->gc.labels.pop();

            auto begin_label = this->gc.labels.top();
            this->gc.labels.pop();

            this->bip_asm_text("JMP", begin_label);
            this->bip_asm_text(end_label, ":");

            break;
        }
        case Flow_Control_Suffix::ACK_PROCEDURE_DECL: {
            this->bip_asm_text(this->bip_asm_hash_function_name(this->names.top().id), ":");
            break;
        }
        case Flow_Control_Suffix::ACK_PROCEDURE_RET: {
            this->bip_asm_text_no_adjacent("RETURN", "0");

            if (this->current_function_scope.has_value()) {
                auto & [ name, returned ] = this->current_function_scope.value();
                returned = true;

                auto expression_node = this->expression_nodes.top();
                this->expression_nodes.pop();

                if (name.inferred.name == Type_Name::VOID) {
                    this->issue_error(detail::to_string("Can't return value within a Void function (procedure) '",
                                                        name.id, "'."));
                }
                else {
                    this->sanitize_type_compatibility(&name, expression_node.type);
                }
            }

            break;
        }
        case Flow_Control_Suffix::ACK_PROCEDURE_END: {
            this->bip_asm_text_no_adjacent("RETURN", "0");

            break;
        }
    }
}

auto Semantic::verify_scope_lifetime(std::size_t scope) -> void {
    static std::vector<std::tuple<std::size_t, std::string, bool>> exceptions = {
        {0, "main", true}
    };

    auto is_exception = [&](Name const& name) -> bool {
        return std::any_of(std::begin(exceptions), std::end(exceptions), [&name](auto const& tuple) -> bool {
            auto [ scope, id, function ] = tuple;

            return name.scope == scope && name.id == id && name.function == function;
        });
    };

    for (auto const& name : this->name_table) {
        if (name.scope == scope && !is_exception(name) && !name.read) {
            this->issue_warning(detail::to_string("Name '", name.id, "' declared and its value was never read."));
            // @Todo enhance Logger Class
        }
    }
}

auto Semantic::sanitize_type(Type const& type, bool param, bool function) -> void {
    if (type.name == Type_Name::ANY && type.array) {
        this->issue_error(detail::to_string("Declaration as array of Any."));
        // @Todo enhance Logger Class
    }
    if (type.name == Type_Name::ANY && param) {
        this->issue_error(detail::to_string("Any can't be used as a function template."));
        // @Todo enhance Logger Class
    }
    if (type.name == Type_Name::ANY && function) {
        this->issue_error(detail::to_string("Any deduced type can't be used in function return."));
        // @Todo enhance Logger Class
    }
    if (type.name == Type_Name::VOID && !function) {
        this->issue_error(detail::to_string("Declaration of Void name is not allowed."));
        // @Todo enhance Logger Class
    }
}

auto Semantic::sanitize_check_declared([[maybe_unused]] std::string const& id) -> void {} // @TODO Refactor

auto Semantic::sanitize_type_compatibility(Name const* name, Type const& type) -> void {
    if (auto support = Semantic_Table::get_type_compatibility(name->inferred.name, type.name);
        support == Type_Compatibility::NONE) {
        this->issue_error(
            detail::to_string("Can't assign ", get_type_description(type.name),
                              " into ", get_type_description(name->inferred.name),
                              " in name '", name->id, "'.")
        ); // @TODO Logger Class
    } else if (support == Type_Compatibility::NARROWING) {
        this->issue_warning(
            detail::to_string("Narrowing ", get_type_description(type.name),
                              " into ", get_type_description(name->inferred.name),
                              " in name '", name->id, "'.")
        ); // @TODO Logger Class
    }
}

auto Semantic::sanitize_type_compatibility(Name const& name, Type const& type) -> void {
    this->sanitize_type_compatibility(&name, type);
}

auto Semantic::get_name(std::function<bool(Name const&)> const& predicate) -> std::optional<Name*> {
    for (auto & name : this->name_table) {
        if (predicate(name)) {
            return std::make_optional(&name);
        }
    }

    return std::nullopt;
}

auto Semantic::get_name(std::size_t scope, std::string const& id) -> std::optional<Name*> {
    return this->get_name([scope, &id](Name const& name) {
        return name.scope == scope && name.id == id;
    });
}

auto Semantic::get_name(std::string const& id) -> std::optional<Name*> {
    auto scopes_copy = this->scopes;

    while (!scopes_copy.empty()) {
        if (auto name = this->get_name(scopes_copy.top(), id); name.has_value()) {
            return name;
        }
        scopes_copy.pop();
    }

    return std::nullopt;
}

auto Semantic::try_get_name(std::string const& id) -> std::optional<Name*> {
    if (auto name = this->get_name(id); name.has_value()) {
        return name;
    } else {
        this->issue_error(detail::to_string("Use of an undeclared name '", id, "'")); // @TODO enhance Logger class
        return std::nullopt;
    }
}

auto Semantic::try_put_name(Name const& name) -> void {
    if (this->get_name(name.scope, name.id).has_value()) {
        this->issue_error(detail::to_string("Redefinition of '", name.id, "'")); // @TODO enhance Logger class
    }
    else {
        this->name_table.push_back(name);
        this->bip_asm_data(name);
    }
}

auto Semantic::fetch_function_params(Name const& name) -> std::vector<Name> {
    std::vector<Name> params {};

    if (name.function) {
        for (auto const& n : this->name_table) {
            if (n.parent.has_value() && n.parent.value().first == name.scope
                    && n.parent.value().second == name.id) {
                params.push_back(n);
            }
        }
    }

    return params;
}

auto Semantic::mismatch_params(std::vector<Name> const& params, std::vector<Expression_Node> const& args) -> bool {
    for (auto i = 0ull, len = std::min(params.size(), args.size()); i < len; ++i) {
        if (params.at(i).inferred != args.at(i).type) {
            return true;
        }
    }

    return params.size() != args.size();
}

auto Semantic::format_function_signature(std::vector<Name> const& params) -> std::string {
    auto head = std::string{"<"};
    auto tail = std::string{">"};

    auto body = std::accumulate(std::begin(params), std::end(params), head, [](auto acc, auto param) -> std::string {
        return acc + to_string(param.inferred) + std::string{","};
    });

    if (params.size() > 0) { body.pop_back(); };

    return body + tail;
}

auto Semantic::format_function_signature(std::vector<Expression_Node> const& args) -> std::string {
    auto head = std::string{"<"};
    auto tail = std::string{">"};

    auto body = std::accumulate(std::begin(args), std::end(args), head, [](auto acc, auto arg) -> std::string {
        return acc + to_string(arg.type) + std::string{","};
    });

    if (args.size() > 0) { body.pop_back(); };

    return body + tail;
}

auto Semantic::get_name_table() const -> Name_Table {
    return this->name_table;
}

auto Semantic::get_issues() const -> std::vector<Issue> {
    return this->issues;
}

auto Semantic::get_program() const -> bip_asm::BIP_Program {
    return this->compiled;
}

auto Semantic::infer_any_type(Type const& type, Expression_Node const& node) -> Type {
    return { node.type.name, node.type.array, type.pointer, type.ref, type.constant };
}

auto Semantic::issue_warning(std::string && message) noexcept -> void {
    logger->log(message);
    this->issues.push_back({ Issue::Issue_Type::WARNING, std::move(message) });
}

auto Semantic::issue_error(std::string && message) -> void {
    logger->log(message);
    this->issues.push_back({ Issue::Issue_Type::ERROR, std::move(message) });
    throw Semantic_Error(message);
}

auto Semantic::bip_asm_data(Name const& name) -> void {
    if (!name.function) {
        if (name.inferred.array) {
            auto array_length = name.inferred.length > 1 ? name.inferred.length : 2;
            this->compiled.data.push_back({ this->bip_asm_hash_name(&name), std::vector<int>(array_length, 0) });
        } else {
            this->compiled.data.push_back({ this->bip_asm_hash_name(&name), {0} });
        }
    }
}

auto Semantic::bip_asm_text(std::string const& op, std::string const& operand) -> void {
    if (this->gc.redirected_output) {
        this->gc.redirection.top().push_back({ op, operand });
    } else {
        this->compiled.text.push_back({ op, operand });
    }
}

auto Semantic::bip_asm_text_no_adjacent(std::string const& op, std::string const& operand) -> void {
    if (this->compiled.text.size() == 0 || this->compiled.text.back().op != op
            || this->compiled.text.back().operand != operand) {
        this->bip_asm_text(op, operand);
    }
}

auto Semantic::bip_asm_text_pop_back() -> void {
    if (this->compiled.text.size() > 0) {
        this->compiled.text.pop_back();
    }
}

auto Semantic::bip_asm_hash_name(Name const* name) -> std::string {
    return std::string("s") + std::to_string(name->scope) + std::string("_") + name->id;
}

auto Semantic::bip_asm_hash_function_name(std::string const& id) -> std::string {
    return std::string("_fn_") + id;
}

auto Semantic::bip_asm_hash_function_name(Name const* name) -> std::string {
    if (name) {
        return this->bip_asm_hash_function_name(name->id);
    }
    return { "_fn_nullptr" };
}

auto Semantic::bip_asm_create_label() -> std::string {
    return std::string("R") + std::to_string(this->gc.label_index++);
}

} //namespace wpl

