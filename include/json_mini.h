#ifndef JSON_MINI_H_INCLUDED
#define JSON_MINI_H_INCLUDED

// A small, strict JSON reader for the files this app owns (the ports catalog
// first of all). Everything else in the project scans JSON with string finds,
// which is fine for picking one field out of a GitHub API reply but not for a
// whole data file whose every field matters. Objects keep their keys in file
// order; numbers are doubles. No dependencies beyond the standard library.

#include <string>
#include <vector>
#include <utility>

struct JsonValue
{
	enum Type { Null, Bool, Number, String, Array, Object };

	Type type = Null;
	bool boolean = false;
	double number = 0.0;
	std::string str;
	std::vector<JsonValue> items;                               // Array
	std::vector<std::pair<std::string, JsonValue>> members;     // Object, in file order

	// Object lookup; nullptr when this is not an object or the key is absent.
	const JsonValue* Find(const char* key) const;
};

// Parses the whole text. On failure returns false and describes the first
// problem (with its byte offset) in error.
bool JsonParse(const std::string& text, JsonValue& out, std::string& error);

// Quotes and escapes s as a JSON string literal.
std::string JsonQuote(const std::string& s);

#endif
