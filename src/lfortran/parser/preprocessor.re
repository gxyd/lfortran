#include <iostream>
#include <map>

#include <lfortran/parser/preprocessor.h>
#include <lfortran/assert.h>

namespace LFortran
{

std::string CPreprocessor::token(unsigned char *tok, unsigned char* cur) const
{
    return std::string((char *)tok, cur - tok);
}

void parse_macro_definition(const std::string &line,
    std::string &name, std::string &subs)
{
    size_t i = 0;
    i += std::string("#define").size();
    while (line[i] == ' ') i++;
    size_t s1 = i;
    while (line[i] != ' ') i++;
    name = std::string(&line[s1], i-s1);
    while (line[i] == ' ') i++;
    subs = line.substr(i, line.size()-i-1);
}

void get_newlines(const std::string &s, std::vector<uint32_t> &newlines) {
    newlines.push_back(0);
    for (uint32_t pos=0; pos < s.size(); pos++) {
        if (s[pos] == '\n') newlines.push_back(pos);
    }
    newlines.push_back(s.size());
}

std::string CPreprocessor::run(const std::string &input, LocationManager &lm) const {
    LFORTRAN_ASSERT(input[input.size()] == '\0');
    unsigned char *string_start=(unsigned char*)(&input[0]);
    unsigned char *cur = string_start;
    Location loc;
    std::string output;
    std::map<std::string, std::string> macro_definitions;
    lm.preprocessor = true;
    get_newlines(input, lm.in_newlines0);
    lm.out_start0.push_back(0);
    lm.in_start0.push_back(0);
    for (;;) {
        unsigned char *tok = cur;
        unsigned char *mar;
        /*!re2c
            re2c:define:YYCURSOR = cur;
            re2c:define:YYMARKER = mar;
            re2c:yyfill:enable = 0;
            re2c:define:YYCTYPE = "unsigned char";

            end = "\x00";
            newline = "\n";
            whitespace = [ \t\v\r]+;
            digit = [0-9];
            char =  [a-zA-Z_];
            name = char (char | digit)*;

            int = digit+;

            * {
                token_loc(loc, tok, cur, string_start);
                output.append(token(tok, cur));
                continue;
            }
            end {
                break;
            }
            "#define" whitespace name whitespace [^\n\x00]* newline  {
                std::string macro_name, macro_subs;
                parse_macro_definition(token(tok, cur),
                    macro_name, macro_subs);
                macro_definitions[macro_name] = macro_subs;
                lm.out_start0.push_back(output.size());
                lm.in_start0.push_back(cur-string_start);
            }
            name {
                std::string t = token(tok, cur);
                if (macro_definitions.find(t) != macro_definitions.end()) {
                    output.append(macro_definitions[t]);
                } else {
                    output.append(t);
                }
                continue;
            }
            '"' ('""'|[^"\x00])* '"' {
                output.append(token(tok, cur));
                continue;
            }
            "'" ("''"|[^'\x00])* "'" {
                output.append(token(tok, cur));
                continue;
            }
        */
    }
    lm.out_start0.push_back(output.size());
    lm.in_start0.push_back(input.size());
    return output;
}

} // namespace LFortran
