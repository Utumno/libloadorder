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

#include "libloadorder/constants.h"
#include "error.h"
#include "plugins.h"
#include "game.h"
#include "helpers.h"
#include "streams.h"
#include <boost/filesystem.hpp>
#include <boost/locale.hpp>
#include <regex>
#include <set>
#include <unordered_map>

using namespace std;
namespace fs = boost::filesystem;

namespace liblo {
    //////////////////////
    // Plugin Members
    //////////////////////

    Plugin::Plugin() : name("") {}

    Plugin::Plugin(const string& filename) : name(filename) {
        if (!name.empty() && name[name.length() - 1] == '\r')
            name = name.substr(0, name.length() - 1);
        if (boost::iends_with(name, ".ghost"))
            name = fs::path(name).stem().string();
    };

    string Plugin::Name() const {
        return name;
    }

    bool Plugin::IsValid(const _lo_game_handle_int& parentGame) const {
        // Rather than just checking the extension, try also parsing the file, and see if it fails.
        if (!boost::iends_with(name, ".esm") && !boost::iends_with(name, ".esp"))
            return false;
        try {
            espm::File * file = ReadHeader(parentGame);
            delete file;
        }
        catch (std::exception& /*e*/) {
            return false;
        }
        return true;
    }

    bool Plugin::IsMasterFile(const _lo_game_handle_int& parentGame) const {
        // TODO(ut): cache ! Plugins.mtimes map<Plugin.name, pair<mod_time, Plugin>> and Plugin.isEsm
        if (!boost::iends_with(name, ".esm") && !boost::iends_with(name, ".esp"))
            throw std::invalid_argument("Invalid file extension: " + name);
        espm::File * file = nullptr;
        try {
            file = ReadHeader(parentGame);
            bool ret = file->isMaster(parentGame.espm_settings);
            isEsm = ret;
            delete file;
            return ret;
        }
        catch (std::exception& e) {
            // TODO(ut): log it !
            if (file != nullptr) delete file;
            throw e;
        }
    }

    bool Plugin::IsMasterFileNoThrow(const _lo_game_handle_int& parentGame) const {
        if (!boost::iends_with(name, ".esm") && !boost::iends_with(name, ".esp"))
            return false;
        espm::File * file = nullptr;
        try {
            file = ReadHeader(parentGame);
            bool ret = file->isMaster(parentGame.espm_settings);
            isEsm = ret;
            delete file;
            return ret;
        }
        catch (std::exception& /*e*/) {
            // TODO(ut): log it !
            if (file != nullptr) delete file;
            return false;
        }
    }

    bool Plugin::IsGhosted(const _lo_game_handle_int& parentGame) const {
        bool isGhost = !fs::exists(parentGame.PluginsFolder() / name) &&
                        fs::exists(parentGame.PluginsFolder() / fs::path(name + ".ghost"));
        return isGhost;
    }

    bool Plugin::Exists(const _lo_game_handle_int& parentGame) const {
        exist = fs::exists(parentGame.PluginsFolder() / name) ||
                fs::exists(parentGame.PluginsFolder() / fs::path(name + ".ghost"));
        return exist;
    }

    time_t Plugin::GetModTime(const _lo_game_handle_int& parentGame) const {
        try {
            if (IsGhosted(parentGame))
                return fs::last_write_time(parentGame.PluginsFolder() / fs::path(name + ".ghost"));
            else
                return fs::last_write_time(parentGame.PluginsFolder() / name);
        }
        catch (fs::filesystem_error& e) {
            throw error(LIBLO_ERROR_TIMESTAMP_READ_FAIL, e.what());
        }
    }

    std::vector<Plugin> Plugin::GetMasters(const _lo_game_handle_int& parentGame) const {
        espm::File * file = ReadHeader(parentGame);

        vector<Plugin> masters;
        for (const auto &master : file->getMasters()) {
            masters.push_back(Plugin(master));
        }

        delete file;
        return masters;
    }

    void Plugin::UnGhost(const _lo_game_handle_int& parentGame) const {
        if (IsGhosted(parentGame)) {
            try {
                fs::rename(parentGame.PluginsFolder() / fs::path(name + ".ghost"), parentGame.PluginsFolder() / name);
            }
            catch (fs::filesystem_error& e) {
                throw error(LIBLO_ERROR_FILE_RENAME_FAIL, e.what());
            }
        }
    }

    void Plugin::SetModTime(const _lo_game_handle_int& parentGame, const time_t modificationTime) const {
        try {
            if (IsGhosted(parentGame))
                fs::last_write_time(parentGame.PluginsFolder() / fs::path(name + ".ghost"), modificationTime);
            else
                fs::last_write_time(parentGame.PluginsFolder() / name, modificationTime);
        }
        catch (fs::filesystem_error& e) {
            throw error(LIBLO_ERROR_TIMESTAMP_WRITE_FAIL, e.what());
        }
    }

