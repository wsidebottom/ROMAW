/*+===================================================================
  File:      parse.y

  Summary:   Implements parse support for ROMAW build and development support.

  Classes:   None.

  Functions: No explicitly exported functions.

  Origin:    Existing repository source in executor/src; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

%{
#include "rsys/common.h"
#include "rsys/options.h"
#include "rsys/prefs.h"
#include "rsys/crc.h"
#include "rsys/parse.h"
#include "rsys/parseopt.h"
#include "rsys/gestalt.h"
#include "rsys/launch.h"
#include "rsys/print.h"
#include "rsys/version.h"
#include "rsys/ecf.h"
#include <limits.h>

/* Copyright 1992 - 2000 by Abacus Research and Development, Inc.
 * All rights reserved.
 */

static long yylex( void );
static void freestring( char *string );
char validblock = 1;
char *ROMlib_win32_token = 0;
char *ROMlib_WindowName = 0;
char *ROMlib_DongleFamily = 0;
char *ROMlib_Comments = 0;
char *ROMlib_new_printer_name = 0;
char *ROMlib_new_label = 0;
static char *ROMlib_mac_cdrom;
pair_t ROMlib_ScreenSize = { INITIALPAIRVALUE, INITIALPAIRVALUE };
pair_t ROMlib_MacSize = { INITIALPAIRVALUE, INITIALPAIRVALUE };
pair_t ROMlib_ScreenLocation = { INITIALPAIRVALUE, INITIALPAIRVALUE };
unsigned short crcval;
int ROMlib_AppleChar = 0;
int ROMlib_display_scale = 100;
/* Classic applications receive the conventional secondary-button gesture. */
int ROMlib_right_button_modifier = ControlKey;

void yyerror(const char *str);
%}

%start configuration

%union	{ long number; char *cptr; char **cptrptr; pair_t *pptr; int *iptr;
	  unsigned long rtype;};

%token	STRINGCONSTANT	/* ptr points to null-terminated string */
%token	HEXCONSTANT	/* number */
%token	INTEGERCONSTANT	/* number */
%token	CHAR4CONSTANT	
%token	OPTIONSCONSTANT	/* number */
%token	WINDOWNAME	/* ptr points to null-terminated string */
%token	DONGLEFAMILY	/* ptr points to null-terminated string */
%token	SYSTEMVERSION
%token	COMMENTS
%token	RDELAY
%token	SCREENSIZE
%token	MACSIZE
%token	SCREENLOCATION
%token	REFRESHNUMBER
%token	CRC
%token	OPTIONS
%token	CACHEPROBLEMS
%token	BITSNUMBER
%token  APPLECHAR
%token  PRINTERNAME
%token  PRINTERTYPELABEL
%token  WIN32TOKEN
%token  MACCDROM
%token  RIGHT_BUTTON_MODIFIER
%token  GESTALT
%token  APP_VERSION
%token	PRVERSION
%token SYSZONE APPLZONE
%token DISPLAY_SCALE
%token <number> SIZE_MULTIPLIER

%type <cptr> STRINGCONSTANT
%type <number> HEXCONSTANT INTEGERCONSTANT OPTIONSCONSTANT
%type <rtype> CHAR4CONSTANT
%type <cptrptr> stringvar
%type <iptr>	optionsvar
%type <iptr>	numervar
%type <number> number optionvalues
%type <pptr> pairvar
%type <number> blockbegin	/* holds previous validblock */
%type <number> gestalt_entry
%type <number> zonevalue

%%

configuration: /* empty */
	| assignmentsandblocks
	;

assignmentsandblocks: assignment
	| block
	| assignmentsandblocks assignment
	| assignmentsandblocks block
	;

assignment:	stringassignment
	| flagassignment
	| arrayassignment
	| optionsassignment
	| numerassignment
	| sysversassignment
	| prversassignment
	| zoneassignment
	| DISPLAY_SCALE '=' number ';'
	{ if (validblock) {
            if ($3 < 25 || $3 > 1600)
              yyerror("DisplayScale must be 25..1600 percent");
            else
              ROMlib_display_scale = $3;
          } }
	;

