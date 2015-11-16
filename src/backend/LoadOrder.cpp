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

#include "LoadOrder.h"
#include "libloadorder/constants.h"
#include "error.h"
#include "game.h"
#include "helpers.h"

#include <regex>
#include <set>
#include <unordered_map>

#include <boost/algorithm/string.hpp>

using namespace std;
namespace fs = boost::filesystem;

namespace liblo {
    /////////////////////////
    // LoadOrder Members
    /////////////////////////

    struct PluginSortInfo {
        PluginSortInfo() : isMasterFile(false), modTime(0) {}
        bool isMasterFile;
        time_t modTime;
    };

    struct pluginComparator {
        const _lo_game_handle_int& parentGame;
        std::unordered_map <std::string, PluginSortInfo> pluginCache;

        pluginComparator(const _lo_game_handle_int& game) : parentGame(game) {}

        bool    operator () (const Plugin& plugin1, const Plugin& plugin2) {
            //Return true if plugin1 goes before plugin2, false otherwise.
            //Master files should go before other files.
            //Earlier stamped plugins should go before later stamped plugins.

            auto p1It = pluginCache.find(plugin1.Name());
            auto p2It = pluginCache.find(plugin2.Name());

            // If either of the plugins haven't been cached, cache them now,
            // but defer reading timestamps, since it's not always necessary.
            if (p1It == pluginCache.end()) {
                PluginSortInfo psi;
                psi.isMasterFile = plugin1.IsMasterFileNoThrow(parentGame);
                p1It = pluginCache.insert(std::pair<std::string, PluginSortInfo>(plugin1.Name(), psi)).first;
            }

            if (p2It == pluginCache.end()) {
                PluginSortInfo psi;
                psi.isMasterFile = plugin2.IsMasterFileNoThrow(parentGame);
                p2It = pluginCache.insert(std::pair<std::string, PluginSortInfo>(plugin2.Name(), psi)).first;
            }

            if (p1It->second.isMasterFile && !p2It->second.isMasterFile)
                return true;
            else if (!p1It->second.isMasterFile && p2It->second.isMasterFile)
                return false;
            else {
                // Need to compare timestamps to decide. If either cached
                // timestamp is zero, read and cache the actual timestamp.
                if (p1It->second.modTime == 0) {
                    p1It->second.modTime = plugin1.GetModTime(parentGame);
                }
                if (p2It->second.modTime == 0) {
                    p2It->second.modTime = plugin2.GetModTime(parentGame);
                }

                return (difftime(p1It->second.modTime, p2It->second.modTime) < 0);
            }
        }
    };

    void LoadOrder::Load(const _lo_game_handle_int& parentGame) {
        loadOrder.clear();
        bool createLoTxt = parentGame.LoadOrderMethod() == LIBLO_METHOD_TEXTFILE;
        if (createLoTxt) {
            /*Game uses the new load order system.

            Check if loadorder.txt exists, and read that if it does.
            If it doesn't exist, then read plugins.txt and scan the given directory for mods,
            adding those that weren't in the plugins.txt to the end of the load order, in the order they are read.

            There is no sure-fire way of managing such a situation. If no loadorder.txt, then
            no utilties compatible with that load order method have been installed, so it won't
            break anything apart from the load order not matching the load order in the Bashed
            Patch's Masters list if it exists. That isn't something that can be easily accounted
            for though.
            */
            if (fs::exists(parentGame.LoadOrderFile())) {
                //If the loadorder.txt exists, get the load order from that.
                LoadFromFile(parentGame, parentGame.LoadOrderFile());
                createLoTxt = false;
            }
            else if (fs::exists(parentGame.ActivePluginsFile())) {
                //If the plugins.txt exists, get the active load order from that.
                LoadFromFile(parentGame, parentGame.ActivePluginsFile());
            }
            else if (parentGame.LoadOrderMethod() == LIBLO_METHOD_TEXTFILE) {
                //Make sure that the main master is first.
                loadOrder.insert(begin(loadOrder), Plugin(parentGame.MasterFile()));
                if (parentGame.Id() == LIBLO_GAME_TES5) {
                    //Add Update.esm if not already present.
                    if (Plugin("Update.esm").IsValid(parentGame))
                        loadOrder.push_back(Plugin("Update.esm"));
                }
            }
        }
        unordered_set<Plugin> added = LoadAdditionalFiles(parentGame);
        if (createLoTxt || (!added.empty() && parentGame.LoadOrderMethod() == LIBLO_METHOD_TEXTFILE)) { // we must update loadorder.txt
            _saveActive = false; // we added files, do not mess with plugins.txt
            try {
                Save(const_cast<_lo_game_handle_int&>(parentGame));
                _saveActive = true;
            } catch (error & e) {
                // we failed to update loadorder.txt on addition
                _saveActive = true;
                throw e;
            }
        }
        //Arrange into timestamp order if required.
        if (parentGame.LoadOrderMethod() == LIBLO_METHOD_TIMESTAMP) {
            pluginComparator pc(parentGame);
            sort(begin(loadOrder), end(loadOrder), pc);
        }
    }