    bool Plugin::operator == (const Plugin& rhs) const {
        return boost::iequals(name, rhs.Name());
    }

    bool Plugin::operator != (const Plugin& rhs) const {
        return !(*this == rhs);
    }

    espm::File * Plugin::ReadHeader(const _lo_game_handle_int& parentGame) const {
        try {
            string filepath = (parentGame.PluginsFolder() / name).string();
            if (IsGhosted(parentGame))
                filepath += ".ghost";

            espm::File * file = nullptr;
            if (parentGame.Id() == LIBLO_GAME_TES3)
                file = new espm::tes3::File(filepath, parentGame.espm_settings, false, true);
            else if (parentGame.Id() == LIBLO_GAME_TES4)
                file = new espm::tes4::File(filepath, parentGame.espm_settings, false, true);
            else if (parentGame.Id() == LIBLO_GAME_TES5)
                file = new espm::tes5::File(filepath, parentGame.espm_settings, false, true);
            else if (parentGame.Id() == LIBLO_GAME_FO3)
                file = new espm::fo3::File(filepath, parentGame.espm_settings, false, true);
            else
                file = new espm::fonv::File(filepath, parentGame.espm_settings, false, true);

            return file;
        }
        catch (std::exception& e) {
            if (!Exists(parentGame))
                throw error(LIBLO_ERROR_FILE_NOT_FOUND, name.c_str());
            throw error(LIBLO_ERROR_FILE_READ_FAIL, name + " : " + e.what());
        }
    }

