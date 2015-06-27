/*  libloadorder

A library for reading and writing the load order of plugin files for
TES III: Morrowind, TES IV: Oblivion, TES V: Skyrim, Fallout 3 and
Fallout: New Vegas.

Copyright (C) 2012    WrinklyNinja

This file is part of libloadorder.

libloadorder is free software: you can redistribute
it and/or modify it under the terms of the GNU General Public License
as published by the Free Software Foundation, either version 3 of
the License, or (at your option) any later version.

libloadorder is distributed in the hope that it will
be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with libloadorder.  If not, see
<http://www.gnu.org/licenses/>.
*/

#ifndef __LIBLO_TEST_API_LOAD_ORDER__
#define __LIBLO_TEST_API_LOAD_ORDER__

#include "tests/fixtures.h"

#include <boost/algorithm/string.hpp>

TEST_F(OblivionOperationsTest, GetLoadOrderMethod) {
    unsigned int method = 0;
    EXPECT_EQ(LIBLO_OK, lo_get_load_order_method(gh, &method));
    EXPECT_EQ(LIBLO_METHOD_TIMESTAMP, method);

    EXPECT_EQ(LIBLO_ERROR_INVALID_ARGS, lo_get_load_order_method(NULL, NULL));
    EXPECT_EQ(LIBLO_ERROR_INVALID_ARGS, lo_get_load_order_method(gh, NULL));
    EXPECT_EQ(LIBLO_ERROR_INVALID_ARGS, lo_get_load_order_method(NULL, &method));
}

TEST_F(SkyrimOperationsTest, GetLoadOrderMethod) {
    unsigned int method = 0;
    EXPECT_EQ(LIBLO_OK, lo_get_load_order_method(gh, &method));
    EXPECT_EQ(LIBLO_METHOD_TEXTFILE, method);

    EXPECT_EQ(LIBLO_ERROR_INVALID_ARGS, lo_get_load_order_method(NULL, NULL));
    EXPECT_EQ(LIBLO_ERROR_INVALID_ARGS, lo_get_load_order_method(gh, NULL));
    EXPECT_EQ(LIBLO_ERROR_INVALID_ARGS, lo_get_load_order_method(NULL, &method));
}

TEST_F(OblivionOperationsTest, SetLoadOrder_MissingPlugin) {
    ASSERT_EQ(LIBLO_OK, lo_set_game_master(gh, "Blank.esm"));

    // Try setting the load order with a missing plugin.
    char * missingPlugins[] = {
        "Blank.esm",
        "Blank.missing.esp"
    };
    size_t pluginsNum = 2;

    ASSERT_EQ(LIBLO_OK, lo_set_game_master(gh, "Blank.esm"));
    EXPECT_EQ(LIBLO_ERROR_FILE_NOT_FOUND, lo_set_load_order(gh, missingPlugins, pluginsNum));
    AssertInitialState();
}

TEST_F(OblivionOperationsTest, SetLoadOrder_DuplicatePlugin) {
    ASSERT_EQ(LIBLO_OK, lo_set_game_master(gh, "Blank.esm"));

    // Try setting the load order with a duplicate entry.
    char * dupPlugins[] = {
        "Blank.esm",
        "Blank.esp",
        "Blank.esp"
    };
    size_t pluginsNum = 3;

    ASSERT_EQ(LIBLO_OK, lo_set_game_master(gh, "Blank.esm"));
    EXPECT_EQ(LIBLO_ERROR_INVALID_ARGS, lo_set_load_order(gh, dupPlugins, pluginsNum));
    AssertInitialState();
}

TEST_F(OblivionOperationsTest, SetLoadOrder_WrongGameMaster) {
    char * plugins[] = {
        "Blank.esm"
    };
    size_t pluginsNum = 1;

    // Test trying to set load order with non-Oblivion.esm without
    // first setting the game master.
    EXPECT_EQ(LIBLO_ERROR_INVALID_ARGS, lo_set_load_order(gh, plugins, pluginsNum));
    AssertInitialState();
}

TEST_F(OblivionOperationsTest, SetLoadOrder_BadMasterOrder) {
    ASSERT_EQ(LIBLO_OK, lo_set_game_master(gh, "Blank.esm"));

    char * badMasterOrderPlugins[] = {
        "Blank.esm",
        "Blank.esp",
        "Blank - Different.esm"
    };
    size_t pluginsNum = 3;
    EXPECT_EQ(LIBLO_ERROR_INVALID_ARGS, lo_set_load_order(gh, badMasterOrderPlugins, pluginsNum));
    AssertInitialState();

    char * badMasterOrderPlugins2[] = {
        "Blank.esm",
        "Blank - Different Master Dependent.esm",
        "Blank - Different.esm",
    };
    EXPECT_EQ(LIBLO_ERROR_INVALID_ARGS, lo_set_load_order(gh, badMasterOrderPlugins2, pluginsNum));
    AssertInitialState();
}

