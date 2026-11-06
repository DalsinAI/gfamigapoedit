#pragma once
#include <string>
#include <functional>
namespace boost { namespace uuids { struct uuid { size_t h; };
 struct string_generator { uuid operator()(const char*){ return {0}; } };
 struct name_generator { name_generator(uuid){} uuid operator()(const std::wstring& s){ return { std::hash<std::wstring>()(s) }; } };
 inline std::string to_string(uuid u){ return std::to_string(u.h); } } }
