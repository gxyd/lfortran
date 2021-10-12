/* Generated by re2c 2.2 on Fri Oct  8 20:28:47 2021 */
#line 1 "preprocessor.re"
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

std::string CPreprocessor::run(const std::string &input) const {
    LFORTRAN_ASSERT(input[input.size()] == '\0');
    unsigned char *string_start=(unsigned char*)(&input[0]);
    unsigned char *cur = string_start;
    Location loc;
    std::string output;
    std::map<std::string, std::string> macro_definitions;
    for (;;) {
        unsigned char *tok = cur;
        unsigned char *mar;
        
#line 42 "preprocessor.cpp"
{
	unsigned char yych;
	unsigned int yyaccept = 0;
	static const unsigned char yybm[] = {
		  0, 176, 176, 176, 176, 176, 176, 176, 
		176, 240,  48, 240, 176, 240, 176, 176, 
		176, 176, 176, 176, 176, 176, 176, 176, 
		176, 176, 176, 176, 176, 176, 176, 176, 
		240, 176, 160, 176, 176, 176, 176, 144, 
		176, 176, 176, 176, 176, 176, 176, 176, 
		184, 184, 184, 184, 184, 184, 184, 184, 
		184, 184, 176, 176, 176, 176, 176, 176, 
		176, 184, 184, 184, 184, 184, 184, 184, 
		184, 184, 184, 184, 184, 184, 184, 184, 
		184, 184, 184, 184, 184, 184, 184, 184, 
		184, 184, 184, 176, 176, 176, 176, 184, 
		176, 184, 184, 184, 184, 184, 184, 184, 
		184, 184, 184, 184, 184, 184, 184, 184, 
		184, 184, 184, 184, 184, 184, 184, 184, 
		184, 184, 184, 176, 176, 176, 176, 176, 
		176, 176, 176, 176, 176, 176, 176, 176, 
		176, 176, 176, 176, 176, 176, 176, 176, 
		176, 176, 176, 176, 176, 176, 176, 176, 
		176, 176, 176, 176, 176, 176, 176, 176, 
		176, 176, 176, 176, 176, 176, 176, 176, 
		176, 176, 176, 176, 176, 176, 176, 176, 
		176, 176, 176, 176, 176, 176, 176, 176, 
		176, 176, 176, 176, 176, 176, 176, 176, 
		176, 176, 176, 176, 176, 176, 176, 176, 
		176, 176, 176, 176, 176, 176, 176, 176, 
		176, 176, 176, 176, 176, 176, 176, 176, 
		176, 176, 176, 176, 176, 176, 176, 176, 
		176, 176, 176, 176, 176, 176, 176, 176, 
		176, 176, 176, 176, 176, 176, 176, 176, 
		176, 176, 176, 176, 176, 176, 176, 176, 
		176, 176, 176, 176, 176, 176, 176, 176, 
	};
	yych = *cur;
	if (yych <= '\'') {
		if (yych <= '"') {
			if (yych <= 0x00) goto yy2;
			if (yych <= '!') goto yy4;
			goto yy6;
		} else {
			if (yych <= '#') goto yy7;
			if (yych <= '&') goto yy4;
			goto yy8;
		}
	} else {
		if (yych <= '^') {
			if (yych <= '@') goto yy4;
			if (yych <= 'Z') goto yy9;
			goto yy4;
		} else {
			if (yych == '`') goto yy4;
			if (yych <= 'z') goto yy9;
			goto yy4;
		}
	}
yy2:
	++cur;
#line 58 "preprocessor.re"
	{
                break;
            }
#line 108 "preprocessor.cpp"
yy4:
	++cur;
yy5:
#line 53 "preprocessor.re"
	{
                token_loc(loc, tok, cur, string_start);
                output.append(token(tok, cur));
                continue;
            }
#line 118 "preprocessor.cpp"
yy6:
	yyaccept = 0;
	yych = *(mar = ++cur);
	if (yych <= 0x00) goto yy5;
	goto yy13;
yy7:
	yyaccept = 0;
	yych = *(mar = ++cur);
	if (yych == 'd') goto yy17;
	goto yy5;
yy8:
	yyaccept = 0;
	yych = *(mar = ++cur);
	if (yych <= 0x00) goto yy5;
	goto yy19;
yy9:
	yych = *++cur;
	if (yybm[0+yych] & 8) {
		goto yy9;
	}
#line 68 "preprocessor.re"
	{
                std::string t = token(tok, cur);
                if (macro_definitions.find(t) != macro_definitions.end()) {
                    output.append(macro_definitions[t]);
                } else {
                    output.append(t);
                }
                continue;
            }
#line 149 "preprocessor.cpp"
yy12:
	yych = *++cur;
yy13:
	if (yybm[0+yych] & 16) {
		goto yy12;
	}
	if (yych >= 0x01) goto yy15;
yy14:
	cur = mar;
	if (yyaccept <= 1) {
		if (yyaccept == 0) {
			goto yy5;
		} else {
			goto yy16;
		}
	} else {
		goto yy21;
	}
yy15:
	yyaccept = 1;
	yych = *(mar = ++cur);
	if (yych == '"') goto yy12;
yy16:
#line 77 "preprocessor.re"
	{
                output.append(token(tok, cur));
                continue;
            }
#line 178 "preprocessor.cpp"
yy17:
	yych = *++cur;
	if (yych == 'e') goto yy22;
	goto yy14;
yy18:
	yych = *++cur;
yy19:
	if (yybm[0+yych] & 32) {
		goto yy18;
	}
	if (yych <= 0x00) goto yy14;
	yyaccept = 2;
	yych = *(mar = ++cur);
	if (yych == '\'') goto yy18;
yy21:
#line 81 "preprocessor.re"
	{
                output.append(token(tok, cur));
                continue;
            }
#line 199 "preprocessor.cpp"
yy22:
	yych = *++cur;
	if (yych != 'f') goto yy14;
	yych = *++cur;
	if (yych != 'i') goto yy14;
	yych = *++cur;
	if (yych != 'n') goto yy14;
	yych = *++cur;
	if (yych != 'e') goto yy14;
	yych = *++cur;
	if (yych <= '^') {
		if (yych <= '@') goto yy28;
		if (yych <= 'Z') goto yy14;
		goto yy28;
	} else {
		if (yych == '`') goto yy28;
		if (yych <= 'z') goto yy14;
		goto yy28;
	}
yy27:
	yych = *++cur;
yy28:
	if (yybm[0+yych] & 64) {
		goto yy27;
	}
	if (yych <= '^') {
		if (yych <= '@') goto yy14;
		if (yych >= '[') goto yy14;
	} else {
		if (yych == '`') goto yy14;
		if (yych >= '{') goto yy14;
	}
yy29:
	yych = *++cur;
	if (yych <= ' ') {
		if (yych <= '\v') {
			if (yych == '\t') goto yy31;
			if (yych <= '\n') goto yy14;
		} else {
			if (yych == '\r') goto yy31;
			if (yych <= 0x1F) goto yy14;
		}
	} else {
		if (yych <= 'Z') {
			if (yych <= '/') goto yy14;
			if (yych <= '9') goto yy29;
			if (yych <= '@') goto yy14;
			goto yy29;
		} else {
			if (yych <= '_') {
				if (yych <= '^') goto yy14;
				goto yy29;
			} else {
				if (yych <= '`') goto yy14;
				if (yych <= 'z') goto yy29;
				goto yy14;
			}
		}
	}
yy31:
	yych = *++cur;
	if (yybm[0+yych] & 128) {
		goto yy31;
	}
	if (yych <= 0x00) goto yy14;
	++cur;
#line 61 "preprocessor.re"
	{
                std::string macro_name, macro_subs;
                parse_macro_definition(token(tok, cur),
                    macro_name, macro_subs);
                macro_definitions[macro_name] = macro_subs;
                //std::cout << "MACRO: " << macro_name << " " << macro_subs << std::endl;
            }
#line 274 "preprocessor.cpp"
}
#line 85 "preprocessor.re"

    }
    return output;
}

} // namespace LFortran