flagassignment: OPTIONSCONSTANT '=' number ';'
  {
    if (validblock)
      {
        if ($3 == 1) ROMlib_options |= $1;
        else if ($3 == 0) ROMlib_options &= ~$1;
        else yyerror ("option flags must be 0 or 1");
      }
  }
  ;

block:	blockbegin '{' assignments '}' ';'
	{
	    validblock = $1;
	}
	;

assignments: assignment
	| assignments assignment
	;

blockbegin:	CRC '(' CHAR4CONSTANT ',' INTEGERCONSTANT ')' ':' HEXCONSTANT
		{
		    $$ = validblock;
		    crcval = getthecrc($3, $5);
		    validblock = $8 == crcval;
		    if (ROMlib_options & ROMLIB_DEBUG_BIT)
			printf("CRC('%c%c%c%c', %d) = 0x%04lx\n", (int) $3>>24,
		 (int) $3>>16, (int) $3>>8, (int) $3, (int) $5, (long) crcval);
		}
		| APP_VERSION '(' HEXCONSTANT ')' ':' HEXCONSTANT
		{
		    $$ = validblock;
		    validblock = (ROMlib_version_long & $3) == $6;
		}
		;

stringassignment:	stringvar '=' STRINGCONSTANT ';'
			{
			    if (validblock)
			      {
				if (*$1)
				  free (*$1);
				*$1 = strdup ($3);
				freestring ($3);
			      }
			    else
				freestring($3);
			}
	;

sysversassignment:	SYSTEMVERSION '=' number ';'
			{ 
			  if (validblock)
			    ROMlib_set_system_version
			      (CREATE_SYSTEM_VERSION ($3, 0, 0));
			}
			|
			SYSTEMVERSION '=' number '.' number ';'
			{
			  if (validblock) 
			    ROMlib_set_system_version
			      (CREATE_SYSTEM_VERSION ($3, $5, 0));
			}
			|
			SYSTEMVERSION '=' number '.' number '.' number ';'
			{
			  if (validblock)
			    ROMlib_set_system_version
			      (CREATE_SYSTEM_VERSION ($3, $5, $7));
			}
	;

zoneassignment: SYSZONE '=' zonevalue ';'
  { if (validblock && !ROMlib_ecf_zone_value ($3, TRUE, &ROMlib_config_syszone_size))
      yyerror ("invalid SysZone size"); }
  | APPLZONE '=' zonevalue ';'
  { if (validblock && !ROMlib_ecf_zone_value ($3, FALSE, &ROMlib_config_applzone_size))
      yyerror ("invalid ApplZone size"); }
  ;

zonevalue: number { $$ = $1; }
  | number SIZE_MULTIPLIER
    { $$ = $1 >= 0 && $1 <= INT_MAX / $2 ? $1 * $2 : -1; }
  | STRINGCONSTANT
    {
      int value;
      /* The system minimum is lower; the final assignment checks its zone. */
      $$ = ROMlib_ecf_parse_zone ($1, TRUE, &value) ? value : -1;
      freestring ($1);
    }
  ;


prversassignment:	PRVERSION '=' number ';'
			{ 
			  if (validblock)
			    ROMlib_PrDrvrVers = $3 * 10;
			}
			|
			PRVERSION '=' number '.' number ';' 
			{
			  if (validblock)
			    ROMlib_PrDrvrVers = $3 * 10 + $5;
			}
	;


numerassignment:	numervar '=' number ';'
			{
			    if (validblock)
				*$1 = $3;
			}
	;

