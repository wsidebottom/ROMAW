/*+===================================================================
  File:      filetypemap_test.c

  Summary:   Implements filetypemap test support for regression tests and test fixtures.

  Classes:   None.

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#include "../../files/filetypemap.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv)
{
    assert(argc == 2);
    {
        FileTypeMapping archive;
        const char* override = "zip,,APPL,BAD!,x,x,Override\nsit,,TEXT,BAD!,x,x,Override\n";
        assert(filetype_map_lookup(override, ".ZiP", 0, 0, 1, &archive));
        assert(archive.type == 0x5a495020 && archive.creator == 0x4d414353 && !archive.open_mac);
        assert(!strcmp(archive.description, "ZIP archive"));
        assert(filetype_map_lookup(override, ".sit", 0, 0, 1, &archive));
        assert(archive.type == 0x53495421 && archive.creator == 0x4d414353);
        assert(filetype_map_lookup(override, "", 0x42696e48, 0x42414421, 1, &archive));
        assert(archive.type == 0x42696e48 && archive.creator == 0x4d414353);
        char application[64] = "unchanged";
        assert(!filetype_map_application("", 0x53495421, 0x42414421, application, sizeof application));
        assert(!*application);
    }
    char suffix[65];
    uint32_t type = 123, creator = 456;
    assert(filetype_map_record("TXT,,TEXT,ttxt,x,x,Plain text", suffix, &type, &creator));
    assert(!strcmp(suffix, ".TXT") && type == 0x54455854 && creator == 0x74747874);
    assert(filetype_map_record(".zip,,ZIP,IZip,x,x,Archive\r\n", suffix, &type, &creator));
    assert(!strcmp(suffix, ".ZIP") && type == 0x5a495020);
    const char* invalid[] = {"",
                             "# comment",
                             "TXT,TEXT,ttxt",
                             "TXT,,TOOLONG,ttxt,x,x",
                             "TXT,,TEXT,,x,x",
                             "../TXT,,TEXT,ttxt,x,x",
                             "TXT,ignored,TEXT,ttxt,x,x"};
    for (unsigned i = 0; i < sizeof invalid / sizeof *invalid; ++i)
        assert(!filetype_map_record(invalid[i], suffix, &type, &creator));
    FileTypeMapping mapping, copy;
    assert(filetype_map_parse(",,TEXT,ttxt,,,\"Mac text\",host,\"D:\\092Apps\\092TeachText\"", &mapping));
    assert(!mapping.extension[0] && !strcmp(mapping.application, "D:\\Apps\\TeachText"));
    char application[4096];
    assert(filetype_map_application(",,TEXT,ttxt,,,\"Mac text\",host,\"D:\\092Apps\\092TeachText\"", 0x54455854,
                                    0x74747874, application, sizeof application));
    assert(!strcmp(application, "D:\\Apps\\TeachText"));
    assert(filetype_map_open_mac("TXT,,TEXT,ttxt,,,\"Text\",mac", ".txt"));
    assert(!filetype_map_open_mac("TXT,,TEXT,ttxt,x,x,Text", ".txt"));
    assert(!filetype_map_open_mac("TXT,,TEXT,ttxt,,,\"Text\",mac\nTXT,,TEXT,ttxt,,,\"Text\",host", "TXT"));
    assert(filetype_map_parse("txt,x,TE,T,\\128abc,,x,\"A, B\"", &mapping));
    assert(mapping.strip && !mapping.to_host && mapping.to_mac && mapping.type == 0x54452c54 &&
           mapping.creator == 0x80616263);
    assert(!strcmp(mapping.description, "A, B"));
    char encoded[2048];
    assert(filetype_map_format(&mapping, encoded, sizeof encoded));
    assert(filetype_map_parse(encoded, &copy));
    assert(!memcmp(&copy, &mapping, sizeof copy));
    assert(!filetype_map_format(&mapping, encoded, 2));
    assert(!filetype_map_parse("TXT,,TEXT,ttxt,x,x,\\999", &copy));
    assert(!filetype_map_record("TXT,,TEXT,ttxt,x,,Export only", suffix, &type, &creator));
    const char* rules = "TXT,,TEXT,ttxt,x,x,First\n"
                        "txt,x,TEXT,ABCD,,x,Import replacement\n"
                        "TXT,,TEXT,ZZZZ,,,Disabled\n";
    assert(filetype_map_lookup(rules, ".tXt", 0, 0, 1, &mapping));
    assert(mapping.creator == 0x41424344 && mapping.strip);
    assert(filetype_map_lookup(rules, NULL, 0x54455854, 0x74747874, 0, &mapping));
    assert(!strcmp(mapping.extension, "TXT") && mapping.to_host);
    char suggested[66];
    assert(filetype_map_extension(rules, 0x54455854, 0x74747874, suggested, sizeof suggested));
    assert(!strcmp(suggested, ".TXT"));
    assert(filetype_map_extension(rules, 0, 0x74747874, suggested, sizeof suggested));
    assert(!strcmp(suggested, ".TXT"));
    assert(!filetype_map_extension(rules, 0, 0x5a5a5a5a, suggested, sizeof suggested));
    assert(!suggested[0]);
    assert(!filetype_map_extension(rules, 0x54455854, 0x74747874, suggested, 4));
    assert(!suggested[0]);
    const char* formats = "TXT,,TEXT,ttxt,x,x,Text\n"
                          "PNG,,PNGf,ttxt,x,x,Image\n"
                          ",,APPL,ttxt,,,Application\n";
    assert(!filetype_map_extension(formats, 0, 0x74747874, suggested, sizeof suggested));
    assert(filetype_map_extension(formats, 0x504e4766, 0x74747874, suggested, sizeof suggested));
    assert(!strcmp(suggested, ".PNG"));
    assert(!filetype_map_extension(formats, 0x54455854, 0, suggested, sizeof suggested));
    const char* assortment = "TXT,,TEXT,ttxt,x,x,Text\n"
                             "CSV,,TEXT,XCEL,x,x,Comma separated\n"
                             "txt,,TEXT,TEST,x,x,Duplicate\n"
                             "PNG,,PNGf,TEST,x,x,Image\n"
                             "APP,,APPL,TEST,x,x,Application\n"
                             "BAD,,TEXT,TEST,,x,Import only\n";
    char suggestions[128];
    uint32_t document_types[] = {0x54455854, 0x504e4766};
    assert(filetype_map_extensions(assortment, document_types, 2, 0x54455354, suggestions, sizeof suggestions));
    assert(!strcmp(suggestions, "*.txt, *.png, *.csv"));
    assert(filetype_map_extensions(assortment, NULL, 0, 0x54455354, suggestions, sizeof suggestions));
    assert(!strcmp(suggestions, "*.txt, *.png"));
    assert(filetype_map_extensions(assortment, document_types, 2, 0, suggestions, 14));
    assert(!strcmp(suggestions, "*.txt, ..."));
    assert(!filetype_map_extensions(assortment, document_types, 2, 0, suggestions, 4));
    assert(!suggestions[0]);
    assert(!filetype_map_extensions(assortment, NULL, 2, 0, suggestions, sizeof suggestions));
    assert(!filetype_map_lookup(rules, NULL, 0x54455854, 0x41424344, 0, &mapping));
    char filename[80] = "Readme.txt";
    type = 0;
    creator = 0;
    assert(filetype_map_copy_name(rules, filename, sizeof filename, &type, &creator, 0, 1, 0));
    assert(!strcmp(filename, "Readme") && type == 0x54455854 && creator == 0x41424344);
    strcpy(filename, "Readme");
    creator = 0x74747874;
    assert(filetype_map_copy_name(rules, filename, sizeof filename, &type, &creator, 1, 0, 0));
    assert(!strcmp(filename, "Readme.TXT"));
    strcpy(filename, "Readme.foo");
    assert(filetype_map_copy_name(rules, filename, sizeof filename, &type, &creator, 1, 0, 0));
    assert(!strcmp(filename, "Readme.foo"));
    strcpy(filename, "Readme.longextension");
    assert(filetype_map_copy_name(rules, filename, sizeof filename, &type, &creator, 1, 0, 0));
    assert(!strcmp(filename, "Readme.TXT"));
    strcpy(filename, "Readme.txt");
    assert(filetype_map_copy_name(rules, filename, sizeof filename, &type, &creator, 0, 1, 1));
    assert(!strcmp(filename, "Readme.txt"));
    strcpy(filename, "Longname");
    assert(!filetype_map_copy_name(rules, filename, 9, &type, &creator, 1, 0, 0));
    char* large = malloc(1000001);
    assert(large);
    memset(large, 'x', 1000000);
    large[1000000] = 0;
    assert(!filetype_map_record(large, suffix, &type, &creator));
    free(large);
    FILE* file = fopen(argv[1], "rb");
    assert(file);
    char line[2048];
    unsigned count = 0;
    while (fgets(line, sizeof line, file)) {
        assert(filetype_map_record(line, suffix, &type, &creator));
        ++count;
    }
    assert(!ferror(file) && count == 300);
    fclose(file);
    puts("FTM: 300 starter records, short fourcc padding, malformed and large input passed");
    return 0;
}