    void LoadOrder::Save(_lo_game_handle_int& parentGame) {
        if (parentGame.LoadOrderMethod() == LIBLO_METHOD_TIMESTAMP) {
            //Update timestamps.
            //Want to make a minimum of changes to timestamps, so use the same timestamps as are currently set, but apply them to the plugins in the new order.
            //First we have to read all the timestamps.
            std::set<time_t> timestamps;
            for (const auto &plugin : loadOrder) {
                timestamps.insert(plugin.GetModTime(parentGame));
            }
            // It may be that two plugins currently share the same timestamp,
            // which will result in fewer timestamps in the set than there are
            // plugins, so pad the set if necessary.
            while (timestamps.size() < loadOrder.size()) {
                timestamps.insert(*timestamps.crbegin() + 60);
            }
            size_t i = 0;
            for (const auto &timestamp : timestamps) {
                loadOrder.at(i).SetModTime(parentGame, timestamp);
                ++i;
            }
        }
        else {
            //Need to write both loadorder.txt and plugins.txt.
            try {
                if (!fs::exists(parentGame.LoadOrderFile().parent_path()))
                    fs::create_directory(parentGame.LoadOrderFile().parent_path());
                fs::ofstream outfile(parentGame.LoadOrderFile(), ios_base::trunc);
                outfile.exceptions(std::ios_base::badbit);

                for (const auto &plugin : loadOrder)
                    outfile << plugin.Name() << endl;
                outfile.close();

                //Now record new loadorder.txt mtime.
                //Plugins.txt doesn't need its mtime updated as only the order of its contents has changed, and it is stored in memory as an unordered set.
                mtime = fs::last_write_time(parentGame.LoadOrderFile());
                mtime_data_dir = fs::last_write_time(parentGame.PluginsFolder());
            }
            catch (std::ios_base::failure& e) {
                throw error(LIBLO_ERROR_FILE_WRITE_FAIL, "\"" + parentGame.LoadOrderFile().string() + "\" cannot be written to. Details: " + e.what());
            }
            if (!_saveActive) return;
            //Now write plugins.txt. Update cache if necessary.
            if (parentGame.activePlugins.HasChanged(parentGame))
                parentGame.activePlugins.Load(parentGame);
            parentGame.activePlugins.Save(parentGame);
        }
    }

    std::vector<std::string> LoadOrder::getLoadOrder() const {
        std::vector<std::string> pluginNames;
        transform(begin(loadOrder),
                  end(loadOrder),
                  back_inserter(pluginNames),
                  [](const Plugin& plugin) {
            return plugin.Name();
        });
        return pluginNames;
    }

    size_t LoadOrder::getPosition(const std::string& pluginName) const {
        return distance(begin(loadOrder),
                        find(begin(loadOrder),
                        end(loadOrder),
                        pluginName));
    }