TEST_F(OblivionOperationsTest, SetLoadOrder_NullInputs) {
    char * plugins[] = {
        "Blank.esm"
    };
    size_t pluginsNum = 1;

    EXPECT_EQ(LIBLO_ERROR_INVALID_ARGS, lo_set_load_order(NULL, plugins, pluginsNum));
    AssertInitialState();
    EXPECT_EQ(LIBLO_ERROR_INVALID_ARGS, lo_set_load_order(gh, NULL, pluginsNum));
    AssertInitialState();
    EXPECT_EQ(LIBLO_ERROR_INVALID_ARGS, lo_set_load_order(gh, NULL, 0));
    AssertInitialState();
    EXPECT_EQ(LIBLO_ERROR_INVALID_ARGS, lo_set_load_order(gh, plugins, 0));
    AssertInitialState();
}

TEST_F(OblivionOperationsTest, SetLoadOrder_NonPluginFile) {
    char * plugins[] = {
        "Blank.esm",
        "NotAPlugin.esm"
    };
    size_t pluginsNum = 2;

    ASSERT_EQ(LIBLO_OK, lo_set_game_master(gh, "Blank.esm"));
    EXPECT_EQ(LIBLO_ERROR_INVALID_ARGS, lo_set_load_order(gh, plugins, pluginsNum));
    AssertInitialState();
}

TEST_F(OblivionOperationsTest, SetLoadOrder_Valid) {
    ASSERT_EQ(LIBLO_OK, lo_set_game_master(gh, "Blank.esm"));

    char * plugins[] = {
        "Blank.esm"
    };
    size_t pluginsNum = 1;

    EXPECT_EQ(LIBLO_OK, lo_set_load_order(gh, plugins, pluginsNum));
    EXPECT_EQ(0, CheckPluginPosition("Blank.esm"));

    // Now test with more than one plugin.
    char * plugins2[] = {
        "Blank.esm",
        "Blank - Different.esm"
    };
    pluginsNum = 2;
    EXPECT_EQ(LIBLO_OK, lo_set_load_order(gh, plugins2, pluginsNum));
    EXPECT_EQ(0, CheckPluginPosition("Blank.esm"));
    EXPECT_EQ(1, CheckPluginPosition("Blank - Different.esm"));
}

TEST_F(SkyrimOperationsTest, SetLoadOrder_MissingPlugin) {
    char * missingPlugins[] = {
        "Skyrim.esm",
        "Blank.esm",
        "Blank.missing.esp"
    };
    size_t pluginsNum = 3;
    EXPECT_EQ(LIBLO_ERROR_FILE_NOT_FOUND, lo_set_load_order(gh, missingPlugins, pluginsNum));
    AssertInitialState();
}

TEST_F(SkyrimOperationsTest, SetLoadOrder_DuplicatePlugin) {
    // Try setting the load order with a duplicate entry.
    char * dupPlugins[] = {
        "Skyrim.esm",
        "Blank.esp",
        "Blank.esp"
    };
    size_t pluginsNum = 3;
    EXPECT_EQ(LIBLO_ERROR_INVALID_ARGS, lo_set_load_order(gh, dupPlugins, pluginsNum));
    AssertInitialState();
}

TEST_F(SkyrimOperationsTest, SetLoadOrder_NoGameMaster) {
    char * noGameMasterPlugins[] = {
        "Blank.esm",
        "Blank - Different.esm"
    };
    size_t pluginsNum = 2;
    EXPECT_EQ(LIBLO_ERROR_INVALID_ARGS, lo_set_load_order(gh, noGameMasterPlugins, pluginsNum));
    AssertInitialState();
}

TEST_F(SkyrimOperationsTest, SetLoadOrder_BadMasterOrder) {
    char * badMasterOrderPlugins[] = {
        "Skyrim.esm",
        "Blank.esm",
        "Blank.esp",
        "Blank - Different.esm"
    };
    size_t pluginsNum = 4;
    EXPECT_EQ(LIBLO_ERROR_INVALID_ARGS, lo_set_load_order(gh, badMasterOrderPlugins, pluginsNum));
    AssertInitialState();

    char * badMasterOrderPlugins2[] = {
        "Skyrim.esm",
        "Blank - Master Dependent.esm",
        "Blank.esm",
    };
    pluginsNum = 3;
    EXPECT_EQ(LIBLO_ERROR_INVALID_ARGS, lo_set_load_order(gh, badMasterOrderPlugins2, pluginsNum));
    AssertInitialState();
}

