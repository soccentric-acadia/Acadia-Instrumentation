// ============================================================================
//  Acadia Instrumentation
//
//  Copyright (C) 2026 SoCcentric
//  Author: Sandesh Ghimire (sandesh@soccentric)
//
//  Licensed under the MIT License; see the LICENSE file in the repository root.
//
//  NO WARRANTY:
//  This software is provided "AS IS", without warranty of any kind, either
//  express or implied, including but not limited to the warranties of
//  merchantability, fitness for a particular purpose, and non-infringement.
//  In no event shall Soccentric be liable for any claim, damages, or other
//  liability arising from the use of this software.
//
//  This software may not be used, copied, modified, distributed, or
//  disclosed, in whole or in part, without the express prior written
//  consent of Soccentric.
// ============================================================================
/**
 * @file lib/common/src/command_table.cpp
 * @brief Implementation of the shared command table: JSON result envelopes, argument parsing, and dispatch helpers (REQ §3.2).
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 SoCcentric. Licensed under the MIT License.
 */

#include "instr/common/command_table.h"

#include <sstream>

#include "instr/common/crypto.h"

namespace instr
{

/// @brief Render the wire envelope: {"ok":true,"data":...} / {"ok":false,"error":{...}}.
/// @return The envelope JSON text.
std::string CommandResult::envelope() const
{
    if (ok)
    {
        JValue v = JValue::object();
        v.set("ok", JValue(true));
        JValue data;
        auto parsed = json_parse(json_data.empty() ? "null" : json_data);
        data = parsed.ok() ? *parsed : JValue(json_data);
        v.set("data", data);
        return v.dump();
    }
    JValue v = JValue::object();
    v.set("ok", JValue(false));
    JValue err = JValue::object();
    err.set("code", JValue(error_code));
    err.set("message", JValue(error_message));
    v.set("error", err);
    return v.dump();
}

/// @brief Parse command arguments into a JValue object.
/// @param json_args Raw JSON text; "{}" is substituted for an empty string.
/// @return Parsed object, or a Corrupt error for malformed JSON.
Result<JValue> args_object(const std::string& json_args)
{
    if (json_args.empty()) return Result<JValue>::good(JValue::object());
    auto v = json_parse(json_args);
    if (!v.ok()) return v;
    if (!(*v).is_object()) return Result<JValue>::fail(Errc::InvalidArgument, "command arguments must be a JSON object", "args_object");
    return v;
}

static bool type_matches(const JValue& value, JsonArgType type)
{
    switch (type)
    {
    case JsonArgType::Any: return true;
    case JsonArgType::String: return value.is_string();
    case JsonArgType::Number: return value.is_number();
    case JsonArgType::Bool: return value.is_bool();
    case JsonArgType::Object: return value.is_object();
    case JsonArgType::Array: return value.is_array();
    }
    return false;
}

static const char* class_name(OperationClass c)
{
    switch (c)
    {
    case OperationClass::ReadOnly: return "read-only";
    case OperationClass::Mutating: return "mutating";
    case OperationClass::Destructive: return "destructive";
    case OperationClass::Manufacturing: return "manufacturing";
    }
    return "unknown";
}

CommandResult CommandTable::dispatch(const std::string& op, const std::string& json_args, const DispatchContext& context) const
{
    const CommandSpec* spec = find(op);
    if (!spec) return CommandResult::err(Errc::NotFound, "unknown op: " + op);
    if (json_args.size() > kMaxCommandArguments) return CommandResult::err(Errc::BudgetExceeded, "command arguments exceed 64 KiB");
    if (spec->local_only && context.actor != "local") return CommandResult::err(Errc::Unavailable, "operation is local-only");
    if (spec->classification == OperationClass::Manufacturing && !context.manufacturing_mode) return CommandResult::err(Errc::Unavailable, "manufacturing mode is not enabled");
    if ((spec->classification == OperationClass::Destructive || spec->require_confirmation) && !context.confirmed) return CommandResult::err(Errc::InvalidArgument, "explicit confirmation is required");

    auto parsed = args_object(json_args);
    if (!parsed.ok()) return CommandResult::from_error(parsed.error);
    for (const auto& arg : spec->args)
    {
        const JValue* value = parsed->find(arg.name);
        if (arg.required && !value) return CommandResult::err(Errc::InvalidArgument, arg.name + " is required");
        if (!value) continue;
        if (!type_matches(*value, arg.type)) return CommandResult::err(Errc::InvalidArgument, arg.name + " has the wrong JSON type");
        if (arg.max_string_bytes && value->is_string() && value->as_string().size() > arg.max_string_bytes) return CommandResult::err(Errc::BudgetExceeded, arg.name + " exceeds its size limit");
    }
    for (const auto& member : parsed->members())
    {
        bool declared = false;
        for (const auto& arg : spec->args)
            if (arg.name == member.first)
            {
                declared = true;
                break;
            }
        if (!declared) return CommandResult::err(Errc::InvalidArgument, "unknown argument: " + member.first);
    }
    CommandResult result = spec->handler(json_args);
    if (result.ok && result.json_data.size() > kMaxCommandResult) return CommandResult::err(Errc::BudgetExceeded, "command result exceeds 256 KiB");
    return result;
}

std::string CommandTable::catalog_json() const
{
    JValue root = JValue::object();
    root.set("revision", JValue(static_cast<int64_t>(revision_)));
    JValue operations = JValue::array();
    for (const CommandSpec* spec : all())
    {
        JValue item = JValue::object();
        item.set("op", JValue(spec->op));
        item.set("description", JValue(spec->description));
        item.set("classification", JValue(class_name(spec->classification)));
        item.set("local_only", JValue(spec->local_only));
        JValue args = JValue::array();
        for (const auto& arg : spec->args)
        {
            JValue a = JValue::object();
            a.set("name", JValue(arg.name));
            a.set("required", JValue(arg.required));
            args.push(a);
        }
        item.set("args", args);
        operations.push(item);
    }
    root.set("operations", operations);
    return root.dump();
}

std::string CommandTable::catalog_hash() const
{
    return crypto::sha256(catalog_json()).hex();
}

/// @brief --help output listing every command.
/// @param prog Tool name used in the usage line.
/// @return The help text.
std::string CommandTable::help_text(const std::string& prog) const
{
    std::ostringstream o;
    o << "Usage: " << prog << " [options] <op> [args...]\n\nOptions:\n"
      << "  --help            show this help\n"
      << "  --help <op>       help for one operation\n"
      << "  --json            emit raw JSON envelope (default)\n"
      << "  --socket PATH     override ctrl socket path\n"
      << "  completion bash   emit bash completion script\n"
      << "  completion zsh    emit zsh completion script\n\nOperations:\n";
    for (const CommandSpec* s : all())
    {
        o << "  " << s->op;
        std::string args;
        for (const auto& a : s->args) args += (a.required ? " <" : " [") + a.name + (a.required ? ">" : "]");
        o << args << "\n      " << s->description << "\n";
    }
    return o.str();
}

/// @brief Help for a single command ("help <op>" output).
/// @param op Operation name to describe.
/// @return Help text, or a NotFound error message when unknown.
std::string CommandTable::help_for(const std::string& op) const
{
    const CommandSpec* s = find(op);
    if (!s) return "unknown op: " + op;
    std::ostringstream o;
    o << s->op << " — " << s->description << "\nArguments:\n";
    for (const auto& a : s->args) o << "  " << a.name << (a.required ? " (required)" : " (optional)") << ": " << a.help << "\n";
    return o.str();
}

/// @brief Generate a bash completion script for @p prog from the registered ops.
/// @param prog Tool name to complete.
/// @return The completion script.
std::string CommandTable::completion_bash(const std::string& prog) const
{
    std::ostringstream o;
    o << "# bash completion for " << prog << "\n"
      << "_" << prog << "_completions() {\n"
      << "  local cur=\"${COMP_WORDS[COMP_CWORD]}\"\n"
      << "  local ops=\"";
    bool first = true;
    for (const CommandSpec* s : all())
    {
        if (!first) o << " ";
        o << s->op;
        first = false;
    }
    o << "\"\n"
      << "  case \"${COMP_WORDS[1]}\" in\n"
      << "    completion) COMPREPLY=( $(compgen -W 'bash zsh' -- \"$cur\") ); return ;;\n"
      << "    *)\n"
      << "      if [ \"$COMP_CWORD\" -eq 1 ]; then\n"
      << "        COMPREPLY=( $(compgen -W \"$ops --help --json --socket completion\" -- \"$cur\") "
         ")\n"
      << "      else\n"
      << "        local op=\"${COMP_WORDS[1]}\"\n"
      << "        local args=\"\"";
    first = true;
    for (const CommandSpec* s : all())
    {
        if (!first) o << ";";
        o << s->op << ":";
        bool fa = true;
        for (const auto& a : s->args)
        {
            if (!fa) o << ",";
            o << a.name << (a.required ? "=*" : "");
            fa = false;
        }
        first = false;
    }
    o << "\"\n"
      << "        local spec\n"
      << "        for spec in ${args//;/ }; do\n"
      << "          if [ \"${spec%%:*}\" = \"$op\" ]; then\n"
      << "            local keys=\"${spec#*:}\"\n"
      << "            keys=\"${keys//,/ }\"\n"
      << "            COMPREPLY=( $(compgen -W \"$keys\" -- \"${cur#--}\") )\n"
      << "          fi\n"
      << "        done\n"
      << "      fi\n"
      << "      ;;\n"
      << "  esac\n"
      << "}\n"
      << "complete -F _" << prog << "_completions " << prog << "\n";
    return o.str();
}

/// @brief Generate a zsh completion script for @p prog from the registered ops.
/// @param prog Tool name to complete.
/// @return The completion script.
std::string CommandTable::completion_zsh(const std::string& prog) const
{
    std::ostringstream o;
    o << "#compdef " << prog << "\n"
      << "_" << prog << "() {\n"
      << "  local -a ops\n"
      << "  ops=(\n";
    for (const CommandSpec* s : all())
    {
        std::string desc = s->description;
        for (auto& c : desc)
            if (c == '\'' || c == '[' || c == ']') c = ' ';
        o << "    '" << s->op << "[" << desc << "]'\n";
    }
    o << "  )\n"
      << "  _describe 'operation' ops\n"
      << "}\n"
      << "_" << prog << " \"$@\"\n";
    return o.str();
}

} // namespace instr