    std::string LoadOrder::getPluginAtPosition(size_t index) const {
        return loadOrder.at(index).Name();
    }

    void LoadOrder::setLoadOrder(const std::vector<std::string>& pluginNames, const _lo_game_handle_int& gameHandle) {
        // For textfile-based load order games, check that the game's master file loads first.
        if (gameHandle.LoadOrderMethod() == LIBLO_METHOD_TEXTFILE && (pluginNames.empty() || !boost::iequals(pluginNames[0], gameHandle.MasterFile())))
            throw error(LIBLO_ERROR_INVALID_ARGS, "\"" + gameHandle.MasterFile() + "\" must load first.");

        // Create vector of Plugin objects, reusing existing objects
        // where possible. Also check for duplicate entries, that new
        // plugins are valid,
        vector<Plugin> plugins;
        unordered_set<string> hashset;
        for_each(begin(pluginNames), end(pluginNames), [&](const std::string& pluginName) {
            if (hashset.find(boost::to_lower_copy(pluginName)) != hashset.end())
                throw error(LIBLO_ERROR_INVALID_ARGS, "\"" + pluginName + "\" is a duplicate entry.");

            hashset.insert(boost::to_lower_copy(pluginName));

            auto it = find(begin(loadOrder), end(loadOrder), pluginName);
            if (it != end(loadOrder))
                plugins.push_back(*it);
            else {
                plugins.push_back(Plugin(pluginName));
                if (!plugins.back().IsValid(gameHandle))
                    throw error(LIBLO_ERROR_INVALID_ARGS, "\"" + pluginName + "\" is not a valid plugin file.");
            }
        });

        // Check that all masters load before non-masters.
        if (!is_partitioned(begin(plugins),
            end(plugins),
            [&](const Plugin& plugin) {
            return plugin.IsMasterFile(gameHandle);
        })) {
            throw error(LIBLO_ERROR_INVALID_ARGS, "Master plugins must load before all non-master plugins.");
        }

        // Swap load order for the new one.
        loadOrder.swap(plugins);

        if (gameHandle.LoadOrderMethod() == LIBLO_METHOD_TEXTFILE) {
            // Make sure that game master is active.
            loadOrder.front().activate();
        }
    }

   void LoadOrder::setPosition(const std::string& pluginName, size_t loadOrderIndex, const _lo_game_handle_int& gameHandle) {
        // For textfile-based load order games, check that this doesn't move the game master file from the beginning of the load order.
        if (gameHandle.LoadOrderMethod() == LIBLO_METHOD_TEXTFILE) {
            if (loadOrderIndex == 0 && !boost::iequals(pluginName, gameHandle.MasterFile()))
                throw error(LIBLO_ERROR_INVALID_ARGS, "Cannot set \"" + pluginName + "\" to load first: \"" + gameHandle.MasterFile() + "\" most load first.");
            else if (loadOrderIndex != 0 && !loadOrder.empty() && boost::iequals(pluginName, gameHandle.MasterFile()))
                throw error(LIBLO_ERROR_INVALID_ARGS, "\"" + pluginName + "\" must load first.");
        }

        // If the plugin is already in the load order, use its existing
        // object.
        Plugin plugin;
        auto it = find(begin(loadOrder), end(loadOrder), pluginName);
        if (it != end(loadOrder))
            plugin = *it;
        else {
            plugin = Plugin(pluginName);
            // Check that the plugin is valid.
            if (!plugin.IsValid(gameHandle))
                throw error(LIBLO_ERROR_INVALID_ARGS, "\"" + pluginName + "\" is not a valid plugin file.");
        }

        // Check that a master isn't being moved before a non-master or the inverse.
        size_t masterPartitionPoint(getMasterPartitionPoint(gameHandle));
        if (!plugin.IsMasterFile(gameHandle) && loadOrderIndex < masterPartitionPoint)
            throw error(LIBLO_ERROR_INVALID_ARGS, "Cannot move a non-master plugin before master files.");
        else if (plugin.IsMasterFile(gameHandle)
                 && ((loadOrderIndex > masterPartitionPoint && masterPartitionPoint != loadOrder.size())
                 || (getPosition(pluginName) < masterPartitionPoint && loadOrderIndex == masterPartitionPoint)))
                 throw error(LIBLO_ERROR_INVALID_ARGS, "Cannot move a master file after non-master plugins.");

        // Erase any existing entry for the plugin.
        loadOrder.erase(remove(begin(loadOrder), end(loadOrder), pluginName), end(loadOrder));

        // If the index is larger than the load order size, set it equal to the size.
        if (loadOrderIndex > loadOrder.size())
            loadOrderIndex = loadOrder.size();

        loadOrder.insert(next(begin(loadOrder), loadOrderIndex), plugin);
    }