    bool Plugin::esm() const { return isEsm; }
    bool Plugin::exists() const { return exist; }

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
        clear();
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
            else if (parentGame.Id() == LIBLO_GAME_TES5) {
                //Make sure that Skyrim.esm is first.
                Move(Plugin(parentGame.MasterFile()), this->begin());
                //Add Update.esm if not already present.
                if (Plugin("Update.esm").Exists(parentGame) && Find(Plugin("Update.esm")) == this->cend())
                    Move(Plugin("Update.esm"), FindFirstNonMaster(parentGame));
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
            sort(begin(), end(), pc);
        }
    }

    void LoadOrder::Save(_lo_game_handle_int& parentGame) {
        if (parentGame.LoadOrderMethod() == LIBLO_METHOD_TIMESTAMP) {
            //Update timestamps.
            //Want to make a minimum of changes to timestamps, so use the same timestamps as are currently set, but apply them to the plugins in the new order.
            //First we have to read all the timestamps.
            std::set<time_t> timestamps;
            for (const auto &plugin : *this) {
                timestamps.insert(plugin.GetModTime(parentGame));
            }
            // It may be that two plugins currently share the same timestamp,
            // which will result in fewer timestamps in the set than there are
            // plugins, so pad the set if necessary.
            while (timestamps.size() < size()) {
                timestamps.insert(*timestamps.crbegin() + 60);
            }
            size_t i = 0;
            for (const auto &timestamp : timestamps) {
                at(i).SetModTime(parentGame, timestamp);
                ++i;
            }
        }
        else {
            //Need to write both loadorder.txt and plugins.txt.
            try {
                if (!fs::exists(parentGame.LoadOrderFile().parent_path()))
                    fs::create_directory(parentGame.LoadOrderFile().parent_path());
                liblo::ofstream outfile(parentGame.LoadOrderFile(), ios_base::trunc);
                outfile.exceptions(std::ios_base::badbit);

                for (const auto &plugin : *this)
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

    void LoadOrder::CheckValidity(const _lo_game_handle_int& parentGame, bool _skip) {
        if (empty())
            return;

        std::string msg = "";

        Plugin masterEsm = Plugin(parentGame.MasterFile());
        if (at(0) != masterEsm)
            msg += "\"" + masterEsm.Name() + "\" is not the first plugin in the load order. " +
                    at(0).Name() + " is first.\n";
        if (parentGame.LoadOrderMethod() != LIBLO_METHOD_TIMESTAMP || !_skip) { // we just loaded, performing all operations below on loading
            bool wasMaster = false;
            bool wasMasterSet = false;
            unordered_set<Plugin> hashset; // check for duplicates
            for (const auto plugin : *this) {
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
        if (empty())
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

    std::vector<Plugin>::iterator LoadOrder::Move(const Plugin& plugin, std::vector<Plugin>::iterator newPos) {
        if (distance(this->begin(), newPos) > this->size())
            throw error(LIBLO_ERROR_INVALID_ARGS, "New plugin position is beyond end of container.");

        if (newPos != this->end() && *newPos == plugin)
            return newPos;  // No movement necessary.

        // Inserting and erasing iterators invalidates later iterators, so first insert into
        // the vector.
        bool moveToEnd = (newPos == this->end());

        newPos = this->insert(newPos, plugin);

        auto it = this->begin();
        while (it != this->end()) {
            if (it != newPos
                && (!moveToEnd || it != --this->end())
                && *it == plugin)
                it = this->erase(it);
            else
                ++it;
        }

        return newPos;
    }

    std::vector<Plugin>::iterator LoadOrder::Find(const Plugin& plugin) {
        return find(this->begin(), this->end(), plugin);
    }

    std::vector<Plugin>::iterator LoadOrder::FindFirstNonMaster(const _lo_game_handle_int& parentGame) {
        return find_if(this->begin(), this->end(), [&parentGame](const Plugin& plugin) {
            return !plugin.IsMasterFileNoThrow(parentGame);
        });
    }

    void LoadOrder::LoadFromFile(const _lo_game_handle_int& parentGame, const fs::path& file) {
        if (!fs::exists(file))
            throw error(LIBLO_ERROR_FILE_NOT_FOUND, file.string() + " cannot be found.");

        //loadorder.txt is simple enough that we can avoid needing a formal parser.
        //It's just a text file with a plugin filename on each line. Skip lines which are blank or start with '#'.
        try {
            liblo::ifstream in(file);
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

                this->push_back(Plugin(line));
            }
            in.close();
        }
        catch (std::ios_base::failure& e) {
            throw error(LIBLO_ERROR_FILE_READ_FAIL, "\"" + file.string() + "\" could not be read. Details: " + e.what());
        }

        if (parentGame.Id() == LIBLO_GAME_TES5 && file == parentGame.ActivePluginsFile()) {
            //Make sure that Skyrim.esm is first.
            Move(Plugin(parentGame.MasterFile()), this->begin());
            //Add Update.esm if not already present.
            if (Plugin("Update.esm").Exists(parentGame) && Find(Plugin("Update.esm")) == this->cend())
                Move(Plugin("Update.esm"), FindFirstNonMaster(parentGame));
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
            auto firstNonMaster = FindFirstNonMaster(parentGame); // again parses the files !
            for (string s : accumulator) {
                const Plugin plugin(s);
                std::string name = plugin.Name(); // lops ghost off
                if (!(Find(plugin) == this->cend())) continue; // for ghosts and textfile method
                bool isMaster = false;
                try {
                    isMaster = plugin.IsMasterFile(parentGame); // throws on "invalid" plugin
                    //If it is a master, add it after the last master, otherwise add it at the end.
                    if (isMaster) {
                        firstNonMaster = ++insert(firstNonMaster, plugin);
                    }
                    else {
                        // push_back may invalidate all current iterators, so reassign firstNonMaster in case.
                        size_t firstNonMasterPos = distance(this->begin(), firstNonMaster);
                        this->push_back(plugin);
                        firstNonMaster = this->begin() + firstNonMasterPos + 1;
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

    ///////////////////////////
    // ActivePlugins Members
    ///////////////////////////

    void ActivePlugins::Load(const _lo_game_handle_int& parentGame) {
        clear();
        if (fs::exists(parentGame.ActivePluginsFile())) {
            string line;
            try {
                liblo::ifstream in(parentGame.ActivePluginsFile());
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
        if (parentGame.Id() == LIBLO_GAME_TES5) {
            Plugin plug = Plugin(parentGame.MasterFile());
            if (find(plug) == end()){
                insert(plug);
                activeOrdered.insert(activeOrdered.begin(), plug); // insert first
            }
            plug = Plugin("Update.esm");
            if (plug.Exists(parentGame) && find(plug) == end()) { // FIXME: must resave plugins.txt
                insert(plug);
                auto firstEsp = find_if(activeOrdered.begin(), activeOrdered.end(),
                    [&parentGame](const Plugin& plugin) { return !plugin.IsMasterFileNoThrow(parentGame);  });
                    activeOrdered.insert(firstEsp, plug); // insert at last esm position
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
            liblo::ofstream outfile(parentGame.ActivePluginsFile(), ios_base::trunc);
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
                for (const auto &plugin : parentGame.loadOrder) {
                    if (find(plugin) == end() || (parentGame.Id() == LIBLO_GAME_TES5 && plugin.Name() == parentGame.MasterFile()))
                        continue;

                    try {
                        outfile << FromUTF8(plugin.Name()) << endl;
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
        else if (parentGame.Id() == LIBLO_GAME_TES5) {
            if (find(Plugin(parentGame.MasterFile())) == end())
                msg += parentGame.MasterFile() + " isn't active.\n";
            else if (Plugin("Update.esm").Exists(parentGame) && find(Plugin("Update.esm")) == end())
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