stringvar:	WINDOWNAME
	{ $$ = &ROMlib_WindowName; }
	| COMMENTS
	{ $$ = &ROMlib_Comments; }
	| DONGLEFAMILY
	{ $$ = &ROMlib_DongleFamily; }
	| PRINTERNAME
	{ $$ = &ROMlib_new_printer_name; }
	| PRINTERTYPELABEL
	{ $$ = &ROMlib_new_label; }
	| WIN32TOKEN
	{ $$ = &ROMlib_win32_token; }
	| MACCDROM
	{ $$ = &ROMlib_mac_cdrom;
          warning_unexpected ("MacCDROM no longer supported "
			      "in configuration files"); }
	;

numervar:	RDELAY
	{ $$ = &ROMlib_delay; }
	| REFRESHNUMBER
	{ $$ = &ROMlib_refresh; }
	| BITSNUMBER
	{ $$ = &ROMlib_desired_bpp; }
	| APPLECHAR
	{ $$ = &ROMlib_AppleChar; }
        | RIGHT_BUTTON_MODIFIER
	{ $$ = &ROMlib_right_button_modifier; }
	;

optionvalues: optionvalues ',' OPTIONSCONSTANT 
	{ $$ = $1 | $3; }
	| OPTIONSCONSTANT
	{ $$ = $1; }
	;

optionsvar:	OPTIONS
	{ 
	  if (validblock)
	    {
	      $$ = &ROMlib_options;
	      ROMlib_options = 0;
	    }
	}
	;

optionsassignment:	optionsvar '=' '{' optionvalues '}' ';'
	{ if (validblock)
	      *$1 = $4;
	}
	| optionsvar '=' '{' '}' ';'
	{
	  if (validblock)
	    *$1 = 0;
	}
	| optionsvar '=' '{' optionvalues ',' '}' ';'
	{
	  if (validblock)
	    *$1 = $4;
	}
	;

arrayassignment:	pairassignment
	| unboundedassignment
        | gestaltassignment
	;

pairassignment:	pairvar '=' '{' number ',' number '}' ';'
	{
	  if (validblock)
	    {
	      $1->first = $4;
	      $1->second = $6;
	    }
	}
	;

gestaltassignment:	gestalt_entry '=' '{' number ',' number '}' ';'
	{ if (validblock)
	    ROMlib_add_to_gestalt_list ($1, $4, $6);
	}
	;

number:	HEXCONSTANT
	{ $$ = $1; }
	| INTEGERCONSTANT
	{ $$ = $1; }
	| '-' number
	{ $$ = $2 == LONG_MIN ? (yyerror ("integer overflow"), -1) : - $2; }
	;

gestalt_entry:	GESTALT '(' CHAR4CONSTANT ')'
	{ $$ = $3; }
	| GESTALT '(' HEXCONSTANT ')'
	{ $$ = $3; }
	;

pairvar:	SCREENSIZE
	{ $$ = &ROMlib_ScreenSize; }
	| SCREENLOCATION
	{ $$ = &ROMlib_ScreenLocation; }
	| MACSIZE
	{ $$ = &ROMlib_MacSize; }
	;

unboundedassignment:	unboundedvar '=' '{' numberlist '}' ';'
	| unboundedvar '=' '{' numberlist ',' '}' ';'
	| unboundedvar '=' '{' '}' ';'
	;

unboundedvar:	CACHEPROBLEMS
	;

/* NOTE: numberlist is expected only to be used for CacheProblems
	 so the reduction directly calls a cache specific routine */
numberlist: numberlist ',' number
	{ if (validblock)
	    /* addtocache($3); */{};
        }
	| number
	{ if (validblock)
	    /* addtocache($1); */{};
	}
	;

%%
#include	<stdio.h>
#include	<ctype.h>