    bool LoadOrder::isActive(const std::string& pluginName) const {
        return find_if(begin(loadOrder), end(loadOrder), [&](const Plugin& plugin) {
            return plugin == pluginName && plugin.isActive();
        }) != end(loadOrder);
    }

    void LoadOrder::activate(const std::string& pluginName, const _lo_game_handle_int& gameHandle) {
        if (countActivePlugins() > 254)
            throw error(LIBLO_ERROR_INVALID_ARGS, "Cannot activate " + pluginName + " as this would mean more than 255 plugins are active.");

        if (!Plugin(pluginName).IsValid(gameHandle))
            throw error(LIBLO_ERROR_INVALID_ARGS, "\"" + pluginName + "\" is not a valid plugin file.");

        auto it = find(begin(loadOrder), end(loadOrder), pluginName);
        if (it == end(loadOrder)) {
            if (gameHandle.LoadOrderMethod() == LIBLO_METHOD_TEXTFILE && boost::iequals(pluginName, gameHandle.MasterFile()))
                it = loadOrder.insert(begin(loadOrder), Plugin(pluginName));
            else if (Plugin(pluginName).IsMasterFile(gameHandle))
                it = loadOrder.insert(next(begin(loadOrder), getMasterPartitionPoint(gameHandle)), Plugin(pluginName));
            else {
                loadOrder.push_back(Plugin(pluginName));
                it = prev(loadOrder.end());
            }
        }
        it->activate();
    }

    void LoadOrder::deactivate(const std::string& pluginName, const _lo_game_handle_int& gameHandle) {
        if (gameHandle.LoadOrderMethod() == LIBLO_METHOD_TEXTFILE && boost::iequals(pluginName, gameHandle.MasterFile()))
            throw error(LIBLO_ERROR_INVALID_ARGS, "Cannot deactivate " + gameHandle.MasterFile() + ".");
        else if (gameHandle.Id() == LIBLO_GAME_TES5 && boost::iequals(pluginName, "Update.esm"))
            throw error(LIBLO_ERROR_INVALID_ARGS, "Cannot deactivate Update.esm.");

        auto it = find(begin(loadOrder), end(loadOrder), pluginName);
        if (it != end(loadOrder))
            it->deactivate();
    }

