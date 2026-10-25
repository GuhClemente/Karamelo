#include "port_catalog.h"
#include "json_mini.h"
#include "ports_catalog_json.h"

#include <set>

std::string PortCatalogEmbeddedJson()
{
    return std::string(reinterpret_cast<const char*>(PORTS_CATALOG_JSON), sizeof(PORTS_CATALOG_JSON));
}

namespace {

bool GetString(const JsonValue& obj, const char* key, std::string& out, std::string& error, const std::string& where)
{
    const JsonValue* v = obj.Find(key);
    if (!v) return true;
    if (v->type != JsonValue::String) { error = where + ": '" + key + "' deve ser texto"; return false; }
    out = v->str;
    return true;
}

bool GetBool(const JsonValue& obj, const char* key, bool& out, std::string& error, const std::string& where)
{
    const JsonValue* v = obj.Find(key);
    if (!v) return true;
    if (v->type != JsonValue::Bool) { error = where + ": '" + key + "' deve ser true/false"; return false; }
    out = v->boolean;
    return true;
}

bool GetInt(const JsonValue& obj, const char* key, int& out, std::string& error, const std::string& where)
{
    const JsonValue* v = obj.Find(key);
    if (!v) return true;
    if (v->type != JsonValue::Number || v->number != (double)(int)v->number) {
        error = where + ": '" + key + "' deve ser inteiro";
        return false;
    }
    out = (int)v->number;
    return true;
}

bool GetList(const JsonValue& obj, const char* key, std::vector<std::string>& out, std::string& error, const std::string& where)
{
    const JsonValue* v = obj.Find(key);
    if (!v) return true;
    if (v->type != JsonValue::Array) { error = where + ": '" + key + "' deve ser lista"; return false; }
    out.clear();
    for (const auto& item : v->items) {
        if (item.type != JsonValue::String) { error = where + ": '" + key + "' so aceita textos"; return false; }
        out.push_back(item.str);
    }
    return true;
}

bool IsSha256Hex(const std::string& s)
{
    if (s.size() != 64) return false;
    for (char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

bool GetHashes(const JsonValue& obj, const char* key, std::vector<std::pair<std::string, std::string>>& out,
               std::string& error, const std::string& where)
{
    const JsonValue* v = obj.Find(key);
    if (!v) return true;
    if (v->type != JsonValue::Object) { error = where + ": '" + key + "' deve ser objeto {arquivo: sha256}"; return false; }
    out.clear();
    for (const auto& m : v->members) {
        if (m.second.type != JsonValue::String || !IsSha256Hex(m.second.str) || m.first.empty() ||
            m.first.find('/') != std::string::npos || m.first.find('\\') != std::string::npos) {
            error = where + ": '" + key + "' tem entrada invalida (" + m.first + ")";
            return false;
        }
        out.emplace_back(m.first, m.second.str);
    }
    return true;
}

// The id becomes a folder under ports/ and a path component on disk: nothing
// that could climb out of it or mean something to the shell.
bool IdIsSafe(const std::string& id)
{
    if (id.empty() || id.size() > 64) return false;
    for (unsigned char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return false;
    return true;
}

// Relative paths inside a port folder: no absolute path, no "..".
bool RelPathIsSafe(const std::string& p)
{
    if (p.empty()) return true;
    if (p[0] == '/' || p[0] == '\\' || (p.size() > 1 && p[1] == ':')) return false;
    return p.find("..") == std::string::npos;
}

void PutString(std::string& o, bool& first, const char* key, const std::string& v)
{
    if (v.empty()) return;
    o += first ? "\n" : ",\n";
    first = false;
    o += "      " + JsonQuote(key) + ": " + JsonQuote(v);
}

void PutBool(std::string& o, bool& first, const char* key, bool v, bool def)
{
    if (v == def) return;
    o += first ? "\n" : ",\n";
    first = false;
    o += "      " + JsonQuote(key) + (v ? ": true" : ": false");
}

void PutInt(std::string& o, bool& first, const char* key, int v, int def)
{
    if (v == def) return;
    o += first ? "\n" : ",\n";
    first = false;
    o += "      " + JsonQuote(key) + ": " + std::to_string(v);
}

void PutList(std::string& o, bool& first, const char* key, const std::vector<std::string>& v)
{
    if (v.empty()) return;
    o += first ? "\n" : ",\n";
    first = false;
    o += "      " + JsonQuote(key) + ": [";
    for (size_t i = 0; i < v.size(); i++) o += (i ? ", " : "") + JsonQuote(v[i]);
    o += "]";
}

} // namespace

bool PortCatalogParse(const std::string& json, PortCatalog& out, std::string& error)
{
    JsonValue root;
    if (!JsonParse(json, root, error)) { error = "JSON invalido: " + error; return false; }
    if (root.type != JsonValue::Object) { error = "a raiz deve ser um objeto"; return false; }

    PortCatalog cat;
    if (!GetInt(root, "catalog_version", cat.catalog_version, error, "catalogo")) return false;
    if (cat.catalog_version <= 0) { error = "catalog_version ausente ou invalido"; return false; }

    const JsonValue* ports = root.Find("ports");
    if (!ports || ports->type != JsonValue::Array) { error = "'ports' ausente ou nao e lista"; return false; }

    std::set<std::string> seen;
    for (size_t i = 0; i < ports->items.size(); i++) {
        const JsonValue& e = ports->items[i];
        std::string where = "port #" + std::to_string(i + 1);
        if (e.type != JsonValue::Object) { error = where + ": deve ser um objeto"; return false; }

        PortDefinition d;
        if (!GetString(e, "id", d.id, error, where)) return false;
        if (!d.id.empty()) where = "port '" + d.id + "'";
        if (!GetString(e, "display_name", d.display_name, error, where) ||
            !GetString(e, "repo", d.repo, error, where) ||
            !GetString(e, "exe_hint", d.exe_hint, error, where) ||
            !GetBool(e, "needs_rom", d.needs_rom, error, where) ||
            !GetList(e, "rom_keywords", d.rom_keywords, error, where) ||
            !GetBool(e, "has_linux_build", d.has_linux_build, error, where) ||
            !GetBool(e, "has_macos_build", d.has_macos_build, error, where) ||
            !GetString(e, "exe_relpath", d.exe_relpath, error, where) ||
            !GetList(e, "launch_args", d.launch_args, error, where) ||
            !GetString(e, "console_log_relpath", d.console_log_relpath, error, where) ||
            !GetString(e, "x360_game_dir", d.x360_game_dir, error, where) ||
            !GetList(e, "iso_keywords", d.iso_keywords, error, where) ||
            !GetList(e, "iso_dirs", d.iso_dirs, error, where) ||
            !GetList(e, "iso_exts", d.iso_exts, error, where) ||
            !GetString(e, "iso_disc_id", d.iso_disc_id, error, where) ||
            !GetInt(e, "iso_disc_rev", d.iso_disc_rev, error, where) ||
            !GetString(e, "iso_disc_label", d.iso_disc_label, error, where) ||
            !GetBool(e, "hidden", d.hidden, error, where) ||
            !GetString(e, "downloads_json_url", d.downloads_json_url, error, where) ||
            !GetList(e, "rom_exclude_keywords", d.rom_exclude_keywords, error, where) ||
            !GetBool(e, "iso_keywords_whole_word", d.iso_keywords_whole_word, error, where) ||
            !GetString(e, "psp_game_dir", d.psp_game_dir, error, where) ||
            !GetString(e, "release_tag", d.release_tag, error, where) ||
            !GetHashes(e, "asset_sha256", d.asset_sha256, error, where) ||
            !GetString(e, "notes", d.notes, error, where))
            return false;

        if (!IdIsSafe(d.id)) { error = where + ": id ausente ou com caracteres invalidos"; return false; }
        if (d.display_name.empty()) { error = where + ": display_name ausente"; return false; }
        if (!seen.insert(d.id).second) { error = where + ": id duplicado"; return false; }
        if (!RelPathIsSafe(d.exe_relpath) || !RelPathIsSafe(d.console_log_relpath) ||
            !RelPathIsSafe(d.x360_game_dir) || !RelPathIsSafe(d.psp_game_dir) ||
            !RelPathIsSafe(d.exe_hint)) {
            error = where + ": caminho fora da pasta do port";
            return false;
        }
        if (!d.downloads_json_url.empty() && d.downloads_json_url.rfind("https://", 0) != 0) {
            error = where + ": downloads_json_url precisa ser https://";
            return false;
        }
        cat.ports.push_back(std::move(d));
    }

    out = std::move(cat);
    error.clear();
    return true;
}

std::string PortCatalogToJson(const PortCatalog& catalog)
{
    std::string o = "{\n  \"catalog_version\": " + std::to_string(catalog.catalog_version) + ",\n  \"ports\": [";
    for (size_t i = 0; i < catalog.ports.size(); i++) {
        const PortDefinition& d = catalog.ports[i];
        o += i ? ",\n    {" : "\n    {";
        bool first = true;
        PutString(o, first, "id", d.id);
        PutString(o, first, "display_name", d.display_name);
        PutString(o, first, "repo", d.repo);
        PutString(o, first, "exe_hint", d.exe_hint);
        PutBool(o, first, "needs_rom", d.needs_rom, false);
        PutList(o, first, "rom_keywords", d.rom_keywords);
        PutBool(o, first, "has_linux_build", d.has_linux_build, false);
        PutBool(o, first, "has_macos_build", d.has_macos_build, false);
        PutString(o, first, "exe_relpath", d.exe_relpath);
        PutList(o, first, "launch_args", d.launch_args);
        PutString(o, first, "console_log_relpath", d.console_log_relpath);
        PutString(o, first, "x360_game_dir", d.x360_game_dir);
        PutList(o, first, "iso_keywords", d.iso_keywords);
        PutList(o, first, "iso_dirs", d.iso_dirs);
        PutList(o, first, "iso_exts", d.iso_exts);
        PutString(o, first, "iso_disc_id", d.iso_disc_id);
        PutInt(o, first, "iso_disc_rev", d.iso_disc_rev, -1);
        PutString(o, first, "iso_disc_label", d.iso_disc_label);
        PutBool(o, first, "hidden", d.hidden, false);
        PutString(o, first, "downloads_json_url", d.downloads_json_url);
        PutList(o, first, "rom_exclude_keywords", d.rom_exclude_keywords);
        PutBool(o, first, "iso_keywords_whole_word", d.iso_keywords_whole_word, false);
        PutString(o, first, "psp_game_dir", d.psp_game_dir);
        PutString(o, first, "release_tag", d.release_tag);
        if (!d.asset_sha256.empty()) {
            o += first ? "\n" : ",\n";
            first = false;
            o += "      \"asset_sha256\": {";
            for (size_t k = 0; k < d.asset_sha256.size(); k++)
                o += (k ? ",\n        " : "\n        ") + JsonQuote(d.asset_sha256[k].first) + ": " +
                     JsonQuote(d.asset_sha256[k].second);
            o += "\n      }";
        }
        PutString(o, first, "notes", d.notes);
        o += "\n    }";
    }
    o += "\n  ]\n}\n";
    return o;
}