struct namevaluestr {
    char *name;
    long value;
    long lval;
} reserved[] = 
{	/* NOTE: these are sorted so we can use a binary search */
  { "Accelerated",	OPTIONSCONSTANT,	ROMLIB_ACCELERATED_BIT },
  { "AppleChar",	APPLECHAR,              0 },
  { "ApplZone", APPLZONE, 0 },
  { "BitsPerPixel",	BITSNUMBER,		0 },
  { "DisplayScale", DISPLAY_SCALE, 0 },
  { "BlitAtTrapEnd",	OPTIONSCONSTANT,	ROMLIB_BLIT_TRAP_BIT },
  { "BlitInOSEvent",	OPTIONSCONSTANT,	ROMLIB_BLIT_OS_BIT },
  { "BlitOften",	OPTIONSCONSTANT,	ROMLIB_BLIT_OFTEN_BIT },
  { "CRC",		CRC,			0 },
  { "CacheProblems",	CACHEPROBLEMS,		0 },
  { "CloseIsQuit",	OPTIONSCONSTANT,	ROMLIB_CLOSE_IS_QUIT_BIT },
  { "Comments",		COMMENTS,		0 },
  { "Debug",		OPTIONSCONSTANT,	ROMLIB_DEBUG_BIT },
  { "Delay",		RDELAY,			0 },
  { "DirectDiskAccess",	OPTIONSCONSTANT,	ROMLIB_DIRECTDISKACCESS_BIT },
  { "DirtyVariant",	OPTIONSCONSTANT,	ROMLIB_DIRTY_VARIANT_BIT },
  { "DisposHandleHack", OPTIONSCONSTANT,        ROMLIB_DISPOSHANDLE_HACK_BIT },
  { "DongleFamily",	DONGLEFAMILY,		0 },
  { "FlushOften",	OPTIONSCONSTANT,	ROMLIB_FLUSHOFTEN_BIT },
  { "Gestalt",          GESTALT,                0 },
  { "MacCDROM",         MACCDROM,               0 },
  { "MacSize",		MACSIZE,		0 },
  { "NewLineToCR",	OPTIONSCONSTANT,	ROMLIB_NEWLINETOCR_BIT },
  { "NoClock",		OPTIONSCONSTANT,	ROMLIB_NOCLOCK_BIT },
  { "NoLower",		OPTIONSCONSTANT,	ROMLIB_NOLOWER_BIT },
  { "NoPrefPanel",	OPTIONSCONSTANT,	ROMLIB_NOPREFS_BIT },
  { "NoSuspend",	OPTIONSCONSTANT,	ROMLIB_NOSUSPEND_BIT },
  { "NoWarn32",		OPTIONSCONSTANT,	ROMLIB_NOWARN32_BIT },
  { "Options",		OPTIONS,		0 },
  { "PassPostscript",	OPTIONSCONSTANT,	ROMLIB_PASSPOSTSCRIPT_BIT },
  { "PretendAlias",	OPTIONSCONSTANT,	ROMLIB_PRETEND_ALIAS_BIT },
  { "PretendEdition",	OPTIONSCONSTANT,	ROMLIB_PRETEND_EDITION_BIT },
  { "PretendHelp",	OPTIONSCONSTANT,	ROMLIB_PRETEND_HELP_BIT },
  { "PretendScript",	OPTIONSCONSTANT,	ROMLIB_PRETEND_SCRIPT_BIT },
  { "PretendSound",	OPTIONSCONSTANT,	ROMLIB_PRETENDSOUND_BIT },
  { "PrinterName",      PRINTERNAME,            0, },
  { "PrinterTypeLabel", PRINTERTYPELABEL,       0, },
  { "PrintingHack",	OPTIONSCONSTANT,	ROMLIB_PRINTING_HACK_BIT },
  { "PrVers",		PRVERSION,		0, },
  { "RectScreen",	OPTIONSCONSTANT,	ROMLIB_RECT_SCREEN_BIT },
  { "Refresh",		OPTIONSCONSTANT,	ROMLIB_REFRESH_BIT },
  { "RefreshNumber",	REFRESHNUMBER,		0 },
  { "RightButtonModifier", RIGHT_BUTTON_MODIFIER, 0 },
  { "ScreenLocation",	SCREENLOCATION,		0 },
  { "ScreenSize",	SCREENSIZE,		0 },
  { "SoundOff",		OPTIONSCONSTANT,	ROMLIB_SOUNDOFF_BIT },
  { "SoundOn",		OPTIONSCONSTANT,	ROMLIB_SOUNDON_BIT },
  { "StripAddress",	OPTIONSCONSTANT,	ROMLIB_STRIPADDRESSHACK_BIT },
  { "SystemVersion",	SYSTEMVERSION,          0 },
  { "SysZone", SYSZONE, 0 },
  { "K", SIZE_MULTIPLIER, 1024 },
  { "k", SIZE_MULTIPLIER, 1024 },
  { "M", SIZE_MULTIPLIER, 1024 * 1024 },
  { "m", SIZE_MULTIPLIER, 1024 * 1024 },
  { "G", SIZE_MULTIPLIER, 1024 * 1024 * 1024 },
  { "g", SIZE_MULTIPLIER, 1024 * 1024 * 1024 },
  { "TextDisableHack",	OPTIONSCONSTANT,	ROMLIB_TEXT_DISABLE_BIT },
  { "Version",		APP_VERSION,		0 },
  { "Win32Token",       WIN32TOKEN,             0 },
  { "WindowName",	WINDOWNAME,		0 },
};