   void LoadOrder::CheckValidity(const _lo_game_handle_int& parentGame, bool _skip) {
       if (loadOrder.empty())
            return;
        std::string msg = "";
        Plugin masterEsm = Plugin(parentGame.MasterFile());
        if (loadOrder.at(0) != masterEsm)
            msg += "\"" + masterEsm.Name() + "\" is not the first plugin in the load order. " +
                loadOrder.at(0).Name() + " is first.\n";
        if (parentGame.LoadOrderMethod() != LIBLO_METHOD_TIMESTAMP || !_skip) { // we just loaded, performing all operations below on loading
            bool wasMaster = false;
            bool wasMasterSet = false;
            unordered_set<Plugin> hashset; // check for duplicates
            for (const auto plugin : loadOrder) {
                if (hashset.find(plugin) != hashset.end()) {
                    msg += "\"" + plugin.Name() + "\" is in the load order twice.\n";
                    if (plugin.exists()) wasMaster = plugin.esm();
                    continue;
                }
                else hashset.insert(plugin);
                if (!plugin.Exists(parentGame)){
                    msg += "\"" + plugin.Name() + "\" is not installed.\n";
                    continue;
                }
                // plugin exists
                bool isMaster = wasMaster;
                try {
                    isMaster = plugin.IsMasterFile(parentGame);
                    if (wasMasterSet && isMaster && !wasMaster)
                        msg += "Master plugin \"" + plugin.Name() + "\" loaded after a non-master plugin.\n";
                    wasMaster = isMaster; wasMasterSet = true;
                }
                catch (std::exception& e) {
                    msg += "Plugin \"" + plugin.Name() + "\" is invalid - details: " + e.what() + "\n";
                }
            }
        }
        if (msg != "") throw error(LIBLO_WARN_INVALID_LIST, msg);
    }

    bool LoadOrder::HasChanged(const _lo_game_handle_int& parentGame) const {
        if (loadOrder.empty())
            return true;

        try {
            if (parentGame.LoadOrderMethod() == LIBLO_METHOD_TEXTFILE &&
                fs::exists(parentGame.LoadOrderFile())) {
                //Load order is stored in parentGame.LoadOrderFile(),
                // but load order must also be reloaded if parentGame.PluginsFolder()
                // has been altered. - (ut) checking Data/ mod time would test additions/removals only
                // Kept it but we should add a force paramneter anyway
                time_t mtext = fs::last_write_time(parentGame.LoadOrderFile());
                time_t mdata = fs::last_write_time(parentGame.PluginsFolder());
                return (mtext != mtime) || (mdata != mtime_data_dir);
            }
            else
                //Checking parent folder modification time doesn't work consistently, and to check if
                // the load order has changed would probably take as long as just assuming it's changed.
                return true;
        }
        catch (fs::filesystem_error& e) {
            throw error(LIBLO_ERROR_TIMESTAMP_READ_FAIL, e.what());
        }
    }

    void LoadOrder::clear() {
        loadOrder.clear();
    }

    void LoadOrder::unique() {
        // Look for duplicate entries, removing all but the last. The reverse
        // iterators make the algorithm move everything towards the end of the
        // collection instead of towards the beginning.
        unordered_set<string> hashset;
        auto it = remove_if(rbegin(loadOrder), rend(loadOrder), [&](const Plugin& plugin) {
            bool isNotUnique = hashset.find(boost::to_lower_copy(plugin.Name())) != hashset.end();
            hashset.insert(boost::to_lower_copy(plugin.Name()));
            return isNotUnique;
        });

        loadOrder.erase(begin(loadOrder), it.base());
    }

    void LoadOrder::partitionMasters(const _lo_game_handle_int& gameHandle) {
        stable_partition(begin(loadOrder),
                         end(loadOrder),
                         [&](const Plugin& plugin) {
            return plugin.IsMasterFileNoThrow(gameHandle);
        });
    }

