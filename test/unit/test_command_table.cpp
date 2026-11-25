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
 * @file test/unit/test_command_table.cpp
 * @brief command table envelope, streaming sink semantics (§3.2)
 * @author Sandesh Ghimire (sandesh@soccentric)
 * @date 2026
 * @copyright (C) 2026 SoCcentric. Licensed under the MIT License.
 */

#include <gtest/gtest.h>

#include "instr/common/command_table.h"

using namespace instr;

/// @brief The envelope wire shapes: ok+data and ok=false+error{code,message}.
TEST(CommandEnvelope, OkAndErrorShapes)
{
    CommandResult ok = CommandResult::good(std::string("{\"x\":1}"));
    auto j = json_parse(ok.envelope());
    ASSERT_TRUE(j.ok());
    EXPECT_TRUE((*j).find("ok")->as_bool());
    EXPECT_EQ((*j).find("data")->find("x")->as_number(), 1);

    CommandResult err = CommandResult::err(Errc::BudgetExceeded, "too much");
    auto j2 = json_parse(err.envelope());
    ASSERT_TRUE(j2.ok());
    EXPECT_FALSE((*j2).find("ok")->as_bool());
    EXPECT_EQ((*j2).find("error")->find("code")->as_string(), "budget-exceeded");
    EXPECT_EQ((*j2).find("error")->find("message")->as_string(), "too much");
}

/// @brief Handlers validate declared args; undeclared JSON is rejected.
TEST(CommandTable, ArgSpecValidationIsDeclared)
{
    CommandTable t;
    t.add({"op", "needs args", {{"a", true, "required"}, {"b", false, "optional"}}, [](const std::string& args) {
               auto a = args_object(args);
               if (!a.ok()) return CommandResult::from_error(a.error);
               if (!(*a).has("a")) return CommandResult::err(Errc::InvalidArgument, "a is required");
               return CommandResult::good(std::string("{}"));
           }});
    EXPECT_FALSE(t.dispatch("op", "{}").ok);
    EXPECT_TRUE(t.dispatch("op", "{\"a\":1}").ok);
    EXPECT_FALSE(t.dispatch("op", "not-json").ok);
}

/// @brief "help <unknown>" answers with a diagnosable unknown-op message.
TEST(CommandTable, HelpForUnknownOpIsDiagnosable)
{
    CommandTable t;
    auto h = t.help_for("ghost");
    EXPECT_NE(h.find("unknown op"), std::string::npos);
}

TEST(CommandTable, GenericValidationPolicyAndLimits)
{
    CommandTable t;
    t.add({"mutate", "bounded mutation", {{"name", true, "name", JsonArgType::String, 4}}, [](const std::string&) { return CommandResult::good("{\"ok\":true}"); }, OperationClass::Mutating, true});
    DispatchContext remote;
    remote.actor = "hq";
    EXPECT_EQ(t.dispatch("mutate", "{\"name\":\"abc\"}", remote).error_code, "unavailable");
    EXPECT_EQ(t.dispatch("mutate", "{\"name\":\"abc\",\"extra\":1}").error_code, "invalid-argument");
    EXPECT_EQ(t.dispatch("mutate", "{\"name\":1}").error_code, "invalid-argument");
    EXPECT_EQ(t.dispatch("mutate", "{\"name\":\"abcde\"}").error_code, "budget-exceeded");

    CommandTable destructive;
    destructive.add({"wipe", "destructive", {}, [](const std::string&) { return CommandResult::good("{}"); }, OperationClass::Destructive, false, true});
    EXPECT_EQ(destructive.dispatch("wipe", "{}").error_code, "invalid-argument");
    DispatchContext confirmed;
    confirmed.confirmed = true;
    EXPECT_TRUE(destructive.dispatch("wipe", "{}", confirmed).ok);
}

TEST(CommandTable, CatalogRevisionAndHashAreStable)
{
    CommandTable t;
    t.add({"one", "first", {}, [](const std::string&) { return CommandResult::good("{}"); }});
    const auto hash = t.catalog_hash();
    EXPECT_EQ(t.catalog_revision(), 1u);
    EXPECT_EQ(hash, t.catalog_hash());
    t.add({"two", "second", {}, [](const std::string&) { return CommandResult::good("{}"); }});
    EXPECT_EQ(t.catalog_revision(), 2u);
    EXPECT_NE(hash, t.catalog_hash());
    auto catalog = json_parse(t.catalog_json());
    ASSERT_TRUE(catalog.ok());
    EXPECT_EQ((*catalog).find("operations")->size(), 2u);
}