#define	RELOAD	(-2)
#define	STRINGSPACE	1024

long binfind(char *tofind, struct namevaluestr table[], short nentries)
{
  short low, mid, high;
  int cmpret;

    for (low = -1, high = nentries; high - low > 1;) {
	mid = (high + low) / 2;
	cmpret = strcmp(tofind, table[mid].name);
	if (cmpret < 0)
	    high = mid;
	else if (cmpret == 0) {
	    yylval.number = table[mid].lval;
/*-->*/	    return table[mid].value;
	} else /* cmpret > 0 */
	    low = mid;
    }
    return 0;
}

static char charspace[STRINGSPACE];
static char *freechar = charspace, *endchar = charspace + STRINGSPACE;

/*
 * NOTE: call freestring only if you know no strings have been assigned
 *       after the one you are freeing.
 */

static void freestring( char *string )
{
    freechar = string;
}

static long linecount = 1;

static int
cmp (const void *p1, const void *p2)
{
  int retval;
  const char *str1;
  const char *str2;

  str1 = ((struct namevaluestr *)p1)->name;
  str2 = ((struct namevaluestr *)p2)->name;
  retval = strcmp (str1, str2);
  return retval;
}

static long yylex( void ) {
    static short c = RELOAD;
    char *tempp;
    long retval;
    int i;

    if (c == RELOAD)
	c = getc(configfile);

    for (;;) {
	switch (c) {

/* NOTE: if you mess with the '"' code, make sure to adjust clean()
   in toolevent.c.  */

	case '"':
            {
              int truncated = 0;
              yylval.cptr = freechar;
              while ((c = getc(configfile)) != '"' && c != EOF) {
                if (freechar < endchar - 1) *freechar++ = c;
                else truncated = 1;
              }
              *freechar = 0;
              if (freechar < endchar - 1) ++freechar;
              if (truncated || c == EOF) {
                yyerror (truncated ? "string too long" : "unterminated string");
                freechar = yylval.cptr;
                c = RELOAD;
                linecount = 1;
                validblock = 1;
                return 0;
              }
              if (c != EOF) c = getc(configfile);
              return STRINGCONSTANT;
            }

	case '0':
	    if ((c = getc(configfile)) == 'x') {
                unsigned long value = 0;
                int invalid = 0, digits = 0;
                while (isalnum(c = getc(configfile))) {
                  unsigned digit = c >= '0' && c <= '9' ? c - '0'
                                   : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                   : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 16;
                  ++digits;
                  if (digit >= 16 || value > (0xFFFFFFFFUL - digit) / 16)
                    invalid = 1;
                  else if (!invalid) value = value * 16 + digit;
                }
                if (invalid || !digits) yyerror ("invalid hex constant");
                yylval.number = invalid || !digits ? -1 : (int32)value;
                return HEXCONSTANT;
            }
	    /* We just ate a 0 that should be part of a decimal integer
	       (we don't support octal).  It is a leading zero, so we can
	       forget about it and fall through */
	    ungetc(c, configfile);
	    c = '0';
	    /* FALL THROUGH */
	case '1': case '2': case '3':
	case '4': case '5': case '6':
	case '7': case '8': case '9':
            {
              int overflow = 0;
              yylval.number = c - '0';
              while (isdigit(c = getc(configfile))) {
                if (yylval.number > (LONG_MAX - (c - '0')) / 10)
                  overflow = 1;
                else if (!overflow)
                  yylval.number = yylval.number * 10 + c - '0';
              }
              if (overflow) {
                yyerror ("integer overflow");
                yylval.number = -1;
              }
            }
	    return INTEGERCONSTANT;
	    break;
	case ' ':
	case '\r':
	case '\n':
	case '\t':
	case '\f':
	    do
		if (c == '\n')
		    linecount++;
	    while (isspace(c = getc(configfile)));
	    break;
	case '/':
	    if ((c = getc(configfile)) == '/') {
		while ((c = getc(configfile)) != '\n' && c != EOF)
		    ;
		linecount++;
		c = getc(configfile);
	    } else
		warning_unexpected ("single '/' parsing configuration file (line %ld)", linecount);
	    break;
	case '\'':
	    yylval.rtype = 0;
	    for (i = 0 ; i < 4 ; i++) {
		c = getc(configfile);
		yylval.rtype = (unsigned long)yylval.rtype * 256 + c;
	    }
	    if ((c = getc(configfile)) != '\'')
		warning_unexpected ("missing \' parsing configuration file (line %ld)", linecount);
	    c = getc(configfile);
	    return CHAR4CONSTANT;
	    break;
	case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
	case 'g': case 'h': case 'i': case 'j': case 'k': case 'l':
	case 'm': case 'n': case 'o': case 'p': case 'q': case 'r':
	case 's': case 't': case 'u': case 'v': case 'w': case 'x':
	case 'y': case 'z':
	case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
	case 'G': case 'H': case 'I': case 'J': case 'K': case 'L':
	case 'M': case 'N': case 'O': case 'P': case 'Q': case 'R':
	case 'S': case 'T': case 'U': case 'V': case 'W': case 'X':
	case 'Y': case 'Z':
	    tempp = freechar;
	    if (tempp < endchar)
		do
		    *tempp++ = c;
		while (tempp < endchar && isalnum(c = getc(configfile)));
	    if (tempp == endchar)
		warning_unexpected ("out of string space while looking up keyword parsing configuration file (line %ld)", linecount);
	    else
		*tempp++ = 0;
	    {
	      static boolean_t sorted = 0;

	      if (!sorted)
		{
		  qsort (reserved, NELEM (reserved), sizeof reserved[0], cmp);
		  sorted = 1;
		}
	    }
	    retval = binfind(freechar, reserved, NELEM (reserved));
	    if (!retval)
		warning_unexpected ("unknown reserved word parsing configuration file (line %ld)", linecount);
	    return retval;
	    break;
	case '{':
	case '}':
	case '(':
	case ')':
	case '=':
	case ':':
	case ',':
	case ';':
	case '-':
	case '.':
	    retval = c;
	    c = getc(configfile);
	    return retval;
	    break;
	case EOF:
	    c = RELOAD;	/* for the next time around */
	    linecount = 1;
	    validblock = 1;
	    return EOF;
	    break;
	default:
	    warning_unexpected ("unknown character parsing configuration file (line %ld)", linecount);
	}
    }
}

void yyerror(const char *str)
{
    warning_unexpected ("configuration file parse error (line %ld): %s", linecount, str);
}