    void LoadOrder::LoadFromFile(const _lo_game_handle_int& parentGame, const fs::path& file) {
        if (!fs::exists(file))
            throw error(LIBLO_ERROR_FILE_NOT_FOUND, file.string() + " cannot be found.");

        //loadorder.txt is simple enough that we can avoid needing a formal parser.
        //It's just a text file with a plugin filename on each line. Skip lines which are blank or start with '#'.
        try {
            fs::ifstream in(file);
            in.exceptions(std::ios_base::badbit);

            string line;
            regex reg("GameFile[0-9]{1,3}=.+\\.es(m|p)", regex::ECMAScript | regex::icase);
            bool transcode = (file == parentGame.ActivePluginsFile());
            while (getline(in, line)) {
                // Check if it's a valid plugin line. The stream doesn't filter out '\r' line endings, hence the check.
                if (line.empty() || line[0] == '#' || line[0] == '\r')
                    continue;

                if (parentGame.Id() == LIBLO_GAME_TES3) {
                    //Morrowind's active file list is stored in Morrowind.ini, and that has a different format from plugins.txt.
                    if (regex_match(line, reg))
                        line = line.substr(line.find('=') + 1);
                    else
                        continue;
                }

                if (transcode)
                    line = ToUTF8(line);
                else {
                    //Test that the string is UTF-8 encoded by trying to convert it to UTF-16. It should throw if an invalid byte is found.
                    try {
                        boost::locale::conv::utf_to_utf<wchar_t>(line, boost::locale::conv::stop);
                    }
                    catch (...) {
                        throw error(LIBLO_ERROR_FILE_NOT_UTF8, "\"" + file.string() + "\" is not encoded in valid UTF-8.");
                    }
                }

                Plugin plugin(line);
                if (plugin.IsValid(parentGame)) // FIXME(ut): this must go
                    loadOrder.push_back(plugin);
            }
            in.close();
        }
        catch (std::ios_base::failure& e) {
            throw error(LIBLO_ERROR_FILE_READ_FAIL, "\"" + file.string() + "\" could not be read. Details: " + e.what());
        }

        if (parentGame.LoadOrderMethod() == LIBLO_METHOD_TEXTFILE) {
            // Make sure that game master is first and active.
            setPosition(parentGame.MasterFile(), 0, parentGame);
            loadOrder.front().activate();

            if (parentGame.Id() == LIBLO_GAME_TES5) {
                //Add Update.esm if not already present.
                if (Plugin("Update.esm").IsValid(parentGame) && count(begin(loadOrder), end(loadOrder), Plugin("Update.esm")) == 0)
                    loadOrder.insert(next(begin(loadOrder), getMasterPartitionPoint(parentGame)), Plugin("Update.esm"));
            }
        }
    }

    unordered_set<Plugin> LoadOrder::LoadAdditionalFiles(const _lo_game_handle_int& parentGame) {
        unordered_set<Plugin> added;
        if (fs::is_directory(parentGame.PluginsFolder())) {
            //Now scan through Data folder. Add any plugins that aren't already in loadorder
            //to loadorder, at the end. // FIXME: TIMESTAMPS METHOD !WHY AT THE END ?
            std::vector<std::string> accumulator; // filter plugins
            for (fs::directory_iterator itr(parentGame.PluginsFolder()); itr != fs::directory_iterator(); ++itr) {
                string name = itr->path().filename().string();
                if ((boost::iends_with(name, ".esm") || boost::iends_with(name, ".esp")
                    || boost::iends_with(name, ".ghost")) && fs::is_regular_file(itr->status())) {
                    accumulator.push_back(name);
                }
            }
            // sort ghosts after regular files
            std::sort(accumulator.begin(), accumulator.end());
            auto firstNonMaster = getMasterPartitionPoint(parentGame); // again parses the files !
            for (string s : accumulator) {
                const Plugin plugin(s);
                std::string name = plugin.Name(); // lops ghost off
                if (count(begin(loadOrder), end(loadOrder), plugin) != 0) continue; // for ghosts and textfile method
                bool isMaster = false;
                try {
                    isMaster = plugin.IsMasterFile(parentGame); // throws on "invalid" plugin
                    //If it is a master, add it after the last master, otherwise add it at the end.
                    if (isMaster) {
                        loadOrder.insert(next(begin(loadOrder), firstNonMaster), plugin);
                        ++firstNonMaster;
                    }
                    else {
                        loadOrder.push_back(plugin);
                    }
                    added.insert(plugin);
                }
                catch (std::exception& /*e*/) {
                    // LOG ! msg += "Plugin \"" + plugin.Name() + "\" is invalid - details: " + e.what() + "\n";
                }
            }
        }
        return added;
    }

