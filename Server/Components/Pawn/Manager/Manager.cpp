/******************************************************
* - Originally written by Y_Less, taken from old open.mp code

*/

#include "Manager.hpp"
#include "../PluginManager/PluginManager.hpp"
#include "../utils.hpp"

#ifdef WIN32
#include <Windows.h>
#endif // WIN32
#include <iostream>

#include <pawn-natives/NativeFunc.hpp>
#include <pawn-natives/NativesMain.hpp>

extern "C" {
#if defined _UNICODE
#include <tchar.h>
#elif !defined __T
typedef char TCHAR;
#define __T(string) string
#define _tcscat strcat
#define _tcschr strchr
#define _tcscpy strcpy
#define _tcslen strlen
#endif

#include <amx/amxcons.h>
}

PawnManager::PawnManager()
    : scriptPath_("")
    , basePath_("./")
{
}

PawnManager::~PawnManager()
{
    for (auto& cur : scripts_) {
        cur.second->Call("OnScriptExit", DefaultReturnValue_False, cur.second->GetID());
        PawnPluginManager::Get()->AmxUnload(cur.second->GetAMX());
    }
}

bool PawnManager::OnServerCommand(std::string const& cmd, std::string const& args)
{
    // Legacy commands.
    if (cmd == "loadfs") {
        Load(args);
    } else if (cmd == "reloadfs") {
        Reload(args);
    } else if (cmd == "unloadfs") {
        Unload(args);
    } else if (cmd == "changemode") {
        Unload(entryScript);
        Load(args, true);
    }
    return false;
}

AMX* PawnManager::AMXFromID(int id) const
{
    for (auto& cur : scripts_) {
        if (cur.second->GetID() == id) {
            return cur.second->GetAMX();
        }
    }
    return nullptr;
}

int PawnManager::IDFromAMX(AMX* amx) const
{
    for (auto& cur : scripts_) {
        if (cur.second->GetAMX() == amx) {
            return cur.second->GetID();
        }
    }
    return 0;
}

void PawnManager::CheckNatives(PawnScript& script)
{
    int
        count;
    script.NumNatives(&count);
    AMX_NATIVE_INFO
    func;
    while (count--) {
        script.GetNativeByIndex(count, &func);
        if (func.func == nullptr) {
            PawnManager::Get()->core->logLn(LogLevel::Error, "Function not registered: %s", func.name);
        }
    }
}

void PawnManager::Load(std::string const& name, bool primary)
{
    if (scripts_.count(name)) {
        return;
    }

    // if the user just supplied a script name, add the extension
    // otherwise, don't, as they may have supplied a full abs/rel path.
    std::string ext = utils::endsWith(name, ".amx") ? "" : ".amx";

    std::string canon;
    utils::Canonicalise(basePath_ + scriptPath_ + name + ext, canon);
    std::unique_ptr<PawnScript> ptr = std::make_unique<PawnScript>(++id_, canon, core);

    if (!ptr.get()->IsLoaded()) {
        PawnManager::Get()->core->logLn(LogLevel::Error, "Unable to load script %s\n\n", name.c_str());
        return;
    }

    PawnScript& script = *ptr;

    auto res = scripts_.emplace(name, std::move(ptr));
    if (res.second) {
        amxToScript_.emplace(script.GetAMX(), res.first->second.get());
    }
    script.Register("CallLocalFunction", &utils::pawn_Script_Call);
    script.Register("CallRemoteFunction", &utils::pawn_Script_CallAll);
    script.Register("Script_CallTargeted", &utils::pawn_Script_CallOne);
    script.Register("format", &utils::pawn_format);
    script.Register("printf", &utils::pawn_printf);
    script.Register("Script_GetID", &utils::pawn_Script_GetID);
    script.Register("SetTimer", &utils::pawn_settimer);
    script.Register("SetTimerEx", &utils::pawn_settimerex);
    script.Register("KillTimer", &utils::pawn_killtimer);

    pawn_natives::AmxLoad(script.GetAMX());

    PawnPluginManager::Get()->AmxLoad(script.GetAMX());

    CheckNatives(script);

    if (primary) {
        entryScript = name;
        script.Call("OnGameModeInit", DefaultReturnValue_False);

        int err = script.Exec(nullptr, AMX_EXEC_MAIN);
        if (err != AMX_ERR_INDEX && err != AMX_ERR_NONE) {
            // If there's no `main` ignore it for now.
            PawnManager::Get()->core->logLn(LogLevel::Error, "%s", aux_StrError(err));
        }
    } else {
        script.Call("OnFilterScriptInit", DefaultReturnValue_False);
    }

    // TODO: `AMX_EXEC_CONT` support.
    // Assume that all initialisation and header mangling is now complete, and that it is safe to
    // cache public pointers.
}

void PawnManager::Reload(std::string const& name)
{
    auto pos = scripts_.find(name);
    bool isEntryScript = entryScript == name;
    if (pos != scripts_.end()) {
        auto& script = *pos->second;

        script.Call("OnScriptExit", DefaultReturnValue_False);
        if (isEntryScript) {
            script.Call("OnGameModeExit", DefaultReturnValue_False);
        } else {
            script.Call("OnFilterScriptExit", DefaultReturnValue_False);
        }

        PawnPluginManager::Get()->AmxUnload(script.GetAMX());
        amxToScript_.erase(script.GetAMX());
        scripts_.erase(pos);
    }
    Load(name, entryScript == name);
}

void PawnManager::Unload(std::string const& name)
{
    auto pos = scripts_.find(name);
    bool isEntryScript = entryScript == name;
    if (pos == scripts_.end()) {
        return;
    }
    auto& script = *pos->second;

    script.Call("OnScriptExit", DefaultReturnValue_False);
    if (isEntryScript) {
        script.Call("OnGameModeExit", DefaultReturnValue_False);
    } else {
        script.Call("OnFilterScriptExit", DefaultReturnValue_False);
    }

    PawnPluginManager::Get()->AmxUnload(script.GetAMX());
    PawnTimerImpl::Get()->killTimers(script.GetAMX());
    amxToScript_.erase(script.GetAMX());
    scripts_.erase(pos);
}

void PawnManager::SetBasePath(std::string const& path)
{
    if (path.length() == 0) {
        basePath_ = "/";
    } else if (path.back() == '/') {
        basePath_ = path;
    } else {
        basePath_ = path + '/';
    }
}

void PawnManager::SetScriptPath(std::string const& path)
{
    if (path.length() == 0) {
        scriptPath_ = "/";
    } else if (path.back() == '/') {
        scriptPath_ = path;
    } else {
        scriptPath_ = path + '/';
    }
}