TEST_F(SkyrimOperationsTest, SetLoadOrder_NullInputs) {
    char * plugins[] = {
        "Skyrim.esm",
        "Blank.esm",
        "Blank - Different.esm"
    };
    size_t pluginsNum = 3;

    EXPECT_EQ(LIBLO_ERROR_INVALID_ARGS, lo_set_load_order(NULL, plugins, pluginsNum));
    AssertInitialState();
    EXPECT_EQ(LIBLO_ERROR_INVALID_ARGS, lo_set_load_order(gh, NULL, pluginsNum));
    AssertInitialState();
    EXPECT_EQ(LIBLO_ERROR_INVALID_ARGS, lo_set_load_order(gh, NULL, 0));
    AssertInitialState();
}

TEST_F(SkyrimOperationsTest, SetLoadOrder_NonPluginFile) {
    char * plugins[] = {
        "Skyrim.esm",
        "Blank.esm",
        "NotAPlugin.esm"
    };
    size_t pluginsNum = 3;

    EXPECT_EQ(LIBLO_ERROR_INVALID_ARGS, lo_set_load_order(gh, plugins, pluginsNum));
    AssertInitialState();
}

TEST_F(SkyrimOperationsTest, SetLoadOrder_Valid) {
    char * plugins[] = {
        "Skyrim.esm",
        "Blank.esm",
        "Blank - Different.esm"
    };
    size_t pluginsNum = 3;

    EXPECT_EQ(LIBLO_OK, lo_set_load_order(gh, plugins, pluginsNum));
    EXPECT_EQ(1, CheckPluginPosition("Blank.esm"));
    EXPECT_EQ(2, CheckPluginPosition("Blank - Different.esm"));
}

TEST_F(OblivionOperationsTest, GetLoadOrder) {
    char ** plugins = {0};
    size_t pluginsNum;
    EXPECT_EQ(LIBLO_ERROR_INVALID_ARGS, lo_get_load_order(NULL, &plugins, &pluginsNum));
    EXPECT_EQ(LIBLO_ERROR_INVALID_ARGS, lo_get_load_order(gh, NULL, &pluginsNum));
    EXPECT_EQ(LIBLO_ERROR_INVALID_ARGS, lo_get_load_order(gh, &plugins, NULL));
    EXPECT_EQ(LIBLO_ERROR_INVALID_ARGS, lo_get_load_order(gh, NULL, NULL));

    EXPECT_EQ(LIBLO_WARN_INVALID_LIST, lo_get_load_order(gh, &plugins, &pluginsNum));

    ASSERT_EQ(LIBLO_OK, lo_set_game_master(gh, "Blank.esm"));
    EXPECT_EQ(LIBLO_OK, lo_get_load_order(gh, &plugins, &pluginsNum));
}

TEST_F(SkyrimOperationsTest, GetLoadOrder) {
    char ** plugins = {0};
    size_t pluginsNum;
    EXPECT_EQ(LIBLO_ERROR_INVALID_ARGS, lo_get_load_order(NULL, &plugins, &pluginsNum));
    EXPECT_EQ(LIBLO_ERROR_INVALID_ARGS, lo_get_load_order(gh, NULL, &pluginsNum));
    EXPECT_EQ(LIBLO_ERROR_INVALID_ARGS, lo_get_load_order(gh, &plugins, NULL));
    EXPECT_EQ(LIBLO_ERROR_INVALID_ARGS, lo_get_load_order(gh, NULL, NULL));

    EXPECT_EQ(LIBLO_OK, lo_get_load_order(gh, &plugins, &pluginsNum));

    // Test that ghosted plugins get put into loadorder.txt correctly.
    std::vector<std::string> actualLines;
    std::string content;
    ASSERT_TRUE(boost::filesystem::exists(localPath / "loadorder.txt"));
    liblo::ifstream in(localPath / "loadorder.txt");
    while (in.good()) {
        std::string line;
        std::getline(in, line);
        actualLines.push_back(line);
    }
    in.close();

    EXPECT_EQ("Blank - Different.esm", actualLines[2]);
}

#endif