    size_t LoadOrder::getMasterPartitionPoint(const _lo_game_handle_int& gameHandle) const {
        return distance(begin(loadOrder),
                        partition_point(begin(loadOrder),
                        end(loadOrder),
                        [&](const Plugin& plugin) {
            return plugin.IsMasterFile(gameHandle);
        }));
    }

    size_t LoadOrder::countActivePlugins() const {
        return count_if(begin(loadOrder), end(loadOrder), [&](const Plugin& plugin) {
            return plugin.isActive();
        });
    }

    ///////////////////////////
    // ActivePlugins Members
    ///////////////////////////

    void ActivePlugins::Load(const _lo_game_handle_int& parentGame) {
        clear();
        if (fs::exists(parentGame.ActivePluginsFile())) {
            string line;
            try {
                fs::ifstream in(parentGame.ActivePluginsFile());
                in.exceptions(std::ios_base::badbit);

                if (!(parentGame.Id() == LIBLO_GAME_TES3)) {
                    while (getline(in, line)) {
                        // Check if it's a valid plugin line. The stream doesn't filter out '\r' line endings, hence the check.
                        if (line.empty() || line[0] == '#' || line[0] == '\r')
                            continue;
                        Plugin plug = Plugin(ToUTF8(line));
                        activeOrdered.push_back(plug);
                        insert(plug);
                    }
                } else {   //Morrowind's active file list is stored in Morrowind.ini, and that has a different format from plugins.txt.
                    regex reg = regex("GameFile[0-9]{1,3}=.+\\.es(m|p)", regex::ECMAScript | regex::icase);
                    while (getline(in, line)) {
                        if (line.empty() || !regex_match(line, reg))
                            continue;
                        //Now cut off everything up to and including the = sign.
                        Plugin plug = Plugin(ToUTF8(line.substr(line.find('=') + 1)));
                        activeOrdered.push_back(plug);
                        insert(plug);
                    }
                }
                in.close();
            }
            catch (std::ios_base::failure& e) {
                throw error(LIBLO_ERROR_FILE_READ_FAIL, "\"" + parentGame.ActivePluginsFile().string() + "\" could not be read. Details: " + e.what());
            }
        }
        // Add skyrim.esm, update.esm if missing. Note that we do not check if loaded list is valid,
        // nevertheless we do stive to keep the list valid if originally was - TODO: check at this point
        // and rewrite plugins txt - this requires passing a valid load order in - liblo 8
        if (parentGame.LoadOrderMethod() == LIBLO_METHOD_TEXTFILE) {
            // Do the game's main master file first
            Plugin plug = Plugin(parentGame.MasterFile());
            if (find(plug) == end()){
                insert(plug);
                activeOrdered.insert(activeOrdered.begin(), plug); // insert first
            }
            if (parentGame.Id() == LIBLO_GAME_TES5) {
                // Do Update.esm for Skyrim
                plug = Plugin("Update.esm");
                if (plug.IsValid(parentGame) && find(plug) == end()) { // FIXME: must resave plugins.txt
                    insert(plug);
                    auto firstEsp = find_if(activeOrdered.begin(), activeOrdered.end(),
                        [&parentGame](const Plugin& plugin) { return !plugin.IsMasterFileNoThrow(parentGame);  });
                        activeOrdered.insert(firstEsp, plug); // insert at last esm position
                }
            }
        }
    }

