/*
 * Flex lexical analyzer for the mini language (project manual Section
 * 4.1 / 5.4). Keyword patterns are listed before the generic
 * identifier pattern (the manual's own tip) so Flex's longest-match
 * tie-break (first rule wins when two rules match the same length)
 * picks the keyword for exact keyword text; %option yylineno gives us
 * line numbers for every diagnostic for free.
 */

%{
#define _POSIX_C_SOURCE 200809L  /* for strdup — see ast.c's identical guard for why this matters under -std=c11 */
#include <stdlib.h>
#include <string.h>
#include "parser.tab.h"
#include "error.h"
%}

%option noyywrap
%option yylineno

DIGIT   [0-9]
ALPHA   [a-zA-Z_]
ALNUM   [a-zA-Z0-9_]

%%

"//".*                              { /* single-line comment: discard */ }
"/*"([^*]|\*+[^*/])*\*+"/"          { /* block comment: discard */ }

"int"                                { return KW_INT; }
"float"                              { return KW_FLOAT; }
"bool"                               { return KW_BOOL; }
"if"                                 { return KW_IF; }
"else"                               { return KW_ELSE; }
"while"                              { return KW_WHILE; }
"print"                              { return KW_PRINT; }
"true"                               { return KW_TRUE; }
"false"                              { return KW_FALSE; }
"function"                           { return KW_FUNCTION; }
"return"                             { return KW_RETURN; }
"for"                                { return KW_FOR; }
"do"                                 { return KW_DO; }
"string"                             { return KW_STRING; }
"switch"                             { return KW_SWITCH; }
"case"                               { return KW_CASE; }
"default"                            { return KW_DEFAULT; }
"break"                              { return KW_BREAK; }
"read"                               { return KW_READ; }

{DIGIT}+"."{DIGIT}+   { yylval.fval = atof(yytext); return FLOAT_LIT; }
{DIGIT}+              { yylval.ival = atoi(yytext); return INT_LIT; }

\"([^"\\\n]|\\.)*\"   {
    /* String literal (advanced feature): strip the surrounding quotes
     * and resolve the handful of backslash escapes we support. */
    size_t len = strlen(yytext);
    char *buf = (char *)malloc(len);
    size_t j = 0;
    for (size_t i = 1; i + 1 < len; i++) {
        if (yytext[i] == '\\' && i + 2 < len) {
            i++;
            switch (yytext[i]) {
                case 'n': buf[j++] = '\n'; break;
                case 't': buf[j++] = '\t'; break;
                case '"': buf[j++] = '"'; break;
                case '\\': buf[j++] = '\\'; break;
                default: buf[j++] = yytext[i]; break;
            }
        } else {
            buf[j++] = yytext[i];
        }
    }
    buf[j] = '\0';
    yylval.sval = buf;
    return STRING_LIT;
}

{ALPHA}{ALNUM}*       { yylval.sval = strdup(yytext); return IDENT; }

"++"   { return INC; }
"--"   { return DEC; }
"<="   { return LE; }
">="   { return GE; }
"=="   { return EQ; }
"!="   { return NEQ; }
"&&"   { return AND; }
"||"   { return OR; }
"<"    { return LT; }
">"    { return GT; }
"!"    { return NOT; }
"="    { return ASSIGN; }
"+"    { return PLUS; }
"-"    { return MINUS; }
"*"    { return STAR; }
"/"    { return SLASH; }
"%"    { return PERCENT; }
";"    { return SEMI; }
","    { return COMMA; }
"{"    { return LBRACE; }
"}"    { return RBRACE; }
"("    { return LPAREN; }
")"    { return RPAREN; }
"["    { return LBRACKET; }
"]"    { return RBRACKET; }
":"    { return COLON; }

[ \t\r\n]+   { /* whitespace: discard */ }

.   { report_error("Lexical", yylineno, "unexpected character '%s'", yytext); }

%%
