#include "json_mini.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const JsonValue* JsonValue::Find(const char* key) const
{
	if (type != Object || !key) return nullptr;
	for (const auto& m : members)
		if (m.first == key) return &m.second;
	return nullptr;
}

namespace {

struct Parser
{
	const std::string& s;
	size_t pos = 0;
	std::string error;
	int depth = 0;

	explicit Parser(const std::string& text) : s(text) {}

	bool Fail(const char* what)
	{
		if (error.empty())
		{
			char buf[96];
			snprintf(buf, sizeof(buf), "%s (byte %zu)", what, pos);
			error = buf;
		}
		return false;
	}

	void SkipSpace()
	{
		while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r'))
			pos++;
	}

	bool Literal(const char* word)
	{
		size_t n = strlen(word);
		if (s.compare(pos, n, word) != 0) return Fail("valor invalido");
		pos += n;
		return true;
	}

	static void AppendUtf8(std::string& out, unsigned cp)
	{
		if (cp < 0x80) out += (char)cp;
		else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
		else if (cp < 0x10000)
		{
			out += (char)(0xE0 | (cp >> 12));
			out += (char)(0x80 | ((cp >> 6) & 0x3F));
			out += (char)(0x80 | (cp & 0x3F));
		}
		else
		{
			out += (char)(0xF0 | (cp >> 18));
			out += (char)(0x80 | ((cp >> 12) & 0x3F));
			out += (char)(0x80 | ((cp >> 6) & 0x3F));
			out += (char)(0x80 | (cp & 0x3F));
		}
	}

	bool Hex4(unsigned& out)
	{
		if (pos + 4 > s.size()) return Fail("escape \\u incompleto");
		out = 0;
		for (int i = 0; i < 4; i++)
		{
			char c = s[pos++];
			out <<= 4;
			if (c >= '0' && c <= '9') out |= (unsigned)(c - '0');
			else if (c >= 'a' && c <= 'f') out |= (unsigned)(c - 'a' + 10);
			else if (c >= 'A' && c <= 'F') out |= (unsigned)(c - 'A' + 10);
			else return Fail("escape \\u invalido");
		}
		return true;
	}

	bool String(std::string& out)
	{
		if (pos >= s.size() || s[pos] != '"') return Fail("esperava string");
		pos++;
		out.clear();
		while (pos < s.size())
		{
			char c = s[pos++];
			if (c == '"') return true;
			if ((unsigned char)c < 0x20) return Fail("caractere de controle dentro de string");
			if (c != '\\') { out += c; continue; }
			if (pos >= s.size()) break;
			char e = s[pos++];
			switch (e)
			{
			case '"': out += '"'; break;
			case '\\': out += '\\'; break;
			case '/': out += '/'; break;
			case 'b': out += '\b'; break;
			case 'f': out += '\f'; break;
			case 'n': out += '\n'; break;
			case 'r': out += '\r'; break;
			case 't': out += '\t'; break;
			case 'u':
			{
				unsigned cp;
				if (!Hex4(cp)) return false;
				if (cp >= 0xD800 && cp <= 0xDBFF)
				{
					unsigned lo;
					if (pos + 2 > s.size() || s[pos] != '\\' || s[pos + 1] != 'u') return Fail("par surrogate incompleto");
					pos += 2;
					if (!Hex4(lo) || lo < 0xDC00 || lo > 0xDFFF) return Fail("par surrogate invalido");
					cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
				}
				AppendUtf8(out, cp);
				break;
			}
			default: return Fail("escape invalido");
			}
		}
		return Fail("string sem fim");
	}

	bool Number(double& out)
	{
		size_t start = pos;
		if (pos < s.size() && s[pos] == '-') pos++;
		if (pos >= s.size() || !(s[pos] >= '0' && s[pos] <= '9')) return Fail("numero invalido");
		while (pos < s.size() && ((s[pos] >= '0' && s[pos] <= '9') || s[pos] == '.' || s[pos] == 'e' ||
		                          s[pos] == 'E' || s[pos] == '+' || s[pos] == '-'))
			pos++;
		std::string num = s.substr(start, pos - start);
		char* end = nullptr;
		out = strtod(num.c_str(), &end);
		if (!end || *end != '\0') return Fail("numero invalido");
		return true;
	}

	bool Value(JsonValue& v)
	{
		if (++depth > 64) return Fail("aninhamento profundo demais");
		SkipSpace();
		if (pos >= s.size()) return Fail("fim inesperado");
		bool ok = false;
		char c = s[pos];
		if (c == '{')
		{
			v.type = JsonValue::Object;
			pos++;
			SkipSpace();
			if (pos < s.size() && s[pos] == '}') { pos++; ok = true; }
			else
			{
				for (;;)
				{
					SkipSpace();
					std::string key;
					if (!String(key)) break;
					SkipSpace();
					if (pos >= s.size() || s[pos] != ':') { Fail("esperava ':'"); break; }
					pos++;
					JsonValue child;
					if (!Value(child)) break;
					v.members.emplace_back(std::move(key), std::move(child));
					SkipSpace();
					if (pos < s.size() && s[pos] == ',') { pos++; continue; }
					if (pos < s.size() && s[pos] == '}') { pos++; ok = true; }
					else Fail("esperava ',' ou '}'");
					break;
				}
			}
		}
		else if (c == '[')
		{
			v.type = JsonValue::Array;
			pos++;
			SkipSpace();
			if (pos < s.size() && s[pos] == ']') { pos++; ok = true; }
			else
			{
				for (;;)
				{
					JsonValue child;
					if (!Value(child)) break;
					v.items.push_back(std::move(child));
					SkipSpace();
					if (pos < s.size() && s[pos] == ',') { pos++; continue; }
					if (pos < s.size() && s[pos] == ']') { pos++; ok = true; }
					else Fail("esperava ',' ou ']'");
					break;
				}
			}
		}
		else if (c == '"') { v.type = JsonValue::String; ok = String(v.str); }
		else if (c == 't') { v.type = JsonValue::Bool; v.boolean = true; ok = Literal("true"); }
		else if (c == 'f') { v.type = JsonValue::Bool; v.boolean = false; ok = Literal("false"); }
		else if (c == 'n') { v.type = JsonValue::Null; ok = Literal("null"); }
		else { v.type = JsonValue::Number; ok = Number(v.number); }
		depth--;
		return ok;
	}
};

} // namespace

bool JsonParse(const std::string& text, JsonValue& out, std::string& error)
{
	Parser p(text);
	// A UTF-8 BOM in front is tolerated: editors on Windows add one.
	if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB &&
	    (unsigned char)text[2] == 0xBF)
		p.pos = 3;
	out = JsonValue();
	if (!p.Value(out)) { error = p.error; return false; }
	p.SkipSpace();
	if (p.pos != text.size()) { p.Fail("conteudo depois do fim"); error = p.error; return false; }
	error.clear();
	return true;
}

std::string JsonQuote(const std::string& s)
{
	std::string out = "\"";
	for (unsigned char c : s)
	{
		switch (c)
		{
		case '"': out += "\\\""; break;
		case '\\': out += "\\\\"; break;
		case '\n': out += "\\n"; break;
		case '\r': out += "\\r"; break;
		case '\t': out += "\\t"; break;
		default:
			if (c < 0x20)
			{
				char buf[8];
				snprintf(buf, sizeof(buf), "\\u%04x", c);
				out += buf;
			}
			else out += (char)c;
		}
	}
	return out + "\"";
}