    void ActivePlugins::Save(const _lo_game_handle_int& parentGame) {
        string settings, badFilename;

        if (parentGame.Id() == LIBLO_GAME_TES3) {  //Must be the plugins file, since loadorder.txt isn't used for MW.
            string contents;
            //If Morrowind, write active plugin list to Morrowind.ini, which also holds a lot of other game settings.
            //libloadorder needs to read everything up to the active plugin list in the current ini and stick that on before the first saved plugin name.
            if (fs::exists(parentGame.ActivePluginsFile())) {
                fileToBuffer(parentGame.ActivePluginsFile(), contents);
                size_t pos = contents.find("[Game Files]");
                if (pos != string::npos)
                    settings = contents.substr(0, pos + 12); //+12 is for the characters in "[Game Files]".
            }
        }

        try {
            if (!fs::exists(parentGame.ActivePluginsFile().parent_path()))
                fs::create_directory(parentGame.ActivePluginsFile().parent_path());
            fs::ofstream outfile(parentGame.ActivePluginsFile(), ios_base::trunc);
            outfile.exceptions(std::ios_base::badbit);

            if (!settings.empty())
                outfile << settings << endl;  //Get those Morrowind settings back in.

            if (parentGame.LoadOrderMethod() == LIBLO_METHOD_TIMESTAMP) {
                //Can write the active plugins in any order.
                size_t i = 0;
                for (const auto &plugin : *this) {
                    if (parentGame.Id() == LIBLO_GAME_TES3) //Need to write "GameFileN=" before plugin name, where N is an integer from 0 up.
                        outfile << "GameFile" << i << "=";

                    try {
                        outfile << FromUTF8(plugin.Name()) << endl;
                    }
                    catch (error& e) {
                        badFilename = e.what();
                    }
                    i++;
                }
            }
            else {
                //Need to write the active plugins in load order.
                for (const auto &plugin : parentGame.loadOrder.getLoadOrder()) {
                    if (find(plugin) == end() || (parentGame.LoadOrderMethod() == LIBLO_METHOD_TEXTFILE && plugin == parentGame.MasterFile()))
                        continue;

                    try {
                        outfile << FromUTF8(plugin) << endl;
                    }
                    catch (error& e) {
                        badFilename = e.what();
                    }
                }
            }
            outfile.close();
        }
        catch (std::ios_base::failure& e) {
            throw error(LIBLO_ERROR_FILE_WRITE_FAIL, "\"" + parentGame.ActivePluginsFile().string() + "\" could not be written. Details: " + e.what());
        }

        if (!badFilename.empty())
            throw error(LIBLO_WARN_BAD_FILENAME, badFilename);
    }

    void ActivePlugins::CheckValidity(const _lo_game_handle_int& parentGame) const {
        std::string msg = "";
        // FIXME tests below most often duplicate the ones in the Load order
        for (const auto& plugin : *this) {
            if (!plugin.Exists(parentGame))
                msg += "\"" + plugin.Name() + "\" is not installed.\n";
            else if (!plugin.IsValid(parentGame))
                msg += "\"" + plugin.Name() + "\" is not a valid plugin file.\n";
        }

        if (size() > 255)
            msg += "More than 255 plugins are active.\n";
        else if (parentGame.LoadOrderMethod() == LIBLO_METHOD_TEXTFILE) {
            if (find(Plugin(parentGame.MasterFile())) == end())
                msg += parentGame.MasterFile() + " isn't active.\n";
            else if (parentGame.Id() == LIBLO_GAME_TES5 && Plugin("Update.esm").Exists(parentGame) && find(Plugin("Update.esm")) == end())
                msg += "Update.esm is installed but isn't active.\n";
        }
        if (msg != "") throw error(LIBLO_WARN_INVALID_LIST, msg);
    }

    bool ActivePlugins::HasChanged(const _lo_game_handle_int& parentGame) const {
        if (empty())
            return true;

        try {
            return (fs::exists(parentGame.ActivePluginsFile())) &&
                   (fs::last_write_time(parentGame.ActivePluginsFile()) != mtime);
        }
        catch (fs::filesystem_error& e) {
            throw error(LIBLO_ERROR_TIMESTAMP_READ_FAIL, e.what());
        }
    }

    std::vector<Plugin> &ActivePlugins::Ordered() { return activeOrdered; }
    void ActivePlugins::clear() { std::unordered_set<Plugin>::clear(); activeOrdered.clear(); }
}
